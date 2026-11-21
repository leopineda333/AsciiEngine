// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file Types.hpp
 * @brief Tipos matematicos, geometricos, de LOD ambiental y de telemetria puros del motor.
 *
 * @details Este archivo es la piedra angular del patron Facade: al definir nuestros propios
 * tipos POD (Plain Old Data) como Vec2, Vec3, Rect2D y ColorRGBA, cualquier juego o aplicacion
 * que enlace AsciiEngine.lib puede compilar sin incluir raylib.h, raygui.h, json.hpp ni windows.h.
 */
#pragma once

#include <cstddef> // size_t para tamanos en memoria (bytes).
#include <cstdint> // uint8_t, uint16_t, uint32_t para control exacto de tamano y alineacion.
#include <string>  // std::string usado solo en campos de texto de FrameTelemetry (no en el render hot-path).

namespace AsciiEngine {

    /**
     * @brief Vector 2D ligero (8 bytes) para planos horizontales (X, Y), proyeccion isometrica y pantalla.
     */
    struct Vec2 {
        float x = 0.0f;
        float y = 0.0f;
    };

    /**
     * @brief Vector 3D ligero (12 bytes) para posiciones en mundo cartesiano (X, Y suelo; Z altura) y normales.
     */
    struct Vec3 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    /**
     * @brief Caja delimitadora 2D alineada a ejes (AABB) en espacio de pantalla para Frustum Culling (Capa 2).
     */
    struct Rect2D {
        float x = 0.0f; // Esquina superior izquierda X en pixeles.
        float y = 0.0f; // Esquina superior izquierda Y en pixeles.
        float width = 0.0f; // Ancho total en pixeles.
        float height = 0.0f; // Alto total en pixeles.

        /**
         * @brief Prueba de interseccion AABB vs AABB mediante el Teorema de los Ejes Separadores (SAT).
         * @return true si ambos rectangulos se solapan al menos parcialmente.
         * @complexity O(1) — solo 4 comparaciones flotantes sin raices cuadradas.
         */
        bool Intersects(const Rect2D& other) const {
            return (x < other.x + other.width) && (x + width > other.x) &&
                (y < other.y + other.height) && (y + height > other.y);
        }
    };

    /**
     * @brief Color de 32 bits empaquetado (4 bytes contiguos) compatible en layout con formatos GPU RGBA8.
     */
    struct ColorRGBA {
        uint8_t r = 255; // Canal Rojo   [0..255]
        uint8_t g = 255; // Canal Verde  [0..255]
        uint8_t b = 255; // Canal Azul   [0..255]
        uint8_t a = 255; // Canal Alfa   [0..255] (255 = opaco)
    };

    /**
     * @brief Indice compacto de material (2 bytes: soporta hasta 65,535 materiales distintos).
     * @details Evita almacenar o comparar std::string dentro de atomos y celdas del Z-Buffer.
     */
    using MaterialId = uint16_t;

    // El indice 0 siempre apunta al material magenta de respaldo ("missing material") para evitar crashes.
    constexpr MaterialId FALLBACK_MATERIAL_ID = 0;

    /**
     * @brief Estrategia de muestreo espacial aplicada durante el Point-Cloud Baking de un material.
     */
    enum class GridMode : uint8_t {
        REGULAR = 0, // Muestreo uniforme exacto (ideal para metales, madera tallada, arquitectura).
        NOISY = 1  // Perturbacion determinista + porosidad (ideal para follaje, piedra, pelaje, nieve).
    };

    /**
     * @brief Estado instantaneo del campo vectorial de viento compartido entre arboles, pasto y particulas.
     *
     * @details Las fases 'phase1' y 'phase2' se integran incrementalmente cada frame (phase += dt * freq)
     * en lugar de calcular (time * freq). Esto evita saltos violentos de fase cuando la velocidad del
     * viento cambia durante una transicion climatica tras varios minutos de ejecucion.
     */
    struct WindState {
        float dirX = 0.7071f; // Componente X unitaria de la direccion del viento.
        float dirY = 0.7071f; // Componente Y unitaria de la direccion del viento.
        float speed = 2.2f;    // Velocidad actual en unidades de mundo por segundo.
        float gustStrength = 1.0f;    // Multiplicador de intensidad de rafagas.
        float time = 0.0f;    // Reloj acumulado para animacion de reflejos y olas de agua.
        float phase1 = 0.0f;    // Fase integrada de la onda principal del viento.
        float phase2 = 0.0f;    // Fase integrada del armonico secundario del viento.
    };

    /**
     * @brief Perfil dinamico de Nivel de Detalle (LOD) para efectos ambientales segun zoom y distancia.
     * @details Compensa el crecimiento cuadratico del area visible al alejar la camara (1 / zoom^2),
     * manteniendo 60 FPS constantes incluso en tormentas o bosques densos.
     */
    struct EffectLodState {
        float densityScale = 1.0f;   // Factor [0.42..1.0] que escala densidad de particulas y follaje.
        float windEffectRadiusSq = 900.0f; // Radio^2 en mundo alrededor del foco donde los arboles oscilan.
        float foliageRadiusWorld = 26.0f;  // Radio maximo en mundo donde se simula pasto y flores interactivas.
        int   particleRenderStride = 1;      // 1 = renderiza todas las particulas; 2 = salta 1 de cada 2 al alejar.
        int   distantAtomStride = 1;      // 1 = todos los atomos; 2 = submuestreo en objetos fuera del radio LOD.
        bool  allowReflections = true;   // Desactiva reflejos espejados en agua en planos muy abiertos.
    };

    /**
     * @brief Instantanea POD de modificadores climaticos y de suelo del bioma interpolados cada frame.
     * @details Al usar arreglos fijos char[8] en lugar de std::string, WeatherSystem entrega esta
     * estructura a LightingSystem por copia o referencia con cero asignaciones en el heap.
     */
    struct WeatherModifiers {
        ColorRGBA biomeGroundColor = { 74, 98, 72, 255 }; // Tono principal del terreno del bioma.
        ColorRGBA biomeAltGroundColor = { 58, 78, 56, 255 }; // Tono secundario para variacion natural del piso.
        ColorRGBA bgTint = { 50, 54, 60, 255 }; // Tinte atmosferico del clima activo.
        float     bgTintBlend = 0.0f;                // Peso [0..1] del tinte climatico sobre el fondo.
        float     sunMultiplier = 1.0f;                // Atenuacion de luz solar/lunar (ej. nublado/tormenta).
        float     ambientMultiplier = 1.0f;                // Modulador de intensidad de GI hemisferica.
        float     snowCoverage = 0.0f;                // Cobertura acumulada de nieve [0.0..1.0].

        // Glifos ASCII del suelo y de las sombras definidos por el JSON del bioma activo.
        char      groundGlyphs[8] = { '.', ',', '\'' };
        uint8_t   groundGlyphCount = 3;
        char      shadowGlyphs[8] = { ':', ';', ',' };
        uint8_t   shadowGlyphCount = 3;
        float     groundDensity = 0.35f;               // Proporcion de celdas visibles en suelo iluminado.
    };

    /**
     * @brief Fotografia completa de rendimiento, culling, memoria y estado de escena para el Debug HUD (F3).
     */
    struct FrameTelemetry {
        // --- Tiempos por etapa del pipeline (en milisegundos) y tasa de refresco ---
        float fps = 0.0f;
        float avgFps = 0.0f; // Promedio movil de los ultimos 60 frames (Ring Buffer).
        float frameTimeMs = 0.0f; // Delta time total del frame anterior en ms.
        float simTimeMs = 0.0f; // Tiempo de Update(): entrada, fisica, IA, clima y particulas.
        float cullingTimeMs = 0.0f; // Tiempo de Capa 1 (SpatialHashGrid) y culling de luces.
        float rasterZBufferMs = 0.0f; // Tiempo de Capa 2/3, iluminacion, sombras y escritura al Z-Buffer.
        float gpuDrawMs = 0.0f; // Tiempo de FlushToScreen() (volcado OpenGL) + overlays F4.

        // --- Contadores del pipeline de Culling en 3 capas y Z-Buffer ---
        uint32_t totalChunks = 0;
        uint32_t activeChunks = 0;
        uint32_t totalInstances = 0;
        uint32_t instancesEvaluated = 0; // Sobrevivieron a Capa 1 (SpatialHashGrid).
        uint32_t instancesRendered = 0; // Sobrevivieron a Capa 2 (Frustum 2D).
        uint32_t atomsEvaluated = 0; // Total de atomos procesados antes de Capa 3.
        uint32_t atomsCulledScreen = 0; // Atomos descartados en Capa 3 (fuera del viewport).
        uint32_t glyphsRendered = 0; // Celdas ocupadas enviadas a GPU (Zero Overdraw).
        uint32_t outlineCellsDrawn = 0; // Celdas que generaron contorno (pixel o ASCII).
        uint32_t groundFoliageDrawn = 0; // Briznas de pasto y flores activas este frame.

        // --- Consumo de memoria (RAM real del proceso + memoria reservada por subsistemas) ---
        float  processRamMB = 0.0f; // PrivateUsage reportado por el sistema operativo (psapi).
        size_t zBufferBytes = 0;
        size_t spatialGridBytes = 0;
        size_t bakedCloudBytes = 0;    // Suma de prefabs estaticos + plantillas de personajes.
        size_t heightfieldBytes = 0;    // Mapa de alturas 2D para sombras 2.5D.
        size_t particlePoolBytes = 0;    // Pool SoA fijo de particulas.
        size_t groundMaskBytes = 0;    // Mascara 2D de ocupacion del suelo (1 byte/celda).

        // --- Estado de iluminacion y ciclo astronomico ---
        float    timeOfDayHours = 12.0f;
        bool     isNight = false;
        bool     dayCycleRunning = true;
        bool     shadowsEnabled = true;
        bool     torchEnabled = true;
        uint32_t activePointLights = 0;
        uint32_t totalMapLights = 0;

        // --- Estado de Bioma, Clima, Viento y Particulas ---
        std::string biomeName = "Default";
        std::string weatherName = "Clear";
        bool        randomWeatherOn = true;
        float       nextWeatherSec = 0.0f;
        float       windSpeed = 0.0f;
        float       snowCoverage = 0.0f;
        float       effectLodScale = 1.0f;
        uint32_t    activeParticles = 0;
        uint32_t    maxParticles = 0;

        // --- Estado de Personajes Modulares ---
        uint32_t    activeCharacters = 0;
        std::string controlledCharName = "None";
        std::string controlledAnimState = "IDLE";

        // --- Estado de Camara y Rejilla ---
        Vec2  cameraWorldPos = {};
        float cameraZoom = 1.0f;
        int   gridWidth = 0;
        int   gridHeight = 0;
        bool  outlinesActive = true;
    };

} // namespace AsciiEngine