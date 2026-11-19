/**
 * @file Engine.cpp
 * @brief Implementacion del ciclo de vida principal sobre Raylib.
 */
#include <AsciiEngine/Engine.hpp>
#include "raylib.h" // Raylib vive exclusivamente dentro de los .cpp del motor.

namespace AsciiEngine {

    bool Context::Initialize(int width, int height, const char* title) {
        // Activamos ventana redimensionable y sincronizacion basica antes de abrir OpenGL.
        SetConfigFlags(FLAG_WINDOW_RESIZABLE);
        InitWindow(width, height, title);
        SetTargetFPS(60);

        m_initialized = IsWindowReady();
        return m_initialized;
    }

    bool Context::IsRunning() const {
        // WindowShouldClose() retorna true al presionar ESC o la X de la ventana.
        return m_initialized && !WindowShouldClose();
    }

    float Context::GetFrameTime() const {
        // Delegamos el delta time de alta precision a Raylib.
        return ::GetFrameTime();
    }

    void Context::Update(float dt) {
        // Reservado para simulacion ambiental, fisica y entidades en fases posteriores.
        (void)dt;
    }

    void Context::Render() const {
        BeginDrawing();

        // Fondo gris oscuro neutro base (RGB 30, 32, 36) definido en la arquitectura
        // para permitir modular dia (gris claro), noche (negro suave) y nieve.
        ClearBackground({ 30, 32, 36, 255 });

        DrawFPS(15, 15);
        DrawText("AsciiEngine Fase 0 — Facade (.lib) + Sandbox (.exe) OK", 15, 45, 20, LIGHTGRAY);

        EndDrawing();
    }

    void Context::Shutdown() {
        if (m_initialized) {
            CloseWindow();
            m_initialized = false;
        }
    }

} // namespace AsciiEngine