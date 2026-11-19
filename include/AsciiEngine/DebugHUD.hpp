/**
 * @file DebugHUD.hpp
 * @brief Vista de telemetria en tiempo real (F3) y control de overlays visuales (F4).
 */
#pragma once

#include <AsciiEngine/Types.hpp>
#include <array>

namespace AsciiEngine {

    // Consulta la memoria RAM privada del proceso actual en Megabytes (implementado en PlatformMemory.cpp).
    float QueryProcessPrivateMemoryMB();

    class DebugHUD {
    public:
        void ToggleTelemetry() { m_showTelemetry = !m_showTelemetry; }
        void ToggleOverlays() { m_showOverlays = !m_showOverlays; }

        bool IsTelemetryVisible() const { return m_showTelemetry; }
        bool AreOverlaysVisible() const { return m_showOverlays; }

        // Registra el FPS del frame actual en un Ring Buffer de 60 muestras (cero asignaciones)
        // y actualiza avgFps y processRamMB periodicamente.
        void UpdateMetrics(FrameTelemetry& telemetry, float dt);

        // Dibuja el panel de telemetria semitransparente (F3).
        void RenderPanel(const FrameTelemetry& stats, const void* raylibFontPtr) const;

    private:
        bool m_showTelemetry = true;  // Activo por defecto en Fase 2 para validar el stress test.
        bool m_showOverlays = false; // F4 activa la visualizacion de chunks y frustum.

        // Ring Buffer fijo de 60 muestras para FPS promedio suavizado.
        std::array<float, 60> m_fpsHistory = {};
        size_t m_historyIndex = 0;
        size_t m_historyFilled = 0;

        // Muestreamos RAM cada 0.5 segundos para no invocar syscalls del OS cada frame.
        float m_ramSampleTimer = 0.5f;
        float m_cachedRamMB = 0.0f;
    };

} // namespace AsciiEngine