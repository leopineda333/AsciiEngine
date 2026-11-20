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
            }
            return true;
        }

        static constexpr int kOffsets[5][2] = { { 0, 0 },{ -1, 0 },{ 1, 0 },{ 0, -1 },{ 0, 1 } };
        for (int k = 0; k < 5; ++k) {
            int nx = cx + kOffsets[k][0];
            int ny = cy + kOffsets[k][1];
            if (nx < 0 || nx >= m_gridWidth || ny < 0 || ny >= m_gridHeight) {
                continue;
            }

            float effDepth = (k == 0) ? depth : (depth - 0.015f);
            size_t idx = static_cast<size_t>(ny) * static_cast<size_t>(m_gridWidth) + static_cast<size_t>(nx);
            ZBufferCell& cell = m_cells[idx];

            if (effDepth > cell.depth) {
                cell.depth = effDepth;
                cell.glyph = glyph;
                cell.color = color;
                cell.materialId = matId;
                cell.flags = baseFlags;
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

                    // CASO A: Celda vacia o de suelo adyacente a un objeto -> Borde direccional ASCII.
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
                            ++stats.outlineCellsDrawn;

                            // Marca esa celda para que el pase 2 no le dibuje suelo encima del borde
                            cell.flags |= CellFlags::IS_SILHOUETTE;
                        }
                        continue;
                    }

                    // CASO B: Celda de objeto -> Detectar contorno exterior (vacio/suelo) o superposicion.
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

                        if ((cell.flags & CellFlags::IS_GROUND) && (cell.flags & CellFlags::IS_SILHOUETTE)) {
                            continue;
                        }

                        const Rectangle& src = lut[uc];
                        float destW = (src.width > 0.0f) ? (src.width * scaleFactor) : (drawHeight * glyphAspectRatio);
                        float destH = (src.height > 0.0f) ? (src.height * scaleFactor) : drawHeight;
                        Vector2 origin = { destW * 0.5f, destH * 0.5f };

                        DrawTexturePro(font.texture, src, { centerX - px, centerY, destW, destH }, origin, 0.0f, BLACK);
                        DrawTexturePro(font.texture, src, { centerX + px, centerY, destW, destH }, origin, 0.0f, BLACK);
                        DrawTexturePro(font.texture, src, { centerX, centerY - px, destW, destH }, origin, 0.0f, BLACK);
                        DrawTexturePro(font.texture, src, { centerX, centerY + px, destW, destH }, origin, 0.0f, BLACK);
                        ++stats.outlineCellsDrawn;
                    }
                }
            }
        }

        // =========================================================================
        // PASE 2: DIBUJADO LIMPIO DE GLIFOS (TERRENO + OBJETOS, ZERO OVERDRAW)
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

                const Rectangle& src = lut[uc];
                float destW = (src.width > 0.0f) ? (src.width * scaleFactor) : (drawHeight * glyphAspectRatio);
                float destH = (src.height > 0.0f) ? (src.height * scaleFactor) : drawHeight;
                float centerX = (static_cast<float>(cx) - 0.5f) * m_cellWidth - m_scrollOffsetX;
                Vector2 origin = { destW * 0.5f, destH * 0.5f };

                Color drawCol = { cell.color.r, cell.color.g, cell.color.b, cell.color.a };
                if (cell.flags & CellFlags::IS_OVERLAP_EDGE) {
                    drawCol.r = static_cast<unsigned char>(static_cast<float>(drawCol.r) * style.overlapDarkenFactor);
                    drawCol.g = static_cast<unsigned char>(static_cast<float>(drawCol.g) * style.overlapDarkenFactor);
                    drawCol.b = static_cast<unsigned char>(static_cast<float>(drawCol.b) * style.overlapDarkenFactor);
                }

                DrawTexturePro(font.texture, src, { centerX, centerY, destW, destH }, origin, 0.0f, drawCol);
                ++stats.glyphsRendered;
            }
        }

        return stats;
    }

} // namespace AsciiEngine