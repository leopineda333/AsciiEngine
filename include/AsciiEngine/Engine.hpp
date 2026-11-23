// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file Engine.hpp
 * @brief Fachada publica (Facade + PIMPL) del motor AsciiEngine.
 *
 * @details Este es el UNICO header que necesita incluir un juego o aplicacion cliente para
 * utilizar el motor completo. Al aplicar el idioma PIMPL (Pointer to Implementation) mediante
 * std::unique_ptr<Impl>, ocultamos por completo raylib.h, raygui.h, nlohmann/json y windows.h:
 *   - Tiempos de compilacion minimos en proyectos clientes.
 *   - Cero colisiones de macros o simbolos con otras librerias del usuario.
 *   - Encapsulamiento estricto: cualquier cambio interno en el pipeline no rompe el ABI publico.
 */
#pragma once

#include <memory> // std::unique_ptr para la propiedad exclusiva de Context::Impl.
#include <string>
#include <AsciiEngine/Settings.hpp>

namespace AsciiEngine {

    /**
     * @brief Instancia principal del motor que administra ciclo de vida, escena, simulacion y renderizado.
     */
    class Context {
    public:
        /**
         * @brief Reserva el estado interno (Impl) sin abrir aun la ventana grafica.
         */
        Context();

        /**
         * @brief Destructor definido en Engine.cpp (requisito de std::unique_ptr con tipo incompleto Impl).
         * @details Garantiza el cierre limpio de recursos graficos llamando a Shutdown() automaticamente (RAII).
         */
        ~Context();

        // Un contexto de motor posee recursos unicos de GPU/ventana: prohibimos copias accidentales.
        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;

        /**
         * @brief Inicializa la ventana grafica, carga la tipografia + LUT O(1) y dimensiona el AsciiZBuffer.
         * @param width  Ancho inicial de la ventana en pixeles.
         * @param height Alto inicial de la ventana en pixeles.
         * @param title  Titulo mostrado en la barra de la ventana.
         * @return true si el contexto OpenGL/Raylib quedo listo para operar.
         */
        bool Initialize(int width, int height, const char* title);

        /**
         * @brief Carga y hornea todas las plantillas data-driven desde la raiz de datos (ej. "assets/data").
         * @details Escanea automaticamente: materials/, objects/, characters/, biomes/, weather/ y particles/.
         *          Soporta invocacion repetida en caliente (Hot-Reload con F5).
         */
        bool LoadRegistry(const std::string& dataRoot);

        /**
         * @brief Carga un mapa JSON, construye el SpatialHashGrid, la GroundMask O(1) y el Heightfield 2.5D.
         */
        bool LoadMap(const std::string& mapPath);

        /**
         * @brief Serializa el estado completo actual del escenario (instancias, luces, zonas, entidades, clima) a JSON.
         */
        bool SaveMap(const std::string& mapPath) const;

        /**
         * @brief Devuelve la configuracion grafica y de simulacion activa del motor.
         */
        const EngineSettings& GetSettings() const;

        /**
         * @brief Aplica en tiempo real una configuracion personalizada a todos los subsistemas.
         */
        void ApplySettings(const EngineSettings& settings);

        /**
         * @brief Aplica de golpe un perfil precalibrado (LOW, MEDIUM, HIGH o ULTRA).
         */
        void ApplyQualityPreset(QualityPreset preset);

        /**
         * @brief Carga ajustes de usuario desde un archivo JSON y los aplica de inmediato.
         */
        bool LoadSettings(const std::string& jsonPath);

        /**
         * @brief Guarda la configuracion actual del motor en un archivo JSON de usuario.
         */
        bool SaveSettings(const std::string& jsonPath) const;

        /**
         * @brief Alterna entre el Modo Juego y el Editor Visual de Mapas embebido (tecla [F1]).
         */
        void ToggleEditorMode();

        /**
         * @brief Alterna la visibilidad del panel de telemetria en tiempo real (tecla [F3]).
         */
        void ToggleDebugHUD();

        /**
         * @brief Indica si el motor sigue activo (inicializado y sin solicitud de cierre de ventana).
         */
        bool IsRunning() const;

        /**
         * @brief Devuelve el tiempo transcurrido del ultimo frame en segundos (delta time).
         */
        float GetFrameTime() const;

        /**
         * @brief Ejecuta las Etapas 1 a 4 del pipeline: entrada, fisica, IA, animacion, clima, iluminacion y particulas.
         */
        void Update(float dt);

        /**
         * @brief Ejecuta las Etapas 5 a 8 del pipeline: Culling 3 capas, Z-Buffer World-Locked, Batch GPU, Overlays y UI.
         */
        void Render() const;

        /**
         * @brief Libera la fuente en VRAM y cierra la ventana grafica de forma idempotente (seguro llamarlo varias veces).
         */
        void Shutdown();

    private:
        // Declaracion adelantada del estado interno privado (definido exclusivamente en src/core/Engine.cpp).
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace AsciiEngine