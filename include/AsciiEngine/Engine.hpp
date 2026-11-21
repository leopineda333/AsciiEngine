/**
 * @file Engine.hpp
 * @brief Fachada publica (Facade) del motor AsciiEngine.
 *
 * @details Usa el patron Pimpl (Pointer to Implementation) para ocultar completamente
 * los tipos internos de Raylib (Font, Rectangle, Camera2D) al proyecto consumidor.
 */
#pragma once

#include <memory>
#include <string>

namespace AsciiEngine {

    class Context {
    public:
        Context();
        ~Context();

        // Deshabilitamos copia para proteger la propiedad unica del contexto grafico.
        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;

        // Inicializa ventana, atlas tipografico con LUT O(1) y subsistemas base.
        bool Initialize(int width, int height, const char* title);

        // Carga los archivos de datos JSON (en Fase 1: materials.json).
        bool LoadRegistry(const std::string& dataRoot);

        // Carga las instancias del mapa JSON y construye el SpatialHashGrid.
        bool LoadMap(const std::string& mapPath);

        // Guarda el mapa desde el editor a un JSON
        bool SaveMap(const std::string& mapPath) const;

        // Utilidades del editor de mapas
        void ToggleEditorMode();
        void ToggleDebugHUD();

        // Indica si la ventana sigue abierta.
        bool IsRunning() const;

        // Tiempo transcurrido del ultimo frame en segundos (Delta Time).
        float GetFrameTime() const;

        // Actualiza controles interactivos de prueba y simulacion.
        void Update(float dt);

        // Ejecuta el renderizado de la escena activa.
        void Render() const;

        // Libera fuentes, memoria y cierra Raylib.
        void Shutdown();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace AsciiEngine