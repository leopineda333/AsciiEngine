/**
 * @file AsciiZBuffer.cpp
 * @brief Rasterizador ASCII Z-Buffer world-locked en 2 pases con soporte de terreno (IS_GROUND).
 */
#include <AsciiEngine/AsciiZBuffer.hpp>
#include "raylib.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace AsciiEngine {

    void AsciiZBuffer::Resize(int screenWidth, int screenHeight, float cellWidth, float cellHeight) {
        if (screenWidth <= 0 || screenHeight <= 0 || cellWidth <= 0.0f || cellHeight <= 0.0f) {
            return;
        }

        // +3 celdas de margen invisible para scroll sub-celda y bordes estables.
        int newCols = static_cast<int>(std::ceil(static_cast<float>(screenWidth) / cellWidth)) + 3;
        int newRows = static_cast<int>(std::ceil(static_cast<float>(screenHeight) / cellHeight)) + 3;

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

    void AsciiZBuffer::BeginFrame(float camTopLeftX, float camTopLeftY) {
        std::fill(m_cells.begin(), m_cells.end(), ZBufferCell{});

        int32_t baseCellX = static_cast<int32_t>(std::floor(camTopLeftX * m_invCellWidth));
        int32_t baseCellY = static_cast<int32_t>(std::floor(camTopLeftY * m_invCellHeight));

        // Scroll redondeado al pixel entero para eliminar shimmer sub-pixel en la fuente.
        m_scrollOffsetX = std::round(camTopLeftX - static_cast<float>(baseCellX) * m_cellWidth);
        m_scrollOffsetY = std::round(camTopLeftY - static_cast<float>(baseCellY) * m_cellHeight);

        m_originCellX = baseCellX - 1;
        m_originCellY = baseCellY - 1;
    }

    bool AsciiZBuffer::SubmitAtom(float zoomedIsoX, float zoomedIsoY, float depth,
        char glyph, ColorRGBA color, MaterialId matId,
        bool wantsOutline, int splatRadius, bool isGround) {
        int32_t worldCellX = static_cast<int32_t>(std::floor(zoomedIsoX * m_invCellWidth));
        int32_t worldCellY = static_cast<int32_t>(std::floor(zoomedIsoY * m_invCellHeight));

        int cx = static_cast<int>(worldCellX - m_originCellX);
        int cy = static_cast<int>(worldCellY - m_originCellY);

        if (cx < 0 || cx >= m_gridWidth || cy < 0 || cy >= m_gridHeight) {
            return false;
        }

        // Calculamos el desplazamiento exacto en pixeles enteros respecto al centro de la celda.
        // Como depende solo de zoomedIso, es 100% invariante al movimiento de la camara (0 swimming).
        const float halfW = m_cellWidth * 0.5f;
        const float halfH = m_cellHeight * 0.5f;
        const float cellCenterIsoX = (static_cast<float>(worldCellX) + 0.5f) * m_cellWidth;
        const float cellCenterIsoY = (static_cast<float>(worldCellY) + 0.5f) * m_cellHeight;

        const int8_t subX = static_cast<int8_t>(std::clamp(std::round(zoomedIsoX - cellCenterIsoX), -halfW, halfW));
        const int8_t subY = static_cast<int8_t>(std::clamp(std::round(zoomedIsoY - cellCenterIsoY), -halfH, halfH));

        uint8_t baseFlags = CellFlags::NONE;
        if (wantsOutline && !isGround) baseFlags |= CellFlags::WANTS_OUTLINE;
        if (isGround)                  baseFlags |= CellFlags::IS_GROUND;

        if (splatRadius <= 0) {
            size_t idx = static_cast<size_t>(cy) * static_cast<size_t>(m_gridWidth) + static_cast<size_t>(cx);
            ZBufferCell& cell = m_cells[idx];
            if (depth > cell.depth) {
                cell.depth = depth;
                cell.glyph = glyph;
                cell.color = color;
                cell.materialId = matId;
                cell.flags = baseFlags;
                cell.subOffsetX = subX;
                cell.subOffsetY = subY;
            }
            return true;
        }

        // Splatting adaptativo:
        // splatRadius == 1 -> cruz de 5 celdas (zoom 1.2x - 1.85x).
        // splatRadius >= 2 -> vecindad 3x3 de 9 celdas (zoom 1.85x - 2.5x) sin huecos al acercarse.
        static constexpr int kOffsets[9][2] = {
            { 0,  0 },{ -1,  0 },{ 1,  0 },{ 0, -1 },{ 0,  1 },
            { -1, -1 },{ 1, -1 },{ -1,  1 },{ 1,  1 }
        };
        const int count = (splatRadius >= 2) ? 9 : 5;

        for (int k = 0; k < count; ++k) {
            int nx = cx + kOffsets[k][0];
            int ny = cy + kOffsets[k][1];
            if (nx < 0 || nx >= m_gridWidth || ny < 0 || ny >= m_gridHeight) {
                continue;
            }

            // Prioridad jerarquica: centro (0) > ortogonales (-0.015) > diagonales (-0.025).
            float bias = (k == 0) ? 0.0f : ((k < 5) ? 0.015f : 0.025f);
            float effDepth = depth - bias;
            size_t idx = static_cast<size_t>(ny) * static_cast<size_t>(m_gridWidth) + static_cast<size_t>(nx);
            ZBufferCell& cell = m_cells[idx];

            if (effDepth > cell.depth) {
                cell.depth = effDepth;
                cell.glyph = glyph;
                cell.color = color;
                cell.materialId = matId;
                cell.flags = baseFlags;
                // El centro conserva el sub-offset exacto; el splat lateral se centra para sellar huecos.
                cell.subOffsetX = (k == 0) ? subX : 0;
                cell.subOffsetY = (k == 0) ? subY : 0;
            }
        }
        return true;
    }

    ZBufferStats AsciiZBuffer::FlushToScreen(const void* raylibFontPtr, const void* glyphLutPtr,
        float glyphAspectRatio, const ZBufferStyle& style) {
        ZBufferStats stats;
        if (!raylibFontPtr || !glyphLutPtr || m_cells.empty()) {
            return stats;
        }

        const Font& font = *static_cast<const Font*>(raylibFontPtr);
        const auto& lut = *static_cast<const std::array<Rectangle, 128>*>(glyphLutPtr);

        const float drawHeight = m_cellHeight * 1.12f;
        const float scaleFactor = drawHeight / static_cast<float>(font.baseSize);
        const float subScale = style.enableSubPixelOffset ? 1.0f : 0.0f;
        ZBufferCell* rawCells = m_cells.data();

        auto getNeighbor = [&](int nx, int ny) -> const ZBufferCell* {
            if (nx < 0 || nx >= m_gridWidth || ny < 0 || ny >= m_gridHeight) {
                return nullptr;
            }
            return &rawCells[static_cast<size_t>(ny) * static_cast<size_t>(m_gridWidth) + static_cast<size_t>(nx)];
            };

        auto isSolidOutlineCell = [](const ZBufferCell* n) -> bool {
            return n && (n->glyph != '\0') && (n->flags & CellFlags::WANTS_OUTLINE);
            };

        auto isBackdropCell = [](const ZBufferCell* n) -> bool {
            return !n || (n->glyph == '\0') || (n->flags & CellFlags::IS_GROUND);
            };

        // =========================================================================
        // PASE 0: RESPALDO VOLUMETRICO PARA OBJETOS SOLIDOS (1 SOLO BATCH DE QUADS)
        // Impide que el fondo gris se cuele entre glifos pequenos (ej. ',' o ';'),
        // dando color y masa solida a arboles, rocas y estructuras sin perder el estilo ASCII.
        // =========================================================================
        if (style.enableCellBacking) {
            const float backW = m_cellWidth * 1.18f;
            const float backH = m_cellHeight * 1.06f;

            for (int cy = 0; cy < m_gridHeight; ++cy) {
                const float centerY = (static_cast<float>(cy) - 0.5f) * m_cellHeight - m_scrollOffsetY;
                const size_t rowOffset = static_cast<size_t>(cy) * static_cast<size_t>(m_gridWidth);

                for (int cx = 0; cx < m_gridWidth; ++cx) {
                    const ZBufferCell& cell = rawCells[rowOffset + static_cast<size_t>(cx)];

                    // Solo aplica a geometria 3D real (excluye suelo, agua, pasto, particulas y estrellas de luz).
                    if (cell.glyph == '\0' || (cell.flags & CellFlags::IS_GROUND) || cell.materialId == FALLBACK_MATERIAL_ID) {
                        continue;
                    }

                    const float drawX = (static_cast<float>(cx) - 0.5f) * m_cellWidth - m_scrollOffsetX
                        + static_cast<float>(cell.subOffsetX) * subScale;
                    const float drawY = centerY + static_cast<float>(cell.subOffsetY) * subScale;

                    Color backCol = {
                        static_cast<unsigned char>(static_cast<float>(cell.color.r) * style.backingDarken),
                        static_cast<unsigned char>(static_cast<float>(cell.color.g) * style.backingDarken),
                        static_cast<unsigned char>(static_cast<float>(cell.color.b) * style.backingDarken),
                        style.backingAlpha
                    };
                    DrawRectangleRec({ drawX - backW * 0.5f, drawY - backH * 0.5f, backW, backH }, backCol);
                }
            }
        }

        // =========================================================================
        // PASE 1: SILUETAS DIRECCIONALES ASCII + OUTLINES NEGROS DE 1-2 PX
        // =========================================================================
        if (style.enableOutlines) {
            const float px = style.pixelOutlinePx;

            for (int cy = 0; cy < m_gridHeight; ++cy) {
                const float centerY = (static_cast<float>(cy) - 0.5f) * m_cellHeight - m_scrollOffsetY;
                const size_t rowOffset = static_cast<size_t>(cy) * static_cast<size_t>(m_gridWidth);

                for (int cx = 0; cx < m_gridWidth; ++cx) {
                    ZBufferCell& cell = rawCells[rowOffset + static_cast<size_t>(cx)];
                    const float centerX = (static_cast<float>(cx) - 0.5f) * m_cellWidth - m_scrollOffsetX;

                    const ZBufferCell* nUp = getNeighbor(cx, cy - 1);
                    const ZBufferCell* nDown = getNeighbor(cx, cy + 1);
                    const ZBufferCell* nLeft = getNeighbor(cx - 1, cy);
                    const ZBufferCell* nRight = getNeighbor(cx + 1, cy);

                    // CASO A: Celda vacia o de suelo junto a un objeto -> Borde direccional ASCII.
                    if (cell.glyph == '\0' || (cell.flags & CellFlags::IS_GROUND)) {
                        if (!style.enableAsciiBorder) continue;

                        bool up = isSolidOutlineCell(nUp);
                        bool down = isSolidOutlineCell(nDown);
                        bool left = isSolidOutlineCell(nLeft);
                        bool right = isSolidOutlineCell(nRight);

                        if (up || down || left || right) {
                            char edgeChar = '.';
                            if ((left || right) && !(up || down))      edgeChar = '|';
                            else if ((up || down) && !(left || right)) edgeChar = '-';
                            else if ((up && left) || (down && right))  edgeChar = '/';
                            else if ((up && right) || (down && left))  edgeChar = '\\';
                            else                                       edgeChar = '+';

                            const Rectangle& src = lut[static_cast<unsigned char>(edgeChar)];
                            float ew = (src.width > 0.0f) ? (src.width * scaleFactor) : (drawHeight * glyphAspectRatio);
                            float eh = (src.height > 0.0f) ? (src.height * scaleFactor) : drawHeight;
                            Vector2 origin = { ew * 0.5f, eh * 0.5f };

                            DrawTexturePro(font.texture, src, { centerX, centerY + 1.0f, ew, eh }, origin, 0.0f, BLACK);
                            DrawTexturePro(font.texture, src, { centerX, centerY, ew, eh }, origin, 0.0f,
                                { style.asciiBorderColor.r, style.asciiBorderColor.g,
                                  style.asciiBorderColor.b, style.asciiBorderColor.a });
                            cell.flags |= CellFlags::IS_SILHOUETTE;
                            ++stats.outlineCellsDrawn;
                        }
                        continue;
                    }

                    // CASO B: Celda de objeto -> Outline negro alineado al sub-pixel exacto del glifo.
                    if (!(cell.flags & CellFlags::WANTS_OUTLINE)) {
                        continue;
                    }

                    bool touchesBackdrop = false;
                    bool touchesOverlap = false;

                    const ZBufferCell* neighbors[4] = { nUp, nDown, nLeft, nRight };
                    for (const ZBufferCell* n : neighbors) {
                        if (isBackdropCell(n)) {
                            touchesBackdrop = true;
                        }
                        else if ((cell.depth - n->depth) > style.overlapDepthThresh) {
                            touchesOverlap = true;
                        }
                    }

                    if (touchesBackdrop || touchesOverlap) {
                        cell.flags |= CellFlags::IS_SILHOUETTE;
                        if (touchesOverlap) {
                            cell.flags |= CellFlags::IS_OVERLAP_EDGE;
                        }

                        unsigned char uc = static_cast<unsigned char>(cell.glyph);
                        if (uc <= 32 || uc >= 127) continue;

                        const Rectangle& src = lut[uc];
                        float destW = (src.width > 0.0f) ? (src.width * scaleFactor) : (drawHeight * glyphAspectRatio);
                        float destH = (src.height > 0.0f) ? (src.height * scaleFactor) : drawHeight;
                        Vector2 origin = { destW * 0.5f, destH * 0.5f };

                        // Dibujamos el contorno exactamente detras de la posicion sub-pixel del glifo.
                        const float drawX = centerX + static_cast<float>(cell.subOffsetX) * subScale;
                        const float drawY = centerY + static_cast<float>(cell.subOffsetY) * subScale;

                        const Color outlineCol = {
                            style.pixelOutlineColor.r, style.pixelOutlineColor.g,
                            style.pixelOutlineColor.b, style.pixelOutlineColor.a
                        };
                        DrawTexturePro(font.texture, src, { drawX - px, drawY, destW, destH }, origin, 0.0f, outlineCol);
                        DrawTexturePro(font.texture, src, { drawX + px, drawY, destW, destH }, origin, 0.0f, outlineCol);
                        DrawTexturePro(font.texture, src, { drawX, drawY - px, destW, destH }, origin, 0.0f, outlineCol);
                        DrawTexturePro(font.texture, src, { drawX, drawY + px, destW, destH }, origin, 0.0f, outlineCol);
                        ++stats.outlineCellsDrawn;
                    }
                }
            }
        }

        // =========================================================================
        // PASE 2: DIBUJADO LIMPIO DE GLIFOS EN POSICION SUB-PIXEL (ZERO OVERDRAW)
        // =========================================================================
        for (int cy = 0; cy < m_gridHeight; ++cy) {
            const float centerY = (static_cast<float>(cy) - 0.5f) * m_cellHeight - m_scrollOffsetY;
            const size_t rowOffset = static_cast<size_t>(cy) * static_cast<size_t>(m_gridWidth);

            for (int cx = 0; cx < m_gridWidth; ++cx) {
                const ZBufferCell& cell = rawCells[rowOffset + static_cast<size_t>(cx)];
                unsigned char uc = static_cast<unsigned char>(cell.glyph);
                if (uc <= 32 || uc >= 127) {
                    continue;
                }
                if ((cell.flags & CellFlags::IS_GROUND) && (cell.flags & CellFlags::IS_SILHOUETTE)) {
                    continue;
                }

                const Rectangle& src = lut[uc];
                float destW = (src.width > 0.0f) ? (src.width * scaleFactor) : (drawHeight * glyphAspectRatio);
                float destH = (src.height > 0.0f) ? (src.height * scaleFactor) : drawHeight;
                const float drawX = (static_cast<float>(cx) - 0.5f) * m_cellWidth - m_scrollOffsetX
                    + static_cast<float>(cell.subOffsetX) * subScale;
                const float drawY = centerY + static_cast<float>(cell.subOffsetY) * subScale;
                Vector2 origin = { destW * 0.5f, destH * 0.5f };

                Color drawCol = { cell.color.r, cell.color.g, cell.color.b, cell.color.a };
                if (cell.flags & CellFlags::IS_OVERLAP_EDGE) {
                    drawCol.r = static_cast<unsigned char>(static_cast<float>(drawCol.r) * style.overlapDarkenFactor);
                    drawCol.g = static_cast<unsigned char>(static_cast<float>(drawCol.g) * style.overlapDarkenFactor);
                    drawCol.b = static_cast<unsigned char>(static_cast<float>(drawCol.b) * style.overlapDarkenFactor);
                }

                DrawTexturePro(font.texture, src, { drawX, drawY, destW, destH }, origin, 0.0f, drawCol);
                ++stats.glyphsRendered;
            }
        }

        return stats;
    }

} // namespace AsciiEngine