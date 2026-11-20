/**
 * @file Engine.cpp
 * @brief Integracion de Fase 4: Ciclo Dia-Noche, GI Hemisferica, Luces Puntuales y Sombras 2.5D.
 */
#include <AsciiEngine/Engine.hpp>
#include <AsciiEngine/MathIso.hpp>
#include <AsciiEngine/MaterialSystem.hpp>
#include <AsciiEngine/AsciiZBuffer.hpp>
#include <AsciiEngine/CullingSystem.hpp>
#include <AsciiEngine/LightingSystem.hpp>
#include <AsciiEngine/World.hpp>
#include <AsciiEngine/DebugHUD.hpp>
#include "raylib.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <vector>

namespace AsciiEngine {

    namespace {
        using Clock = std::chrono::steady_clock;
        float ElapsedMs(Clock::time_point start, Clock::time_point end) {
            return std::chrono::duration<float, std::milli>(end - start).count();
        }
    } // namespace anonimo

    struct Context::Impl {
        bool initialized = false;
        int  screenWidth = 1600;
        int  screenHeight = 900;

        // Tipografia con Lookup Table O(1).
        Font renderFont = {};
        bool customFontLoaded = false;
        float glyphAspectRatio = 0.58f;
        std::array<Rectangle, 128> glyphLUT = {};

        // Rutas activas para soportar Hot-Reloading (F5).
        std::string dataRootPath = "assets/data";
        std::string activeMapPath;

        // Subsistemas del motor.
        MaterialSystem     materialSystem;
        MathIso::IsoConfig isoConfig;
        AsciiZBuffer       zBuffer;
        SpatialHashGrid    spatialGrid{ 24.0f };
        LightingSystem     lightingSystem;
        World              world;
        DebugHUD           debugHUD;

        // Jugador fisico de prueba (en Fase 6 pasara al CharacterSystem modular).
        SimpleActor player;
        MaterialId  playerBodyMat = FALLBACK_MATERIAL_ID;
        MaterialId  playerHeadMat = FALLBACK_MATERIAL_ID;
        bool        playerTorchOn = true;

        // Camara 2D isometrica y rejilla Z-Buffer calibrada (7x12 px) con zoom artistico.
        Camera2D camera = {};
        float viewPaddingPx = 140.0f;
        float cellWidthPx = 7.0f;
        float cellHeightPx = 12.0f;
        float minZoom = 0.75f;
        float maxZoom = 1.45f;

        ZBufferStyle zBufferStyle;

        // Buffers pre-alojados para consultas de render y fisica (cero asignaciones por frame).
        std::vector<uint32_t>   visibleInstanceIds;
        std::vector<ChunkCoord> activeChunks;
        std::vector<uint32_t>   physicsQueryIds;
        std::vector<ChunkCoord> physicsQueryChunks;

        FrameTelemetry telemetry;

        void LoadTypography() {
            const char* candidates[] = {
                "C:/Windows/Fonts/consola.ttf"
            };

            customFontLoaded = false;
            for (const char* path : candidates) {
                if (FileExists(path)) {
                    // 24px + mipmaps trilineales: trazos suaves y limpios sin pixel-art duro.
                    renderFont = LoadFontEx(path, 24, nullptr, 95);
                    GenTextureMipmaps(&renderFont.texture);
                    SetTextureFilter(renderFont.texture, TEXTURE_FILTER_TRILINEAR);
                    customFontLoaded = true;
                    break;
                }
            }
            if (!customFontLoaded) {
                renderFont = GetFontDefault();
            }

            for (int c = 32; c < 127; ++c) {
                int idx = GetGlyphIndex(renderFont, c);
                glyphLUT[c] = renderFont.recs[idx];
            }
        }

        Vec2 IsoToScreen(Vec2 isoPt) const {
            return {
                (isoPt.x - camera.target.x) * camera.zoom + camera.offset.x,
                (isoPt.y - camera.target.y) * camera.zoom + camera.offset.y
            };
        }

        // Inversa de IsoToScreen: convierte un pixel de pantalla al espacio isometrico.
        Vec2 ScreenToIso(Vec2 screenPt) const {
            float invZoom = 1.0f / camera.zoom;
            return {
                (screenPt.x - camera.offset.x) * invZoom + camera.target.x,
                (screenPt.y - camera.offset.y) * invZoom + camera.target.y
            };
        }

        // Calcula el rectangulo del mundo [minWX..maxWX, minWY..maxWY] visible en pantalla.
        void ComputeVisibleWorldBounds(float& outMinWX, float& outMaxWX,
            float& outMinWY, float& outMaxWY) const {
            const float pad = viewPaddingPx;
            const std::array<Vec2, 4> corners = { {
                { -pad, -pad },
                { static_cast<float>(screenWidth) + pad, -pad },
                { -pad, static_cast<float>(screenHeight) + pad * 2.0f },
                { static_cast<float>(screenWidth) + pad, static_cast<float>(screenHeight) + pad * 2.0f }
            } };

            outMinWX = 1e9f;  outMaxWX = -1e9f;
            outMinWY = 1e9f;  outMaxWY = -1e9f;

            for (const Vec2& c : corners) {
                Vec2 worldPt = MathIso::IsoToWorldGround(ScreenToIso(c), isoConfig);
                outMinWX = std::min(outMinWX, worldPt.x);
                outMaxWX = std::max(outMaxWX, worldPt.x);
                outMinWY = std::min(outMinWY, worldPt.y);
                outMaxWY = std::max(outMaxWY, worldPt.y);
            }
        }

        ChunkRange ComputeVisibleChunkRange(float minWX, float maxWX, float minWY, float maxWY) const {
            const float invChunk = 1.0f / spatialGrid.GetChunkSize();
            return {
                static_cast<int32_t>(std::floor(minWX * invChunk)),
                static_cast<int32_t>(std::floor(maxWX * invChunk)),
                static_cast<int32_t>(std::floor(minWY * invChunk)),
                static_cast<int32_t>(std::floor(maxWY * invChunk))
            };
        }

        // Renderiza el terreno ASCII (Z = 0) anclado al mundo para mostrar sombras 2.5D y halos de luz.
        // Rellena unicamente las celdas vacias del Z-Buffer con el terreno ASCII y sombras 2.5D.
        // Al ejecutarse despues de los objetos, ahorra rayos de sombra y garantiza sombras continuas.
        void SubmitGroundPlaneToZBuffer(float zoom, uint32_t& atomsEvaluated) {
            const int     gridW = zBuffer.GetGridWidth();
            const int     gridH = zBuffer.GetGridHeight();
            const int32_t origX = zBuffer.GetOriginCellX();
            const int32_t origY = zBuffer.GetOriginCellY();
            const float   cellW = zBuffer.GetCellWidth();
            const float   cellH = zBuffer.GetCellHeight();
            const float   invZoom = 1.0f / zoom;
            const float   limit = world.GetWorldHalfSize();

            for (int cy = 0; cy < gridH; ++cy) {
                const float isoY = (static_cast<float>(origY + cy) + 0.5f) * cellH * invZoom;
                for (int cx = 0; cx < gridW; ++cx) {
                    if (!zBuffer.IsCellEmpty(cx, cy)) continue;

                    const float isoX = (static_cast<float>(origX + cx) + 0.5f) * cellW * invZoom;
                    Vec2 groundW = MathIso::IsoToWorldGround({ isoX, isoY }, isoConfig);

                    if (groundW.x < -limit || groundW.x > limit || groundW.y < -limit || groundW.y > limit) {
                        continue;
                    }

                    ++atomsEvaluated;
                    float noise = MathIso::Hash3D(static_cast<float>(origX + cx), static_cast<float>(origY + cy), 0.0f, 5.41f);
                    ShadedAtom shaded = lightingSystem.ShadeGroundPoint({ groundW.x, groundW.y, 0.0f }, noise, true);

                    if (shaded.glyph != '\0') {
                        float depth = MathIso::ComputeDepthKey({ groundW.x, groundW.y, 0.0f }) - 0.15f;
                        zBuffer.SetGroundCell(cx, cy, depth, shaded.glyph, shaded.color);
                    }
                }
            }
        }

        // Dibuja un destello ASCII en el origen de cada luz estatica del mapa (antorchas/cristales).
        void SubmitLightMarkersToZBuffer(float zoom, float camTopLeftX, float camTopLeftY) {
            const float screenW = static_cast<float>(screenWidth);
            const float screenH = static_cast<float>(screenHeight);

            for (const PointLight& pl : lightingSystem.GetMapLights()) {
                Vec2 isoPt = MathIso::WorldToIso(pl.position, isoConfig);
                float zoomedX = isoPt.x * zoom;
                float zoomedY = isoPt.y * zoom;
                float sx = zoomedX - camTopLeftX;
                float sy = zoomedY - camTopLeftY;

                if (sx < 0.0f || sx >= screenW || sy < 0.0f || sy >= screenH) continue;

                // Nucleo brillante de la fuente de luz (sin outline para efecto emisivo).
                zBuffer.SubmitAtom(zoomedX, zoomedY, MathIso::ComputeDepthKey(pl.position) + 0.05f,
                    '*', pl.color, FALLBACK_MATERIAL_ID, false, 0, false);
            }
        }

        // Envia la capsula del jugador al Z-Buffer usando el LightingSystem completo.
        void SubmitPlayerAvatarToZBuffer(int splatRadius, uint32_t& atomsEvaluated, uint32_t& atomsCulled) {
            const float step = 0.22f;
            const float bodyR = 0.55f;
            const float bodyH = 1.85f;
            const float zoom = camera.zoom;

            // 1. Torso cilindrico (material metal_iron).
            for (float z = 0.0f; z <= bodyH; z += step) {
                for (float y = -bodyR; y <= bodyR; y += step) {
                    for (float x = -bodyR; x <= bodyR; x += step) {
                        float rSq = x * x + y * y;
                        if (rSq > bodyR * bodyR || (z < bodyH - step && rSq < 0.08f)) continue;
                        ++atomsEvaluated;

                        Vec3 worldPt = { player.position.x + x, player.position.y + y, z };
                        Vec3 norm = MathIso::Normalize({ x, y, 0.25f });
                        ShadedAtom s = lightingSystem.ShadeAtom(worldPt, norm, 0.95f, materialSystem, playerBodyMat, true);

                        Vec2 isoPt = MathIso::WorldToIso(worldPt, isoConfig);
                        if (!zBuffer.SubmitAtom(isoPt.x * zoom, isoPt.y * zoom, MathIso::ComputeDepthKey(worldPt),
                            s.glyph, s.color, playerBodyMat, true, splatRadius, false)) {
                            ++atomsCulled;
                        }
                    }
                }
            }

            // 2. Cabeza esferica (material snow_fresh).
            const float headR = 0.48f;
            const float headZ = bodyH + 0.45f;
            for (float z = -headR; z <= headR; z += step) {
                for (float y = -headR; y <= headR; y += step) {
                    for (float x = -headR; x <= headR; x += step) {
                        if ((x * x + y * y + z * z) > headR * headR) continue;
                        ++atomsEvaluated;

                        Vec3 worldPt = { player.position.x + x, player.position.y + y, headZ + z };
                        Vec3 norm = MathIso::Normalize({ x, y, z });
                        ShadedAtom s = lightingSystem.ShadeAtom(worldPt, norm, 1.0f, materialSystem, playerHeadMat, true);

                        Vec2 isoPt = MathIso::WorldToIso(worldPt, isoConfig);
                        if (!zBuffer.SubmitAtom(isoPt.x * zoom, isoPt.y * zoom, MathIso::ComputeDepthKey(worldPt),
                            s.glyph, s.color, playerHeadMat, true, splatRadius, false)) {
                            ++atomsCulled;
                        }
                    }
                }
            }
        }

        // Dibuja un circulo plano en el suelo isometrico (Z = 0) para depuracion visual F4.
        void DrawIsoCircle(Vec2 centerWorld, float radius, Color col) const {
            constexpr int segments = 18;
            constexpr float stepRad = 6.2831853f / static_cast<float>(segments);

            Vec2 prev = IsoToScreen(MathIso::WorldToIso({ centerWorld.x + radius, centerWorld.y, 0.0f }, isoConfig));
            for (int i = 1; i <= segments; ++i) {
                float a = static_cast<float>(i) * stepRad;
                Vec2 curr = IsoToScreen(MathIso::WorldToIso(
                    { centerWorld.x + std::cos(a) * radius, centerWorld.y + std::sin(a) * radius, 0.0f },
                    isoConfig));
                DrawLineV({ prev.x, prev.y }, { curr.x, curr.y }, col);
                prev = curr;
            }
        }

        // Dibuja un rectangulo OBB rotado en el suelo isometrico (Z = 0) para depuracion F4.
        void DrawIsoOBB(Vec2 center, Vec2 halfSize, float cosRot, float sinRot, Color col) const {
            const std::array<Vec2, 4> localCorners = { {
                { -halfSize.x, -halfSize.y },
                { halfSize.x, -halfSize.y },
                { halfSize.x,  halfSize.y },
                { -halfSize.x,  halfSize.y }
            } };

            std::array<Vec2, 4> screenCorners;
            for (size_t i = 0; i < 4; ++i) {
                float wx = center.x + (localCorners[i].x * cosRot - localCorners[i].y * sinRot);
                float wy = center.y + (localCorners[i].x * sinRot + localCorners[i].y * cosRot);
                screenCorners[i] = IsoToScreen(MathIso::WorldToIso({ wx, wy, 0.0f }, isoConfig));
            }

            for (size_t i = 0; i < 4; ++i) {
                const Vec2& a = screenCorners[i];
                const Vec2& b = screenCorners[(i + 1) % 4];
                DrawLineV({ a.x, a.y }, { b.x, b.y }, col);
            }
        }

        // Overlays visuales (F4): Chunks (cian), Colisiones (verde), Luces (naranja) y Vector Solar (amarillo).
        void RenderDebugOverlays() const {
            if (!debugHUD.AreOverlaysVisible()) {
                return;
            }

            // 1. Chunks activos del SpatialHashGrid (cian).
            const float cs = spatialGrid.GetChunkSize();
            for (const ChunkCoord& cc : activeChunks) {
                float x0 = static_cast<float>(cc.cx) * cs;
                float y0 = static_cast<float>(cc.cy) * cs;
                DrawIsoOBB({ x0 + cs * 0.5f, y0 + cs * 0.5f }, { cs * 0.5f, cs * 0.5f }, 1.0f, 0.0f, { 0, 220, 240, 100 });
            }

            // 2. Volumenes de colision de instancias visibles (verde).
            const auto& instances = world.GetInstances();
            const Color colGreen = { 60, 245, 110, 200 };
            for (uint32_t id : visibleInstanceIds) {
                const MapInstance& inst = instances[id];
                const BakedPrefab* prefab = world.GetPrefab(inst.prefabId);
                if (!prefab || prefab->collider.type == ColliderType::NONE) continue;

                const ColliderDef& col = prefab->collider;
                const Vec2 center2D = { inst.position.x, inst.position.y };

                if (col.type == ColliderType::CIRCLE) {
                    float scaleAvg = (inst.scale.x + inst.scale.y) * 0.5f;
                    DrawIsoCircle(center2D, col.radius * scaleAvg, colGreen);
                }
                else if (col.type == ColliderType::BOX) {
                    DrawIsoOBB(center2D, { col.halfSize.x * inst.scale.x, col.halfSize.y * inst.scale.y },
                        inst.cosRot, inst.sinRot, colGreen);
                }
                else if (col.type == ColliderType::ARCH_PILLARS) {
                    float totalW = (col.halfSize.x * 2.0f) * inst.scale.x;
                    float openW = col.openingWidth * inst.scale.x;
                    float pillarW = std::max(0.2f, (totalW - openW) * 0.5f);
                    float offsetDist = (openW * 0.5f) + (pillarW * 0.5f);
                    Vec2  pillarHalf = { pillarW * 0.5f, col.halfSize.y * inst.scale.y };

                    DrawIsoOBB({ center2D.x - inst.cosRot * offsetDist, center2D.y - inst.sinRot * offsetDist },
                        pillarHalf, inst.cosRot, inst.sinRot, colGreen);
                    DrawIsoOBB({ center2D.x + inst.cosRot * offsetDist, center2D.y + inst.sinRot * offsetDist },
                        pillarHalf, inst.cosRot, inst.sinRot, colGreen);
                }
            }

            // 3. Radios de alcance de las luces puntuales activas (naranja).
            for (const ActiveFrameLight& al : lightingSystem.GetActiveLights()) {
                DrawIsoCircle({ al.position.x, al.position.y }, 1.0f / al.invRadius, { 255, 165, 60, 120 });
            }

            // 4. Circulo de colision del jugador + vector direccional del Sol/Luna.
            DrawIsoCircle(player.position, player.collisionRadius, GOLD);

            const SunState& sun = lightingSystem.GetSunState();
            Vec2 pStart = IsoToScreen(MathIso::WorldToIso({ player.position.x, player.position.y, 2.5f }, isoConfig));
            Vec2 pEnd = IsoToScreen(MathIso::WorldToIso({
                player.position.x + sun.lightDir.x * 5.0f,
                player.position.y + sun.lightDir.y * 5.0f,
                2.5f + sun.lightDir.z * 5.0f
                }, isoConfig));
            DrawLineEx({ pStart.x, pStart.y }, { pEnd.x, pEnd.y }, 2.0f, sun.isNight ? SKYBLUE : YELLOW);
        }
    };

    Context::Context() : m_impl(std::make_unique<Impl>()) {}
    Context::~Context() { Shutdown(); }

    bool Context::Initialize(int width, int height, const char* title) {
        m_impl->screenWidth = width;
        m_impl->screenHeight = height;

        SetConfigFlags(FLAG_WINDOW_RESIZABLE);
        InitWindow(width, height, title);
        SetTargetFPS(60);

        m_impl->initialized = IsWindowReady();
        if (m_impl->initialized) {
            m_impl->LoadTypography();
            m_impl->zBuffer.Resize(width, height, m_impl->cellWidthPx, m_impl->cellHeightPx);

            m_impl->camera.offset = { width * 0.5f, height * 0.5f };
            m_impl->camera.target = { 0.0f, 0.0f };
            m_impl->camera.zoom = 1.05f;
        }
        return m_impl->initialized;
    }

    bool Context::LoadRegistry(const std::string& dataRoot) {
        m_impl->dataRootPath = dataRoot;

        std::filesystem::path matPath = std::filesystem::path(dataRoot) / "materials" / "materials.json";
        std::filesystem::path objDir = std::filesystem::path(dataRoot) / "objects";

        bool matOk = m_impl->materialSystem.LoadFromFile(matPath.string());
        bool objOk = m_impl->world.LoadObjectsDirectory(objDir.string(), m_impl->materialSystem);

        m_impl->playerBodyMat = m_impl->materialSystem.GetId("metal_iron");
        m_impl->playerHeadMat = m_impl->materialSystem.GetId("snow_fresh");
        return matOk && objOk;
    }

    bool Context::LoadMap(const std::string& mapPath) {
        m_impl->activeMapPath = mapPath;
        bool ok = m_impl->world.LoadMapFromFile(mapPath, m_impl->spatialGrid);

        if (ok) {
            m_impl->player.position = m_impl->world.GetPlayerSpawn();
            m_impl->player.velocity = { 0.0f, 0.0f };

            Vec2 spawnIso = MathIso::WorldToIso({ m_impl->player.position.x, m_impl->player.position.y, 1.0f }, m_impl->isoConfig);
            m_impl->camera.target = { spawnIso.x, spawnIso.y };

            // Configuramos iluminacion dinamica y horneamos el Heightfield 2D de sombras del mapa.
            m_impl->lightingSystem.Configure(
                m_impl->world.GetDayNightConfig(),
                m_impl->world.GetGIConfig(),
                m_impl->world.GetMapLights()
            );
            m_impl->lightingSystem.BuildShadowHeightfield(m_impl->world, m_impl->materialSystem, 0.6f);

            m_impl->visibleInstanceIds.reserve(m_impl->world.GetInstances().size());
            m_impl->activeChunks.reserve(m_impl->spatialGrid.GetTotalChunks());
            m_impl->physicsQueryIds.reserve(128);
            m_impl->physicsQueryChunks.reserve(16);
        }
        return ok;
    }

    bool Context::IsRunning() const {
        return m_impl->initialized && !WindowShouldClose();
    }

    float Context::GetFrameTime() const {
        return ::GetFrameTime();
    }

    void Context::Update(float dt) {
        auto simStart = Clock::now();

        m_impl->screenWidth = GetScreenWidth();
        m_impl->screenHeight = GetScreenHeight();
        m_impl->camera.offset = { m_impl->screenWidth * 0.5f, m_impl->screenHeight * 0.5f };
        m_impl->zBuffer.Resize(m_impl->screenWidth, m_impl->screenHeight, m_impl->cellWidthPx, m_impl->cellHeightPx);

        // 1. Entrada WASD traducida de pantalla a direcciones cartesianas del mundo isometrico.
        Vec2 screenInput = { 0.0f, 0.0f };
        if (IsKeyDown(KEY_W)) screenInput.y -= 1.0f;
        if (IsKeyDown(KEY_S)) screenInput.y += 1.0f;
        if (IsKeyDown(KEY_A)) screenInput.x -= 1.0f;
        if (IsKeyDown(KEY_D)) screenInput.x += 1.0f;

        Vec2 desiredWorldDir = MathIso::IsoToWorldGround(screenInput, m_impl->isoConfig);
        m_impl->player.moveSpeed = IsKeyDown(KEY_LEFT_SHIFT) ? 24.0f : 14.0f;

        // 2. Fisica semi-implicita y resolucion de colisiones (Circulo, OBB, Arcos).
        CollisionSolver::UpdateActorPhysics(
            m_impl->player,
            desiredWorldDir,
            dt,
            m_impl->world,
            m_impl->spatialGrid,
            m_impl->physicsQueryIds,
            m_impl->physicsQueryChunks
        );

        // 3. Seguimiento suave de camara al jugador + Zoom artistico acotado.
        Vec2 targetIso = MathIso::WorldToIso({ m_impl->player.position.x, m_impl->player.position.y, 1.2f }, m_impl->isoConfig);
        float camSmooth = MathIso::Clamp(9.0f * dt, 0.0f, 1.0f);
        m_impl->camera.target.x = MathIso::Lerp(m_impl->camera.target.x, targetIso.x, camSmooth);
        m_impl->camera.target.y = MathIso::Lerp(m_impl->camera.target.y, targetIso.y, camSmooth);

        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            m_impl->camera.zoom = MathIso::Clamp(
                m_impl->camera.zoom + wheel * 0.08f,
                m_impl->minZoom,
                m_impl->maxZoom
            );
        }

        // 4. Controles de Ciclo Dia-Noche, Sombras, Antorcha y Hot-Reloading.
        if (IsKeyPressed(KEY_SPACE)) m_impl->lightingSystem.ToggleCycleRunning();
        if (IsKeyDown(KEY_LEFT))     m_impl->lightingSystem.AdjustTimeHours(-dt * 4.5f);
        if (IsKeyDown(KEY_RIGHT))    m_impl->lightingSystem.AdjustTimeHours(dt * 4.5f);
        if (IsKeyPressed(KEY_H))     m_impl->lightingSystem.ToggleShadows();
        if (IsKeyPressed(KEY_T))     m_impl->playerTorchOn = !m_impl->playerTorchOn;
        if (IsKeyPressed(KEY_O))     m_impl->zBufferStyle.enableOutlines = !m_impl->zBufferStyle.enableOutlines;
        if (IsKeyPressed(KEY_B))     m_impl->zBufferStyle.enableAsciiBorder = !m_impl->zBufferStyle.enableAsciiBorder;
        if (IsKeyPressed(KEY_F3))    m_impl->debugHUD.ToggleTelemetry();
        if (IsKeyPressed(KEY_F4))    m_impl->debugHUD.ToggleOverlays();
        if (IsKeyPressed(KEY_F5)) {
            Vec2 savedPos = m_impl->player.position;
            float savedHr = m_impl->lightingSystem.GetTimeHours();
            LoadRegistry(m_impl->dataRootPath);
            if (!m_impl->activeMapPath.empty()) {
                LoadMap(m_impl->activeMapPath);
                m_impl->player.position = savedPos;
                m_impl->lightingSystem.AdjustTimeHours(savedHr - m_impl->lightingSystem.GetTimeHours());
            }
        }

        // 5. Avanzar ciclo Dia-Noche y parpadeo de luces.
        m_impl->lightingSystem.Update(dt);

        auto simEnd = Clock::now();

        // Actualizar telemetria del frame.
        const SunState& sun = m_impl->lightingSystem.GetSunState();
        m_impl->telemetry.simTimeMs = ElapsedMs(simStart, simEnd);
        m_impl->telemetry.fps = static_cast<float>(GetFPS());
        m_impl->telemetry.frameTimeMs = dt * 1000.0f;
        m_impl->telemetry.cameraWorldPos = m_impl->player.position;
        m_impl->telemetry.cameraZoom = m_impl->camera.zoom;
        m_impl->telemetry.outlinesActive = m_impl->zBufferStyle.enableOutlines;
        m_impl->telemetry.timeOfDayHours = m_impl->lightingSystem.GetTimeHours();
        m_impl->telemetry.isNight = sun.isNight;
        m_impl->telemetry.dayCycleRunning = m_impl->lightingSystem.IsCycleRunning();
        m_impl->telemetry.shadowsEnabled = m_impl->lightingSystem.AreShadowsEnabled();
        m_impl->telemetry.torchEnabled = m_impl->playerTorchOn;
        m_impl->telemetry.totalMapLights = static_cast<uint32_t>(m_impl->lightingSystem.GetMapLights().size());
        m_impl->debugHUD.UpdateMetrics(m_impl->telemetry, dt);
    }

    void Context::Render() const {
        // =========================================================================
        // ETAPA 5: CULLING CAPA 1 (Spatial Hash Grid) + CULLING DE LUCES ACTIVAS
        // =========================================================================
        auto cullStart = Clock::now();

        float minWX = 0.0f, maxWX = 0.0f, minWY = 0.0f, maxWY = 0.0f;
        m_impl->ComputeVisibleWorldBounds(minWX, maxWX, minWY, maxWY);
        ChunkRange visibleRange = m_impl->ComputeVisibleChunkRange(minWX, maxWX, minWY, maxWY);
        m_impl->spatialGrid.QueryChunks(visibleRange, m_impl->visibleInstanceIds, m_impl->activeChunks);

        // Construimos la antorcha dinamica del jugador si esta encendida.
        PointLight playerTorch;
        const PointLight* torchPtr = nullptr;
        if (m_impl->playerTorchOn) {
            playerTorch.position = { m_impl->player.position.x, m_impl->player.position.y, 2.3f };
            playerTorch.color = { 255, 190, 110, 255 };
            playerTorch.intensity = 1.45f;
            playerTorch.radius = 11.0f;
            playerTorch.flicker = true;
            playerTorch.flickerPhase = 0.7f;
            torchPtr = &playerTorch;
        }
        m_impl->lightingSystem.PrepareFrameLights(minWX, maxWX, minWY, maxWY, torchPtr);

        auto cullEnd = Clock::now();

        // =========================================================================
        // ETAPA 6: TERRENO + FRUSTUM 2D + ILUMINACION/SOMBRAS + Z-BUFFER O(N)
        // =========================================================================
        auto zbufStart = Clock::now();

        const float zoom = m_impl->camera.zoom;
        const float camTopLeftX = m_impl->camera.target.x * zoom - m_impl->camera.offset.x;
        const float camTopLeftY = m_impl->camera.target.y * zoom - m_impl->camera.offset.y;

        m_impl->zBuffer.BeginFrame(camTopLeftX, camTopLeftY);

        const Rect2D viewportRect = {
            -m_impl->viewPaddingPx,
            -m_impl->viewPaddingPx,
            static_cast<float>(m_impl->screenWidth) + m_impl->viewPaddingPx * 2.0f,
            static_cast<float>(m_impl->screenHeight) + m_impl->viewPaddingPx * 2.0f
        };

        const int splatRadius = (zoom > 1.15f) ? 1 : 0;

        uint32_t instancesRendered = 0;
        uint32_t atomsEvaluated = 0;
        uint32_t atomsCulledScreen = 0;

        

        // 2. Destellos emisivos en el origen de las luces del mapa.
        m_impl->SubmitLightMarkersToZBuffer(zoom, camTopLeftX, camTopLeftY);

        // 3. Avatar del jugador sombreado por el LightingSystem.
        m_impl->SubmitPlayerAvatarToZBuffer(splatRadius, atomsEvaluated, atomsCulledScreen);

        // 4. Instancias visibles del mundo (Flyweight + GI + Sombras 2.5D + Luces Puntuales).
        const auto& instances = m_impl->world.GetInstances();
        const float screenW = static_cast<float>(m_impl->screenWidth);
        const float screenH = static_cast<float>(m_impl->screenHeight);

        for (uint32_t instId : m_impl->visibleInstanceIds) {
            const MapInstance& inst = instances[instId];
            const BakedPrefab* prefab = m_impl->world.GetPrefab(inst.prefabId);
            if (!prefab) continue;

            // Capa 2 — Frustum Culling 2D.
            Vec2 baseScreen = m_impl->IsoToScreen(MathIso::WorldToIso(inst.position, m_impl->isoConfig));
            float maxScaleXY = std::max(inst.scale.x, inst.scale.y);
            float screenRadX = prefab->boundRadiusXY * maxScaleXY * m_impl->isoConfig.tileWidthHalf * zoom;
            float screenHgt = (prefab->boundHeightZ * inst.scale.z * m_impl->isoConfig.scaleZ
                + prefab->boundRadiusXY * maxScaleXY * m_impl->isoConfig.tileHeightHalf) * zoom;

            Rect2D objRect = {
                baseScreen.x - screenRadX,
                baseScreen.y - screenHgt,
                screenRadX * 2.0f,
                screenHgt + screenRadX * 0.6f
            };
            if (!viewportRect.Intersects(objRect)) {
                continue;
            }

            ++instancesRendered;

            const BakedAtom* atoms = prefab->atoms.data();
            const size_t atomCount = prefab->atoms.size();

            for (size_t i = 0; i < atomCount; ++i) {
                const BakedAtom& a = atoms[i];
                ++atomsEvaluated;

                float sx = a.localPos.x * inst.scale.x;
                float sy = a.localPos.y * inst.scale.y;
                float sz = a.localPos.z * inst.scale.z;

                Vec3 worldPt = {
                    inst.position.x + (sx * inst.cosRot - sy * inst.sinRot),
                    inst.position.y + (sx * inst.sinRot + sy * inst.cosRot),
                    inst.position.z + sz
                };

                Vec2 isoPt = MathIso::WorldToIso(worldPt, m_impl->isoConfig);
                float zoomedX = isoPt.x * zoom;
                float zoomedY = isoPt.y * zoom;

                // Pre-check rapido de Capa 3 antes de calcular sombras e iluminacion.
                float screenX = zoomedX - camTopLeftX;
                float screenY = zoomedY - camTopLeftY;
                if (screenX < -m_impl->cellWidthPx || screenX >= screenW + m_impl->cellWidthPx ||
                    screenY < -m_impl->cellHeightPx || screenY >= screenH + m_impl->cellHeightPx) {
                    ++atomsCulledScreen;
                    continue;
                }

                Vec3 worldNorm = {
                    a.localNormal.x * inst.cosRot - a.localNormal.y * inst.sinRot,
                    a.localNormal.x * inst.sinRot + a.localNormal.y * inst.cosRot,
                    a.localNormal.z
                };

                MaterialId matId = (inst.overrideMaterialId != FALLBACK_MATERIAL_ID)
                    ? inst.overrideMaterialId
                    : a.materialId;
                const Material& mat = m_impl->materialSystem.Get(matId);

                ShadedAtom shaded = m_impl->lightingSystem.ShadeAtom(
                    worldPt, worldNorm, a.ao, m_impl->materialSystem, matId, true, a.structuralOverride
                );

                float depth = MathIso::ComputeDepthKey(worldPt);
                if (!m_impl->zBuffer.SubmitAtom(zoomedX, zoomedY, depth, shaded.glyph, shaded.color,
                    matId, mat.outline, splatRadius, false)) {
                    ++atomsCulledScreen;
                }
            }
        }
        // 4. Terreno y sombras 2.5D solo sobre las celdas vacias restantes (cero trabajo detras de objetos).
        m_impl->SubmitGroundPlaneToZBuffer(zoom, atomsEvaluated);
        auto zbufEnd = Clock::now();

        // =========================================================================
        // ETAPA 7 & 8: BATCH DRAW GPU + OVERLAYS (F4) + TELEMETRIA (F3)
        // =========================================================================
        auto gpuStart = Clock::now();

        BeginDrawing();

        // Fondo modulado dinamicamente segun el ciclo Dia-Noche (gris claro -> atardecer -> noche).
        const ColorRGBA& bg = m_impl->lightingSystem.GetSunState().backgroundColor;
        ClearBackground({ bg.r, bg.g, bg.b, bg.a });

        ZBufferStats zStats = m_impl->zBuffer.FlushToScreen(
            &m_impl->renderFont,
            &m_impl->glyphLUT,
            m_impl->glyphAspectRatio,
            m_impl->zBufferStyle
        );

        m_impl->RenderDebugOverlays();

        auto gpuEnd = Clock::now();

        auto& t = m_impl->telemetry;
        t.cullingTimeMs = ElapsedMs(cullStart, cullEnd);
        t.rasterZBufferMs = ElapsedMs(zbufStart, zbufEnd);
        t.gpuDrawMs = ElapsedMs(gpuStart, gpuEnd);
        t.totalChunks = m_impl->spatialGrid.GetTotalChunks();
        t.activeChunks = static_cast<uint32_t>(m_impl->activeChunks.size());
        t.totalInstances = static_cast<uint32_t>(instances.size());
        t.instancesEvaluated = static_cast<uint32_t>(m_impl->visibleInstanceIds.size());
        t.instancesRendered = instancesRendered;
        t.atomsEvaluated = atomsEvaluated;
        t.atomsCulledScreen = atomsCulledScreen;
        t.glyphsRendered = zStats.glyphsRendered;
        t.outlineCellsDrawn = zStats.outlineCellsDrawn;
        t.zBufferBytes = m_impl->zBuffer.GetMemoryBytes();
        t.spatialGridBytes = m_impl->spatialGrid.GetMemoryBytes();
        t.bakedCloudBytes = m_impl->world.GetBakedMemoryBytes();
        t.heightfieldBytes = m_impl->lightingSystem.GetHeightfieldBytes();
        t.activePointLights = static_cast<uint32_t>(m_impl->lightingSystem.GetActiveLights().size());
        t.gridWidth = m_impl->zBuffer.GetGridWidth();
        t.gridHeight = m_impl->zBuffer.GetGridHeight();

        m_impl->debugHUD.RenderPanel(t, &m_impl->renderFont);

        EndDrawing();
    }

    void Context::Shutdown() {
        if (m_impl && m_impl->initialized) {
            if (m_impl->customFontLoaded) {
                UnloadFont(m_impl->renderFont);
                m_impl->customFontLoaded = false;
            }
            CloseWindow();
            m_impl->initialized = false;
        }
    }

} // namespace AsciiEngine