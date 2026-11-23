// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file main.cpp
 * @brief Aplicacion Sandbox de demostracion consumiendo AsciiEngine.lib via Fachada + Gameplay API.
 *
 * @details Demuestra el desacoplo completo logrado en v1.1.0: el juego decide sus propios controles,
 * dispara FX efimeros (ataque con [E] y hechizo/impacto por raycast con Clic Izquierdo), ajusta
 * EngineSettings en vivo y controla el Editor/HUD sin incluir raylib.h, raygui.h ni json.hpp.
 */
#include <AsciiEngine/Engine.hpp>
#include <cmath>

int main() {
    using namespace AsciiEngine;

    // 1. Inicializacion del motor.
    Context engine;
    if (!engine.Initialize(1600, 900, "AsciiEngine v1.1.0 - Sandbox Demo")) {
        return -1;
    }

    // 2. Configuracion de usuario (carga user_settings.json o crea el preset HIGH por defecto).
    if (!engine.LoadSettings("user_settings.json")) {
        engine.ApplyQualityPreset(QualityPreset::HIGH);
        engine.SaveSettings("user_settings.json");
    }

    // 3. Carga de plantillas JSON y escenario inicial.
    engine.LoadRegistry("assets/data");
    engine.LoadMap("assets/data/maps/forest_demo.json");

    // 4. Bucle principal controlado por la aplicacion cliente.
    while (engine.IsRunning()) {
        const float dt = engine.GetFrameTime();

        // --- Herramientas globales y recarga en caliente ---
        if (engine.IsKeyPressed(Key::F1)) engine.ToggleEditorMode();
        if (engine.IsKeyPressed(Key::F3)) engine.ToggleDebugHUD();
        if (engine.IsKeyPressed(Key::F4)) engine.ToggleDebugOverlays();
        if (engine.IsKeyPressed(Key::F5)) engine.HotReload();

        // --- Atajos de estilo/calidad sobre EngineSettings (disponibles en Juego y Editor) ---
        bool settingsChanged = false;
        EngineSettings cfg = engine.GetSettings();
        if (engine.IsKeyPressed(Key::H)) { cfg.enableShadows = !cfg.enableShadows;        settingsChanged = true; }
        if (engine.IsKeyPressed(Key::O)) { cfg.enableOutlines = !cfg.enableOutlines;       settingsChanged = true; }
        if (engine.IsKeyPressed(Key::B)) { cfg.enableAsciiBorder = !cfg.enableAsciiBorder;    settingsChanged = true; }
        if (engine.IsKeyPressed(Key::G)) { cfg.enableSubPixelOffset = !cfg.enableSubPixelOffset; settingsChanged = true; }
        if (engine.IsKeyPressed(Key::V)) { cfg.enableCellBacking = !cfg.enableCellBacking;    settingsChanged = true; }
        if (settingsChanged) {
            engine.ApplySettings(cfg);
        }

        if (engine.IsKeyPressed(Key::T)) {
            engine.SetPlayerTorchEnabled(!engine.IsPlayerTorchEnabled());
        }

        // --- Controles exclusivos del Modo Juego (se pausan automaticamente dentro del Editor F1) ---
        if (!engine.IsEditorEnabled()) {
            // Movimiento direccional (WASD + Stick Izquierdo analogico) y Sprint (Shift / RT / L3).
            Vec2 moveDir = { 0.0f, 0.0f };
            if (engine.IsKeyDown(Key::W)) moveDir.y -= 1.0f;
            if (engine.IsKeyDown(Key::S)) moveDir.y += 1.0f;
            if (engine.IsKeyDown(Key::A)) moveDir.x -= 1.0f;
            if (engine.IsKeyDown(Key::D)) moveDir.x += 1.0f;

            Vec2 stickL = engine.GetGamepadLeftStick(0);
            if (stickL.x != 0.0f || stickL.y != 0.0f) {
                moveDir = stickL;
            }
            bool wantsSprint = engine.IsKeyDown(Key::LeftShift)
                || engine.GetGamepadAxis(GamepadAxis::RightTrigger, 0) > 0.35f
                || engine.IsGamepadButtonDown(GamepadButton::LeftStick, 0);
            engine.SetControlledMoveInput(moveDir, wantsSprint);

            // Zoom de camara (Rueda del raton + Stick Derecho vertical).
            float wheel = engine.GetMouseWheelDelta();
            if (wheel != 0.0f) {
                engine.AdjustCameraZoom(wheel * 0.12f);
            }
            Vec2 stickR = engine.GetGamepadRightStick(0);
            if (std::abs(stickR.y) > 0.01f) {
                engine.AdjustCameraZoom(-stickR.y * 1.4f * dt);
            }

            // Alternar criatura controlada [TAB] o [LB / RB].
            if (engine.IsKeyPressed(Key::Tab) || engine.IsGamepadButtonPressed(GamepadButton::RightBumper, 0)) {
                engine.CycleControlledCharacter();
            }

            // Accion de ataque [E] o [X / A en mando]: animacion + rafaga + luz temporal.
            if (engine.IsKeyPressed(Key::E)
                || engine.IsGamepadButtonPressed(GamepadButton::FaceLeft, 0)
                || engine.IsGamepadButtonPressed(GamepadButton::FaceDown, 0)) {
                engine.TriggerControlledAttack();
                Vec2  pos = engine.GetControlledPosition();
                float angle = engine.GetControlledFacingRad();
                Vec3  hitPt = { pos.x + std::cos(angle) * 1.15f, pos.y + std::sin(angle) * 1.15f, 1.15f };

                engine.SpawnParticleBurst("impact_burst", hitPt, 16, { 0.25f, 0.25f, 0.20f });
                engine.AddTemporaryLight({ hitPt.x, hitPt.y, 1.45f }, { 255, 200, 115, 255 }, 2.2f, 9.5f, 0.34f, true);
            }

            // Demo de raycast en Modo Juego (Clic Izquierdo): estallido + emisor temporal de chispas en el cursor.
            if (engine.IsMouseButtonPressed(MouseButton::Left)) {
                Vec2 aim = engine.GetCursorWorldPosition();
                engine.SpawnParticleBurst("impact_burst", { aim.x, aim.y, 0.45f }, 20, { 0.30f, 0.30f, 0.18f });
                engine.AddTimedParticleEmitter("fire_sparks", { aim.x, aim.y, 0.35f }, { 0.25f, 0.25f, 0.15f }, 18.0f, 1.8f);
                engine.AddTemporaryLight({ aim.x, aim.y, 1.10f }, { 255, 170, 80, 255 }, 2.4f, 10.5f, 1.8f, true);
            }

            // Controles de entorno (teclado + D-Pad del mando).
            if (engine.IsKeyPressed(Key::Space) || engine.IsGamepadButtonPressed(GamepadButton::Start, 0))
                engine.ToggleDayNightCycle();
            if (engine.IsKeyDown(Key::Left) || engine.IsGamepadButtonDown(GamepadButton::DPadLeft, 0))
                engine.AdjustTimeOfDay(-dt * 4.5f);
            if (engine.IsKeyDown(Key::Right) || engine.IsGamepadButtonDown(GamepadButton::DPadRight, 0))
                engine.AdjustTimeOfDay(dt * 4.5f);
            if (engine.IsGamepadButtonPressed(GamepadButton::FaceUp, 0))
                engine.SetPlayerTorchEnabled(!engine.IsPlayerTorchEnabled());
            if (engine.IsGamepadButtonPressed(GamepadButton::DPadUp, 0))
                engine.CycleBiome();
        }

        engine.Update(dt);
        engine.Render();
    }

    engine.Shutdown();
    return 0;
}