/**
 * @file AsciiZBuffer.hpp
 * @brief Buffer de profundidad ASCII 2D en espacio de pantalla (Complejidad O(N)).
 *
 * @details Sustituye el ordenamiento global O(N log N) por una grilla fija alineada
 * a la ventana. Garantiza Zero Overdraw en GPU, cero asignaciones dinamicas por frame
 * y deteccion de siluetas (outlines) en O(1) comparando saltos de profundidad vecinos.
 */
#pragma once

#include <AsciiEngine/Types.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace AsciiEngine {

    // Bitflags almacenados por celda para efectos de posprocesamiento en el Z-Buffer.
    namespace CellFlags {
        constexpr uint8_t NONE = 0;
        constexpr uint8_t WANTS_OUTLINE = 1 << 0; // El material solicita silueta negra.
    }

    // Celda compacta de 12 bytes (5 celdas por linea de cache de 64 bytes).
    struct ZBufferCell {
        float      depth = -1e9f; // Profundidad isometrica maxima registrada.
        ColorRGBA  color = { 0, 0, 0, 0 };
        MaterialId materialId = FALLBACK_MATERIAL_ID;
        char       glyph = '\0';  // '\0' indica celda vacia.
        uint8_t    flags = CellFlags::NONE;
    };

    // Estadisticas devueltas por el pase de renderizado del Z-Buffer.
    struct ZBufferStats {
        uint32_t glyphsRendered = 0;
        uint32_t outlineCellsDrawn = 0;
    };

    class AsciiZBuffer {
    public:
        // Dimensiona la grilla segun el tamano de ventana y de cada celda en pixeles.
        // Solo realoja memoria si cambia el tamano de la ventana.
        void Resize(int screenWidth, int screenHeight, float cellWidth, float cellHeight);

        // Reinicia todas las celdas en O(W * H) sin liberar ni realojar memoria.
        void Clear();

        // Inserta un atomo proyectado en O(1). Aplica Culling Capa 3 (limites de pantalla)
        // y prueba de profundidad (solo sobrevive el atomo mas cercano a la camara).
        // Retorna false si el atomo quedo fuera de pantalla.
        bool SubmitAtom(float screenX, float screenY, float depth,
            char glyph, ColorRGBA color, MaterialId matId, bool wantsOutline);

        // Ejecuta el pase lineal final en O(W * H) usando la textura atlas y LUT de la fuente.
        // Se reciben como punteros opacos para no exponer raylib.h en este header publico.
        ZBufferStats FlushToScreen(const void* raylibFontPtr, const void* glyphLutPtr,
            float glyphAspectRatio, bool enableOutlines,
            float outlineDepthThreshold = 2.2f) const;

        int    GetGridWidth() const { return m_gridWidth; }
        int    GetGridHeight() const { return m_gridHeight; }
        float  GetCellWidth() const { return m_cellWidth; }
        float  GetCellHeight() const { return m_cellHeight; }
        size_t GetMemoryBytes() const { return m_cells.capacity() * sizeof(ZBufferCell); }

    private:
        int   m_gridWidth = 0;
        int   m_gridHeight = 0;
        float m_cellWidth = 6.0f;
        float m_cellHeight = 10.0f;
        float m_invCellWidth = 1.0f / 6.0f;  // Precalculado para multiplicar en vez de dividir.
        float m_invCellHeight = 1.0f / 10.0f;

        std::vector<ZBufferCell> m_cells;
    };

} // namespace AsciiEngine