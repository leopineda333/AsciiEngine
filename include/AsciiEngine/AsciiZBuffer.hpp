// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file AsciiZBuffer.hpp
 * @brief Buffer de profundidad ASCII 2D anclado al mundo (World-Locked) con Sub-Pixel Offset.
 *
 * @details Reemplaza el ordenamiento global O(N log N) por una grilla fija de complejidad O(N)
 * para insercion y O(W * H) para dibujado:
 *   1. Zero Overdraw: cada celda dibuja a lo sumo 1 glifo de color en GPU, sin importar cuantos
 *      objetos se superpongan detras.
 *   2. World-Locked Grid: cuantiza atomos en espacio isometrico del mundo (no de ventana) y
 *      desliza toda la grilla con el residuo de camara, eliminando el character swimming.
 *   3. Sub-Pixel Offset: conserva en 2 bytes (subOffsetX/Y) el desplazamiento exacto respecto
 *      al centro de la celda, rompiendo el aspecto de cuadricula rigida sin perder O(N).
 */
#pragma once

#include <AsciiEngine/Types.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace AsciiEngine {

    /**
     * @brief Mascaras de bits compactas almacenadas en cada ZBufferCell para clasificar bordes y suelo.
     */
    namespace CellFlags {
        constexpr uint8_t NONE = 0;
        constexpr uint8_t WANTS_OUTLINE = 1 << 0; // El material solicita contorno en sus siluetas.
        constexpr uint8_t IS_SILHOUETTE = 1 << 1; // Marcado en Pase 1 si la celda forma un borde visible.
        constexpr uint8_t IS_OVERLAP_EDGE = 1 << 2; // Marcado en Pase 1 si se superpone a otro objeto trasero.
        constexpr uint8_t IS_GROUND = 1 << 3; // Celda de terreno/agua/pasto (tratada como fondo para outlines).
    }

    /**
     * @brief Celda individual del Z-Buffer alineada exactamente a 16 bytes.
     *
     * @details Layout de memoria:
     *   depth (4B) + color (4B) + materialId (2B) + glyph (1B) + flags (1B)
     *   + subOffsetX (1B) + subOffsetY (1B) + _pad (2B) = 16 bytes exactos.
     *   Con 16 bytes caben exactamente 4 celdas por linea de cache de 64 bytes sin cruces de frontera.
     */
    struct ZBufferCell {
        float      depth = -1e9f;                // Profundidad isometrica maxima registrada (Z-Test).
        ColorRGBA  color = { 0, 0, 0, 0 };       // Color final ya iluminado del atomo ganador.
        MaterialId materialId = FALLBACK_MATERIAL_ID; // ID del material (0 excluye relleno volumetrico en Pase 0).
        char       glyph = '\0';                 // Caracter ASCII ganador ('\0' significa celda vacia).
        uint8_t    flags = CellFlags::NONE;      // Combinacion de bits de CellFlags.
        int8_t     subOffsetX = 0;                    // Desplazamiento exacto en px dentro de la celda [-W/2..+W/2].
        int8_t     subOffsetY = 0;                    // Desplazamiento exacto en px dentro de la celda [-H/2..+H/2].
        uint16_t   _pad = 0;                    // Relleno explicito para sellar alineacion a 16 bytes.
    };
    static_assert(sizeof(ZBufferCell) == 16, "ZBufferCell debe medir exactamente 16 bytes.");

    /**
     * @brief Metricas devueltas por FlushToScreen() para alimentar el Debug HUD (F3).
     */
    struct ZBufferStats {
        uint32_t glyphsRendered = 0; // Cantidad de glifos de color dibujados en el Pase 2.
        uint32_t outlineCellsDrawn = 0; // Cantidad de celdas que dibujaron contorno en el Pase 1.
    };

    /**
     * @brief Parametros esteticos configurables en vivo para el volcado en 3 pases del Z-Buffer.
     */
    struct ZBufferStyle {
        bool      enableOutlines = true;  // Tecla [O]: activa/desactiva contornos de silueta.
        bool      enableAsciiBorder = false; // Tecla [B]: dibuja bordes direccionales (|, -, /, \) en perimetro.
        bool      enableSubPixelOffset = true;  // Tecla [G]: posicionamiento organico vs cuadricula rigida.
        bool      enableCellBacking = true;  // Tecla [V]: Pase 0 de relleno volumetrico tras objetos solidos.
        float     backingDarken = 0.42f; // Multiplicador de oscuridad del relleno volumetrico respecto al glifo.
        uint8_t   backingAlpha = 210;   // Opacidad [0..255] del relleno volumetrico.
        float     pixelOutlinePx = 1.0f;  // Grosor en pixeles del rim oscuro en siluetas (Pase 1).
        float     overlapDepthThresh = 2.6f;  // Salto minimo de profundidad para distinguir dos objetos superpuestos.
        float     overlapDarkenFactor = 0.84f; // Oscurece 16% el borde delantero sobre otro objeto para dar relieve.
        ColorRGBA pixelOutlineColor = { 8, 10, 14, 165 };
        ColorRGBA asciiBorderColor = { 18, 22, 28, 170 };
    };

    class AsciiZBuffer {
    public:
        /**
         * @brief Dimensiona la grilla segun el tamano de ventana y tamano de celda en pixeles.
         * @details Solo realoja memoria si cambian las dimensiones; agrega +3 celdas de margen
         * invisible para soportar el scroll sub-celda sin parpadeos en los bordes de pantalla.
         */
        void Resize(int screenWidth, int screenHeight, float cellWidth, float cellHeight);

        /**
         * @brief Reinicia las celdas en O(W * H) sin realojar memoria y ancla la grilla al mundo.
         * @param camTopLeftX Esquina superior izquierda del viewport en espacio isometrico con zoom.
         * @param camTopLeftY Esquina superior izquierda del viewport en espacio isometrico con zoom.
         */
        void BeginFrame(float camTopLeftX, float camTopLeftY);

        /**
         * @brief Inserta un atomo proyectado aplicando Culling Capa 3 y Z-Test en O(1).
         * @param zoomedIsoX Coordenada X isometrica multiplicada por zoom (invariante al pan de camara).
         * @param zoomedIsoY Coordenada Y isometrica multiplicada por zoom (invariante al pan de camara).
         * @param depth Clave de profundidad (MathIso::ComputeDepthKey); sobrevive el valor mayor.
         * @param splatRadius 0 = 1 celda; 1 = cruz de 5 celdas; 2 = vecindad 3x3 (sella huecos en zoom alto).
         * @param isGround true para celdas de suelo/agua/pasto/reflejos que no deben generar silueta propia.
         * @return true si el atomo cayo dentro de la rejilla activa; false si fue descartado (Capa 3).
         * @complexity O(1).
         */
        bool SubmitAtom(float zoomedIsoX, float zoomedIsoY, float depth,
            char glyph, ColorRGBA color, MaterialId matId,
            bool wantsOutline, int splatRadius = 0, bool isGround = false);

        /**
         * @brief Vuelca la grilla completa a pantalla en 3 pases lineales O(W * H):
         *        Pase 0 (Backing volumetrico) -> Pase 1 (Siluetas/Outlines) -> Pase 2 (Glifos a color).
         * @details Recibe la fuente y su LUT como punteros opacos (const void*) para mantener este
         * header publico 100% desacoplado de raylib.h.
         * @complexity O(W * H).
         */
        ZBufferStats FlushToScreen(const void* raylibFontPtr, const void* glyphLutPtr,
            float glyphAspectRatio, const ZBufferStyle& style);

        // --- Consultas rapidas usadas por Engine para rellenar terreno y reportar telemetria ---
        int     GetGridWidth() const { return m_gridWidth; }
        int     GetGridHeight() const { return m_gridHeight; }
        float   GetCellWidth() const { return m_cellWidth; }
        float   GetCellHeight() const { return m_cellHeight; }
        int32_t GetOriginCellX() const { return m_originCellX; }
        int32_t GetOriginCellY() const { return m_originCellY; }
        size_t  GetMemoryBytes() const { return m_cells.capacity() * sizeof(ZBufferCell); }

        /**
         * @brief Comprueba en O(1) si la celda (cx, cy) sigue libre tras insertar objetos y personajes.
         * @details Permite al pase de terreno evaluar sombras y agua unicamente donde el cielo es visible.
         */
        bool IsCellEmpty(int cx, int cy) const {
            return m_cells[static_cast<size_t>(cy) * static_cast<size_t>(m_gridWidth) + static_cast<size_t>(cx)].glyph == '\0';
        }

        /**
         * @brief Escribe directamente una celda de terreno/agua ya validada como vacia.
         * @complexity O(1).
         */
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
        float   m_invCellWidth = 1.0f / 7.0f;  // Inverso precalculado para multiplicar en vez de dividir.
        float   m_invCellHeight = 1.0f / 12.0f;

        // Estado del anclaje World-Locked calculado en BeginFrame():
        int32_t m_originCellX = 0;    // Coordenada entera en mundo de la columna 0 del buffer.
        int32_t m_originCellY = 0;    // Coordenada entera en mundo de la fila 0 del buffer.
        float   m_scrollOffsetX = 0.0f; // Residuo de desplazamiento suave en pixeles enteros [0..cellW).
        float   m_scrollOffsetY = 0.0f; // Residuo de desplazamiento suave en pixeles enteros [0..cellH).

        // Arreglo plano 1D contiguo (indexado como cy * m_gridWidth + cx) para maxima velocidad de cache.
        std::vector<ZBufferCell> m_cells;
    };

} // namespace AsciiEngine