/**
 * @file Engine.hpp
 * @brief Interfaz publica (Facade) del motor AsciiEngine.
 *
 * @details Este header NO incluye raylib.h ni json.hpp a proposito.
 * Cualquier proyecto externo (como Sandbox) solo ve tipos estandar de C++,
 * manteniendo el acoplamiento en cero y tiempos de compilacion minimos.
 */
#pragma once

namespace AsciiEngine {

    class Context {
    public:
        // Inicializa la ventana grafica, el contexto de OpenGL y el reloj de FPS.
        bool Initialize(int width, int height, const char* title);

        // Indica si la ventana sigue abierta (false al pulsar ESC o cerrar ventana).
        bool IsRunning() const;

        // Devuelve el tiempo transcurrido desde el ultimo frame en segundos (Delta Time).
        float GetFrameTime() const;

        // Actualiza la simulacion (en Fase 0 solo prepara el estado por frame).
        void Update(float dt);

        // Ejecuta el pase de renderizado limpiando con el gris oscuro neutro base.
        void Render() const;

        // Libera el contexto grafico y cierra la ventana limpiamente.
        void Shutdown();

    private:
        bool m_initialized = false; // Evita cierres dobles si Shutdown() se llama dos veces.
    };

} // namespace AsciiEngine