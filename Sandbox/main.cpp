/**
 * @file main.cpp
 * @brief Ejecutable Sandbox consumiendo AsciiEngine (Fase 1: MathIso + MaterialSystem).
 */
#include <AsciiEngine/Engine.hpp>

int main() {
    AsciiEngine::Context engine;
    if (!engine.Initialize(1600, 900, "Sandbox Demo - AsciiEngine (Fase 1)")) {
        return -1;
    }

    // Carga materials.json desde la raiz del proyecto.
    engine.LoadRegistry("assets/data");

    while (engine.IsRunning()) {
        float dt = engine.GetFrameTime();
        engine.Update(dt);
        engine.Render();
    }

    engine.Shutdown();
    return 0;
}