// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file main.cpp
 * @brief Aplicacion Sandbox de demostracion consumiendo AsciiEngine.lib a traves de su Fachada publica.
 *
 * @details Sirve como prueba viva de encapsulamiento: este ejecutable NO incluye raylib.h,
 * raygui.h, json.hpp ni windows.h. Toda la interaccion con el motor sucede a traves de
 * AsciiEngine::Context, demostrando lo limpio que resulta crear cualquier juego sobre el motor.
 */
#include <AsciiEngine/Engine.hpp>

int main() {
    // 1. Creamos el contexto del motor y abrimos una ventana redimensionable a 60 FPS.
    AsciiEngine::Context engine;
    if (!engine.Initialize(1600, 900, "AsciiEngine v1.0.0 - Sandbox Demo")) {
        return -1;
    }

    // 2. Cargamos el registro data-driven completo:
    //    materiales, prefabs de objetos, anatomias de personajes, biomas, climas y particulas.
    engine.LoadRegistry("assets/data");

    // 3. Cargamos el escenario inicial (bosque con 580+ instancias Flyweight, lago, puente, luces y criaturas).
    engine.LoadMap("assets/data/maps/forest_demo.json");

    // 4. Bucle principal del juego:
    //    - Update(dt): entrada, fisica, IA, animacion procedural, clima, ciclo Dia-Noche y particulas.
    //    - Render():   culling en 3 capas, AsciiZBuffer World-Locked, batch GPU, Debug HUD [F3] y Editor [F1].
    while (engine.IsRunning()) {
        float dt = engine.GetFrameTime();
        engine.Update(dt);
        engine.Render();
    }

    // 5. Liberacion ordenada de recursos graficos (tambien respaldada por RAII en el destructor).
    engine.Shutdown();
    return 0;
}