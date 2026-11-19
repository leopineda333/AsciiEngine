/**
 * @file AsciiZBuffer.hpp
 * @brief Buffer de profundidad ASCII 2D en espacio de pantalla (Complejidad O(N)).
 *
 * @details Sustituye el ordenamiento global O(N log N) por una grilla fija alineada
 * a la ventana. Garantiza Zero Overdraw en GPU, cero asignaciones dinamicas por frame
 * Incluye splatting adaptativo por zoom para sellar huecos sin duplicar RAM,
 * y un volcado en 2 pases O(W * H) que combina outline negro (1-2px), borde direccional
 * ASCII (|, -, /, \) y oscurecimiento sutil en bordes de objetos superpuestos.
 */
#pragma once

#include <AsciiEngine/Types.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace AsciiEngine {

    // Bitflags compactos por celda para clasificar siluetas y superposiciones sin memoria extra.
    namespace CellFlags {
        constexpr uint8_t NONE = 0;
        constexpr uint8_t WANTS_OUTLINE = 1 << 0; // El material solicita contorno.
        constexpr uint8_t IS_SILHOUETTE = 1 << 1; // Borde exterior o frente a salto de profundidad.
        constexpr uint8_t IS_OVERLAP_EDGE = 1 << 2; // Celda delantera superpuesta sobre otro objeto.
    }

    // Celda compacta de 12 bytes (5 celdas por linea de cache de 64 bytes).
    struct ZBufferCell {
        float      depth = -1e9f;
        ColorRGBA  color = { 0, 0, 0, 0 };
        MaterialId materialId = FALLBACK_MATERIAL_ID;
        char       glyph = '\0';
        uint8_t    flags = CellFlags::NONE;
    };

    // Estadisticas reportadas al Debug HUD (F3).
    struct ZBufferStats {
        uint32_t glyphsRendered = 0;
        uint32_t outlineCellsDrawn = 0;
    };

    // Parametros esteticos del volcado del Z-Buffer (faciles de ajustar o mover a JSON luego).
    struct ZBufferStyle {
        bool  enableOutlines = true;
        bool  enableAsciiBorder = false;   // Dibuja |, -, /, \ en el perimetro exterior vacio.
        float pixelOutlinePx = 1.5f;   // Grosor del borde negro en pixeles (recomendado: 1.0 a 2.0).
        float overlapDepthThresh = 2.6f;   // Salto minimo de depth para detectar dos objetos distintos.
        float overlapDarkenFactor = 0.82f;  // Oscurece un 18% el borde delantero al superponerse.
        ColorRGBA asciiBorderColor = { 16, 18, 22, 240 };
    };

    class AsciiZBuffer {
    public:
        // Dimensiona la grilla segun el tamano de ventana y de cada celda en pixeles.
        // Solo realoja memoria si cambia el tamano de la ventana.
        void Resize(int screenWidth, int screenHeight, float cellWidth, float cellHeight);

        // Limpia el buffer y ancla la grilla a la celda entera del mundo mas cercana a la camara.
        // Guarda el residuo en pixeles enteros para desplazar toda la rejilla sin character swimming.
        void BeginFrame(float camTopLeftX, float camTopLeftY);

        // Inserta un atomo usando sus coordenadas isometricas con zoom (invariantes al pan de camara).
        bool SubmitAtom(float zoomedIsoX, float zoomedIsoY, float depth,
            char glyph, ColorRGBA color, MaterialId matId,
            bool wantsOutline, int splatRadius = 0);

        ZBufferStats FlushToScreen(const void* raylibFontPtr, const void* glyphLutPtr,
            float glyphAspectRatio, const ZBufferStyle& style);

        int    GetGridWidth() const { return m_gridWidth; }
        int    GetGridHeight() const { return m_gridHeight; }
        float  GetCellWidth() const { return m_cellWidth; }
        float  GetCellHeight() const { return m_cellHeight; }
        size_t GetMemoryBytes() const { return m_cells.capacity() * sizeof(ZBufferCell); }

    private:
        int     m_gridWidth = 0;
        int     m_gridHeight = 0;
        float   m_cellWidth = 7.0f;
        float   m_cellHeight = 12.0f;
        float   m_invCellWidth = 1.0f / 7.0f;
        float   m_invCellHeight = 1.0f / 12.0f;

        // Anclaje World-Locked del frame actual.
        int32_t m_originCellX = 0;
        int32_t m_originCellY = 0;
        float   m_scrollOffsetX = 0.0f;
        float   m_scrollOffsetY = 0.0f;

        std::vector<ZBufferCell> m_cells;
    };

} // namespace AsciiEngine