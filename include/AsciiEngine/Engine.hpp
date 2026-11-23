// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file Engine.hpp
 * @brief Fachada publica (Facade + PIMPL) del motor AsciiEngine.
 *
 * @details Unico header necesario para construir un juego o aplicacion sobre AsciiEngine.lib.
 * Expone ciclo de vida, configuracion grafica (EngineSettings), entrada encapsulada, control de
 * personajes, camara, entorno, efectos efimeros y herramientas de editor sin filtrar raylib.h,
 * raygui.h, json.hpp ni windows.h.
 */
#pragma once

#include <AsciiEngine/Types.hpp>
#include <AsciiEngine/Settings.hpp>
#include <AsciiEngine/Input.hpp>
#include <memory>
#include <string>

namespace AsciiEngine {

    /**
     * @brief Instancia principal del motor que administra ciclo de vida, escena, simulacion y renderizado.
     */
    class Context {
    public:
        Context();
        ~Context();

        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;

        // --- Ciclo de vida y datos ---
        bool Initialize(int width, int height, const char* title);
        bool LoadRegistry(const std::string& dataRoot);
        bool LoadMap(const std::string& mapPath);
        bool SaveMap(const std::string& mapPath) const;

        /**
         * @brief Recarga plantillas JSON desde disco en caliente.
         * @details En Modo Editor conserva los cambios en RAM; en Modo Juego recarga tambien el mapa
         *          manteniendo la hora actual del ciclo Dia-Noche.
         */
        void HotReload();

        // --- Configuracion grafica y presets (v1.1.0) ---
        const EngineSettings& GetSettings() const;
        void ApplySettings(const EngineSettings& settings);
        void ApplyQualityPreset(QualityPreset preset);
        bool LoadSettings(const std::string& jsonPath);
        bool SaveSettings(const std::string& jsonPath) const;

        // --- Entrada encapsulada (sin raylib.h en el juego cliente) ---
        bool  IsKeyDown(Key key) const;
        bool  IsKeyPressed(Key key) const;
        bool  IsMouseButtonDown(MouseButton button) const;
        bool  IsMouseButtonPressed(MouseButton button) const;
        float GetMouseWheelDelta() const;

        /**
         * @brief Raycast inverso desde el cursor actual del raton hacia el plano del suelo (Z = 0).
         */
        Vec2  GetCursorWorldPosition() const;

        // --- Gamepad nativo (soporte multi-mando con deadzone radial y gatillos normalizados) ---
        bool  IsGamepadAvailable(int gamepadIndex = 0) const;
        bool  IsGamepadButtonDown(GamepadButton button, int gamepadIndex = 0) const;
        bool  IsGamepadButtonPressed(GamepadButton button, int gamepadIndex = 0) const;
        float GetGamepadAxis(GamepadAxis axis, int gamepadIndex = 0) const;
        Vec2  GetGamepadLeftStick(int gamepadIndex = 0, float deadzone = 0.16f) const;
        Vec2  GetGamepadRightStick(int gamepadIndex = 0, float deadzone = 0.16f) const;

        // --- Control de personajes y entidades ---
        /**
         * @brief Envia la intencion de movimiento del jugador para el frame actual en ejes de pantalla
         *        (ej. W => y = -1, D => x = +1). El motor lo proyecta a coordenadas del mundo isometrico.
         */
        void SetControlledMoveInput(Vec2 screenInputDir, bool sprint = false);
        void TriggerControlledAttack();
        void CycleControlledCharacter();
        bool SpawnCharacter(const std::string& templateId, Vec2 worldPos, float facingDeg = 45.0f,
            bool isPlayer = false, float wanderRadius = 8.0f, float scale = 1.0f);

        Vec2  GetControlledPosition() const;
        float GetControlledFacingRad() const;

        // --- Camara ---
        void  AdjustCameraZoom(float delta);
        void  SetCameraZoom(float zoom);
        float GetCameraZoom() const;

        // --- Entorno, clima e iluminacion ---
        void  SetTimeOfDay(float hours);
        void  AdjustTimeOfDay(float deltaHours);
        void  ToggleDayNightCycle();
        void  SetPlayerTorchEnabled(bool enabled);
        bool  IsPlayerTorchEnabled() const;
        void  TransitionWeather(const std::string& weatherId);
        void  CycleBiome();
        void  ToggleRandomWeather();

        // --- Efectos efimeros (particulas y luces temporales) ---
        void SpawnParticleBurst(const std::string& templateId, Vec3 worldPos, uint32_t count,
            Vec3 areaHalfExtents = { 0.25f, 0.25f, 0.10f });
        void AddTimedParticleEmitter(const std::string& templateId, Vec3 worldPos, Vec3 areaHalfExtents,
            float spawnRate, float durationSec);
        void AddTemporaryLight(Vec3 worldPos, ColorRGBA color, float intensity, float radius,
            float durationSec, bool fadeOut = true);

        // --- Herramientas integradas (Editor Visual y Depuracion) ---
        void ToggleEditorMode();
        bool IsEditorEnabled() const;
        void ToggleDebugHUD();
        void ToggleDebugOverlays();

        // --- Bucle principal ---
        bool  IsRunning() const;
        float GetFrameTime() const;
        void  Update(float dt);
        void  Render() const;
        void  Shutdown();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace AsciiEngine