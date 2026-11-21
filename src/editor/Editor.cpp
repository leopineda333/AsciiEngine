/**
 * @file Editor.cpp
 * @brief Implementacion del Editor Visual de Mapas con Raygui 4.0+, Ghost Preview y Undo/Redo.
 */
#include <AsciiEngine/Editor.hpp>
#include <AsciiEngine/MaterialSystem.hpp>
#include <AsciiEngine/CullingSystem.hpp>
#include <AsciiEngine/LightingSystem.hpp>
#include <AsciiEngine/WeatherSystem.hpp>

#include "raylib.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244 4267 4996)
#endif
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <array>
#include <cmath>

namespace AsciiEngine {

    namespace {

        constexpr float LEFT_PANEL_W = 255.0f;
        constexpr float RIGHT_PANEL_W = 315.0f;
        constexpr float BOTTOM_BAR_H = 44.0f;
        constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;

        Vec2 IsoToScreen(Vec2 isoPt, const EditorContext& ctx) {
            return {
                (isoPt.x - ctx.cameraTargetIso.x) * ctx.cameraZoom + ctx.cameraOffset.x,
                (isoPt.y - ctx.cameraTargetIso.y) * ctx.cameraZoom + ctx.cameraOffset.y
            };
        }

        Vec2 ScreenToIso(Vec2 scrPt, const EditorContext& ctx) {
            float invZoom = 1.0f / ctx.cameraZoom;
            return {
                (scrPt.x - ctx.cameraOffset.x) * invZoom + ctx.cameraTargetIso.x,
                (scrPt.y - ctx.cameraOffset.y) * invZoom + ctx.cameraTargetIso.y
            };
        }

        void DrawIsoCircleScreen(Vec2 centerWorld, float radius, Color col, const EditorContext& ctx) {
            constexpr int segs = 20;
            constexpr float step = 6.2831853f / static_cast<float>(segs);
            Vec2 prev = IsoToScreen(MathIso::WorldToIso({ centerWorld.x + radius, centerWorld.y, 0.05f }, ctx.isoConfig), ctx);
            for (int i = 1; i <= segs; ++i) {
                float a = static_cast<float>(i) * step;
                Vec2 cur = IsoToScreen(MathIso::WorldToIso(
                    { centerWorld.x + std::cos(a) * radius, centerWorld.y + std::sin(a) * radius, 0.05f },
                    ctx.isoConfig), ctx);
                DrawLineEx({ prev.x, prev.y }, { cur.x, cur.y }, 1.8f, col);
                prev = cur;
            }
        }

        void DrawIsoRectScreen(Vec2 center, Vec2 halfSize, float rotDeg, Color col, const EditorContext& ctx) {
            float rad = rotDeg * DEG_TO_RAD;
            float c = std::cos(rad);
            float s = std::sin(rad);
            const std::array<Vec2, 4> local = { {
                { -halfSize.x, -halfSize.y },{ halfSize.x, -halfSize.y },
                { halfSize.x,  halfSize.y },{ -halfSize.x,  halfSize.y }
            } };
            std::array<Vec2, 4> scr;
            for (size_t i = 0; i < 4; ++i) {
                float wx = center.x + (local[i].x * c - local[i].y * s);
                float wy = center.y + (local[i].x * s + local[i].y * c);
                scr[i] = IsoToScreen(MathIso::WorldToIso({ wx, wy, 0.05f }, ctx.isoConfig), ctx);
            }
            for (size_t i = 0; i < 4; ++i) {
                DrawLineEx({ scr[i].x, scr[i].y }, { scr[(i + 1) % 4].x, scr[(i + 1) % 4].y }, 2.0f, col);
            }
        }

        // Configura un tema oscuro sobrio de Raygui acorde a la estetica terminal del motor.
        void ApplyDarkTerminalTheme(const Font& font) {
            GuiLoadStyleDefault();
            GuiSetFont(font);
            GuiSetStyle(DEFAULT, TEXT_SIZE, 14);
            GuiSetStyle(DEFAULT, BACKGROUND_COLOR, 0x12151aff);
            GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL, 0x1e242dff);
            GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, 0xd6dfe8ff);
            GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, 0x3b4958ff);
            GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED, 0x2b3948ff);
            GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED, 0xffffffff);
            GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, 0x5ea2d6ff);
            GuiSetStyle(DEFAULT, BASE_COLOR_PRESSED, 0x2c648aff);
            GuiSetStyle(DEFAULT, TEXT_COLOR_PRESSED, 0xffffffff);
            GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED, 0x7cc4f5ff);
            GuiSetStyle(DEFAULT, LINE_COLOR, 0x3b4958ff);
        }

    } // namespace anonimo

    void Editor::ClearHistory() {
        m_undoStack.clear();
        m_redoStack.clear();
        m_selectedInstanceIdx = -1;
    }

    void Editor::ShowToast(const std::string& msg, float durationSec) {
        m_toastMessage = msg;
        m_toastTimer = durationSec;
    }

    bool Editor::IsPointerOverUI(int screenWidth, int screenHeight) const {
        if (!m_enabled) return false;
        Vector2 mp = GetMousePosition();
        if (mp.x <= LEFT_PANEL_W) return true;
        if (mp.x >= static_cast<float>(screenWidth) - RIGHT_PANEL_W) return true;
        if (mp.y >= static_cast<float>(screenHeight) - BOTTOM_BAR_H) return true;
        return false;
    }

    void Editor::PushCommand(EditorCommand cmd) {
        m_undoStack.push_back(std::move(cmd));
        m_redoStack.clear();
        // Limitamos la pila a 100 acciones recientes para mantener uso de memoria acotado.
        if (m_undoStack.size() > 100) {
            m_undoStack.erase(m_undoStack.begin());
        }
    }

    void Editor::Undo(EditorContext& ctx) {
        if (m_undoStack.empty()) {
            ShowToast("Nada que deshacer", 1.5f);
            return;
        }
        EditorCommand cmd = m_undoStack.back();
        m_undoStack.pop_back();

        switch (cmd.type) {
        case CommandType::ADD_INSTANCE:
            ctx.world.RemoveInstance(cmd.index, ctx.spatialGrid);
            if (m_selectedInstanceIdx == static_cast<int>(cmd.index)) m_selectedInstanceIdx = -1;
            ctx.requestWorldRebuild = true;
            break;
        case CommandType::REMOVE_INSTANCE:
            ctx.world.InsertInstanceAt(cmd.index, cmd.beforeInst, ctx.spatialGrid);
            ctx.requestWorldRebuild = true;
            break;
        case CommandType::MODIFY_INSTANCE:
            ctx.world.UpdateInstanceTransform(cmd.index, cmd.beforeInst.position, cmd.beforeInst.rotationDeg,
                cmd.beforeInst.scale, cmd.beforeInst.overrideMaterialId, ctx.spatialGrid);
            ctx.requestWorldRebuild = true;
            break;
        case CommandType::ADD_LIGHT: {
            auto& lights = ctx.lighting.GetMutableMapLights();
            if (cmd.index < lights.size()) lights.erase(lights.begin() + static_cast<ptrdiff_t>(cmd.index));
            ctx.requestLightEmittersSync = true;
            break;
        }
        case CommandType::REMOVE_LIGHT: {
            auto& lights = ctx.lighting.GetMutableMapLights();
            size_t pos = std::min(cmd.index, lights.size());
            lights.insert(lights.begin() + static_cast<ptrdiff_t>(pos), cmd.lightData);
            ctx.requestLightEmittersSync = true;
            break;
        }
        case CommandType::ADD_CHARACTER:
            ctx.characters.RemoveCharacter(cmd.index);
            break;
        case CommandType::REMOVE_CHARACTER: {
            auto& chars = ctx.characters.GetMutableInstances();
            size_t pos = std::min(cmd.index, chars.size());
            chars.insert(chars.begin() + static_cast<ptrdiff_t>(pos), cmd.charData);
            break;
        }
        case CommandType::ADD_WATER: {
            auto& wz = ctx.world.GetMutableWaterZones();
            if (cmd.index < wz.size()) wz.erase(wz.begin() + static_cast<ptrdiff_t>(cmd.index));
            ctx.requestWorldRebuild = true;
            break;
        }
        case CommandType::REMOVE_WATER: {
            auto& wz = ctx.world.GetMutableWaterZones();
            size_t pos = std::min(cmd.index, wz.size());
            wz.insert(wz.begin() + static_cast<ptrdiff_t>(pos), cmd.waterData);
            ctx.requestWorldRebuild = true;
            break;
        }
        case CommandType::ADD_SURFACE: {
            auto& sz = ctx.world.GetMutableSurfaceZones();
            if (cmd.index < sz.size()) sz.erase(sz.begin() + static_cast<ptrdiff_t>(cmd.index));
            ctx.requestWorldRebuild = true;
            break;
        }
        case CommandType::REMOVE_SURFACE: {
            auto& sz = ctx.world.GetMutableSurfaceZones();
            size_t pos = std::min(cmd.index, sz.size());
            sz.insert(sz.begin() + static_cast<ptrdiff_t>(pos), cmd.surfaceData);
            ctx.requestWorldRebuild = true;
            break;
        }
        }

        m_redoStack.push_back(std::move(cmd));
        ShowToast("Undo aplicado", 1.5f);
    }

    void Editor::Redo(EditorContext& ctx) {
        if (m_redoStack.empty()) {
            ShowToast("Nada que rehacer", 1.5f);
            return;
        }
        EditorCommand cmd = m_redoStack.back();
        m_redoStack.pop_back();

        switch (cmd.type) {
        case CommandType::ADD_INSTANCE:
            ctx.world.InsertInstanceAt(cmd.index, cmd.afterInst, ctx.spatialGrid);
            ctx.requestWorldRebuild = true;
            break;
        case CommandType::REMOVE_INSTANCE:
            ctx.world.RemoveInstance(cmd.index, ctx.spatialGrid);
            if (m_selectedInstanceIdx == static_cast<int>(cmd.index)) m_selectedInstanceIdx = -1;
            ctx.requestWorldRebuild = true;
            break;
        case CommandType::MODIFY_INSTANCE:
            ctx.world.UpdateInstanceTransform(cmd.index, cmd.afterInst.position, cmd.afterInst.rotationDeg,
                cmd.afterInst.scale, cmd.afterInst.overrideMaterialId, ctx.spatialGrid);
            ctx.requestWorldRebuild = true;
            break;
        case CommandType::ADD_LIGHT: {
            auto& lights = ctx.lighting.GetMutableMapLights();
            size_t pos = std::min(cmd.index, lights.size());
            lights.insert(lights.begin() + static_cast<ptrdiff_t>(pos), cmd.lightData);
            ctx.requestLightEmittersSync = true;
            break;
        }
        case CommandType::REMOVE_LIGHT: {
            auto& lights = ctx.lighting.GetMutableMapLights();
            if (cmd.index < lights.size()) lights.erase(lights.begin() + static_cast<ptrdiff_t>(cmd.index));
            ctx.requestLightEmittersSync = true;
            break;
        }
        case CommandType::ADD_CHARACTER: {
            auto& chars = ctx.characters.GetMutableInstances();
            size_t pos = std::min(cmd.index, chars.size());
            chars.insert(chars.begin() + static_cast<ptrdiff_t>(pos), cmd.charData);
            break;
        }
        case CommandType::REMOVE_CHARACTER:
            ctx.characters.RemoveCharacter(cmd.index);
            break;
        case CommandType::ADD_WATER: {
            auto& wz = ctx.world.GetMutableWaterZones();
            size_t pos = std::min(cmd.index, wz.size());
            wz.insert(wz.begin() + static_cast<ptrdiff_t>(pos), cmd.waterData);
            ctx.requestWorldRebuild = true;
            break;
        }
        case CommandType::REMOVE_WATER: {
            auto& wz = ctx.world.GetMutableWaterZones();
            if (cmd.index < wz.size()) wz.erase(wz.begin() + static_cast<ptrdiff_t>(cmd.index));
            ctx.requestWorldRebuild = true;
            break;
        }
        case CommandType::ADD_SURFACE: {
            auto& sz = ctx.world.GetMutableSurfaceZones();
            size_t pos = std::min(cmd.index, sz.size());
            sz.insert(sz.begin() + static_cast<ptrdiff_t>(pos), cmd.surfaceData);
            ctx.requestWorldRebuild = true;
            break;
        }
        case CommandType::REMOVE_SURFACE: {
            auto& sz = ctx.world.GetMutableSurfaceZones();
            if (cmd.index < sz.size()) sz.erase(sz.begin() + static_cast<ptrdiff_t>(cmd.index));
            ctx.requestWorldRebuild = true;
            break;
        }
        }

        m_undoStack.push_back(std::move(cmd));
        ShowToast("Redo aplicado", 1.5f);
    }

    void Editor::Update(float dt, EditorContext& ctx) {
        if (!m_enabled) return;

        if (m_toastTimer > 0.0f) {
            m_toastTimer = std::max(0.0f, m_toastTimer - dt);
        }

        const bool ctrlDown = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        const bool shiftDown = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

        // 1. Atajos globales del editor: Ctrl+Z (Undo), Ctrl+Y (Redo), Ctrl+S (Guardar), K (Snap).
        if (ctrlDown && IsKeyPressed(KEY_Z)) Undo(ctx);
        if (ctrlDown && IsKeyPressed(KEY_Y)) Redo(ctx);
        if (ctrlDown && IsKeyPressed(KEY_S)) {
            WorldWindConfig wc = {
                ctx.weather.GetWindDirectionDeg(),
                ctx.weather.GetWind().speed,
                ctx.weather.GetWind().gustStrength
            };
            bool ok = ctx.world.SaveMapToFile(
                ctx.activeMapPath, ctx.materials,
                ctx.lighting.GetDayNightConfig(), ctx.lighting.GetGIConfig(),
                ctx.lighting.GetMapLights(), wc,
                ctx.weather.GetActiveBiomeId(), ctx.weather.GetActiveWeatherId(),
                ctx.weather.AreRandomTransitionsOn(), ctx.viewPaddingPx,
                ctx.characters.ExportEntitySpawns()
            );
            ShowToast(ok ? ("Guardado: " + ctx.activeMapPath) : "Error al guardar JSON", 3.0f);
        }
        if (IsKeyPressed(KEY_K)) m_gridSnap = !m_gridSnap;

        // 2. Movimiento libre de camara en el editor (WASD o arrastrar con boton central).
        const float panSpeed = (shiftDown ? 780.0f : 440.0f) / ctx.cameraZoom;
        if (!ctrlDown) {
            if (IsKeyDown(KEY_W)) ctx.cameraTargetIso.y -= panSpeed * dt;
            if (IsKeyDown(KEY_S)) ctx.cameraTargetIso.y += panSpeed * dt;
            if (IsKeyDown(KEY_A)) ctx.cameraTargetIso.x -= panSpeed * dt;
            if (IsKeyDown(KEY_D)) ctx.cameraTargetIso.x += panSpeed * dt;
        }

        Vector2 mp = GetMousePosition();
        bool overUI = IsPointerOverUI(ctx.screenWidth, ctx.screenHeight);

        if (!overUI && IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
            m_draggingPan = true;
            m_lastMouseScreen = { mp.x, mp.y };
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_MIDDLE)) {
            m_draggingPan = false;
        }
        if (m_draggingPan) {
            float dx = (mp.x - m_lastMouseScreen.x) / ctx.cameraZoom;
            float dy = (mp.y - m_lastMouseScreen.y) / ctx.cameraZoom;
            ctx.cameraTargetIso.x -= dx;
            ctx.cameraTargetIso.y -= dy;
            m_lastMouseScreen = { mp.x, mp.y };
        }

        // 3. Rotacion (Q/E) y Escala (Shift + Rueda o [ / ]) del pincel activo.
        if (IsKeyPressed(KEY_Q)) m_brushRotationDeg = std::fmod(m_brushRotationDeg - 15.0f + 360.0f, 360.0f);
        if (IsKeyPressed(KEY_E)) m_brushRotationDeg = std::fmod(m_brushRotationDeg + 15.0f, 360.0f);

        float wheel = GetMouseWheelMove();
        if (!overUI && wheel != 0.0f) {
            if (shiftDown) {
                m_brushScale = MathIso::Clamp(m_brushScale + wheel * 0.1f, 0.4f, 2.5f);
            }
            else {
                ctx.cameraZoom = MathIso::Clamp(ctx.cameraZoom + wheel * 0.12f, ctx.minZoom, ctx.maxZoom);
            }
        }

        // 4. Raycast inverso Mouse -> Suelo (Z = 0) con Grid Snap opcional.
        Vec2 worldPos = MathIso::IsoToWorldGround(ScreenToIso({ mp.x, mp.y }, ctx), ctx.isoConfig);
        if (m_gridSnap) {
            worldPos.x = std::round(worldPos.x / m_gridSnapStep) * m_gridSnapStep;
            worldPos.y = std::round(worldPos.y / m_gridSnapStep) * m_gridSnapStep;
        }
        float limit = ctx.world.GetWorldHalfSize() - 1.0f;
        m_cursorWorldPos = { MathIso::Clamp(worldPos.x, -limit, limit), MathIso::Clamp(worldPos.y, -limit, limit) };

        if (!overUI) {
            HandleViewportInteraction(ctx);
        }
    }

    void Editor::HandleViewportInteraction(EditorContext& ctx) {
        // --- CLIC IZQUIERDO: COLOCAR O SELECCIONAR ---
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (m_category == EditorCategory::OBJECTS) {
                const auto& prefabs = ctx.world.GetPrefabs();
                if (m_selectedPrefabIdx >= 0 && m_selectedPrefabIdx < static_cast<int>(prefabs.size())) {
                    PrefabId pid = static_cast<PrefabId>(m_selectedPrefabIdx);
                    uint32_t newIdx = ctx.world.SpawnInstance(
                        pid,
                        { m_cursorWorldPos.x, m_cursorWorldPos.y, 0.0f },
                        m_brushRotationDeg,
                        { m_brushScale, m_brushScale, m_brushScale },
                        ctx.spatialGrid,
                        m_brushOverrideMat
                    );
                    if (newIdx != UINT32_MAX) {
                        EditorCommand cmd;
                        cmd.type = CommandType::ADD_INSTANCE;
                        cmd.index = newIdx;
                        cmd.afterInst = ctx.world.GetInstances()[newIdx];
                        PushCommand(std::move(cmd));
                        m_selectedInstanceIdx = static_cast<int>(newIdx);
                        ctx.requestWorldRebuild = true;
                        ShowToast("Objeto colocado: " + prefabs[pid].name, 1.8f);
                    }
                }
            }
            else if (m_category == EditorCategory::CHARACTERS) {
                const auto& tpls = ctx.characters.GetTemplates();
                if (m_selectedCharIdx >= 0 && m_selectedCharIdx < static_cast<int>(tpls.size())) {
                    size_t newIdx = ctx.characters.GetInstances().size();
                    if (ctx.characters.SpawnCharacter(tpls[m_selectedCharIdx].id, m_cursorWorldPos,
                        m_brushRotationDeg, false, 8.0f, m_brushScale)) {
                        EditorCommand cmd;
                        cmd.type = CommandType::ADD_CHARACTER;
                        cmd.index = newIdx;
                        cmd.charData = ctx.characters.GetInstances().back();
                        PushCommand(std::move(cmd));
                        ShowToast("Personaje colocado: " + tpls[m_selectedCharIdx].name, 1.8f);
                    }
                }
            }
            else if (m_category == EditorCategory::LIGHTS) {
                PointLight pl;
                pl.position = { m_cursorWorldPos.x, m_cursorWorldPos.y, 2.3f };
                pl.flickerPhase = static_cast<float>(ctx.lighting.GetMapLights().size()) * 1.9f;

                if (m_selectedLightPres == 0) {
                    pl.color = { 255, 152, 56, 255 }; pl.intensity = 1.65f; pl.radius = 11.5f; pl.flicker = true;
                }
                else if (m_selectedLightPres == 1) {
                    pl.color = { 96, 210, 255, 255 }; pl.intensity = 1.45f; pl.radius = 12.0f; pl.flicker = true;
                }
                else {
                    pl.color = { 155, 255, 130, 255 }; pl.intensity = 1.25f; pl.radius = 10.0f; pl.flicker = false;
                }

                auto& lights = ctx.lighting.GetMutableMapLights();
                EditorCommand cmd;
                cmd.type = CommandType::ADD_LIGHT;
                cmd.index = lights.size();
                cmd.lightData = pl;
                lights.push_back(pl);
                PushCommand(std::move(cmd));
                ctx.requestLightEmittersSync = true;
                ShowToast("Luz anadida al mapa", 1.8f);
            }
            else if (m_category == EditorCategory::ZONES) {
                float minX = m_cursorWorldPos.x - m_brushZoneHalfW;
                float maxX = m_cursorWorldPos.x + m_brushZoneHalfW;
                float minY = m_cursorWorldPos.y - m_brushZoneHalfH;
                float maxY = m_cursorWorldPos.y + m_brushZoneHalfH;

                if (m_selectedZonePres == 0) {
                    WaterZone wz;
                    wz.minX = minX; wz.maxX = maxX; wz.minY = minY; wz.maxY = maxY;
                    auto& list = ctx.world.GetMutableWaterZones();
                    EditorCommand cmd;
                    cmd.type = CommandType::ADD_WATER;
                    cmd.index = list.size();
                    cmd.waterData = wz;
                    list.push_back(wz);
                    PushCommand(std::move(cmd));
                    ctx.requestWorldRebuild = true;
                    ShowToast("Zona de Agua colocada", 1.8f);
                }
                else {
                    SurfaceZone sz;
                    sz.minX = minX; sz.maxX = maxX; sz.minY = minY; sz.maxY = maxY;
                    if (m_selectedZonePres == 1) {
                        sz.id = "wood_bridge"; sz.elevationZ = 0.08f;
                        sz.primaryColor = { 140, 91, 56, 255 }; sz.secondaryColor = { 94, 58, 34, 255 };
                        sz.glyphs = "=#-"; sz.pattern = SurfacePattern::PLANKS_X; sz.walkableOverWater = true;
                    }
                    else {
                        sz.id = "stone_floor"; sz.elevationZ = 0.03f;
                        sz.primaryColor = { 123, 130, 138, 255 }; sz.secondaryColor = { 86, 92, 99, 255 };
                        sz.glyphs = ".:+"; sz.pattern = SurfacePattern::GRID; sz.walkableOverWater = false;
                    }
                    auto& list = ctx.world.GetMutableSurfaceZones();
                    EditorCommand cmd;
                    cmd.type = CommandType::ADD_SURFACE;
                    cmd.index = list.size();
                    cmd.surfaceData = sz;
                    list.push_back(sz);
                    PushCommand(std::move(cmd));
                    ctx.requestWorldRebuild = true;
                    ShowToast("Superficie transitable colocada", 1.8f);
                }
            }
            else if (m_category == EditorCategory::SELECT) {
                // Buscamos la instancia de objeto mas cercana al cursor (dentro de 3.2 unidades).
                const auto& instances = ctx.world.GetInstances();
                int   bestIdx = -1;
                float bestDist = 3.2f * 3.2f;
                for (size_t i = 0; i < instances.size(); ++i) {
                    float dx = instances[i].position.x - m_cursorWorldPos.x;
                    float dy = instances[i].position.y - m_cursorWorldPos.y;
                    float d2 = dx * dx + dy * dy;
                    if (d2 < bestDist) {
                        bestDist = d2;
                        bestIdx = static_cast<int>(i);
                    }
                }
                m_selectedInstanceIdx = bestIdx;
                if (bestIdx >= 0) {
                    m_rightTab = 1; // Cambiamos al inspector de seleccion automaticamente.
                }
            }
        }

        // --- CLIC DERECHO: BORRAR ELEMENTO MAS CERCANO AL CURSOR ---
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            // Prioridad 1: Luces cercanas (< 2.5u).
            auto& lights = ctx.lighting.GetMutableMapLights();
            int bestL = -1; float bestLD = 2.5f * 2.5f;
            for (size_t i = 0; i < lights.size(); ++i) {
                float dx = lights[i].position.x - m_cursorWorldPos.x;
                float dy = lights[i].position.y - m_cursorWorldPos.y;
                if (dx * dx + dy * dy < bestLD) { bestLD = dx * dx + dy * dy; bestL = static_cast<int>(i); }
            }
            if (bestL >= 0 && (m_category == EditorCategory::LIGHTS || m_category == EditorCategory::SELECT)) {
                EditorCommand cmd;
                cmd.type = CommandType::REMOVE_LIGHT;
                cmd.index = static_cast<size_t>(bestL);
                cmd.lightData = lights[bestL];
                lights.erase(lights.begin() + bestL);
                PushCommand(std::move(cmd));
                ctx.requestLightEmittersSync = true;
                ShowToast("Luz eliminada (Ctrl+Z para deshacer)", 2.0f);
                return;
            }

            // Prioridad 2: Personajes cercanos (< 2.5u) si estamos en pestana Personajes.
            if (m_category == EditorCategory::CHARACTERS) {
                const auto& chars = ctx.characters.GetInstances();
                int bestC = -1; float bestCD = 2.8f * 2.8f;
                for (size_t i = 0; i < chars.size(); ++i) {
                    float dx = chars[i].actor.position.x - m_cursorWorldPos.x;
                    float dy = chars[i].actor.position.y - m_cursorWorldPos.y;
                    if (dx * dx + dy * dy < bestCD) { bestCD = dx * dx + dy * dy; bestC = static_cast<int>(i); }
                }
                if (bestC >= 0) {
                    EditorCommand cmd;
                    cmd.type = CommandType::REMOVE_CHARACTER;
                    cmd.index = static_cast<size_t>(bestC);
                    cmd.charData = chars[bestC];
                    ctx.characters.RemoveCharacter(static_cast<size_t>(bestC));
                    PushCommand(std::move(cmd));
                    ShowToast("Personaje eliminado", 2.0f);
                    return;
                }
            }

            // Prioridad 3: Zonas de agua/superficie bajo el cursor si estamos en pestana Zonas.
            if (m_category == EditorCategory::ZONES) {
                auto& surfs = ctx.world.GetMutableSurfaceZones();
                for (int i = static_cast<int>(surfs.size()) - 1; i >= 0; --i) {
                    if (surfs[i].Contains(m_cursorWorldPos.x, m_cursorWorldPos.y)) {
                        EditorCommand cmd;
                        cmd.type = CommandType::REMOVE_SURFACE;
                        cmd.index = static_cast<size_t>(i);
                        cmd.surfaceData = surfs[i];
                        surfs.erase(surfs.begin() + i);
                        PushCommand(std::move(cmd));
                        ctx.requestWorldRebuild = true;
                        ShowToast("Superficie eliminada", 2.0f);
                        return;
                    }
                }
                auto& waters = ctx.world.GetMutableWaterZones();
                for (int i = static_cast<int>(waters.size()) - 1; i >= 0; --i) {
                    if (waters[i].Contains(m_cursorWorldPos.x, m_cursorWorldPos.y)) {
                        EditorCommand cmd;
                        cmd.type = CommandType::REMOVE_WATER;
                        cmd.index = static_cast<size_t>(i);
                        cmd.waterData = waters[i];
                        waters.erase(waters.begin() + i);
                        PushCommand(std::move(cmd));
                        ctx.requestWorldRebuild = true;
                        ShowToast("Zona de agua eliminada", 2.0f);
                        return;
                    }
                }
            }

            // Prioridad 4: Instancia de objeto mas cercana al cursor (< 3.0u).
            const auto& instances = ctx.world.GetInstances();
            int   bestI = -1;
            float bestID = 3.0f * 3.0f;
            for (size_t i = 0; i < instances.size(); ++i) {
                float dx = instances[i].position.x - m_cursorWorldPos.x;
                float dy = instances[i].position.y - m_cursorWorldPos.y;
                float d2 = dx * dx + dy * dy;
                if (d2 < bestID) { bestID = d2; bestI = static_cast<int>(i); }
            }
            if (bestI >= 0) {
                EditorCommand cmd;
                cmd.type = CommandType::REMOVE_INSTANCE;
                cmd.index = static_cast<size_t>(bestI);
                cmd.beforeInst = instances[bestI];
                ctx.world.RemoveInstance(static_cast<size_t>(bestI), ctx.spatialGrid);
                if (m_selectedInstanceIdx == bestI) m_selectedInstanceIdx = -1;
                PushCommand(std::move(cmd));
                ctx.requestWorldRebuild = true;
                ShowToast("Instancia eliminada (Ctrl+Z para deshacer)", 2.0f);
            }
        }
    }

    void Editor::DrawGhostPreview(const EditorContext& ctx) const {
        // Resaltado dorado sobre la instancia seleccionada actualmente.
        if (m_selectedInstanceIdx >= 0 && m_selectedInstanceIdx < static_cast<int>(ctx.world.GetInstances().size())) {
            const MapInstance& sel = ctx.world.GetInstances()[m_selectedInstanceIdx];
            const BakedPrefab* p = ctx.world.GetPrefab(sel.prefabId);
            float rad = p ? (p->boundRadiusXY * std::max(sel.scale.x, sel.scale.y)) : 1.5f;
            DrawIsoCircleScreen({ sel.position.x, sel.position.y }, rad, GOLD, ctx);
        }

        if (IsPointerOverUI(ctx.screenWidth, ctx.screenHeight)) return;

        // Ghost preview en el cursor segun la herramienta activa.
        Vec2 centerScr = IsoToScreen(MathIso::WorldToIso({ m_cursorWorldPos.x, m_cursorWorldPos.y, 0.1f }, ctx.isoConfig), ctx);

        // Flecha de orientacion segun m_brushRotationDeg.
        float rad = m_brushRotationDeg * DEG_TO_RAD;
        Vec2 dirScr = IsoToScreen(MathIso::WorldToIso({
            m_cursorWorldPos.x + std::cos(rad) * 2.2f,
            m_cursorWorldPos.y + std::sin(rad) * 2.2f,
            0.1f
            }, ctx.isoConfig), ctx);

        if (m_category == EditorCategory::OBJECTS) {
            const auto& prefabs = ctx.world.GetPrefabs();
            if (m_selectedPrefabIdx >= 0 && m_selectedPrefabIdx < static_cast<int>(prefabs.size())) {
                const BakedPrefab& p = prefabs[m_selectedPrefabIdx];
                if (p.collider.type == ColliderType::BOX || p.collider.type == ColliderType::ARCH_PILLARS) {
                    DrawIsoRectScreen(m_cursorWorldPos,
                        { p.collider.halfSize.x * m_brushScale, p.collider.halfSize.y * m_brushScale },
                        m_brushRotationDeg, { 110, 235, 255, 220 }, ctx);
                }
                else {
                    DrawIsoCircleScreen(m_cursorWorldPos, p.boundRadiusXY * m_brushScale, { 110, 235, 255, 220 }, ctx);
                }
                DrawLineEx({ centerScr.x, centerScr.y }, { dirScr.x, dirScr.y }, 2.0f, YELLOW);
                DrawText(TextFormat("+ %s (%.0f deg | %.1fx)", p.name.c_str(), m_brushRotationDeg, m_brushScale),
                    static_cast<int>(centerScr.x) + 12, static_cast<int>(centerScr.y) - 22, 14, SKYBLUE);
            }
        }
        else if (m_category == EditorCategory::CHARACTERS) {
            const auto& tpls = ctx.characters.GetTemplates();
            if (m_selectedCharIdx >= 0 && m_selectedCharIdx < static_cast<int>(tpls.size())) {
                DrawIsoCircleScreen(m_cursorWorldPos, tpls[m_selectedCharIdx].collisionRadius * m_brushScale, PINK, ctx);
                DrawLineEx({ centerScr.x, centerScr.y }, { dirScr.x, dirScr.y }, 2.0f, YELLOW);
                DrawText(TextFormat("+ %s", tpls[m_selectedCharIdx].name.c_str()),
                    static_cast<int>(centerScr.x) + 12, static_cast<int>(centerScr.y) - 22, 14, PINK);
            }
        }
        else if (m_category == EditorCategory::LIGHTS) {
            DrawIsoCircleScreen(m_cursorWorldPos, 11.0f, { 255, 180, 60, 140 }, ctx);
            DrawCircleV({ centerScr.x, centerScr.y }, 5.0f, ORANGE);
            DrawText("+ Luz Puntual", static_cast<int>(centerScr.x) + 12, static_cast<int>(centerScr.y) - 18, 14, ORANGE);
        }
        else if (m_category == EditorCategory::ZONES) {
            Color zCol = (m_selectedZonePres == 0) ? SKYBLUE : GOLD;
            DrawIsoRectScreen(m_cursorWorldPos, { m_brushZoneHalfW, m_brushZoneHalfH }, 0.0f, zCol, ctx);
        }
        else {
            DrawIsoCircleScreen(m_cursorWorldPos, 1.0f, WHITE, ctx);
        }
    }

    void Editor::Render(EditorContext& ctx, const void* raylibFontPtr) {
        if (!m_enabled || !raylibFontPtr) return;

        const Font& font = *static_cast<const Font*>(raylibFontPtr);
        if (!m_styleInit) {
            ApplyDarkTerminalTheme(font);
            m_styleInit = true;
        }

        // 1. Primero el ghost preview en el viewport isométrico.
        DrawGhostPreview(ctx);

        const float sw = static_cast<float>(ctx.screenWidth);
        const float sh = static_cast<float>(ctx.screenHeight);
        const float bodyH = sh - BOTTOM_BAR_H;

        // =========================================================================
        // 2. PANEL IZQUIERDO: NAVEGADOR DE TEMPLATES Y HERRAMIENTAS
        // =========================================================================
        GuiPanel({ 0.0f, 0.0f, LEFT_PANEL_W, bodyH }, "EDITOR DE MAPAS [F1]");

        float lx = 10.0f;
        float ly = 32.0f;
        const float lw = LEFT_PANEL_W - 20.0f;

        // Pestanas de categorias (2 filas compactas de botones).
        float halfW = (lw - 4.0f) * 0.5f;
        if (GuiButton({ lx, ly, halfW, 24.0f }, m_category == EditorCategory::OBJECTS ? "> Objetos" : "Objetos"))
            m_category = EditorCategory::OBJECTS;
        if (GuiButton({ lx + halfW + 4.0f, ly, halfW, 24.0f }, m_category == EditorCategory::CHARACTERS ? "> Personajes" : "Personajes"))
            m_category = EditorCategory::CHARACTERS;
        ly += 28.0f;

        float thirdW = (lw - 8.0f) / 3.0f;
        if (GuiButton({ lx, ly, thirdW, 24.0f }, m_category == EditorCategory::LIGHTS ? "> Luces" : "Luces"))
            m_category = EditorCategory::LIGHTS;
        if (GuiButton({ lx + thirdW + 4.0f, ly, thirdW, 24.0f }, m_category == EditorCategory::ZONES ? "> Zonas" : "Zonas"))
            m_category = EditorCategory::ZONES;
        if (GuiButton({ lx + (thirdW + 4.0f) * 2.0f, ly, thirdW, 24.0f }, m_category == EditorCategory::SELECT ? "> Selec." : "Selec."))
            m_category = EditorCategory::SELECT;
        ly += 34.0f;

        GuiLine({ lx, ly, lw, 10.0f }, "Templates Disponibles");
        ly += 16.0f;

        if (m_category == EditorCategory::OBJECTS) {
            const auto& prefabs = ctx.world.GetPrefabs();
            for (int i = 0; i < static_cast<int>(prefabs.size()); ++i) {
                std::string label = (i == m_selectedPrefabIdx ? "[*] " : "    ") + prefabs[i].name;
                if (GuiButton({ lx, ly, lw, 26.0f }, label.c_str())) {
                    m_selectedPrefabIdx = i;
                }
                ly += 30.0f;
            }
        }
        else if (m_category == EditorCategory::CHARACTERS) {
            const auto& tpls = ctx.characters.GetTemplates();
            for (int i = 0; i < static_cast<int>(tpls.size()); ++i) {
                std::string label = (i == m_selectedCharIdx ? "[*] " : "    ") + tpls[i].name;
                if (GuiButton({ lx, ly, lw, 26.0f }, label.c_str())) {
                    m_selectedCharIdx = i;
                }
                ly += 30.0f;
            }
        }
        else if (m_category == EditorCategory::LIGHTS) {
            static constexpr const char* kLights[3] = { "Antorcha Calida (Flicker)", "Cristal Cian (Flicker)", "Orbe Esmeralda (Fijo)" };
            for (int i = 0; i < 3; ++i) {
                std::string label = std::string(i == m_selectedLightPres ? "[*] " : "    ") + kLights[i];
                if (GuiButton({ lx, ly, lw, 26.0f }, label.c_str())) m_selectedLightPres = i;
                ly += 30.0f;
            }
        }
        else if (m_category == EditorCategory::ZONES) {
            static constexpr const char* kZones[3] = { "Agua (Estanque)", "Puente de Madera", "Patio de Losas" };
            for (int i = 0; i < 3; ++i) {
                std::string label = std::string(i == m_selectedZonePres ? "[*] " : "    ") + kZones[i];
                if (GuiButton({ lx, ly, lw, 26.0f }, label.c_str())) m_selectedZonePres = i;
                ly += 30.0f;
            }
            ly += 8.0f;
            GuiLabel({ lx, ly, lw, 18.0f }, TextFormat("Ancho Zona: %.1f u", m_brushZoneHalfW * 2.0f));
            ly += 18.0f;
            GuiSliderBar({ lx, ly, lw, 18.0f }, "", "", &m_brushZoneHalfW, 1.0f, 12.0f);
            ly += 26.0f;
            GuiLabel({ lx, ly, lw, 18.0f }, TextFormat("Largo Zona: %.1f u", m_brushZoneHalfH * 2.0f));
            ly += 18.0f;
            GuiSliderBar({ lx, ly, lw, 18.0f }, "", "", &m_brushZoneHalfH, 1.0f, 12.0f);
            ly += 28.0f;
        }
        else {
            GuiLabel({ lx, ly, lw, 20.0f }, "Clic Izq: Seleccionar objeto"); ly += 22.0f;
            GuiLabel({ lx, ly, lw, 20.0f }, "Clic Der: Eliminar cercano");   ly += 22.0f;
        }

        // Controles del pincel en la mitad inferior del panel izquierdo.
        float brushY = std::max(ly + 12.0f, bodyH - 220.0f);
        GuiLine({ lx, brushY, lw, 10.0f }, "Pincel & Ayudas"); brushY += 18.0f;

        GuiCheckBox({ lx, brushY, 18.0f, 18.0f }, "Grid Snap [K]", &m_gridSnap); brushY += 26.0f;

        GuiLabel({ lx, brushY, lw, 18.0f }, TextFormat("Rotacion [Q/E]: %.0f deg", m_brushRotationDeg)); brushY += 18.0f;
        GuiSliderBar({ lx, brushY, lw, 16.0f }, "", "", &m_brushRotationDeg, 0.0f, 360.0f); brushY += 24.0f;

        GuiLabel({ lx, brushY, lw, 18.0f }, TextFormat("Escala [Shift+Rueda]: %.2fx", m_brushScale)); brushY += 18.0f;
        GuiSliderBar({ lx, brushY, lw, 16.0f }, "", "", &m_brushScale, 0.4f, 2.5f); brushY += 26.0f;

        // Material Override para objetos colocados.
        std::string ovName = (m_brushOverrideMat == FALLBACK_MATERIAL_ID)
            ? "Original"
            : ctx.materials.Get(m_brushOverrideMat).name;
        if (GuiButton({ lx, brushY, lw, 24.0f }, TextFormat("Mat Override: %s", ovName.c_str()))) {
            m_brushOverrideMat = static_cast<MaterialId>((m_brushOverrideMat + 1) % ctx.materials.Count());
        }
        brushY += 30.0f;
        GuiLabel({ lx, brushY, lw, 18.0f }, TextFormat("Cursor: (%.1f, %.1f)", m_cursorWorldPos.x, m_cursorWorldPos.y));

        // =========================================================================
        // 3. PANEL DERECHO: INSPECTOR DE MUNDO Y SELECCION
        // =========================================================================
        const float rx = sw - RIGHT_PANEL_W;
        GuiPanel({ rx, 0.0f, RIGHT_PANEL_W, bodyH }, "INSPECTOR");

        float ix = rx + 12.0f;
        float iy = 32.0f;
        const float iw = RIGHT_PANEL_W - 24.0f;
        float tabW = (iw - 4.0f) * 0.5f;

        if (GuiButton({ ix, iy, tabW, 24.0f }, m_rightTab == 0 ? "> Mundo" : "Mundo"))       m_rightTab = 0;
        if (GuiButton({ ix + tabW + 4.0f, iy, tabW, 24.0f }, m_rightTab == 1 ? "> Seleccion" : "Seleccion")) m_rightTab = 1;
        iy += 32.0f;

        if (m_rightTab == 0) {
            // --- CICLO DIA-NOCHE & SOMBRAS ---
            GuiLine({ ix, iy, iw, 10.0f }, "Ciclo Dia-Noche & Sombras"); iy += 16.0f;

            float timeHr = ctx.lighting.GetTimeHours();
            GuiLabel({ ix, iy, iw, 18.0f }, TextFormat("Hora del Dia: %.2f h", timeHr)); iy += 18.0f;
            float prevHr = timeHr;
            GuiSliderBar({ ix, iy, iw, 16.0f }, "", "", &timeHr, 0.0f, 23.95f); iy += 22.0f;
            if (std::abs(timeHr - prevHr) > 0.001f) ctx.lighting.SetTimeHours(timeHr);

            float dayLen = ctx.lighting.GetDayLengthSeconds();
            GuiLabel({ ix, iy, iw, 18.0f }, TextFormat("Duracion Ciclo: %.0f s", dayLen)); iy += 18.0f;
            GuiSliderBar({ ix, iy, iw, 16.0f }, "", "", &dayLen, 15.0f, 600.0f); iy += 24.0f;
            ctx.lighting.SetDayLengthSeconds(dayLen);

            bool cycleOn = ctx.lighting.IsCycleRunning();
            GuiCheckBox({ ix, iy, 18.0f, 18.0f }, "Ciclo Automatico", &cycleOn);
            ctx.lighting.SetCycleRunning(cycleOn);

            bool shadOn = ctx.lighting.AreShadowsEnabled();
            GuiCheckBox({ ix + 150.0f, iy, 18.0f, 18.0f }, "Sombras 2.5D", &shadOn);
            ctx.lighting.SetShadowsEnabled(shadOn);
            iy += 28.0f;

            // --- ILUMINACION GLOBAL (GI) ---
            GuiLine({ ix, iy, iw, 10.0f }, "Iluminacion Global (GI)"); iy += 16.0f;

            auto& gi = ctx.lighting.GetMutableGIConfig();
            GuiLabel({ ix, iy, iw, 18.0f }, TextFormat("Fuerza Ambiental GI: %.2f", gi.ambientStrength)); iy += 18.0f;
            GuiSliderBar({ ix, iy, iw, 16.0f }, "", "", &gi.ambientStrength, 0.05f, 0.85f); iy += 24.0f;

            float skyB = static_cast<float>(gi.skyColor.b);
            GuiLabel({ ix, iy, iw, 18.0f }, TextFormat("Tono Azul Cielo GI: %.0f", skyB)); iy += 18.0f;
            GuiSliderBar({ ix, iy, iw, 16.0f }, "", "", &skyB, 60.0f, 255.0f); iy += 26.0f;
            gi.skyColor.b = static_cast<uint8_t>(skyB);

            // --- BIOMA, CLIMA & VIENTO ---
            GuiLine({ ix, iy, iw, 10.0f }, "Bioma, Clima & Viento"); iy += 16.0f;

            if (GuiButton({ ix, iy, iw, 24.0f }, TextFormat("Bioma: %s  [Siguiente]", ctx.weather.GetActiveBiomeName().c_str()))) {
                ctx.weather.CycleBiome();
            }
            iy += 28.0f;

            float wBtn = (iw - 8.0f) / 3.0f;
            if (GuiButton({ ix, iy, wBtn, 24.0f }, "Sol"))      ctx.weather.TransitionToWeather("clear_day");
            if (GuiButton({ ix + wBtn + 4.0f, iy, wBtn, 24.0f }, "Lluvia"))   ctx.weather.TransitionToWeather("heavy_rain");
            if (GuiButton({ ix + (wBtn + 4.0f) * 2.0f, iy, wBtn, 24.0f }, "Ventisca")) ctx.weather.TransitionToWeather("blizzard");
            iy += 28.0f;

            bool randW = ctx.weather.AreRandomTransitionsOn();
            GuiCheckBox({ ix, iy, 18.0f, 18.0f }, "Clima Aleatorio (Markov)", &randW);
            ctx.weather.SetRandomTransitions(randW);
            iy += 26.0f;

            float wDir = ctx.weather.GetWindDirectionDeg();
            GuiLabel({ ix, iy, iw, 18.0f }, TextFormat("Direccion Viento: %.0f deg", wDir)); iy += 18.0f;
            float prevDir = wDir;
            GuiSliderBar({ ix, iy, iw, 16.0f }, "", "", &wDir, 0.0f, 360.0f); iy += 22.0f;
            if (std::abs(wDir - prevDir) > 0.5f) ctx.weather.SetWindDirectionDeg(wDir);

            float wSpd = ctx.weather.GetWind().speed;
            GuiLabel({ ix, iy, iw, 18.0f }, TextFormat("Velocidad Viento: %.1f m/s", wSpd)); iy += 18.0f;
            float prevSpd = wSpd;
            GuiSliderBar({ ix, iy, iw, 16.0f }, "", "", &wSpd, 0.0f, 14.0f); iy += 26.0f;
            if (std::abs(wSpd - prevSpd) > 0.05f) ctx.weather.SetWindBaseSpeed(wSpd);

            // --- CULLING ---
            GuiLine({ ix, iy, iw, 10.0f }, "Parametros de Culling"); iy += 16.0f;

            GuiLabel({ ix, iy, iw, 18.0f }, TextFormat("Frustum Padding: %.0f px", ctx.viewPaddingPx)); iy += 18.0f;
            GuiSliderBar({ ix, iy, iw, 16.0f }, "", "", &ctx.viewPaddingPx, 40.0f, 360.0f); iy += 24.0f;

            float chunkSz = ctx.world.GetChunkSize();
            GuiLabel({ ix, iy, iw, 18.0f }, TextFormat("Chunk Size: %.0f u", chunkSz)); iy += 18.0f;
            float prevCs = chunkSz;
            GuiSliderBar({ ix, iy, iw, 16.0f }, "", "", &chunkSz, 12.0f, 64.0f); iy += 24.0f;
            if (std::abs(chunkSz - prevCs) > 0.5f) {
                ctx.world.SetChunkSize(std::round(chunkSz));
                ctx.requestWorldRebuild = true;
            }
        }
        else {
            // --- INSPECTOR DE INSTANCIA SELECCIONADA ---
            if (m_selectedInstanceIdx < 0 || m_selectedInstanceIdx >= static_cast<int>(ctx.world.GetInstances().size())) {
                GuiLabel({ ix, iy, iw, 22.0f }, "Ninguna instancia seleccionada."); iy += 24.0f;
                GuiLabel({ ix, iy, iw, 22.0f }, "Usa pestana [Selec.] y clic izq.");
            }
            else {
                const size_t idx = static_cast<size_t>(m_selectedInstanceIdx);
                MapInstance before = ctx.world.GetInstances()[idx];
                const BakedPrefab* prefab = ctx.world.GetPrefab(before.prefabId);

                GuiLabel({ ix, iy, iw, 20.0f }, TextFormat("Objeto #%zu: %s", idx, prefab ? prefab->name.c_str() : "?"));
                iy += 26.0f;

                Vec3  newPos = before.position;
                float newRot = before.rotationDeg;
                float newScl = before.scale.x;
                MaterialId newOv = before.overrideMaterialId;
                bool changed = false;

                GuiLabel({ ix, iy, iw, 18.0f }, TextFormat("Pos X: %.2f", newPos.x)); iy += 18.0f;
                if (GuiButton({ ix, iy, 60.0f, 22.0f }, "-0.5")) { newPos.x -= 0.5f; changed = true; }
                if (GuiButton({ ix + 68.0f, iy, 60.0f, 22.0f }, "+0.5")) { newPos.x += 0.5f; changed = true; }
                iy += 28.0f;

                GuiLabel({ ix, iy, iw, 18.0f }, TextFormat("Pos Y: %.2f", newPos.y)); iy += 18.0f;
                if (GuiButton({ ix, iy, 60.0f, 22.0f }, "-0.5")) { newPos.y -= 0.5f; changed = true; }
                if (GuiButton({ ix + 68.0f, iy, 60.0f, 22.0f }, "+0.5")) { newPos.y += 0.5f; changed = true; }
                iy += 28.0f;

                GuiLabel({ ix, iy, iw, 18.0f }, TextFormat("Rotacion: %.0f deg", newRot)); iy += 18.0f;
                if (GuiButton({ ix, iy, 60.0f, 22.0f }, "-15")) { newRot = std::fmod(newRot - 15.0f + 360.0f, 360.0f); changed = true; }
                if (GuiButton({ ix + 68.0f, iy, 60.0f, 22.0f }, "+15")) { newRot = std::fmod(newRot + 15.0f, 360.0f); changed = true; }
                iy += 28.0f;

                GuiLabel({ ix, iy, iw, 18.0f }, TextFormat("Escala: %.2fx", newScl)); iy += 18.0f;
                if (GuiButton({ ix, iy, 60.0f, 22.0f }, "-0.1")) { newScl = std::max(0.3f, newScl - 0.1f); changed = true; }
                if (GuiButton({ ix + 68.0f, iy, 60.0f, 22.0f }, "+0.1")) { newScl = std::min(3.0f, newScl + 0.1f); changed = true; }
                iy += 30.0f;

                std::string matLbl = (newOv == FALLBACK_MATERIAL_ID) ? "Original" : ctx.materials.Get(newOv).name;
                if (GuiButton({ ix, iy, iw, 26.0f }, TextFormat("Material: %s", matLbl.c_str()))) {
                    newOv = static_cast<MaterialId>((newOv + 1) % ctx.materials.Count());
                    changed = true;
                }
                iy += 36.0f;

                if (changed) {
                    ctx.world.UpdateInstanceTransform(idx, newPos, newRot, { newScl, newScl, newScl }, newOv, ctx.spatialGrid);
                    EditorCommand cmd;
                    cmd.type = CommandType::MODIFY_INSTANCE;
                    cmd.index = idx;
                    cmd.beforeInst = before;
                    cmd.afterInst = ctx.world.GetInstances()[idx];
                    PushCommand(std::move(cmd));
                    ctx.requestWorldRebuild = true;
                }

                if (GuiButton({ ix, iy, iw, 28.0f }, "ELIMINAR INSTANCIA")) {
                    EditorCommand cmd;
                    cmd.type = CommandType::REMOVE_INSTANCE;
                    cmd.index = idx;
                    cmd.beforeInst = before;
                    ctx.world.RemoveInstance(idx, ctx.spatialGrid);
                    m_selectedInstanceIdx = -1;
                    PushCommand(std::move(cmd));
                    ctx.requestWorldRebuild = true;
                    ShowToast("Instancia eliminada", 1.8f);
                }
            }
        }

        // =========================================================================
        // 4. BARRA INFERIOR: GUARDAR / CARGAR / HOT-RELOAD / UNDO-REDO / TOAST
        // =========================================================================
        const float by = sh - BOTTOM_BAR_H;
        GuiPanel({ 0.0f, by, sw, BOTTOM_BAR_H }, nullptr);

        float bx = 12.0f;
        const float btnY = by + 8.0f;

        if (GuiButton({ bx, btnY, 130.0f, 28.0f }, "Guardar JSON [Ctrl+S]")) {
            WorldWindConfig wc = {
                ctx.weather.GetWindDirectionDeg(),
                ctx.weather.GetWind().speed,
                ctx.weather.GetWind().gustStrength
            };
            bool ok = ctx.world.SaveMapToFile(
                ctx.activeMapPath, ctx.materials,
                ctx.lighting.GetDayNightConfig(), ctx.lighting.GetGIConfig(),
                ctx.lighting.GetMapLights(), wc,
                ctx.weather.GetActiveBiomeId(), ctx.weather.GetActiveWeatherId(),
                ctx.weather.AreRandomTransitionsOn(), ctx.viewPaddingPx,
                ctx.characters.ExportEntitySpawns()
            );
            ShowToast(ok ? ("Guardado: " + ctx.activeMapPath) : "Error al guardar JSON", 3.0f);
        }
        bx += 138.0f;

        if (GuiButton({ bx, btnY, 105.0f, 28.0f }, "Cargar JSON")) {
            ctx.requestReloadMap = true;
            ShowToast("Recargando mapa desde disco...", 2.0f);
        }
        bx += 113.0f;

        if (GuiButton({ bx, btnY, 128.0f, 28.0f }, "Hot-Reload [F5]")) {
            ctx.requestHotReload = true;
            ShowToast("Plantillas JSON recargadas", 2.0f);
        }
        bx += 138.0f;

        if (GuiButton({ bx, btnY, 92.0f, 28.0f }, TextFormat("Undo (%zu)", m_undoStack.size()))) Undo(ctx);
        bx += 98.0f;
        if (GuiButton({ bx, btnY, 92.0f, 28.0f }, TextFormat("Redo (%zu)", m_redoStack.size()))) Redo(ctx);
        bx += 108.0f;

        const char* statusText = (m_toastTimer > 0.0f)
            ? m_toastMessage.c_str()
            : TextFormat("Mapa: %s  |  Instancias: %zu  |  [F1] Salir del Editor",
                ctx.activeMapPath.c_str(), ctx.world.GetInstances().size());
        GuiLabel({ bx, btnY, sw - bx - 14.0f, 28.0f }, statusText);
    }

} // namespace AsciiEngine