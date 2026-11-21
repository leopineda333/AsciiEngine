/**
 * @file Types.hpp
 * @brief Tipos matematicos, geometricos, ambientales, de LOD ambiental y de telemetria puros del motor.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

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

    using MaterialId = uint16_t;
    constexpr MaterialId FALLBACK_MATERIAL_ID = 0;

    enum class GridMode : uint8_t {
        REGULAR = 0, // Paso uniforme exacto (solidos, metales, muros).
        NOISY = 1  // Perturbacion determinista y porosidad (hojas, piedra, nieve).
    };

    // Estado instantaneo del campo vectorial de viento (compartido por follaje y particulas).
    struct WindState {
        float dirX = 0.7071f;
        float dirY = 0.7071f;
        float speed = 2.2f;
        float gustStrength = 1.0f;
        float time = 0.0f;
        float phase1 = 0.0f; // Fase integrada continuamente (cero sacudidas al cambiar clima).
        float phase2 = 0.0f;
    };
    // Perfil dinamico de LOD de efectos segun zoom y distancia a la camara.
    struct EffectLodState {
        float densityScale = 1.0f;  // [0.35 .. 1.0] escala particulas y follaje de suelo.
        float windEffectRadiusSq = 900.0f; // Radio^2 en mundo donde los arboles calculan viento.
        float foliageRadiusWorld = 26.0f; // Radio en mundo donde se simula pasto/flores interactivas.
        int   particleRenderStride = 1;     // 1 = todas las particulas; 2 = mitad al alejar camara.
        int   distantAtomStride = 1;     // 1 normal; 2 para objetos en los bordes lejanos.
        bool  allowReflections = true;
    };

    // Modificadores atmosfericos y de suelo del bioma (100% POD: cero asignaciones al copiar).
    struct WeatherModifiers {
        ColorRGBA biomeGroundColor = { 74, 98, 72, 255 };
        ColorRGBA biomeAltGroundColor = { 58, 78, 56, 255 };
        ColorRGBA bgTint = { 50, 54, 60, 255 };
        float     bgTintBlend = 0.0f;
        float     sunMultiplier = 1.0f;
        float     ambientMultiplier = 1.0f;
        float     snowCoverage = 0.0f;

        // Glifos y densidad del suelo definidos por el JSON del bioma activo.
        char      groundGlyphs[8] = { '.', ',', '\'' };
        uint8_t   groundGlyphCount = 3;
        char      shadowGlyphs[8] = { ':', ';', ',' };
        uint8_t   shadowGlyphCount = 3;
        float     groundDensity = 0.35f;
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
        uint32_t instancesEvaluated = 0;
        uint32_t instancesRendered = 0;
        uint32_t atomsEvaluated = 0;
        uint32_t atomsCulledScreen = 0;
        uint32_t glyphsRendered = 0;
        uint32_t outlineCellsDrawn = 0;
        uint32_t groundFoliageDrawn = 0;

        // Memoria (RAM real del proceso + subsistemas clave).
        float  processRamMB = 0.0f;
        size_t zBufferBytes = 0;
        size_t spatialGridBytes = 0;
        size_t bakedCloudBytes = 0;
        size_t heightfieldBytes = 0;
        size_t particlePoolBytes = 0;
        size_t groundMaskBytes = 0;

        // Estado de iluminacion y ciclo dia/noche (Fase 4).
        float    timeOfDayHours = 12.0f;
        bool     isNight = false;
        bool     dayCycleRunning = true;
        bool     shadowsEnabled = true;
        bool     torchEnabled = true;
        uint32_t activePointLights = 0;
        uint32_t totalMapLights = 0;

        // Estado de Bioma, Clima, Viento y Particulas (Fase 5).
        std::string biomeName = "Default";
        std::string weatherName = "Clear";
        bool        randomWeatherOn = true;
        float       nextWeatherSec = 0.0f;
        float       windSpeed = 0.0f;
        float       snowCoverage = 0.0f;
        float       effectLodScale = 1.0f;
        uint32_t    activeParticles = 0;
        uint32_t    maxParticles = 0;

        Vec2  cameraWorldPos = {};
        float cameraZoom = 1.0f;
        int   gridWidth = 0;
        int   gridHeight = 0;
        bool  outlinesActive = true;
    };

} // namespace AsciiEngine