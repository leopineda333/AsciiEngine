// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file DebugHUD.hpp
 * @brief Panel de telemetria en tiempo real (F3), control de overlays (F4) y lectura de RAM del SO.
 *
 * @details Encapsulado sin exponer raylib.h ni windows.h en la interfaz publica.
 * Mantiene un Ring Buffer estatico de 60 muestras para suavizar la lectura de FPS promedio
 * y muestrea la RAM privada del proceso cada 0.5 segundos para no introducir llamadas al kernel
 * en cada frame.
 */
#pragma once

#include <AsciiEngine/Types.hpp>
#include <array>
#include <cstddef>

namespace AsciiEngine {

    /**
     * @brief Consulta al sistema operativo la memoria RAM privada actual del proceso en megabytes.
     * @details Implementada en src/platform/PlatformMemory.cpp (usa psapi en Windows; 0.0f en otras plataformas).
     *          Aislar esta llamada aqui evita contaminar el motor con headers de sistema (windows.h).
     */
    float QueryProcessPrivateMemoryMB();

    class DebugHUD {
    public:
        /**
         * @brief Alterna la visibilidad del panel de telemetria en pantalla (tecla [F3]).
         */
        void ToggleTelemetry() { m_showTelemetry = !m_showTelemetry; }

        /**
         * @brief Alterna la visibilidad de los overlays geometricos sobre el escenario (tecla [F4]):
         *        chunks activos, zonas de agua/suelo, colisiones, radios de luz y vectores de Sol/Viento.
         */
        void ToggleOverlays() { m_showOverlays = !m_showOverlays; }

        bool IsTelemetryVisible() const { return m_showTelemetry; }
        bool AreOverlaysVisible() const { return m_showOverlays; }

        /**
         * @brief Actualiza el promedio movil de FPS (ultimos 60 frames) y refresca la RAM cada 0.5s.
         * @complexity O(1) — arreglo fijo de 60 elementos sin asignaciones dinamicas.
         */
        void UpdateMetrics(FrameTelemetry& telemetry, float dt);

        /**
         * @brief Renderiza el panel semi-transparente de telemetria sobre la esquina superior izquierda.
         * @param raylibFontPtr Puntero opaco a la fuente de Raylib (const Font*) para preservar el Facade.
         */
        void RenderPanel(const FrameTelemetry& telemetry, const void* raylibFontPtr) const;

    private:
        bool m_showTelemetry = true;  // Visible por defecto al arrancar el sandbox de pruebas.
        bool m_showOverlays = false; // Oculto por defecto para no ensuciar la escena.

        // Buffer circular (Ring Buffer) de 60 ranuras para calcular el promedio movil de FPS.
        std::array<float, 60> m_fpsHistory = {};
        size_t                m_historyIndex = 0;
        size_t                m_historyFilled = 0;

        // Temporizador de muestreo de RAM (0.5s = 2 llamadas al SO por segundo en vez de 60).
        float m_ramSampleTimer = 0.5f; // Arranca en 0.5f para tomar la primera lectura en el frame 1.
        float m_cachedRamMB = 0.0f;
    };

} // namespace AsciiEngine