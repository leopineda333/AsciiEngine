// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file AsciiZBuffer.cpp
 * @brief Implementacion comentada linea por linea del Rasterizador ASCII Z-Buffer en 3 pases.
 */
#include <AsciiEngine/AsciiZBuffer.hpp>
#include "raylib.h" // Dependencia privada: usada solo aqui para DrawTexturePro y DrawRectangleRec.

#include <algorithm> // std::fill, std::clamp
#include <array>     // std::array<Rectangle, 128> para la LUT de glifos
#include <cmath>     // std::ceil, std::floor, std::round

namespace AsciiEngine {

    void AsciiZBuffer::Resize(int screenWidth, int screenHeight, float cellWidth, float cellHeight) {
        // 1. Proteccion contra ventana minimizada en Windows (GetScreenWidth() == 0) o celdas invalidas.
        if (screenWidth <= 0 || screenHeight <= 0 || cellWidth <= 0.0f || cellHeight <= 0.0f) {
            return;
        }

        // 2. Calculamos cuantas columnas y filas caben en la ventana y sumamos +3 celdas de margen:
        //    - 1 celda extra a la izquierda/arriba (columna 0 / fila 0 invisible).
        //    - 1 celda extra para absorber el desplazamiento sub-celda (m_scrollOffset).
        //    - 1 celda extra a la derecha/abajo para que los bordes de silueta no parpadeen al entrar.
        int newCols = static_cast<int>(std::ceil(static_cast<float>(screenWidth) / cellWidth)) + 3;
        int newRows = static_cast<int>(std::ceil(static_cast<float>(screenHeight) / cellHeight)) + 3;

        // 3. Salida temprana si nada cambio: evita tocar el heap durante el bucle de 60 FPS.
        if (newCols == m_gridWidth && newRows == m_gridHeight &&
            cellWidth == m_cellWidth && cellHeight == m_cellHeight) {
            return;
        }

        // 4. Actualizamos dimensiones y precalculamos inversos (multiplicar es ~4x mas rapido que dividir).
        m_gridWidth = newCols;
        m_gridHeight = newRows;
        m_cellWidth = cellWidth;
        m_cellHeight = cellHeight;
        m_invCellWidth = 1.0f / cellWidth;
        m_invCellHeight = 1.0f / cellHeight;

        // 5. Alojamos el arreglo plano 1D contiguo en RAM (tipicamente ~300-450 KB en 1600x900).
        m_cells.assign(static_cast<size_t>(m_gridWidth) * static_cast<size_t>(m_gridHeight), ZBufferCell{});
    }

    void AsciiZBuffer::BeginFrame(float camTopLeftX, float camTopLeftY) {
        // 1. Limpiamos todas las celdas sobreescribiendo con ZBufferCell{} (depth = -1e9, glyph = '\0').
        //    std::fill recorre memoria lineal sin liberar ni realojar el vector.
        std::fill(m_cells.begin(), m_cells.end(), ZBufferCell{});

        // 2. Tecnica World-Locked Grid: dividimos la esquina de la camara en:
        //    - Celda entera en el espacio del mundo (baseCellX, baseCellY).
        //    - Residuo de desplazamiento en pixeles dentro de esa celda (m_scrollOffset).
        int32_t baseCellX = static_cast<int32_t>(std::floor(camTopLeftX * m_invCellWidth));
        int32_t baseCellY = static_cast<int32_t>(std::floor(camTopLeftY * m_invCellHeight));

        // 3. Redondeamos el residuo al pixel entero para evitar que el filtro trilineal de la fuente
        //    genere brillo oscilante (shimmer sub-pixel) en trazos delgados al deslizarse suavemente.
        m_scrollOffsetX = std::round(camTopLeftX - static_cast<float>(baseCellX) * m_cellWidth);
        m_scrollOffsetY = std::round(camTopLeftY - static_cast<float>(baseCellY) * m_cellHeight);

        // 4. Retrocedemos 1 celda el origen para reservar el indice 0 como borde invisible de seguridad.
        m_originCellX = baseCellX - 1;
        m_originCellY = baseCellY - 1;
    }

    bool AsciiZBuffer::SubmitAtom(float zoomedIsoX, float zoomedIsoY, float depth,
        char glyph, ColorRGBA color, MaterialId matId,
        bool wantsOutline, int splatRadius, bool isGround) {
        // 1. Cuantizamos el punto en coordenadas enteras de la rejilla DEL MUNDO.
        //    Como zoomedIsoX/Y no dependen de la posicion de la camara, un objeto estatico cae
        //    siempre en exactamente la misma celda del mundo sin importar hacia donde nos movamos.
        int32_t worldCellX = static_cast<int32_t>(std::floor(zoomedIsoX * m_invCellWidth));
        int32_t worldCellY = static_cast<int32_t>(std::floor(zoomedIsoY * m_invCellHeight));

        // 2. Convertimos a indices locales [0 .. m_gridWidth-1] relativos a la ventana actual.
        int cx = static_cast<int>(worldCellX - m_originCellX);
        int cy = static_cast<int>(worldCellY - m_originCellY);

        // 3. Culling Capa 3: si el atomo cae fuera del buffer visible, lo descartamos en O(1).
        if (cx < 0 || cx >= m_gridWidth || cy < 0 || cy >= m_gridHeight) {
            return false;
        }

        // 4. Calculo de Sub-Pixel Offset (rompe la rigidez visual de la cuadricula):
        //    Medimos la distancia exacta en pixeles desde el centro de su celda de mundo hasta
        //    la posicion real proyectada del atomo, y la guardamos en un entero compacto int8_t.
        const float halfW = m_cellWidth * 0.5f;
        const float halfH = m_cellHeight * 0.5f;
        const float cellCenterIsoX = (static_cast<float>(worldCellX) + 0.5f) * m_cellWidth;
        const float cellCenterIsoY = (static_cast<float>(worldCellY) + 0.5f) * m_cellHeight;

        const int8_t subX = static_cast<int8_t>(std::clamp(std::round(zoomedIsoX - cellCenterIsoX), -halfW, halfW));
        const int8_t subY = static_cast<int8_t>(std::clamp(std::round(zoomedIsoY - cellCenterIsoY), -halfH, halfH));

        // 5. Empaquetamos los flags iniciales de la celda (el suelo nunca solicita outline propio).
        uint8_t baseFlags = CellFlags::NONE;
        if (wantsOutline && !isGround) baseFlags |= CellFlags::WANTS_OUTLINE;
        if (isGround)                  baseFlags |= CellFlags::IS_GROUND;

        // 6A. Camino rapido sin splatting (zoom normal/alejado, suelo, particulas, pasto):
        //     Una unica lectura/escritura directa en el arreglo plano 1D.
        if (splatRadius <= 0) {
            size_t idx = static_cast<size_t>(cy) * static_cast<size_t>(m_gridWidth) + static_cast<size_t>(cx);
            ZBufferCell& cell = m_cells[idx];
            // Prueba de profundidad (Z-Test): solo sobrevive el atomo mas cercano a la camara.
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

        // 6B. Splatting adaptativo para zoom cercano (> 1.18x) que sella huecos sin duplicar RAM:
        //     - splatRadius == 1: rellena centro + 4 vecinos ortogonales en cruz (5 celdas).
        //     - splatRadius >= 2: rellena la vecindad completa 3x3 (9 celdas, ideal hasta zoom 2.5x).
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

            // Jerarquia de profundidad artificial:
            //   centro real (0) > vecinos en cruz (-0.015) > esquinas diagonales (-0.025).
            // Asi, si otro atomo real cae exactamente en la celda vecina, siempre gana al relleno.
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
                // Solo el centro hereda el sub-pixel offset; el relleno queda centrado para cerrar grietas.
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

        // Recuperamos las referencias tipadas de Raylib ocultas tras los punteros opacos del header.
        const Font& font = *static_cast<const Font*>(raylibFontPtr);
        const auto& lut = *static_cast<const std::array<Rectangle, 128>*>(glyphLutPtr);

        // Escalamos los glifos a 1.12x la altura de celda para que el volumen se sienta solido y legible.
        const float drawHeight = m_cellHeight * 1.12f;
        const float scaleFactor = drawHeight / static_cast<float>(font.baseSize);
        const float subScale = style.enableSubPixelOffset ? 1.0f : 0.0f;
        ZBufferCell* rawCells = m_cells.data(); // Puntero crudo para evitar chequeos en bucles internos.

        // Lambda auxiliar O(1) para consultar celdas vecinas con comprobacion segura de limites.
        auto getNeighbor = [&](int nx, int ny) -> const ZBufferCell* {
            if (nx < 0 || nx >= m_gridWidth || ny < 0 || ny >= m_gridHeight) {
                return nullptr;
            }
            return &rawCells[static_cast<size_t>(ny) * static_cast<size_t>(m_gridWidth) + static_cast<size_t>(nx)];
            };

        // Determina si un vecino pertenece a una figura solida con contorno habilitado.
        auto isSolidOutlineCell = [](const ZBufferCell* n) -> bool {
            return n && (n->glyph != '\0') && (n->flags & CellFlags::WANTS_OUTLINE);
            };

        // Determina si un vecino actua como telon de fondo (vacio, fuera de pantalla o celda de suelo).
        auto isBackdropCell = [](const ZBufferCell* n) -> bool {
            return !n || (n->glyph == '\0') || (n->flags & CellFlags::IS_GROUND);
            };

        // =========================================================================
        // PASE 0: RESPALDO VOLUMETRICO PARA GEOMETRIA 3D (1 SOLO BATCH DE QUADS)
        // =========================================================================
        // Dibuja un rectangulo oscurecido del color del propio material detras de cada celda
        // solida. Esto impide que el fondo oscuro/gris se cuele a traves de caracteres pequenos
        // (como '.' o ','), manteniendo masa y color legibles incluso en crepusculo o tormenta.
        if (style.enableCellBacking) {
            const float backW = m_cellWidth * 1.18f;
            const float backH = m_cellHeight * 1.06f;

            for (int cy = 0; cy < m_gridHeight; ++cy) {
                // Restamos 0.5 porque la fila 0 es el margen invisible (originCell = baseCell - 1).
                const float centerY = (static_cast<float>(cy) - 0.5f) * m_cellHeight - m_scrollOffsetY;
                const size_t rowOffset = static_cast<size_t>(cy) * static_cast<size_t>(m_gridWidth);

                for (int cx = 0; cx < m_gridWidth; ++cx) {
                    const ZBufferCell& cell = rawCells[rowOffset + static_cast<size_t>(cx)];

                    // Excluimos celdas vacias, terreno/agua/pasto (IS_GROUND) y emisivos/particulas (FALLBACK).
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
        // PASE 1: DETECCION DE SILUETAS + DIBUJADO DE OUTLINES (ASCII Y PIXEL)
        // =========================================================================
        // Al dibujar todos los contornos ANTES que los glifos a color (Pase 2), el contorno
        // oscuro de una celda nunca se dibuja por encima ni mancha el caracter de la celda vecina.
        if (style.enableOutlines) {
            const float px = style.pixelOutlinePx;
            const Color outlineCol = {
                style.pixelOutlineColor.r, style.pixelOutlineColor.g,
                style.pixelOutlineColor.b, style.pixelOutlineColor.a
            };

            for (int cy = 0; cy < m_gridHeight; ++cy) {
                const float centerY = (static_cast<float>(cy) - 0.5f) * m_cellHeight - m_scrollOffsetY;
                const size_t rowOffset = static_cast<size_t>(cy) * static_cast<size_t>(m_gridWidth);

                for (int cx = 0; cx < m_gridWidth; ++cx) {
                    ZBufferCell& cell = rawCells[rowOffset + static_cast<size_t>(cx)];
                    const float centerX = (static_cast<float>(cx) - 0.5f) * m_cellWidth - m_scrollOffsetX;

                    // Consultamos los 4 vecinos ortogonales (arriba, abajo, izquierda, derecha) en O(1).
                    const ZBufferCell* nUp = getNeighbor(cx, cy - 1);
                    const ZBufferCell* nDown = getNeighbor(cx, cy + 1);
                    const ZBufferCell* nLeft = getNeighbor(cx - 1, cy);
                    const ZBufferCell* nRight = getNeighbor(cx + 1, cy);

                    // --- CASO A: Celda de fondo/suelo pegada a un objeto -> Borde direccional ASCII ---
                    if (cell.glyph == '\0' || (cell.flags & CellFlags::IS_GROUND)) {
                        if (!style.enableAsciiBorder) continue;

                        bool up = isSolidOutlineCell(nUp);
                        bool down = isSolidOutlineCell(nDown);
                        bool left = isSolidOutlineCell(nLeft);
                        bool right = isSolidOutlineCell(nRight);

                        if (up || down || left || right) {
                            // Elegimos la orientacion del caracter segun que vecinos estan ocupados:
                            //   pared lateral -> '|' | borde horizontal -> '-' | esquinas -> '/' o '\'.
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

                            // Marcamos IS_SILHOUETTE para que el Pase 2 no pise este borde con un glifo de suelo.
                            cell.flags |= CellFlags::IS_SILHOUETTE;
                            ++stats.outlineCellsDrawn;
                        }
                        continue;
                    }

                    // --- CASO B: Celda de objeto solido -> Detectar perimetro exterior o superposicion ---
                    if (!(cell.flags & CellFlags::WANTS_OUTLINE)) {
                        continue;
                    }

                    bool touchesBackdrop = false;
                    bool touchesOverlap = false;

                    const ZBufferCell* neighbors[4] = { nUp, nDown, nLeft, nRight };
                    for (const ZBufferCell* n : neighbors) {
                        if (isBackdropCell(n)) {
                            // Borde contra el vacio o contra el suelo.
                            touchesBackdrop = true;
                        }
                        else if ((cell.depth - n->depth) > style.overlapDepthThresh) {
                            // Salto brusco de profundidad: esta celda pertenece a un objeto claramente
                            // mas cercano que el objeto que esta detras (ej. arbol delante de otro arbol).
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

                        // Estampamos el contorno de 1px exactamente en la posicion sub-pixel del glifo.
                        const float drawX = centerX + static_cast<float>(cell.subOffsetX) * subScale;
                        const float drawY = centerY + static_cast<float>(cell.subOffsetY) * subScale;

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
        // PASE 2: VOLCADO UNICO DE GLIFOS A COLOR (ZERO OVERDRAW, 1 TEXTURE BATCH)
        // =========================================================================
        for (int cy = 0; cy < m_gridHeight; ++cy) {
            const float centerY = (static_cast<float>(cy) - 0.5f) * m_cellHeight - m_scrollOffsetY;
            const size_t rowOffset = static_cast<size_t>(cy) * static_cast<size_t>(m_gridWidth);

            for (int cx = 0; cx < m_gridWidth; ++cx) {
                const ZBufferCell& cell = rawCells[rowOffset + static_cast<size_t>(cx)];
                unsigned char uc = static_cast<unsigned char>(cell.glyph);

                // Omitimos celdas vacias o caracteres de control no imprimibles.
                if (uc <= 32 || uc >= 127) {
                    continue;
                }
                // Si una celda de suelo ya dibujo un borde ASCII en el Pase 1, no la sobreescribimos.
                if ((cell.flags & CellFlags::IS_GROUND) && (cell.flags & CellFlags::IS_SILHOUETTE)) {
                    continue;
                }

                // Consulta O(1) en la Lookup Table de coordenadas UV del atlas de fuente.
                const Rectangle& src = lut[uc];
                float destW = (src.width > 0.0f) ? (src.width * scaleFactor) : (drawHeight * glyphAspectRatio);
                float destH = (src.height > 0.0f) ? (src.height * scaleFactor) : drawHeight;

                const float drawX = (static_cast<float>(cx) - 0.5f) * m_cellWidth - m_scrollOffsetX
                    + static_cast<float>(cell.subOffsetX) * subScale;
                const float drawY = centerY + static_cast<float>(cell.subOffsetY) * subScale;
                Vector2 origin = { destW * 0.5f, destH * 0.5f };

                Color drawCol = { cell.color.r, cell.color.g, cell.color.b, cell.color.a };

                // Oscurecimiento sutil en celdas superpuestas para despegar objetos del mismo color.
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