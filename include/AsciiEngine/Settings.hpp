// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file Settings.hpp
 * @brief Configuracion publica del motor expuesta al juego cliente (presets de calidad y ajustes finos).
 *
 * @details Estructura 100% POD y desacoplada de librerias externas (sin raylib.h ni json.hpp).
 * Permite a cualquier juego construir menus de configuracion propios eligiendo que parametros
 * exponer al jugador (bordes, respaldo volumetrico, influencia atmosferica, sombras, LOD, etc.).
 */
#pragma once

#include <cstdint>
#include <string>

namespace AsciiEngine {

    /**
     * @brief Perfiles de calidad precalibrados para menus rapidos de configuracion grafica.
     */
    enum class QualityPreset : uint8_t {
        LOW = 0, // Rejilla gruesa (7.5x13), sin sombras/reflejos/pasto -> maximo rendimiento en portatiles.
        MEDIUM = 1, // Rejilla media (6.5x11), sombras ligeras (6 pasos), reflejos y pasto moderado.
        HIGH = 2, // Configuracion balanceada por defecto de AsciiEngine (5.5x9.5, sombras 14 pasos).
        ULTRA = 3  // Maxima nitidez tipografica (4.8x8.2), sombras largas (18 pasos) y densidad 125%.
    };

    /**
     * @brief Parametros configurables en tiempo real de renderizado, iluminacion, atmosfera y rendimiento.
     */
    struct EngineSettings {
        // --- 1. Rejilla ASCII y Rendimiento Base ---
        float cellWidthPx = 5.5f;  // Ancho de cada celda ASCII en px [3.5 .. 12.0] (menor = mayor definicion).
        float cellHeightPx = 9.5f;  // Alto de cada celda ASCII en px  [6.0 .. 20.0].
        int   targetFps = 60;    // Limite de FPS (30, 60, 120; 0 = sin limite).

        // --- 2. Estetica del AsciiZBuffer (Bordes, Sub-Pixel y Correccion Volumetrica) ---
        bool    enableOutlines = true;  // Contorno fino en siluetas y bordes superpuestos (Pase 1).
        bool    enableAsciiBorder = false; // Borde direccional clasico con caracteres | - / \ alrededor de objetos.
        bool    enableSubPixelOffset = true;  // Posicionamiento organico sub-pixel (rompe rigidez de cuadricula).
        float   pixelOutlinePx = 1.0f;  // Grosor del contorno de silueta en pixeles [0.5 .. 2.5].
        bool    enableCellBacking = false;  // Respaldo volumetrico tras solidos (Pase 0: evita lavado gris).
        float   backingDarken = 0.42f; // Oscuridad del respaldo volumetrico respecto al glifo [0.10 .. 0.85].
        uint8_t backingAlpha = 210;   // Opacidad del respaldo volumetrico [0 .. 255].

        // --- 3. Iluminacion, Atmosfera y Detalle de Sombras ---
        bool  enableShadows = true;  // Activa o desactiva sombras proyectadas 2.5D por Heightfield.
        int   shadowRaySteps = 14;    // Pasos de raymarch de sombra [0 .. 20] (mas pasos = sombras mas largas).
        bool  enablePointLights = true;  // Activa luces puntuales dinamicas y antorchas.
        float giStrengthScale = 1.0f;  // Multiplicador de iluminacion global hemisferica [0.0 .. 1.6].
        float atmosphericInfluence = 1.0f;  // Intensidad de tintes y atenuacion climatica [0.0 .. 1.0].

        // --- 4. Efectos Ambientales y Follaje ---
        bool  enableWaterReflections = true; // Reflejos espejados ondulantes en WaterZones.
        bool  enableWindSway = true; // Movimiento por campo de viento en arboles y pasto.
        float foliageDensityScale = 1.0f; // Escala de densidad de pasto y flores interactivas [0.0 .. 1.5].
        float particleDensityScale = 1.0f; // Escala de densidad de particulas climaticas [0.0 .. 1.5].
    };

    /**
     * @brief Genera un bloque EngineSettings precalibrado segun el preset de calidad elegido.
     */
    EngineSettings MakeQualityPreset(QualityPreset preset);

    /**
     * @brief Carga un archivo de configuracion de usuario JSON (ej. "user_settings.json").
     * @details Mantiene valores por defecto para cualquier campo ausente y acota rangos invalidos.
     */
    bool LoadEngineSettings(const std::string& jsonPath, EngineSettings& outSettings);

    /**
     * @brief Guarda la configuracion actual en formato JSON legible con sangria de 2 espacios.
     */
    bool SaveEngineSettings(const std::string& jsonPath, const EngineSettings& settings);

} // namespace AsciiEngine