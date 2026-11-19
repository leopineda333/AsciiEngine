/**
 * @file main.cpp
 * @brief Ejecutable Sandbox consumiendo AsciiEngine (Fase 3: Prefabs JSON + Colisiones).
 */
#include <AsciiEngine/Engine.hpp>

int main() {
    AsciiEngine::Context engine;
    if (!engine.Initialize(1600, 900, "Sandbox Demo - AsciiEngine (Fase 3)")) {
        return -1;
    }

    // 1. Carga materiales y hornea todos los prefabs en assets/data/objects/.
    engine.LoadRegistry("assets/data");

    // 2. Carga el bosque con 500+ instancias Flyweight y colisionadores.
    engine.LoadMap("assets/data/maps/forest_demo.json");

    while (engine.IsRunning()) {
        float dt = engine.GetFrameTime();
        engine.Update(dt);
        engine.Render();
    }

    engine.Shutdown();
    return 0;
}