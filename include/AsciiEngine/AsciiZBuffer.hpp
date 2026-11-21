/**
 * @file AsciiZBuffer.hpp
 * @brief Buffer de profundidad ASCII 2D world-locked con posicionamiento Sub-Pixel (O(N)).
 *
 * @details La grilla resuelve oclusion O(1) y Zero Overdraw, mientras cada celda conserva
 * su desplazamiento exacto en pixeles (subOffsetX/Y) respecto al centro de celda. Esto rompe
 * el efecto de cuadricula rigida y permite movimiento organico continuo sin character swimming.
 */
#pragma once

#include <AsciiEngine/Types.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace AsciiEngine {

    namespace CellFlags {
        constexpr uint8_t NONE = 0;
        constexpr uint8_t WANTS_OUTLINE = 1 << 0;
        constexpr uint8_t IS_SILHOUETTE = 1 << 1;
        constexpr uint8_t IS_OVERLAP_EDGE = 1 << 2;
        constexpr uint8_t IS_GROUND = 1 << 3;
    }

    // Celda alineada exactamente a 16 bytes (4 celdas caben en 1 linea de cache L1/L2 de 64 bytes).
    struct ZBufferCell {
        float      depth = -1e9f;
        ColorRGBA  color = { 0, 0, 0, 0 };
        MaterialId materialId = FALLBACK_MATERIAL_ID;
        char       glyph = '\0';
        uint8_t    flags = CellFlags::NONE;
        int8_t     subOffsetX = 0; // Desplazamiento exacto en pixeles dentro de la celda [-W/2 .. +W/2].
        int8_t     subOffsetY = 0; // Desplazamiento exacto en pixeles dentro de la celda [-H/2 .. +H/2].
        uint16_t   _pad = 0;
    };
    static_assert(sizeof(ZBufferCell) == 16, "ZBufferCell debe medir exactamente 16 bytes.");

    struct ZBufferStats {
        uint32_t glyphsRendered = 0;
        uint32_t outlineCellsDrawn = 0;
    };

    struct ZBufferStyle {
        bool      enableOutlines = true;
        bool      enableAsciiBorder = false; // Desactivado por defecto para look realista (tecla [B] lo alterna).
        bool      enableSubPixelOffset = true;  // Tecla [G]: organico vs grilla rigida.
        bool      enableCellBacking = true;  // Tecla [V]: respaldo volumetrico que evita sangrado del fondo gris.
        float     backingDarken = 0.42f; // Oscuridad del relleno respecto al color del glifo.
        uint8_t   backingAlpha = 210;   // Opacidad del relleno de volumen [0..255].
        float     pixelOutlinePx = 1.0f;  // Rim sutil de 1px en lugar de tinta negra gruesa.
        float     overlapDepthThresh = 2.6f;
        float     overlapDarkenFactor = 0.84f;
        ColorRGBA pixelOutlineColor = { 8, 10, 14, 165 };
        ColorRGBA asciiBorderColor = { 18, 22, 28, 170 };
    };

    class AsciiZBuffer {
    public:
        void Resize(int screenWidth, int screenHeight, float cellWidth, float cellHeight);
        void BeginFrame(float camTopLeftX, float camTopLeftY);

        bool SubmitAtom(float zoomedIsoX, float zoomedIsoY, float depth,
            char glyph, ColorRGBA color, MaterialId matId,
            bool wantsOutline, int splatRadius = 0, bool isGround = false);

        ZBufferStats FlushToScreen(const void* raylibFontPtr, const void* glyphLutPtr,
            float glyphAspectRatio, const ZBufferStyle& style);

        int     GetGridWidth() const { return m_gridWidth; }
        int     GetGridHeight() const { return m_gridHeight; }
        float   GetCellWidth() const { return m_cellWidth; }
        float   GetCellHeight() const { return m_cellHeight; }
        int32_t GetOriginCellX() const { return m_originCellX; }
        int32_t GetOriginCellY() const { return m_originCellY; }
        size_t  GetMemoryBytes() const { return m_cells.capacity() * sizeof(ZBufferCell); }

        bool IsCellEmpty(int cx, int cy) const {
            return m_cells[static_cast<size_t>(cy) * static_cast<size_t>(m_gridWidth) + static_cast<size_t>(cx)].glyph == '\0';
        }

        void SetGroundCell(int cx, int cy, float depth, char glyph, ColorRGBA color,
            int8_t subOffsetX = 0, int8_t subOffsetY = 0) {
            ZBufferCell& cell = m_cells[static_cast<size_t>(cy) * static_cast<size_t>(m_gridWidth) + static_cast<size_t>(cx)];
            cell.depth = depth;
            cell.glyph = glyph;
            cell.color = color;
            cell.materialId = FALLBACK_MATERIAL_ID;
            cell.flags = CellFlags::IS_GROUND;
            cell.subOffsetX = subOffsetX;
            cell.subOffsetY = subOffsetY;
        }

    private:
        int     m_gridWidth = 0;
        int     m_gridHeight = 0;
        float   m_cellWidth = 7.0f;
        float   m_cellHeight = 12.0f;
        float   m_invCellWidth = 1.0f / 7.0f;
        float   m_invCellHeight = 1.0f / 12.0f;

        int32_t m_originCellX = 0;
        int32_t m_originCellY = 0;
        float   m_scrollOffsetX = 0.0f;
        float   m_scrollOffsetY = 0.0f;

        std::vector<ZBufferCell> m_cells;
    };

} // namespace AsciiEngine