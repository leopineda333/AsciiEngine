// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file Editor.hpp
 * @brief Editor visual de mapas embebido con Raygui, Ghost Preview, pila Undo/Redo y guardado JSON.
 *
 * @details Header 100% agnostico (no expone raylib.h ni raygui.h).
 * Implementa el patron Command mediante instantaneas por valor (EditorCommand) para deshacer y
 * rehacer cualquier alta, baja o modificacion de objetos, luces, personajes y zonas sin riesgo
 * de punteros colgantes al mutar los vectores del mundo.
 */
#pragma once

#include <AsciiEngine/Types.hpp>
#include <AsciiEngine/MathIso.hpp>
#include <AsciiEngine/World.hpp>
#include <AsciiEngine/CharacterSystem.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace AsciiEngine {

    class MaterialSystem;
    class SpatialHashGrid;
    class LightingSystem;
    class WeatherSystem;

    /**
     * @brief Herramienta / categoria activa en el panel izquierdo del Editor Visual.
     */
    enum class EditorCategory : uint8_t {
        OBJECTS = 0, // Colocar prefabs horneados de assets/data/objects/.
        CHARACTERS = 1, // Colocar criaturas/personajes de assets/data/characters/.
        LIGHTS = 2, // Colocar fuentes de luz puntual (antorcha, cristal, orbe).
        ZONES = 3, // Colocar rectangulos de agua o superficies transitables (puentes/pisos).
        SELECT = 4  // Seleccionar instancias existentes para inspeccionarlas o editarlas.
    };

    /**
     * @brief Tipos de operaciones reversibles soportadas por las pilas Undo (Ctrl+Z) y Redo (Ctrl+Y).
     */
    enum class CommandType : uint8_t {
        ADD_INSTANCE,
        REMOVE_INSTANCE,
        MODIFY_INSTANCE,
        ADD_LIGHT,
        REMOVE_LIGHT,
        ADD_CHARACTER,
        REMOVE_CHARACTER,
        ADD_WATER,
        REMOVE_WATER,
        ADD_SURFACE,
        REMOVE_SURFACE
    };

    /**
     * @brief Registro por valor de una accion reversible ejecutada en el editor.
     * @details Guardar copias por valor (beforeInst/afterInst, lightData, etc.) hace que Undo/Redo
     *          sea inmune a invalidaciones de iteradores o realojamientos de std::vector.
     */
    struct EditorCommand {
        CommandType       type = CommandType::ADD_INSTANCE;
        size_t            index = 0; // Indice exacto donde se inserto, elimino o modifico el elemento.
        MapInstance       beforeInst;
        MapInstance       afterInst;
        PointLight        lightData;
        CharacterInstance charData;
        WaterZone         waterData;
        SurfaceZone       surfaceData;
    };

    /**
     * @brief Puente de referencias inyectado cada frame por Engine hacia el Editor.
     * @details Evita que Editor almacene punteros propios a los subsistemas y comunica de vuelta
     *          mediante banderas booleanas (request*) cuando conviene reconstruir estructuras pesadas
     *          como el SpatialHashGrid, la GroundMask o el Heightfield de sombras.
     */
    struct EditorContext {
        World& world;
        MaterialSystem& materials;
        SpatialHashGrid& spatialGrid;
        LightingSystem& lighting;
        WeatherSystem& weather;
        CharacterSystem& characters;
        const MathIso::IsoConfig& isoConfig;

        int          screenWidth = 1600;
        int          screenHeight = 900;
        Vec2& cameraTargetIso;
        Vec2         cameraOffset;
        float& cameraZoom;
        float        minZoom = 0.95f;
        float        maxZoom = 2.5f;
        float& viewPaddingPx;
        std::string& activeMapPath;

        // Banderas de salida leidas por Engine::ProcessEditorRequests() al terminar Update/Render.
        bool requestWorldRebuild = false; // Reconstruye SpatialHashGrid, GroundMask y Heightfield.
        bool requestLightEmittersSync = false; // Resincroniza emisores de chispas de fuego con las luces.
        bool requestHotReload = false; // Recarga plantillas JSON desde disco (F5).
        bool requestReloadMap = false; // Descarta cambios no guardados y reabre el mapa JSON.
    };

    class Editor {
    public:
        void Toggle() { m_enabled = !m_enabled; }
        void SetEnabled(bool enabled) { m_enabled = enabled; }
        bool IsEnabled() const { return m_enabled; }

        /**
         * @brief Vacía seleccion y pilas Undo/Redo al cargar un mapa diferente.
         */
        void ClearHistory();

        /**
         * @brief Muestra una notificacion temporal en la barra inferior del editor.
         */
        void ShowToast(const std::string& msg, float durationSec = 3.0f);

        /**
         * @brief Procesa atajos (Ctrl+Z/Y/S), camara libre, raycast del cursor e interacciones en el mundo.
         */
        void Update(float dt, EditorContext& ctx);

        /**
         * @brief Dibuja el ghost preview isometrico en el escenario y los 3 paneles de Raygui.
         */
        void Render(EditorContext& ctx, const void* raylibFontPtr);

        /**
         * @brief Indica si el puntero del raton esta encima de algun panel UI (bloquea clics al escenario).
         */
        bool IsPointerOverUI(int screenWidth, int screenHeight) const;

    private:
        void PushCommand(EditorCommand cmd);
        void Undo(EditorContext& ctx);
        void Redo(EditorContext& ctx);

        void HandleViewportInteraction(EditorContext& ctx);
        void DrawGhostPreview(const EditorContext& ctx) const;

        bool           m_enabled = false;
        bool           m_styleInit = false; // Aplica la paleta oscura de Raygui una sola vez.
        EditorCategory m_category = EditorCategory::OBJECTS;
        int            m_rightTab = 0;     // 0 = Inspector de Mundo | 1 = Inspector de Seleccion.

        // Indices activos en el panel izquierdo.
        int m_selectedPrefabIdx = 0;
        int m_selectedCharIdx = 0;
        int m_selectedLightPres = 0; // 0 = Antorcha Calida | 1 = Cristal Cian | 2 = Orbe Esmeralda.
        int m_selectedZonePres = 0; // 0 = Estanque Agua   | 1 = Puente Madera | 2 = Patio Piedra.

        // Parametros del pincel de colocacion.
        float      m_brushRotationDeg = 0.0f;
        float      m_brushScale = 1.0f;
        float      m_brushZoneHalfW = 3.0f;
        float      m_brushZoneHalfH = 3.0f;
        MaterialId m_brushOverrideMat = FALLBACK_MATERIAL_ID;
        bool       m_gridSnap = true;
        float      m_gridSnapStep = 1.0f;

        // Seleccion y navegacion en viewport.
        int  m_selectedInstanceIdx = -1;
        bool m_draggingPan = false;
        Vec2 m_lastMouseScreen = {};
        Vec2 m_cursorWorldPos = {};

        // Pilas de historial acotadas a 100 comandos recientes.
        std::vector<EditorCommand> m_undoStack;
        std::vector<EditorCommand> m_redoStack;

        // Estado de aviso temporal (toast) en la barra inferior.
        std::string m_toastMessage;
        float       m_toastTimer = 0.0f;
    };

} // namespace AsciiEngine