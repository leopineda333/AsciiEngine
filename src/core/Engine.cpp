/**
 * @file Engine.cpp
 * @brief Integracion de Fase 5: Campo de Viento, Agua con Reflejos, Particulas SoA y Clima por Biomas 
 * y 5.5: LOD de Efectos por Zoom/Distancia, Pasto/Flores Interactivas, Bioma Data-Driven y SurfaceZones..
 */
#include <AsciiEngine/Engine.hpp>
#include <AsciiEngine/MathIso.hpp>
#include <AsciiEngine/MaterialSystem.hpp>
#include <AsciiEngine/AsciiZBuffer.hpp>
#include <AsciiEngine/CullingSystem.hpp>
#include <AsciiEngine/LightingSystem.hpp>
#include <AsciiEngine/WeatherSystem.hpp>
#include <AsciiEngine/ParticleSystem.hpp>
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
        WeatherSystem      weatherSystem;
        ParticleSystem     particleSystem;
        World              world;
        DebugHUD           debugHUD;

        // Jugador fisico de prueba (en Fase 6 pasara al CharacterSystem modular).
        SimpleActor player;
        MaterialId  playerBodyMat = FALLBACK_MATERIAL_ID;
        MaterialId  playerHeadMat = FALLBACK_MATERIAL_ID;
        bool        playerTorchOn = true;

        // Camara 2D isometrica y rejilla Z-Buffer calibrada (7x12 px) con zoom artistico.
        // Rango de zoom calibrado para maxima estetica y rendimiento estable.
        Camera2D camera = {};
        float viewPaddingPx = 140.0f;
        float cellWidthPx = 5.5f;
        float cellHeightPx = 9.5f;
        float minZoom = 0.95f;
        float maxZoom = 2.5f;

        ZBufferStyle   zBufferStyle;
        EffectLodState effectLod;


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

        // Calcula el perfil de LOD de efectos segun que tan alejada esta la camara.
        void UpdateEffectLod() {
            // Umbral de zoom: por encima de 1.35x -> 100% calidad; al bajar hacia minZoom (0.95x) -> escala suave.
            constexpr float lodFullZoom = 1.35f;
            float t = MathIso::Clamp((camera.zoom - minZoom) / (lodFullZoom - minZoom), 0.0f, 1.0f);

            effectLod.densityScale = MathIso::Lerp(0.42f, 1.0f, t);
            effectLod.windEffectRadiusSq = MathIso::Lerp(22.0f * 22.0f, 42.0f * 42.0f, t);
            effectLod.foliageRadiusWorld = MathIso::Lerp(16.0f, 30.0f, t);
            effectLod.particleRenderStride = (camera.zoom < 1.08f) ? 2 : 1;
            effectLod.distantAtomStride = (camera.zoom < 1.12f) ? 2 : 1;
            effectLod.allowReflections = (camera.zoom >= 1.02f);
        }

        Vec2 IsoToScreen(Vec2 isoPt) const {
            return {
                (isoPt.x - camera.target.x) * camera.zoom + camera.offset.x,
                (isoPt.y - camera.target.y) * camera.zoom + camera.offset.y
            };
        }

        Vec2 ScreenToIso(Vec2 screenPt) const {
            float invZoom = 1.0f / camera.zoom;
            return {
                (screenPt.x - camera.offset.x) * invZoom + camera.target.x,
                (screenPt.y - camera.offset.y) * invZoom + camera.target.y
            };
        }

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

        // Proyecta un atomo reflejado en agua (invirtiendo Z + distorsion de onda) si cae en una WaterZone.
        void TrySubmitWaterReflection(Vec3 worldPt, const ShadedAtom& shaded,
            float zoom, float timeSec) {
            if (!effectLod.allowReflections || worldPt.z <= 0.12f) return;

            Vec2 refIso = MathIso::WorldToIso({ worldPt.x, worldPt.y, -worldPt.z * 0.80f }, isoConfig);
            float waveRipple = std::sin(refIso.y * 0.22f + timeSec * 3.8f + worldPt.x * 0.6f) * 2.4f;
            refIso.x += waveRipple;

            Vec2 refGround = MathIso::IsoToWorldGround(refIso, isoConfig);
            // Si hay un puente encima del agua en ese punto, el puente oculta el reflejo.
            if (const SurfaceZone* sz = world.FindSurfaceZoneAt(refGround.x, refGround.y)) {
                if (sz->walkableOverWater) return;
            }
            const WaterZone* wz = world.FindWaterZoneAt(refGround.x, refGround.y);
            if (!wz) return;

            ColorRGBA refCol = MathIso::LerpColor(shaded.color, wz->deepColor, 0.58f);
            refCol.r = static_cast<uint8_t>(refCol.r * 0.82f);
            refCol.g = static_cast<uint8_t>(refCol.g * 0.85f);
            refCol.b = static_cast<uint8_t>(refCol.b * 0.92f);

            float depthRef = MathIso::ComputeDepthKey({ refGround.x, refGround.y, 0.0f })
                - 0.10f
                + MathIso::Clamp((worldPt.x + worldPt.y) * 0.00005f, -0.015f, 0.015f);

            zBuffer.SubmitAtom(refIso.x * zoom, refIso.y * zoom, depthRef,
                shaded.glyph, refCol, FALLBACK_MATERIAL_ID, false, 0, true);
        }

        // Simula matas organicas de pasto multi-atomo y flores usando ValueNoise2D (clumping).
        // Respeta GroundMask en O(1): cero computo debajo de rocas, arboles, agua o puentes.
        void SubmitGroundFoliageToZBuffer(float minWX, float maxWX, float minWY, float maxWY,
            float zoom, float camTopLeftX, float camTopLeftY,
            uint32_t& atomsEvaluated, uint32_t& outFoliageDrawn) {
            const BiomeDef* biome = weatherSystem.GetActiveBiome();
            if (!biome || biome->groundFoliage.empty()) return;

            const float rLod = effectLod.foliageRadiusWorld;
            const float rLodSq = rLod * rLod;
            const float limit = world.GetWorldHalfSize() - 1.0f;

            // Celdas de cumulo mas amplias (1.45u): dentro de cada manchon sembramos 2-4 briznas juntas.
            const FoliageDistributionConfig& distCfg = biome->foliageDistribution;
            const float clumpStep = distCfg.clumpStep;
            const float invClumpStep = 1.0f / clumpStep;
            const float patchScale = distCfg.patchScale;
            const float patchSep = distCfg.patchSeparation;
            const float densityScale = effectLod.densityScale * distCfg.globalDensity;

            int gx0 = static_cast<int>(std::floor(MathIso::Clamp(std::max(minWX, player.position.x - rLod), -limit, limit) * invClumpStep));
            int gx1 = static_cast<int>(std::ceil(MathIso::Clamp(std::min(maxWX, player.position.x + rLod), -limit, limit) * invClumpStep));
            int gy0 = static_cast<int>(std::floor(MathIso::Clamp(std::max(minWY, player.position.y - rLod), -limit, limit) * invClumpStep));
            int gy1 = static_cast<int>(std::ceil(MathIso::Clamp(std::min(maxWY, player.position.y + rLod), -limit, limit) * invClumpStep));

            const float screenW = static_cast<float>(screenWidth);
            const float screenH = static_cast<float>(screenHeight);
            const bool  drawFullStem = (zoom >= 1.15f);

            constexpr float interactR = 1.75f;
            constexpr float interactRSq = interactR * interactR;

            for (int gy = gy0; gy <= gy1; ++gy) {
                for (int gx = gx0; gx <= gx1; ++gx) {
                    const float fgx = static_cast<float>(gx);
                    const float fgy = static_cast<float>(gy);

                    float cx = (fgx + 0.5f) * clumpStep;
                    float cy = (fgy + 0.5f) * clumpStep;

                    // 1. Culling radial por LOD.
                    float cdx = cx - player.position.x;
                    float cdy = cy - player.position.y;
                    if ((cdx * cdx + cdy * cdy) > (rLod + 1.5f) * (rLod + 1.5f)) continue;

                    // 2. Separacion y escala de praderas controlada por JSON del bioma.
                    float patchNoise = MathIso::ValueNoise2D(cx * patchScale, cy * patchScale, 31.7f);
                    if (patchNoise < patchSep) continue;

                    // Pre-check rapido de pantalla para la mata completa.
                    Vec2 clumpIso = MathIso::WorldToIso({ cx, cy, 0.0f }, isoConfig);
                    float csx = clumpIso.x * zoom - camTopLeftX;
                    float csy = clumpIso.y * zoom - camTopLeftY;
                    if (csx < -cellWidthPx * 4.0f || csx >= screenW + cellWidthPx * 4.0f ||
                        csy < -cellHeightPx * 4.0f || csy >= screenH + cellHeightPx * 3.0f) {
                        continue;
                    }

                    // 3. Seleccion determinista de especie dominante para esta mata (escala con patchNoise).
                    float patchBoost = MathIso::Clamp((patchNoise - patchSep) / std::max(1.0f - patchSep, 0.05f), 0.2f, 1.0f);
                    float roll = MathIso::Hash3D(fgx, fgy, 3.0f, 19.3f);
                    float accum = 0.0f;
                    const GroundFoliageDef* chosen = nullptr;

                    for (const GroundFoliageDef& def : biome->groundFoliage) {
                        // Multiplicamos x2.2 porque cada mata cubre un area 2.9x mayor que la grilla anterior.
                        accum += def.spawnChance * 2.2f * patchBoost * densityScale;
                        if (roll < accum) {
                            chosen = &def;
                            break;
                        }
                    }
                    if (!chosen) continue;

                    // Cantidad de briznas en la mata: 2 a 4 para pasto, 1 a 2 para flores.
                    int maxBlades = chosen->isTallBlade ? (2 + static_cast<int>(patchBoost * 2.5f)) : 2;
                    int span = std::max(0, chosen->maxBlades - chosen->minBlades);
                    int targetCnt = chosen->minBlades + static_cast<int>(std::round(patchBoost * static_cast<float>(span)));
                    int bladeCount = std::max(1, static_cast<int>(std::round(static_cast<float>(targetCnt) * effectLod.densityScale)));

                    for (int b = 0; b < bladeCount; ++b) {
                        float fb = static_cast<float>(b);
                        float ox = (MathIso::Hash3D(fgx, fgy, fb + 1.0f, 4.3f) - 0.5f) * chosen->clumpSpread;
                        float oy = (MathIso::Hash3D(fgx, fgy, fb + 7.0f, 9.1f) - 0.5f) * chosen->clumpSpread;
                        float bx = cx + ox;
                        float by = cy + oy;

                        // 4. O(1) GroundMask por brizna: nunca crece sobre rocas, troncos, agua ni puentes.
                        if (!world.CanSpawnGroundFoliageAt(bx, by)) continue;

                        float bdx = bx - player.position.x;
                        float bdy = by - player.position.y;
                        float distPlayerSq = bdx * bdx + bdy * bdy;
                        if (distPlayerSq > rLodSq) continue;

                        // Variacion individual de altura por brizna para romper cualquier uniformidad.
                        float hVar = 0.72f + MathIso::Hash3D(fgx, fgy, fb + 13.0f, 2.9f) * 0.55f;
                        float bladeH = chosen->heightZ * hVar;

                        // 5. Viento continuo + empuje radial suave al caminar el jugador encima.
                        Vec2 bend = weatherSystem.SampleWindOffset(bx, by, 1.0f, chosen->windWeight * 1.55f);
                        float flattenZ = 1.0f;

                        if (chosen->interactive && distPlayerSq < interactRSq) {
                            float dist = std::sqrt(distPlayerSq);
                            float push = 1.0f - (dist / interactR);
                            float invD = (dist > 0.01f) ? (1.0f / dist) : 1.0f;
                            bend.x += (bdx * invD) * push * 0.85f;
                            bend.y += (bdy * invD) * push * 0.85f;
                            flattenZ = MathIso::Lerp(1.0f, 0.38f, push);
                        }

                        float effH = bladeH * flattenZ;
                        float isoBend = bend.x - bend.y;

                        // Caracteres direccionales segun inclinacion horizontal en isometria.
                        char midGlyph = (isoBend > 0.24f) ? '/' : ((isoBend < -0.24f) ? '\\' : '|');
                        char tipGlyph = '|';
                        if (chosen->isTallBlade) {
                            if (isoBend > 0.12f) tipGlyph = '/';
                            else if (isoBend < -0.12f) tipGlyph = '\\';
                            else                       tipGlyph = '|';
                        }
                        else {
                            float gh = MathIso::Hash3D(fgx, fgy, fb + 21.0f, 11.1f);
                            size_t gIdx = std::min<size_t>(static_cast<size_t>(gh * chosen->glyphs.size()), chosen->glyphs.size() - 1);
                            tipGlyph = chosen->glyphs[gIdx];
                        }

                        // Iluminamos una sola vez por brizna y derivamos gradiente oscuro -> brillante.
                        Vec3 tipPos = { bx + bend.x * 0.55f, by + bend.y * 0.55f, effH };
                        ColorRGBA litBase = lightingSystem.ShadeFoliageColor({ bx, by, 0.15f }, chosen->baseColor, true);
                        ColorRGBA litTip = lightingSystem.ShadeFoliageColor(tipPos, chosen->tipColor, true);

                        // Atomo 1: Base anclada al suelo (solo si no estamos muy alejados).
                        if (drawFullStem) {
                            Vec3 basePos = { bx, by, 0.06f };
                            Vec2 baseIso = MathIso::WorldToIso(basePos, isoConfig);
                            zBuffer.SubmitAtom(baseIso.x * zoom, baseIso.y * zoom,
                                MathIso::ComputeDepthKey(basePos),
                                ',', litBase, FALLBACK_MATERIAL_ID, false, 0, true);
                            ++atomsEvaluated;
                        }

                        // Atomo 2: Tallo intermedio (curvatura cuadratica suave 0.25x).
                        if (drawFullStem && effH > 0.42f) {
                            Vec3 midPos = { bx + bend.x * 0.22f, by + bend.y * 0.22f, effH * 0.52f };
                            Vec2 midIso = MathIso::WorldToIso(midPos, isoConfig);
                            ColorRGBA midCol = chosen->isTallBlade
                                ? MathIso::LerpColor(litBase, litTip, 0.45f)
                                : litBase;
                            zBuffer.SubmitAtom(midIso.x * zoom, midIso.y * zoom,
                                MathIso::ComputeDepthKey(midPos),
                                midGlyph, midCol, FALLBACK_MATERIAL_ID, false, 0, true);
                            ++atomsEvaluated;
                        }

                        // Atomo 3: Punta del tallo o petalo de la flor.
                        Vec2 tipIso = MathIso::WorldToIso(tipPos, isoConfig);
                        zBuffer.SubmitAtom(tipIso.x * zoom, tipIso.y * zoom,
                            MathIso::ComputeDepthKey(tipPos),
                            tipGlyph, litTip, FALLBACK_MATERIAL_ID, false, 0, true);
                        ++atomsEvaluated;
                        ++outFoliageDrawn;
                    }
                }
            }
        }

        // Rellena unicamente las celdas vacias del Z-Buffer con terreno ASCII, agua animada y sombras 2.5D y 
        // SurfaceZone (puentes/pisos) -> WaterZone -> Suelo Bioma.
        void SubmitGroundPlaneToZBuffer(float zoom, uint32_t& atomsEvaluated) {
            const int     gridW = zBuffer.GetGridWidth();
            const int     gridH = zBuffer.GetGridHeight();
            const int32_t origX = zBuffer.GetOriginCellX();
            const int32_t origY = zBuffer.GetOriginCellY();
            const float   cellW = zBuffer.GetCellWidth();
            const float   cellH = zBuffer.GetCellHeight();
            const float   invZoom = 1.0f / zoom;
            const float   limit = world.GetWorldHalfSize();
            const float   windTime = weatherSystem.GetWind().time;

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
                    Vec3 groundPt3D = { groundW.x, groundW.y, 0.0f };
                    const float wcx = static_cast<float>(origX + cx);
                    const float wcy = static_cast<float>(origY + cy);
                    float noise = MathIso::Hash3D(wcx, wcy, 0.0f, 5.41f);

                    ShadedAtom shaded;
                    int8_t subX = 0;
                    int8_t subY = 0;

                    // Prioridad 1: Objetos de suelo transitables (mantiene alineacion arquitectonica recta).
                    if (const SurfaceZone* sz = world.FindSurfaceZoneAt(groundW.x, groundW.y)) {
                        shaded = lightingSystem.ShadeSurfacePoint({ groundW.x, groundW.y, sz->elevationZ }, *sz, noise, true);
                    }
                    // Prioridad 2: Agua animada con vaiven horizontal sub-pixel.
                    else if (const WaterZone* wz = world.FindWaterZoneAt(groundW.x, groundW.y)) {
                        float df = wz->ComputeDepthFactor(groundW.x, groundW.y);
                        shaded = lightingSystem.ShadeWaterPoint(groundPt3D, wz->deepColor, wz->shallowColor,
                            df, wz->waveSpeed, true);
                        subX = static_cast<int8_t>(std::round(std::sin(groundW.y * 1.4f + windTime * wz->waveSpeed) * 2.0f));
                        subY = static_cast<int8_t>(std::round((noise - 0.5f) * 3.0f));
                    }
                    // Prioridad 3: Suelo natural del bioma con offset determinista para romper la cuadricula.
                    else {
                        shaded = lightingSystem.ShadeGroundPoint(groundPt3D, noise, true);
                        if (shaded.glyph != '\0') {
                            float jx = MathIso::Hash3D(wcx, wcy, 1.0f, 8.23f) - 0.5f;
                            float jy = MathIso::Hash3D(wcx, wcy, 2.0f, 14.7f) - 0.5f;
                            // Las sombras llevan menos jitter (-1..+1) para mantenerse compactas; el terreno libre mas.
                            bool isShadow = (shaded.glyph == ':' || shaded.glyph == ';');
                            subX = static_cast<int8_t>(std::round(jx * (isShadow ? 2.0f : 5.0f)));
                            subY = static_cast<int8_t>(std::round(jy * (isShadow ? 2.0f : 7.0f)));
                        }
                    }

                    if (shaded.glyph != '\0') {
                        float depth = MathIso::ComputeDepthKey(groundPt3D) - 0.15f;
                        zBuffer.SetGroundCell(cx, cy, depth, shaded.glyph, shaded.color, subX, subY);
                    }
                }
            }
        }

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

                zBuffer.SubmitAtom(zoomedX, zoomedY, MathIso::ComputeDepthKey(pl.position) + 0.05f,
                    '*', pl.color, FALLBACK_MATERIAL_ID, false, 0, false);
            }
        }

        void SubmitPlayerAvatarToZBuffer(int splatRadius, uint32_t& atomsEvaluated, uint32_t& atomsCulled) {
            const float step = 0.22f;
            const float bodyR = 0.55f;
            const float bodyH = 1.85f;
            const float zoom = camera.zoom;
            const float timeSec = weatherSystem.GetWind().time;
            const bool  nearWater = world.IsNearAnyWater(player.position.x, player.position.y, 4.5f);

            int sampleCounter = 0;

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
                        if (nearWater && ((sampleCounter++ & 1) == 0)) {
                            TrySubmitWaterReflection(worldPt, s, zoom, timeSec);
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
                        if (nearWater && ((sampleCounter++ & 1) == 0)) {
                            TrySubmitWaterReflection(worldPt, s, zoom, timeSec);
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

        // Overlays visuales (F4)
        void RenderDebugOverlays() const {
            if (!debugHUD.AreOverlaysVisible()) return;

            // 1. Chunks activos (cian).
            const float cs = spatialGrid.GetChunkSize();
            for (const ChunkCoord& cc : activeChunks) {
                float x0 = static_cast<float>(cc.cx) * cs;
                float y0 = static_cast<float>(cc.cy) * cs;
                DrawIsoOBB({ x0 + cs * 0.5f, y0 + cs * 0.5f }, { cs * 0.5f, cs * 0.5f }, 1.0f, 0.0f, { 0, 220, 240, 100 });
            }

            // 2. Zonas de agua (azul brillante).
            for (const WaterZone& wz : world.GetWaterZones()) {
                Vec2 center = { (wz.minX + wz.maxX) * 0.5f, (wz.minY + wz.maxY) * 0.5f };
                Vec2 half = { (wz.maxX - wz.minX) * 0.5f, (wz.maxY - wz.minY) * 0.5f };
                DrawIsoOBB(center, half, 1.0f, 0.0f, { 70, 180, 255, 220 });
            }

            // 2.5 Superficies transitables (puentes / pisos) en blanco calido.
            for (const SurfaceZone& sz : world.GetSurfaceZones()) {
                Vec2 center = { (sz.minX + sz.maxX) * 0.5f, (sz.minY + sz.maxY) * 0.5f };
                Vec2 half = { (sz.maxX - sz.minX) * 0.5f, (sz.maxY - sz.minY) * 0.5f };
                DrawIsoOBB(center, half, 1.0f, 0.0f, { 245, 215, 140, 220 });
            }

            // 3. Volumenes de colision visibles (verde).
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

            // 4. Radios de luces puntuales activas (naranja).
            for (const ActiveFrameLight& al : lightingSystem.GetActiveLights()) {
                DrawIsoCircle({ al.position.x, al.position.y }, 1.0f / al.invRadius, { 255, 165, 60, 120 });
            }

            // 5. Jugador + vector solar (amarillo/azul) + vector de viento (magenta).
            DrawIsoCircle(player.position, player.collisionRadius, GOLD);

            const SunState& sun = lightingSystem.GetSunState();
            Vec2 pStart = IsoToScreen(MathIso::WorldToIso({ player.position.x, player.position.y, 2.5f }, isoConfig));
            Vec2 pEnd = IsoToScreen(MathIso::WorldToIso({
                player.position.x + sun.lightDir.x * 5.0f,
                player.position.y + sun.lightDir.y * 5.0f,
                2.5f + sun.lightDir.z * 5.0f
                }, isoConfig));
            DrawLineEx({ pStart.x, pStart.y }, { pEnd.x, pEnd.y }, 2.0f, sun.isNight ? SKYBLUE : YELLOW);

            const WindState& wind = weatherSystem.GetWind();
            Vec2 wEnd = IsoToScreen(MathIso::WorldToIso({
                player.position.x + wind.dirX * (1.2f + wind.speed * 0.6f),
                player.position.y + wind.dirY * (1.2f + wind.speed * 0.6f),
                2.5f
                }, isoConfig));
            DrawLineEx({ pStart.x, pStart.y }, { wEnd.x, wEnd.y }, 2.0f, MAGENTA);
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
            m_impl->camera.zoom = 1.35f;
        }
        return m_impl->initialized;
    }

    bool Context::LoadRegistry(const std::string& dataRoot) {
        m_impl->dataRootPath = dataRoot;
        const std::filesystem::path root(dataRoot);

        bool matOk = m_impl->materialSystem.LoadFromFile((root / "materials" / "materials.json").string());
        bool objOk = m_impl->world.LoadObjectsDirectory((root / "objects").string(), m_impl->materialSystem);
        m_impl->weatherSystem.LoadBiomesDirectory((root / "biomes").string());
        m_impl->weatherSystem.LoadWeathersDirectory((root / "weather").string());
        m_impl->particleSystem.LoadTemplatesDirectory((root / "particles").string());

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

            const WorldWindConfig& wc = m_impl->world.GetWindConfig();
            m_impl->weatherSystem.Configure(
                m_impl->world.GetBiomeId(),
                m_impl->world.GetInitialWeatherId(),
                wc.directionDeg,
                wc.baseSpeed,
                wc.gustStrength,
                m_impl->world.GetRandomWeather()
            );

            m_impl->lightingSystem.Configure(
                m_impl->world.GetDayNightConfig(),
                m_impl->world.GetGIConfig(),
                m_impl->world.GetMapLights()
            );
            m_impl->lightingSystem.BuildShadowHeightfield(m_impl->world, m_impl->materialSystem, 0.6f);

            m_impl->particleSystem.ClearWorldState();
            for (const PointLight& pl : m_impl->world.GetMapLights()) {
                if (pl.flicker && pl.color.r >= 200) {
                    m_impl->particleSystem.AddWorldEmitter(pl.position, { 0.35f, 0.35f, 0.25f }, "fire_sparks", 14.0f, false);
                }
            }

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

        // 1. Entrada WASD traducida de pantalla a espacio del mundo.
        Vec2 screenInput = { 0.0f, 0.0f };
        if (IsKeyDown(KEY_W)) screenInput.y -= 1.0f;
        if (IsKeyDown(KEY_S)) screenInput.y += 1.0f;
        if (IsKeyDown(KEY_A)) screenInput.x -= 1.0f;
        if (IsKeyDown(KEY_D)) screenInput.x += 1.0f;

        Vec2 desiredWorldDir = MathIso::IsoToWorldGround(screenInput, m_impl->isoConfig);
        m_impl->player.moveSpeed = IsKeyDown(KEY_LEFT_SHIFT) ? 24.0f : 14.0f;

        // La nieve reduce ligeramente la friccion del suelo (fisica reactiva al clima).
        m_impl->player.friction = MathIso::Lerp(10.0f, 4.5f, m_impl->weatherSystem.GetModifiers().snowCoverage);

        // 2. Fisica semi-implicita y colisiones.
        CollisionSolver::UpdateActorPhysics(
            m_impl->player,
            desiredWorldDir,
            dt,
            m_impl->world,
            m_impl->spatialGrid,
            m_impl->physicsQueryIds,
            m_impl->physicsQueryChunks
        );

        // 3. Seguimiento suave de camara + Zoom acotado.
        Vec2 targetIso = MathIso::WorldToIso({ m_impl->player.position.x, m_impl->player.position.y, 1.2f }, m_impl->isoConfig);
        float camSmooth = MathIso::Clamp(9.0f * dt, 0.0f, 1.0f);
        m_impl->camera.target.x = MathIso::Lerp(m_impl->camera.target.x, targetIso.x, camSmooth);
        m_impl->camera.target.y = MathIso::Lerp(m_impl->camera.target.y, targetIso.y, camSmooth);

        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            m_impl->camera.zoom = MathIso::Clamp(m_impl->camera.zoom + wheel * 0.08f, m_impl->minZoom, m_impl->maxZoom);
        }

        // 4. Controles de ciclo Dia-Noche, Clima (1/2/3), Bioma (M) y Hot-Reloading (F5).
        // Actualizamos el perfil de LOD de efectos segun el zoom actual.
        m_impl->UpdateEffectLod();

        if (IsKeyPressed(KEY_SPACE)) m_impl->lightingSystem.ToggleCycleRunning();
        if (IsKeyDown(KEY_LEFT))     m_impl->lightingSystem.AdjustTimeHours(-dt * 4.5f);
        if (IsKeyDown(KEY_RIGHT))    m_impl->lightingSystem.AdjustTimeHours(dt * 4.5f);
        if (IsKeyPressed(KEY_H))     m_impl->lightingSystem.ToggleShadows();
        if (IsKeyPressed(KEY_T))     m_impl->playerTorchOn = !m_impl->playerTorchOn;
        if (IsKeyPressed(KEY_O))     m_impl->zBufferStyle.enableOutlines = !m_impl->zBufferStyle.enableOutlines;
        if (IsKeyPressed(KEY_B))     m_impl->zBufferStyle.enableAsciiBorder = !m_impl->zBufferStyle.enableAsciiBorder;
        if (IsKeyPressed(KEY_G))     m_impl->zBufferStyle.enableSubPixelOffset = !m_impl->zBufferStyle.enableSubPixelOffset;
        if (IsKeyPressed(KEY_V))     m_impl->zBufferStyle.enableCellBacking = !m_impl->zBufferStyle.enableCellBacking;
        if (IsKeyPressed(KEY_ONE))   m_impl->weatherSystem.TransitionToWeather("clear_day");
        if (IsKeyPressed(KEY_TWO))   m_impl->weatherSystem.TransitionToWeather("heavy_rain");
        if (IsKeyPressed(KEY_THREE)) m_impl->weatherSystem.TransitionToWeather("blizzard");
        if (IsKeyPressed(KEY_M))     m_impl->weatherSystem.CycleBiome();
        if (IsKeyPressed(KEY_R))     m_impl->weatherSystem.ToggleRandomTransitions();
        if (IsKeyPressed(KEY_F3))    m_impl->debugHUD.ToggleTelemetry();
        if (IsKeyPressed(KEY_F4))    m_impl->debugHUD.ToggleOverlays();
        if (IsKeyPressed(KEY_F5)) {
            Vec2  savedPos = m_impl->player.position;
            float savedHr = m_impl->lightingSystem.GetTimeHours();
            LoadRegistry(m_impl->dataRootPath);
            if (!m_impl->activeMapPath.empty()) {
                LoadMap(m_impl->activeMapPath);
                m_impl->player.position = savedPos;
                m_impl->lightingSystem.AdjustTimeHours(savedHr - m_impl->lightingSystem.GetTimeHours());
            }
        }

        // 5. Avanzar Clima/Viento -> Iluminacion -> Particulas SoA.
        m_impl->weatherSystem.Update(dt);
        m_impl->lightingSystem.Update(dt, m_impl->weatherSystem.GetModifiers());

        // Escalamos la tasa de spawn climatico con effectLod.densityScale para mantener 60 FPS al alejar.
        const SunState& sun = m_impl->lightingSystem.GetSunState();
        m_impl->particleSystem.Update(
            dt,
            m_impl->weatherSystem.GetWind(),
            sun.isNight,
            m_impl->player.position,
            m_impl->weatherSystem.GetWeatherParticleId(),
            m_impl->weatherSystem.GetWeatherParticleRate() * m_impl->effectLod.densityScale
        );

        auto simEnd = Clock::now();

        // Actualizar telemetria del frame.
        auto& t = m_impl->telemetry;
        t.simTimeMs = ElapsedMs(simStart, simEnd);
        t.fps = static_cast<float>(GetFPS());
        t.frameTimeMs = dt * 1000.0f;
        t.cameraWorldPos = m_impl->player.position;
        t.cameraZoom = m_impl->camera.zoom;
        t.outlinesActive = m_impl->zBufferStyle.enableOutlines;
        t.timeOfDayHours = m_impl->lightingSystem.GetTimeHours();
        t.isNight = sun.isNight;
        t.dayCycleRunning = m_impl->lightingSystem.IsCycleRunning();
        t.shadowsEnabled = m_impl->lightingSystem.AreShadowsEnabled();
        t.torchEnabled = m_impl->playerTorchOn;
        t.totalMapLights = static_cast<uint32_t>(m_impl->lightingSystem.GetMapLights().size());
        t.biomeName = m_impl->weatherSystem.GetActiveBiomeName();
        t.weatherName = m_impl->weatherSystem.GetActiveWeatherName();
        t.randomWeatherOn = m_impl->weatherSystem.AreRandomTransitionsOn();
        t.nextWeatherSec = m_impl->weatherSystem.GetNextTransitionSec();
        t.windSpeed = m_impl->weatherSystem.GetWind().speed;
        t.snowCoverage = m_impl->weatherSystem.GetModifiers().snowCoverage;
        t.effectLodScale = m_impl->effectLod.densityScale;
        t.activeParticles = m_impl->particleSystem.GetActiveCount();
        t.maxParticles = m_impl->particleSystem.GetCapacity();
        t.particlePoolBytes = m_impl->particleSystem.GetMemoryBytes();
        t.groundMaskBytes = m_impl->world.GetGroundMaskBytes();
        m_impl->debugHUD.UpdateMetrics(t, dt);
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
        // ETAPA 6: OBJETOS (VIENTO + REFLEJOS) + PARTICULAS + TERRENO/AGUA EN Z-BUFFER
        // =========================================================================
        auto zbufStart = Clock::now();

        const float zoom = m_impl->camera.zoom;
        const float camTopLeftX = m_impl->camera.target.x * zoom - m_impl->camera.offset.x;
        const float camTopLeftY = m_impl->camera.target.y * zoom - m_impl->camera.offset.y;
        const float screenW = static_cast<float>(m_impl->screenWidth);
        const float screenH = static_cast<float>(m_impl->screenHeight);
        const float windTime = m_impl->weatherSystem.GetWind().time;

        m_impl->zBuffer.BeginFrame(camTopLeftX, camTopLeftY);

        const Rect2D viewportRect = {
            -m_impl->viewPaddingPx,
            -m_impl->viewPaddingPx,
            screenW + m_impl->viewPaddingPx * 2.0f,
            screenH + m_impl->viewPaddingPx * 2.0f
        };

        // Splat adaptativo en 3 niveles para mantener volumenes cerrados de 0.95x hasta 2.5x.
        const int splatRadius = (zoom > 1.85f) ? 2 : ((zoom > 1.18f) ? 1 : 0);

        uint32_t instancesRendered = 0;
        uint32_t atomsEvaluated = 0;
        uint32_t atomsCulledScreen = 0;
        uint32_t groundFoliageDrawn = 0;

        // 1. Destellos de fuentes de luz del mapa.
        m_impl->SubmitLightMarkersToZBuffer(zoom, camTopLeftX, camTopLeftY);

        // 2. Avatar del jugador (con reflejo en agua si camina cerca de la orilla).
        m_impl->SubmitPlayerAvatarToZBuffer(splatRadius, atomsEvaluated, atomsCulledScreen);

        // 3. Instancias visibles del mundo (Viento + Reflejos en Agua + GI + Sombras 2.5D)
        // con LOD radial de viento, stride y reflejos.
        const auto& instances = m_impl->world.GetInstances();
        const float windRadSq = m_impl->effectLod.windEffectRadiusSq;

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

            // LOD radial por distancia al jugador: apaga viento/reflejos y activa stride en periferias lejanas>

            // Solo evaluamos reflejos de agua para objetos cercanos a una WaterZone.            
            float odx = inst.position.x - m_impl->player.position.x;
            float ody = inst.position.y - m_impl->player.position.y;
            float distSq = odx * odx + ody * ody;
            const bool   inEffectRadius = (distSq <= windRadSq);
            const size_t atomStep = (!inEffectRadius && m_impl->effectLod.distantAtomStride > 1) ? 2 : 1;

            const float invBoundH = 1.0f / std::max(prefab->boundHeightZ * inst.scale.z, 0.5f);
            const float reflectReach = prefab->boundRadiusXY * maxScaleXY + prefab->boundHeightZ * inst.scale.z * 1.4f;
            const bool  nearWater = m_impl->effectLod.allowReflections && inEffectRadius &&
                m_impl->world.IsNearAnyWater(inst.position.x, inst.position.y, reflectReach);

            const BakedAtom* atoms = prefab->atoms.data();
            const size_t atomCount = prefab->atoms.size();

            for (size_t i = 0; i < atomCount; i += atomStep) {
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

                Vec3 worldNorm = {
                    a.localNormal.x * inst.cosRot - a.localNormal.y * inst.sinRot,
                    a.localNormal.x * inst.sinRot + a.localNormal.y * inst.cosRot,
                    a.localNormal.z
                };

                // Aplicacion del campo de viento en follaje y ramas flexibles.
                // Viento activo solo dentro del radio LOD (ahorra sin/cos en arboles distantes).
                if (inEffectRadius && a.windWeightByte > 0) {
                    float windW = static_cast<float>(a.windWeightByte) * (1.0f / 255.0f);
                    float heightNorm = MathIso::Clamp(sz * invBoundH, 0.0f, 1.0f);
                    Vec2  sway = m_impl->weatherSystem.SampleWindOffset(worldPt.x, worldPt.y, heightNorm, windW);

                    worldPt.x += sway.x;
                    worldPt.y += sway.y;

                    // Perturbamos ligeramente la normal para dar brillo vivo al moverse las hojas.
                    worldNorm = MathIso::Normalize({
                        worldNorm.x + sway.x * 0.45f,
                        worldNorm.y + sway.y * 0.45f,
                        worldNorm.z
                        });
                }

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

                // Proyectamos reflejo en agua (submuestreo 50% en arboles para ultra rendimiento).
                if (nearWater && ((i & 1) == 0)) {
                    m_impl->TrySubmitWaterReflection(worldPt, shaded, zoom, windTime);
                }
            }
        }

        // 4. Pasto interactivo y flores del bioma (saltando en O(1) objetos, agua y pisos).
        m_impl->SubmitGroundFoliageToZBuffer(minWX, maxWX, minWY, maxWY, zoom, camTopLeftX, camTopLeftY,
            atomsEvaluated, groundFoliageDrawn);

        // 5. Particulas activas del pool SoA con stride de LOD.
        m_impl->particleSystem.SubmitToZBuffer(
            m_impl->zBuffer, m_impl->isoConfig, zoom, camTopLeftX, camTopLeftY,
            screenW, screenH, m_impl->effectLod.particleRenderStride
        );

        // 6. Terreno modular (SurfaceZone -> WaterZone -> Suelo Bioma) y sombras 2.5D.
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
        t.groundFoliageDrawn = groundFoliageDrawn;
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