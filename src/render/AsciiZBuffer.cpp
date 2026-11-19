/**
 * @file AsciiZBuffer.cpp
 * @brief Implementacion del Rasterizador ASCII Z-Buffer en espacio de pantalla.
 */
#include <AsciiEngine/AsciiZBuffer.hpp>
#include "raylib.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace AsciiEngine {

    void AsciiZBuffer::Resize(int screenWidth, int screenHeight, float cellWidth, float cellHeight) {
        // Evitamos tamanos invalidos si la ventana se minimiza en Windows (0x0).
        if (screenWidth <= 0 || screenHeight <= 0 || cellWidth <= 0.0f || cellHeight <= 0.0f) {
            return;
        }

        int newCols = static_cast<int>(std::ceil(static_cast<float>(screenWidth) / cellWidth));
        int newRows = static_cast<int>(std::ceil(static_cast<float>(screenHeight) / cellHeight));

        // Si las dimensiones ya coinciden, evitamos tocar la memoria.
        if (newCols == m_gridWidth && newRows == m_gridHeight &&
            cellWidth == m_cellWidth && cellHeight == m_cellHeight) {
            return;
        }

        m_gridWidth = newCols;
        m_gridHeight = newRows;
        m_cellWidth = cellWidth;
        m_cellHeight = cellHeight;
        m_invCellWidth = 1.0f / cellWidth;
        m_invCellHeight = 1.0f / cellHeight;

        m_cells.assign(static_cast<size_t>(m_gridWidth) * static_cast<size_t>(m_gridHeight), ZBufferCell{});
    }

    void AsciiZBuffer::Clear() {
        // Llenado lineal sobre memoria contigua (sin asignaciones en heap).
        std::fill(m_cells.begin(), m_cells.end(), ZBufferCell{});
    }

    bool AsciiZBuffer::SubmitAtom(float screenX, float screenY, float depth,
        char glyph, ColorRGBA color, MaterialId matId, bool wantsOutline) {
        // Culling Capa 3: descartamos coordenadas negativas antes del cast entero
        // (en C++, static_cast<int>(-0.5f) trunca a 0 y colaria pixeles fuera del borde izquierdo/superior).
        if (screenX < 0.0f || screenY < 0.0f) {
            return false;
        }

        // Convertimos posicion de pantalla a indices discretos multiplicando por el inverso.
        int cx = static_cast<int>(screenX * m_invCellWidth);
        int cy = static_cast<int>(screenY * m_invCellHeight);

        if (cx >= m_gridWidth || cy >= m_gridHeight) {
            return false;
        }

        // Acceso directo O(1) en el arreglo plano 1D (fila * ancho + columna).
        size_t idx = static_cast<size_t>(cy) * static_cast<size_t>(m_gridWidth) + static_cast<size_t>(cx);
        ZBufferCell& cell = m_cells[idx];

        // Prueba de profundidad: solo conservamos el atomo mas cercano a la camara.
        if (depth > cell.depth) {
            cell.depth = depth;
            cell.glyph = glyph;
            cell.color = color;
            cell.materialId = matId;
            cell.flags = wantsOutline ? CellFlags::WANTS_OUTLINE : CellFlags::NONE;
        }
        return true;
    }

    ZBufferStats AsciiZBuffer::FlushToScreen(const void* raylibFontPtr, const void* glyphLutPtr,
        float glyphAspectRatio, bool enableOutlines,
        float outlineDepthThreshold) const {
        ZBufferStats stats;
        if (!raylibFontPtr || !glyphLutPtr || m_cells.empty()) {
            return stats;
        }

        const Font& font = *static_cast<const Font*>(raylibFontPtr);
        const auto& lut = *static_cast<const std::array<Rectangle, 128>*>(glyphLutPtr);

        // Escalamos ligeramente la altura del glifo (1.15x) para llenar visualmente la celda sin huecos.
        const float drawHeight = m_cellHeight * 1.15f;
        const float scaleFactor = drawHeight / static_cast<float>(font.baseSize);
        const ZBufferCell* rawCells = m_cells.data();

        // Lambda auxiliar O(1) para detectar si un vecino esta vacio o mucho mas atras en profundidad.
        auto isDepthDrop = [&](int nx, int ny, float currentDepth) -> bool {
            if (nx < 0 || nx >= m_gridWidth || ny < 0 || ny >= m_gridHeight) {
                return true; // El borde de la pantalla actua como frontera de contorno.
            }
            const ZBufferCell& neighbor = rawCells[static_cast<size_t>(ny) * m_gridWidth + static_cast<size_t>(nx)];
            return (neighbor.glyph == '\0') || ((currentDepth - neighbor.depth) > outlineDepthThreshold);
            };

        // Pase lineal unico por filas (optimo para el prefetcher de cache de la CPU).
        for (int cy = 0; cy < m_gridHeight; ++cy) {
            const float centerY = (static_cast<float>(cy) + 0.5f) * m_cellHeight;
            const size_t rowOffset = static_cast<size_t>(cy) * static_cast<size_t>(m_gridWidth);

            for (int cx = 0; cx < m_gridWidth; ++cx) {
                const ZBufferCell& cell = rawCells[rowOffset + static_cast<size_t>(cx)];
                unsigned char uc = static_cast<unsigned char>(cell.glyph);
                if (uc <= 32 || uc >= 127) {
                    continue; // Celda vacia o espacio: cero trabajo en GPU.
                }

                const Rectangle& src = lut[uc];
                const float destW = (src.width > 0.0f) ? (src.width * scaleFactor) : (drawHeight * glyphAspectRatio);
                const float destH = (src.height > 0.0f) ? (src.height * scaleFactor) : drawHeight;
                const float centerX = (static_cast<float>(cx) + 0.5f) * m_cellWidth;
                const Vector2 origin = { destW * 0.5f, destH * 0.5f };

                // Deteccion O(1) de borde negro (Silhouette Outline) solo en celdas de contorno real.
                if (enableOutlines && (cell.flags & CellFlags::WANTS_OUTLINE)) {
                    bool isSilhouette =
                        isDepthDrop(cx - 1, cy, cell.depth) ||
                        isDepthDrop(cx + 1, cy, cell.depth) ||
                        isDepthDrop(cx, cy - 1, cell.depth) ||
                        isDepthDrop(cx, cy + 1, cell.depth);

                    if (isSilhouette) {
                        // Estampamos contorno de 1px en cruz usando el mismo atlas (mismo Texture Batch).
                        DrawTexturePro(font.texture, src, { centerX - 1.0f, centerY, destW, destH }, origin, 0.0f, BLACK);
                        DrawTexturePro(font.texture, src, { centerX + 1.0f, centerY, destW, destH }, origin, 0.0f, BLACK);
                        DrawTexturePro(font.texture, src, { centerX, centerY - 1.0f, destW, destH }, origin, 0.0f, BLACK);
                        DrawTexturePro(font.texture, src, { centerX, centerY + 1.0f, destW, destH }, origin, 0.0f, BLACK);
                        ++stats.outlineCellsDrawn;
                    }
                }

                // Dibujado unico del glifo ganador (Zero Overdraw).
                DrawTexturePro(
                    font.texture,
                    src,
                    { centerX, centerY, destW, destH },
                    origin,
                    0.0f,
                    { cell.color.r, cell.color.g, cell.color.b, cell.color.a }
                );
                ++stats.glyphsRendered;
            }
        }

        return stats;
    }

} // namespace AsciiEngine