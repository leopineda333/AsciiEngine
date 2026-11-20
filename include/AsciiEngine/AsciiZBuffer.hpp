/**
 * @file AsciiZBuffer.hpp
 * @brief Buffer de profundidad ASCII 2D en espacio de pantalla (Complejidad O(N)).
 *
 * @details Sustituye el ordenamiento global O(N log N) por una grilla fija alineada
 * a la ventana. Garantiza Zero Overdraw en GPU, cero asignaciones dinamicas por frame
 * Incluye splatting adaptativo por zoom para sellar huecos sin duplicar RAM,
 * y un volcado en 2 pases O(W * H) que combina outline negro (1-2px), borde direccional
 * ASCII (|, -, /, \) y oscurecimiento sutil en bordes de objetos superpuestos
 * y diferenciacion de celdas de suelo (IS_GROUND) para no romper siluetas.
 */
#pragma once

#include <AsciiEngine/Types.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace AsciiEngine {

    namespace CellFlags {
        constexpr uint8_t NONE = 0;
        constexpr uint8_t WANTS_OUTLINE = 1 << 0; // El material solicita contorno.
        constexpr uint8_t IS_SILHOUETTE = 1 << 1; // Borde exterior o frente a salto de profundidad.
        constexpr uint8_t IS_OVERLAP_EDGE = 1 << 2; // Celda delantera superpuesta sobre otro objeto.
        constexpr uint8_t IS_GROUND = 1 << 3; // Celda perteneciente al terreno base (Z = 0).
    }

    struct ZBufferCell {
        float      depth = -1e9f;
        ColorRGBA  color = { 0, 0, 0, 0 };
        MaterialId materialId = FALLBACK_MATERIAL_ID;
        char       glyph = '\0';
        uint8_t    flags = CellFlags::NONE;
    };

    struct ZBufferStats {
        uint32_t glyphsRendered = 0;
        uint32_t outlineCellsDrawn = 0;
    };

    struct ZBufferStyle {
        bool      enableOutlines = true;
        bool      enableAsciiBorder = true;
        float     pixelOutlinePx = 1.5f;
        float     overlapDepthThresh = 2.6f;
        float     overlapDarkenFactor = 0.82f;
        ColorRGBA asciiBorderColor = { 16, 18, 22, 240 };
    };

    class AsciiZBuffer {
    public:


        void Resize(int screenWidth, int screenHeight, float cellWidth, float cellHeight);

        // Limpia el buffer y ancla la grilla a la celda entera del mundo mas cercana a la camara.
        void BeginFrame(float camTopLeftX, float camTopLeftY);

        // Inserta un atomo usando coordenadas isometricas con zoom (invariantes al pan).
        bool SubmitAtom(float zoomedIsoX, float zoomedIsoY, float depth,
            char glyph, ColorRGBA color, MaterialId matId,
            bool wantsOutline, int splatRadius = 0, bool isGround = false);

        ZBufferStats FlushToScreen(const void* raylibFontPtr, const void* glyphLutPtr,
            float glyphAspectRatio, const ZBufferStyle& style);

        int    GetGridWidth() const { return m_gridWidth; }
        int    GetGridHeight() const { return m_gridHeight; }
        float  GetCellWidth() const { return m_cellWidth; }
        float  GetCellHeight() const { return m_cellHeight; }
        size_t GetMemoryBytes() const { return m_cells.capacity() * sizeof(ZBufferCell); }

        int32_t GetOriginCellX() const { return m_originCellX; }
        int32_t GetOriginCellY() const { return m_originCellY; }

        bool IsCellEmpty(int cx, int cy) const {
            return m_cells[static_cast<size_t>(cy) * static_cast<size_t>(m_gridWidth) + static_cast<size_t>(cx)].glyph == '\0';
        }

        void SetGroundCell(int cx, int cy, float depth, char glyph, ColorRGBA color) {
            ZBufferCell& cell = m_cells[static_cast<size_t>(cy) * static_cast<size_t>(m_gridWidth) + static_cast<size_t>(cx)];
            cell.depth = depth;
            cell.glyph = glyph;
            cell.color = color;
            cell.materialId = FALLBACK_MATERIAL_ID;
            cell.flags = CellFlags::IS_GROUND;
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