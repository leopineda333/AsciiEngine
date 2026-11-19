/**
 * @file main.cpp
 * @brief Ejecutable de prueba (Sandbox) que consume AsciiEngine como libreria estatica.
 */
#include <AsciiEngine/Engine.hpp>

int main() {
    // 1. Instanciamos la fachada del motor sin tocar Raylib directamente.
    AsciiEngine::Context engine;

    // 2. Arrancamos ventana base de 1600x900 segun el criterio de verificacion de Fase 0.
    if (!engine.Initialize(1600, 900, "Sandbox Demo - AsciiEngine")) {
        return -1;
    }

    // 3. Bucle principal totalmente desacoplado.
    while (engine.IsRunning()) {
        float dt = engine.GetFrameTime();
        engine.Update(dt);
        engine.Render();
    }

    // 4. Limpieza ordenada de recursos.
    engine.Shutdown();
    return 0;
}