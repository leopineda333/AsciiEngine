// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file DebugHUD.cpp
 * @brief Implementacion comentada linea por linea del panel de telemetria F3 y Ring Buffer de FPS.
 */
#include <AsciiEngine/DebugHUD.hpp>
#include "raylib.h" // Dependencia privada: usada aqui para primitivas 2D y TextFormat().

#include <cmath> // std::floor para formatear horas y minutos del ciclo Dia-Noche.

namespace AsciiEngine {

    void DebugHUD::UpdateMetrics(FrameTelemetry& telemetry, float dt) {
        // 1. Insertamos los FPS actuales en el buffer circular sobrescribiendo la muestra mas antigua.
        m_fpsHistory[m_historyIndex] = telemetry.fps;
        m_historyIndex = (m_historyIndex + 1) % m_fpsHistory.size();
        if (m_historyFilled < m_fpsHistory.size()) {
            ++m_historyFilled;
        }

        // 2. Calculamos el promedio aritmetico de las muestras acumuladas (hasta 60 frames).
        float sum = 0.0f;
        for (size_t i = 0; i < m_historyFilled; ++i) {
            sum += m_fpsHistory[i];
        }
        telemetry.avgFps = (m_historyFilled > 0) ? (sum / static_cast<float>(m_historyFilled)) : telemetry.fps;

        // 3. Muestreamos la memoria privada del proceso cada 500 ms para no penalizar el frame time.
        m_ramSampleTimer += dt;
        if (m_ramSampleTimer >= 0.5f) {
            m_cachedRamMB = QueryProcessPrivateMemoryMB();
            m_ramSampleTimer = 0.0f;
        }
        telemetry.processRamMB = m_cachedRamMB;
    }

    void DebugHUD::RenderPanel(const FrameTelemetry& s, const void* raylibFontPtr) const {
        if (!m_showTelemetry || !raylibFontPtr) {
            return;
        }

        const Font& font = *static_cast<const Font*>(raylibFontPtr);
        const float fontSize = 15.0f;
        const float lineH = 19.0f;
        const float panelX = 14.0f;
        const float panelY = 14.0f;
        const float panelW = 635.0f;
        const float panelH = 500.0f;

        // 1. Fondo semitransparente oscuro y borde sutil tipo consola de diagnostico.
        DrawRectangleRounded({ panelX, panelY, panelW, panelH }, 0.03f, 6, { 12, 14, 18, 224 });
        DrawRectangleRoundedLinesEx({ panelX, panelY, panelW, panelH }, 0.03f, 6, 1.5f, { 75, 110, 130, 200 });

        float       y = panelY + 12.0f;
        const float x = panelX + 14.0f;

        // Helper local que dibuja una linea y avanza el cursor vertical automaticamente.
        auto drawLine = [&](const char* text, Color col) {
            DrawTextEx(font, text, { x, y }, fontSize, 1.0f, col);
            y += lineH;
            };

        // Semaforo visual de rendimiento: verde >= 58 FPS, amarillo >= 40 FPS, rojo < 40 FPS.
        Color fpsCol = (s.avgFps >= 58.0f) ? GREEN : ((s.avgFps >= 40.0f) ? YELLOW : RED);

        // Descomponemos timeOfDayHours (ej. 9.5h) en formato reloj legible (09:30).
        int hours = static_cast<int>(std::floor(s.timeOfDayHours)) % 24;
        int minutes = static_cast<int>((s.timeOfDayHours - std::floor(s.timeOfDayHours)) * 60.0f);

        // --- SECCION 1: TIEMPOS DEL PIPELINE Y FPS ---
        drawLine("============= ASCII ENGINE TELEMETRY [F3] =============", SKYBLUE);
        drawLine(TextFormat("FPS: %.0f  (Avg 60f: %.1f)  | Frame: %.2f ms", s.fps, s.avgFps, s.frameTimeMs), fpsCol);
        drawLine(TextFormat("Etapas -> Sim: %.2f | Cull: %.2f | ZBuf: %.2f | GPU: %.2f ms",
            s.simTimeMs, s.cullingTimeMs, s.rasterZBufferMs, s.gpuDrawMs), LIGHTGRAY);

        // --- SECCION 2: ILUMINACION, BIOMA, CLIMA, VIENTO Y PARTICULAS ---
        y += 5.0f;
        drawLine("--- ILUMINACION, CLIMA, VIENTO & PARTICULAS ---", GOLD);
        drawLine(TextFormat("Hora: %02d:%02d [%s] (%s) | Sombras [H]: %s | Antorcha [T]: %s",
            hours, minutes,
            s.isNight ? "NOCHE" : "DIA",
            s.dayCycleRunning ? "AUTO" : "PAUSA",
            s.shadowsEnabled ? "ON" : "OFF",
            s.torchEnabled ? "ON" : "OFF"), RAYWHITE);
        drawLine(TextFormat("Bioma [M]: %s | Clima [1/2/3]: %s (%.0fs)",
            s.biomeName.c_str(), s.weatherName.c_str(),
            s.randomWeatherOn ? s.nextWeatherSec : 0.0f), SKYBLUE);
        drawLine(TextFormat("Viento: %.1f | Nieve: %.0f%% | LOD: %.0f%% | Part: %u/%u | Pasto: %u",
            s.windSpeed, s.snowCoverage * 100.0f, s.effectLodScale * 100.0f,
            s.activeParticles, s.maxParticles, s.groundFoliageDrawn), LIGHTGRAY);

        // --- SECCION 3: CULLING EN 3 CAPAS Y ZERO OVERDRAW ---
        y += 5.0f;
        drawLine("--- CULLING EN 3 CAPAS & Z-BUFFER (ZERO OVERDRAW) ---", GOLD);
        drawLine(TextFormat("Capa 1 (Chunks):     %u activos / %u totales | Luces: %u/%u",
            s.activeChunks, s.totalChunks, s.activePointLights, s.totalMapLights), RAYWHITE);
        drawLine(TextFormat("Capa 2 (Instancias): %u visibles / %u eval / %u mapa",
            s.instancesRendered, s.instancesEvaluated, s.totalInstances), RAYWHITE);
        drawLine(TextFormat("Capa 3 (Atomos):     %u evaluados | %u fuera pantalla",
            s.atomsEvaluated, s.atomsCulledScreen), LIGHTGRAY);
        drawLine(TextFormat("Z-Buffer (%dx%d):  %u glifos | %u bordes [%s]",
            s.gridWidth, s.gridHeight, s.glyphsRendered, s.outlineCellsDrawn,
            s.outlinesActive ? "ON" : "OFF"), LIME);

        // --- SECCION 4: CONSUMO DE MEMORIA POR SUBSISTEMA ---
        y += 5.0f;
        drawLine("--- MEMORIA DEL PROCESO Y SUBSISTEMAS ---", GOLD);
        drawLine(TextFormat("RAM Privada (SO): %.2f MB | Particulas SoA: %.0f KB | Mask: %.0f KB",
            s.processRamMB,
            static_cast<float>(s.particlePoolBytes) / 1024.0f,
            static_cast<float>(s.groundMaskBytes) / 1024.0f), RAYWHITE);
        drawLine(TextFormat("ZBuf: %.0f KB | Grid: %.0f KB | Clouds: %.0f KB | HF: %.0f KB",
            static_cast<float>(s.zBufferBytes) / 1024.0f,
            static_cast<float>(s.spatialGridBytes) / 1024.0f,
            static_cast<float>(s.bakedCloudBytes) / 1024.0f,
            static_cast<float>(s.heightfieldBytes) / 1024.0f), LIGHTGRAY);

        // --- SECCION 5: CONTROL DE PERSONAJES Y ATAJOS ---
        y += 5.0f;
        drawLine("--- ESCENA Y CONTROLES ---", GOLD);
        drawLine(TextFormat("Control [TAB]: %s [%s] (%u activos) | Zoom: %.2fx",
            s.controlledCharName.c_str(), s.controlledAnimState.c_str(),
            s.activeCharacters, s.cameraZoom), RAYWHITE);
        drawLine("[F1] Editor | [1/2/3] Clima | [M] Bioma", { 160, 175, 190, 255 });
        drawLine("[E] Accion | [IZQ/DER] Hora", { 160, 175, 190, 255 });
        drawLine("[WASD+Shift] Mover | [O/B/G/V] Estilo", { 160, 175, 190, 255 });
        drawLine("[F4] Overlays | [F5] Reload", { 160, 175, 190, 255 });
    }

} // namespace AsciiEngine