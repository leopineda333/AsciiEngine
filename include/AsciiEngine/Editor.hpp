/**
 * @file Editor.hpp
 * @brief Editor visual embebido con Raygui: colocacion por raycast, inspector de mundo, Undo/Redo y JSON.
 *
 * @details Header 100% encapsulado (no expone raylib.h ni raygui.h). Permite listar y colocar
 * objetos, personajes, luces y zonas de agua/suelo, configurar el entorno y serializar a JSON.
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

    // Pestanas principales del navegador de templates (Panel Izquierdo).
    enum class EditorCategory : uint8_t {
        OBJECTS = 0,
        CHARACTERS = 1,
        LIGHTS = 2,
        ZONES = 3,
        SELECT = 4
    };

    // Acciones discretas soportadas por la pila Undo/Redo (Ctrl+Z / Ctrl+Y).
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

    struct EditorCommand {
        CommandType       type = CommandType::ADD_INSTANCE;
        size_t            index = 0;
        MapInstance       beforeInst;
        MapInstance       afterInst;
        PointLight        lightData;
        CharacterInstance charData;
        WaterZone         waterData;
        SurfaceZone       surfaceData;
    };

    // Referencias a los subsistemas del motor que manipula el editor en tiempo real.
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

        // Banderas de salida para que Engine reconstruya estructuras pesadas solo al ocurrir un cambio.
        bool requestWorldRebuild = false;
        bool requestLightEmittersSync = false;
        bool requestHotReload = false;
        bool requestReloadMap = false;
    };

    class Editor {
    public:
        void Toggle() { m_enabled = !m_enabled; }
        void SetEnabled(bool enabled) { m_enabled = enabled; }
        bool IsEnabled() const { return m_enabled; }

        // Limpia seleccion y pilas Undo/Redo al cargar un mapa nuevo.
        void ClearHistory();

        // Muestra un aviso temporal en la barra inferior (ej. "Mapa guardado").
        void ShowToast(const std::string& msg, float durationSec = 3.0f);

        // Procesa atajos, raycast del mouse, pan libre de camara y colocacion/borrado en el viewport.
        void Update(float dt, EditorContext& ctx);

        // Dibuja el ghost preview en el mundo + paneles Raygui (Izquierdo, Derecho y Barra Inferior).
        void Render(EditorContext& ctx, const void* raylibFontPtr);

        // Indica si el cursor esta sobre alguno de los paneles del editor (bloquea zoom/clics al mundo).
        bool IsPointerOverUI(int screenWidth, int screenHeight) const;

    private:
        void PushCommand(EditorCommand cmd);
        void Undo(EditorContext& ctx);
        void Redo(EditorContext& ctx);

        void HandleViewportInteraction(EditorContext& ctx);
        void DrawGhostPreview(const EditorContext& ctx) const;

        bool           m_enabled = false;
        bool           m_styleInit = false;
        EditorCategory m_category = EditorCategory::OBJECTS;
        int            m_rightTab = 0; // 0 = Mundo, 1 = Seleccion / Pincel.

        // Indices seleccionados en las listas de plantillas.
        int m_selectedPrefabIdx = 0;
        int m_selectedCharIdx = 0;
        int m_selectedLightPres = 0; // 0=Antorcha Calida, 1=Cristal Cian, 2=Orbe Verde.
        int m_selectedZonePres = 0; // 0=Estanque Agua, 1=Puente Madera, 2=Patio Piedra.

        // Parametros del pincel de colocacion.
        float      m_brushRotationDeg = 0.0f;
        float      m_brushScale = 1.0f;
        float      m_brushZoneHalfW = 3.0f;
        float      m_brushZoneHalfH = 3.0f;
        MaterialId m_brushOverrideMat = FALLBACK_MATERIAL_ID;
        bool       m_gridSnap = true;
        float      m_gridSnapStep = 1.0f;

        // Estado de seleccion de una instancia ya colocada en el mapa.
        int        m_selectedInstanceIdx = -1;
        bool       m_draggingPan = false;
        Vec2       m_lastMouseScreen = {};
        Vec2       m_cursorWorldPos = {};

        // Pilas de comandos Undo / Redo.
        std::vector<EditorCommand> m_undoStack;
        std::vector<EditorCommand> m_redoStack;

        // Mensaje temporal de confirmacion en barra inferior.
        std::string m_toastMessage;
        float       m_toastTimer = 0.0f;
    };

} // namespace AsciiEngine