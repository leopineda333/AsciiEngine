/**
 * @file Engine.cpp
 * @brief Integracion de Fase 2: SpatialHashGrid + Frustum 2D + AsciiZBuffer O(N) + DebugHUD (F3/F4).
 */
#include <AsciiEngine/Engine.hpp>
#include <AsciiEngine/MathIso.hpp>
#include <AsciiEngine/MaterialSystem.hpp>
#include <AsciiEngine/AsciiZBuffer.hpp>
#include <AsciiEngine/CullingSystem.hpp>
#include <AsciiEngine/DebugHUD.hpp>
#include "raylib.h"

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <vector>

namespace AsciiEngine {

    namespace {

        // Punto precalculado (Preview del Point-Cloud Baking Flyweight que expandiremos en Fase 3).
        struct BakedUnitAtom {
            Vec3  localPos;
            Vec3  localNormal;
            float ao = 1.0f;
        };

        // Instancia ligera en el mapa (solo 24 bytes: comparte el nube de puntos horneada).
        struct StressTestInstance {
            Vec3       worldPos;
            float      scale = 1.0f;
            MaterialId materialId = FALLBACK_MATERIAL_ID;
            uint8_t    cloudIndex = 0; // Indice en el arreglo de nubes horneadas.
        };

        using Clock = std::chrono::steady_clock;
        float ElapsedMs(Clock::time_point start, Clock::time_point end) {
            return std::chrono::duration<float, std::milli>(end - start).count();
        }

    } // namespace anonimo

    struct Context::Impl {
        bool initialized = false;
        int screenWidth = 1600;
        int screenHeight = 900;

        // Tipografia con Lookup Table O(1).
        Font renderFont = {};
        bool customFontLoaded = false;
        float glyphAspectRatio = 0.58f;
        std::array<Rectangle, 128> glyphLUT = {};

        // Subsistemas principales de Fase 1 y Fase 2.
        std::string dataRootPath = "assets/data";
        MaterialSystem  materialSystem;
        MathIso::IsoConfig isoConfig;
        AsciiZBuffer    zBuffer;
        SpatialHashGrid spatialGrid{ 24.0f };
        DebugHUD        debugHUD;

        // Camara 2D isometrica y parametros de Culling.
        Camera2D camera = {};
        float viewPaddingPx = 96.0f; // Margen extra para no cortar objetos altos en los bordes.
        float cellWidthPx = 6.0f;
        float cellHeightPx = 10.0f;

        // Iluminacion de prueba y controles interactivos.
        float lightAngleRad = 0.8f;
        float lightElevation = 0.68f;
        float lightIntensity = 1.05f;
        bool  autoOrbitLight = true;
        bool  enableOutlines = true;

        // Datos del Stress Test de Fase 2 (2,025 objetos compartiendo 5 nubes horneadas).
        std::vector<std::vector<BakedUnitAtom>> bakedClouds;
        std::vector<StressTestInstance>         instances;

        // Buffers pre-alojados reutilizados cada frame (CERO asignaciones en el bucle principal).
        std::vector<uint32_t>   visibleInstanceIds;
        std::vector<ChunkCoord> activeChunks;

        // Telemetria actualizada frame a frame.
        FrameTelemetry telemetry;
        size_t cachedBakedCloudBytes = 0;

        void LoadTypography() {
            const char* candidates[] = {
                "assets/fonts/SourceCodePro-Bold.ttf",
                "C:/Windows/Fonts/consolab.ttf",
                "C:/Windows/Fonts/consola.ttf"
            };

            customFontLoaded = false;
            for (const char* path : candidates) {
                if (FileExists(path)) {
                    renderFont = LoadFontEx(path, 36, nullptr, 95);
                    SetTextureFilter(renderFont.texture, TEXTURE_FILTER_BILINEAR);
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

        // Hornea una nube de puntos unitaria (radio 2.0) respetando REGULAR vs NOISY del material.
        std::vector<BakedUnitAtom> BakeTestSphereCloud(MaterialId matId, float seed) const {
            std::vector<BakedUnitAtom> cloud;
            const Material& mat = materialSystem.Get(matId);
            const float radius = 2.0f;
            const Vec3 radii = { radius, radius, radius };
            const float step = 0.34f;

            for (float z = -radius; z <= radius; z += step) {
                for (float y = -radius; y <= radius; y += step) {
                    for (float x = -radius; x <= radius; x += step) {
                        Vec3 p = { x, y, z };

                        if (mat.gridMode == GridMode::NOISY) {
                            if (MathIso::Hash3D(x, y, z, seed + 11.7f) > mat.density) {
                                continue;
                            }
                            float jAmp = mat.jitter * step * 2.0f;
                            p.x += (MathIso::Hash3D(x, y, z, seed + 1.3f) - 0.5f) * jAmp;
                            p.y += (MathIso::Hash3D(x, y, z, seed + 2.7f) - 0.5f) * jAmp;
                            p.z += (MathIso::Hash3D(x, y, z, seed + 5.1f) - 0.5f) * jAmp;
                        }

                        float metric = MathIso::EllipsoidMetric(p, radii);
                        if (metric > 1.0f || metric < 0.62f) {
                            continue; // Solo conservamos la cascara superficial.
                        }

                        Vec3 normal = MathIso::EllipsoidNormal(p, radii);
                        float ao = MathIso::Clamp(0.45f + 0.55f * normal.z, 0.25f, 1.0f);
                        cloud.push_back({ p, normal, ao });
                    }
                }
            }
            return cloud;
        }

        // Construye el escenario Stress Test con 2,025 objetos registrados en el SpatialHashGrid.
        void BuildPhase2StressTestScene() {
            const std::array<MaterialId, 5> palette = {
                materialSystem.GetId("metal_iron"),
                materialSystem.GetId("wood_bark"),
                materialSystem.GetId("foliage_pine"),
                materialSystem.GetId("stone_granite"),
                materialSystem.GetId("snow_fresh")
            };

            // 1. Horneamos solo 5 plantillas en memoria (Patron Flyweight).
            bakedClouds.clear();
            cachedBakedCloudBytes = 0;
            for (size_t i = 0; i < palette.size(); ++i) {
                bakedClouds.push_back(BakeTestSphereCloud(palette[i], static_cast<float>(i) * 3.7f));
                cachedBakedCloudBytes += bakedClouds.back().capacity() * sizeof(BakedUnitAtom);
            }

            // 2. Generamos 45 x 45 = 2,025 instancias distribuidas en [-140, +140].
            constexpr int gridSide = 45;
            const float spacing = 6.4f;
            const float halfExtent = (gridSide - 1) * spacing * 0.5f;

            instances.clear();
            instances.reserve(gridSide * gridSide);
            spatialGrid.Reset(24.0f, gridSide * gridSide);

            for (int gy = 0; gy < gridSide; ++gy) {
                for (int gx = 0; gx < gridSide; ++gx) {
                    float baseX = static_cast<float>(gx) * spacing - halfExtent;
                    float baseY = static_cast<float>(gy) * spacing - halfExtent;

                    // Desplazamiento organico determinista para romper la alineacion rigida.
                    float jx = (MathIso::Hash3D(baseX, baseY, 1.0f, 4.2f) - 0.5f) * 2.2f;
                    float jy = (MathIso::Hash3D(baseX, baseY, 2.0f, 9.1f) - 0.5f) * 2.2f;
                    float scale = 0.75f + MathIso::Hash3D(baseX, baseY, 3.0f, 1.8f) * 0.55f;

                    uint8_t variant = static_cast<uint8_t>((gx * 7 + gy * 13) % palette.size());
                    uint32_t id = static_cast<uint32_t>(instances.size());

                    StressTestInstance inst;
                    inst.worldPos = { baseX + jx, baseY + jy, 1.6f * scale };
                    inst.scale = scale;
                    inst.materialId = palette[variant];
                    inst.cloudIndex = variant;

                    instances.push_back(inst);
                    spatialGrid.InsertInstance(id, inst.worldPos.x, inst.worldPos.y, 2.2f * scale);
                }
            }

            // Pre-reservamos los vectores de consulta para cero asignaciones durante el bucle.
            visibleInstanceIds.reserve(instances.size());
            activeChunks.reserve(spatialGrid.GetTotalChunks());

            TraceLog(LOG_INFO, "PHASE 2 STRESS TEST: %zu instancias horneadas en %u chunks.",
                instances.size(), spatialGrid.GetTotalChunks());
        }

        // Convierte un punto isometrico del mundo a coordenadas reales de pantalla segun Camera2D.
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

        // Calcula el rango de chunks [minCX..maxCX, minCY..maxCY] que intersectan la pantalla.
        ChunkRange ComputeVisibleChunkRange() const {
            const float pad = viewPaddingPx;
            const std::array<Vec2, 4> corners = { {
                { -pad, -pad },
                { static_cast<float>(screenWidth) + pad, -pad },
                { -pad, static_cast<float>(screenHeight) + pad * 1.5f }, // Margen inferior extra por altura Z.
                { static_cast<float>(screenWidth) + pad, static_cast<float>(screenHeight) + pad * 1.5f }
            } };

            float minWX = 1e9f, maxWX = -1e9f;
            float minWY = 1e9f, maxWY = -1e9f;

            for (const Vec2& c : corners) {
                Vec2 worldPt = MathIso::IsoToWorldGround(ScreenToIso(c), isoConfig);
                minWX = std::min(minWX, worldPt.x);
                maxWX = std::max(maxWX, worldPt.x);
                minWY = std::min(minWY, worldPt.y);
                maxWY = std::max(maxWY, worldPt.y);
            }

            const float invChunk = 1.0f / spatialGrid.GetChunkSize();
            return {
                static_cast<int32_t>(std::floor(minWX * invChunk)),
                static_cast<int32_t>(std::floor(maxWX * invChunk)),
                static_cast<int32_t>(std::floor(minWY * invChunk)),
                static_cast<int32_t>(std::floor(maxWY * invChunk))
            };
        }

        // Dibuja los overlays de depuracion visual (F4): diamantes de chunks activos + caja de frustum.
        void RenderDebugOverlays() const {
            if (!debugHUD.AreOverlaysVisible()) {
                return;
            }

            const float cs = spatialGrid.GetChunkSize();
            for (const ChunkCoord& cc : activeChunks) {
                float x0 = static_cast<float>(cc.cx) * cs;
                float y0 = static_cast<float>(cc.cy) * cs;
                float x1 = x0 + cs;
                float y1 = y0 + cs;

                Vec2 p0 = IsoToScreen(MathIso::WorldToIso({ x0, y0, 0.0f }, isoConfig));
                Vec2 p1 = IsoToScreen(MathIso::WorldToIso({ x1, y0, 0.0f }, isoConfig));
                Vec2 p2 = IsoToScreen(MathIso::WorldToIso({ x1, y1, 0.0f }, isoConfig));
                Vec2 p3 = IsoToScreen(MathIso::WorldToIso({ x0, y1, 0.0f }, isoConfig));

                Color chunkCol = { 0, 220, 240, 120 };
                DrawLineV({ p0.x, p0.y }, { p1.x, p1.y }, chunkCol);
                DrawLineV({ p1.x, p1.y }, { p2.x, p2.y }, chunkCol);
                DrawLineV({ p2.x, p2.y }, { p3.x, p3.y }, chunkCol);
                DrawLineV({ p3.x, p3.y }, { p0.x, p0.y }, chunkCol);
            }

            // Borde interno de referencia para visualizar el frustum activo.
            DrawRectangleLinesEx({ 4.0f, 4.0f, static_cast<float>(screenWidth - 8), static_cast<float>(screenHeight - 8) },
                2.0f, { 255, 220, 0, 140 });
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
            m_impl->camera.zoom = 1.0f;
        }
        return m_impl->initialized;
    }

    bool Context::LoadRegistry(const std::string& dataRoot) {
        m_impl->dataRootPath = dataRoot;
        std::filesystem::path matPath = std::filesystem::path(dataRoot) / "materials" / "materials.json";

        bool ok = m_impl->materialSystem.LoadFromFile(matPath.string());
        m_impl->BuildPhase2StressTestScene();
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

        // Sincronizar tamano de ventana y Z-Buffer si se redimensiona.
        m_impl->screenWidth = GetScreenWidth();
        m_impl->screenHeight = GetScreenHeight();
        m_impl->camera.offset = { m_impl->screenWidth * 0.5f, m_impl->screenHeight * 0.5f };
        m_impl->zBuffer.Resize(m_impl->screenWidth, m_impl->screenHeight, m_impl->cellWidthPx, m_impl->cellHeightPx);

        // Movimiento de camara isometrica con WASD (ajustado al zoom actual).
        const float panSpeed = 480.0f / m_impl->camera.zoom;
        if (IsKeyDown(KEY_W)) m_impl->camera.target.y -= panSpeed * dt;
        if (IsKeyDown(KEY_S)) m_impl->camera.target.y += panSpeed * dt;
        if (IsKeyDown(KEY_A)) m_impl->camera.target.x -= panSpeed * dt;
        if (IsKeyDown(KEY_D)) m_impl->camera.target.x += panSpeed * dt;

        // Zoom suave con rueda del raton.
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            m_impl->camera.zoom = MathIso::Clamp(m_impl->camera.zoom + wheel * 0.12f, 0.45f, 2.5f);
        }

        // Controles de prueba: orbita solar, outlines, Debug HUD (F3), Overlays (F4) y Hot-Reload (F5).
        if (IsKeyPressed(KEY_SPACE)) m_impl->autoOrbitLight = !m_impl->autoOrbitLight;
        if (m_impl->autoOrbitLight)  m_impl->lightAngleRad += dt * 0.7f;
        if (IsKeyDown(KEY_LEFT)) { m_impl->autoOrbitLight = false; m_impl->lightAngleRad -= dt * 2.0f; }
        if (IsKeyDown(KEY_RIGHT)) { m_impl->autoOrbitLight = false; m_impl->lightAngleRad += dt * 2.0f; }
        if (IsKeyPressed(KEY_O))     m_impl->enableOutlines = !m_impl->enableOutlines;
        if (IsKeyPressed(KEY_F3))    m_impl->debugHUD.ToggleTelemetry();
        if (IsKeyPressed(KEY_F4))    m_impl->debugHUD.ToggleOverlays();
        if (IsKeyPressed(KEY_F5))    LoadRegistry(m_impl->dataRootPath);

        auto simEnd = Clock::now();

        // Actualizar telemetria base de rendimiento y memoria.
        m_impl->telemetry.simTimeMs = ElapsedMs(simStart, simEnd);
        m_impl->telemetry.fps = static_cast<float>(GetFPS());
        m_impl->telemetry.frameTimeMs = dt * 1000.0f;
        m_impl->telemetry.cameraWorldPos = MathIso::IsoToWorldGround(
            { m_impl->camera.target.x, m_impl->camera.target.y },
            m_impl->isoConfig
        );
        m_impl->telemetry.cameraZoom = m_impl->camera.zoom;
        m_impl->telemetry.outlinesActive = m_impl->enableOutlines;
        m_impl->debugHUD.UpdateMetrics(m_impl->telemetry, dt);
    }

    void Context::Render() const {
        // =========================================================================
        // ETAPA 5: CULLING CAPA 1 (Spatial Hash Grid)
        // =========================================================================
        auto cullStart = Clock::now();

        ChunkRange visibleRange = m_impl->ComputeVisibleChunkRange();
        m_impl->spatialGrid.QueryChunks(visibleRange, m_impl->visibleInstanceIds, m_impl->activeChunks);

        auto cullEnd = Clock::now();

        // =========================================================================
        // ETAPA 6: CULLING CAPA 2 (Frustum 2D) + ILUMINACION + Z-BUFFER O(N) (CAPA 3)
        // =========================================================================
        auto zbufStart = Clock::now();

        m_impl->zBuffer.Clear();

        const Rect2D viewportRect = {
            -m_impl->viewPaddingPx,
            -m_impl->viewPaddingPx,
            static_cast<float>(m_impl->screenWidth) + m_impl->viewPaddingPx * 2.0f,
            static_cast<float>(m_impl->screenHeight) + m_impl->viewPaddingPx * 2.0f
        };

        const Vec3 lightDir = MathIso::Normalize({
            std::cos(m_impl->lightAngleRad),
            std::sin(m_impl->lightAngleRad),
            m_impl->lightElevation
            });

        uint32_t instancesRendered = 0;
        uint32_t atomsEvaluated = 0;
        uint32_t atomsCulledScreen = 0;

        for (uint32_t instId : m_impl->visibleInstanceIds) {
            const StressTestInstance& inst = m_impl->instances[instId];

            // Capa 2 — Frustum Culling 2D: proyectamos el centro y comprobamos su AABB de pantalla.
            Vec2 centerScreen = m_impl->IsoToScreen(MathIso::WorldToIso(inst.worldPos, m_impl->isoConfig));
            float screenRadius = 2.4f * inst.scale * m_impl->isoConfig.tileWidthHalf * m_impl->camera.zoom;
            Rect2D objRect = {
                centerScreen.x - screenRadius,
                centerScreen.y - screenRadius,
                screenRadius * 2.0f,
                screenRadius * 2.0f
            };

            if (!viewportRect.Intersects(objRect)) {
                continue; // Descartado en bloque antes de tocar su nube de puntos.
            }

            ++instancesRendered;

            const Material& mat = m_impl->materialSystem.Get(inst.materialId);
            const auto& cloud = m_impl->bakedClouds[inst.cloudIndex];
            const BakedUnitAtom* atoms = cloud.data();
            const size_t atomCount = cloud.size();

            for (size_t i = 0; i < atomCount; ++i) {
                const BakedUnitAtom& a = atoms[i];
                ++atomsEvaluated;

                Vec3 worldPt = {
                    inst.worldPos.x + a.localPos.x * inst.scale,
                    inst.worldPos.y + a.localPos.y * inst.scale,
                    inst.worldPos.z + a.localPos.z * inst.scale
                };

                // Proyeccion 3D -> Isometrico -> Pantalla.
                Vec2 screenPt = m_impl->IsoToScreen(MathIso::WorldToIso(worldPt, m_impl->isoConfig));

                // Pre-check rapido de Capa 3 antes de calcular iluminacion y color.
                if (screenPt.x < 0.0f || screenPt.x >= static_cast<float>(m_impl->screenWidth) ||
                    screenPt.y < 0.0f || screenPt.y >= static_cast<float>(m_impl->screenHeight)) {
                    ++atomsCulledScreen;
                    continue;
                }

                // Iluminacion Lambertiana + oclusion ambiental precalculada.
                float diffuse = std::max(0.0f, MathIso::Dot(a.localNormal, lightDir)) * m_impl->lightIntensity;
                float luma = MathIso::Clamp((diffuse + 0.14f) * a.ao, 0.0f, 1.0f);

                char glyph = m_impl->materialSystem.SampleGlyph(inst.materialId, luma);
                ColorRGBA col = m_impl->materialSystem.SampleColor(inst.materialId, luma);
                float depth = MathIso::ComputeDepthKey(worldPt);

                // Insercion O(1) en el Screen-Space ASCII Z-Buffer.
                if (!m_impl->zBuffer.SubmitAtom(screenPt.x, screenPt.y, depth, glyph, col, inst.materialId, mat.outline)) {
                    ++atomsCulledScreen;
                }
            }
        }

        auto zbufEnd = Clock::now();

        // =========================================================================
        // ETAPA 7 & 8: RASTERIZADO BATCH UNICO EN GPU + OVERLAYS (F4) + DEBUG HUD (F3)
        // =========================================================================
        auto gpuStart = Clock::now();

        BeginDrawing();

        // Fondo gris oscuro neutro (RGB 30, 32, 36).
        ClearBackground({ 30, 32, 36, 255 });

        // Volcado lineal O(W * H) del Z-Buffer con Zero Overdraw y siluetas O(1).
        ZBufferStats zStats = m_impl->zBuffer.FlushToScreen(
            &m_impl->renderFont,
            &m_impl->glyphLUT,
            m_impl->glyphAspectRatio,
            m_impl->enableOutlines,
            2.2f
        );

        // Overlays visuales (F4) sobre el viewport.
        m_impl->RenderDebugOverlays();

        auto gpuEnd = Clock::now();

        // Actualizamos los contadores finales de telemetria para dibujarlos en el HUD (F3).
        auto& t = m_impl->telemetry;
        t.cullingTimeMs = ElapsedMs(cullStart, cullEnd);
        t.rasterZBufferMs = ElapsedMs(zbufStart, zbufEnd);
        t.gpuDrawMs = ElapsedMs(gpuStart, gpuEnd);
        t.totalChunks = m_impl->spatialGrid.GetTotalChunks();
        t.activeChunks = static_cast<uint32_t>(m_impl->activeChunks.size());
        t.totalInstances = static_cast<uint32_t>(m_impl->instances.size());
        t.instancesEvaluated = static_cast<uint32_t>(m_impl->visibleInstanceIds.size());
        t.instancesRendered = instancesRendered;
        t.atomsEvaluated = atomsEvaluated;
        t.atomsCulledScreen = atomsCulledScreen;
        t.glyphsRendered = zStats.glyphsRendered;
        t.outlineCellsDrawn = zStats.outlineCellsDrawn;
        t.zBufferBytes = m_impl->zBuffer.GetMemoryBytes();
        t.spatialGridBytes = m_impl->spatialGrid.GetMemoryBytes();
        t.bakedCloudBytes = m_impl->cachedBakedCloudBytes;
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