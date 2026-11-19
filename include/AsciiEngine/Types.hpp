/**
 * @file Types.hpp
 * @brief Tipos matematicos, geometricos y de telemetria puros del motor.
 *
 * @details Ningun tipo aqui depende de Raylib ni de la API de Windows para mantener
 * encapsulado el motor detras del patron Facade.
 */
#pragma once

#include <cstdint>

namespace AsciiEngine {

    // Vector 2D para coordenadas de pantalla, proyeccion isometrica y planos 2D.
    struct Vec2 {
        float x = 0.0f;
        float y = 0.0f;
    };

    // Vector 3D para coordenadas cartesianas del mundo (X, Y suelo; Z altura) y normales.
    struct Vec3 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    // Rectangulo 2D alineado a ejes (AABB) en espacio de pantalla para Frustum Culling.
    struct Rect2D {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;

        // Comprueba interseccion AABB vs AABB en O(1) sin ramificaciones costosas.
        bool Intersects(const Rect2D& other) const {
            return (x < other.x + other.width) && (x + width > other.x) &&
                (y < other.y + other.height) && (y + height > other.y);
        }
    };

    // Color compacto de 32 bits (4 bytes contiguos, amigable con cache L1/L2).
    struct ColorRGBA {
        uint8_t r = 255;
        uint8_t g = 255;
        uint8_t b = 255;
        uint8_t a = 255;
    };

    // Identificador numerico de material (2 bytes). Evita comparar strings en el render loop.
    using MaterialId = uint16_t;
    constexpr MaterialId FALLBACK_MATERIAL_ID = 0;

    // Modo de distribucion espacial de los atomos al hornear geometria.
    enum class GridMode : uint8_t {
        REGULAR = 0, // Paso uniforme exacto (solidos, metales, muros).
        NOISY = 1  // Perturbacion determinista y porosidad (hojas, piedra, nieve).
    };

    // Instantanea de telemetria recopilada en cada frame para la vista de Debug (F3).
    struct FrameTelemetry {
        // Rendimiento temporal (en milisegundos y FPS).
        float fps = 0.0f;
        float avgFps = 0.0f;
        float frameTimeMs = 0.0f;
        float simTimeMs = 0.0f;
        float cullingTimeMs = 0.0f;
        float rasterZBufferMs = 0.0f;
        float gpuDrawMs = 0.0f;

        // Contadores de Culling en 3 capas y Z-Buffer.
        uint32_t totalChunks = 0;
        uint32_t activeChunks = 0;
        uint32_t totalInstances = 0;
        uint32_t instancesEvaluated = 0; // Sobrevivieron a Capa 1 (Spatial Hash)
        uint32_t instancesRendered = 0; // Sobrevivieron a Capa 2 (Frustum 2D)
        uint32_t atomsEvaluated = 0; // Atomos enviados al Z-Buffer
        uint32_t atomsCulledScreen = 0; // Descartados en Capa 3 (fuera de pantalla)
        uint32_t glyphsRendered = 0; // Celdas ocupadas dibujadas (Zero Overdraw)
        uint32_t outlineCellsDrawn = 0; // Celdas de contorno que recibieron borde negro

        // Memoria (RAM real del proceso + memoria de subsistemas clave).
        float    processRamMB = 0.0f;
        size_t   zBufferBytes = 0;
        size_t   spatialGridBytes = 0;
        size_t   bakedCloudBytes = 0;

        // Estado de escena y resolucion de grilla.
        Vec2     cameraWorldPos = {};
        float    cameraZoom = 1.0f;
        int      gridWidth = 0;
        int      gridHeight = 0;
        bool     outlinesActive = true;
    };

} // namespace AsciiEngine