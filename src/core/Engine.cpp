// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file Engine.cpp
 * @brief Orquestador central del motor AsciiEngine (implementacion completa del PIMPL Context::Impl).
 *
 * @details Coordina las 8 etapas del pipeline por frame garantizando cero asignaciones en el heap
 * dentro del bucle principal:
 *   [Update] Etapa 1: Entrada (Juego o Editor) | Etapa 2: Fisica + Colisiones + IA |
 *            Etapa 3: LOD + Clima/Viento | Etapa 4: Ciclo Dia-Noche + Particulas SoA.
 *   [Render] Etapa 5: Culling Capa 1 (SpatialHashGrid) + Culling de Luces |
 *            Etapa 6: Capa 2 (Frustum 2D) + Iluminacion/Sombras + AsciiZBuffer World-Locked O(N) |
 *            Etapa 7: Batch Draw GPU en 3 pases | Etapa 8: Overlays (F4), DebugHUD (F3) y Editor (F1).
 */
#include <AsciiEngine/Engine.hpp>
#include <AsciiEngine/MathIso.hpp>
#include <AsciiEngine/MaterialSystem.hpp>
#include <AsciiEngine/AsciiZBuffer.hpp>
#include <AsciiEngine/CullingSystem.hpp>
#include <AsciiEngine/LightingSystem.hpp>
#include <AsciiEngine/WeatherSystem.hpp>
#include <AsciiEngine/ParticleSystem.hpp>
#include <AsciiEngine/CharacterSystem.hpp>
#include <AsciiEngine/World.hpp>
#include <AsciiEngine/DebugHUD.hpp>
#include <AsciiEngine/Editor.hpp>
#include "raylib.h" // Dependencia privada: encapsulada por completo detras de Engine.hpp.

#include <algorithm>
#include <array>
#include <chrono>     // std::chrono::steady_clock para perfilar cada etapa en milisegundos.
#include <cmath>
#include <filesystem> // Construccion portable de rutas dentro de dataRoot.
#include <vector>

namespace AsciiEngine {

    namespace {
        using Clock = std::chrono::steady_clock;

        /**
         * @brief Calcula el tiempo transcurrido en milisegundos con precision sub-milisegundo para el HUD F3.
         */
        float ElapsedMs(Clock::time_point start, Clock::time_point end) {
            return std::chrono::duration<float, std::milli>(end - start).count();
        }
    } // namespace anonimo

    /**
     * @brief Estado interno completo del motor oculto detras del patron PIMPL.
     */
    struct Context::Impl {
        bool initialized = false;
        int  screenWidth = 1600;
        int  screenHeight = 900;

        // --- Tipografia y Lookup Table (LUT) O(1) de coordenadas UV en el atlas de fuente ---
        Font                       renderFont = {};
        bool                       customFontLoaded = false;
        float                      glyphAspectRatio = 0.58f; // Relacion ancho/alto de respaldo si un glifo no reporta ancho.
        std::array<Rectangle, 128> glyphLUT = {};    // Indexado por codigo ASCII [32..126] -> evita GetGlyphIndex() por celda.

        // Rutas activas recordadas para soportar Hot-Reloading (F5) y guardado directo (Ctrl+S).
        std::string dataRootPath = "assets/data";
        std::string activeMapPath;

        // --- Subsistemas modulares del motor ---
        MaterialSystem     materialSystem;
        MathIso::IsoConfig isoConfig;
        AsciiZBuffer       zBuffer;
        SpatialHashGrid    spatialGrid{ 24.0f };
        LightingSystem     lightingSystem;
        WeatherSystem      weatherSystem;
        ParticleSystem     particleSystem;
        CharacterSystem    characterSystem;
        World              world;
        DebugHUD           debugHUD;
        Editor             editor;

        bool playerTorchOn = true; // Antorcha dinamica acoplada al foco activo (tecla [T]).

        // --- Camara 2D isometrica y rejilla Z-Buffer calibrada (5.5x9.5 px) ---
        Camera2D camera = {};
        float    viewPaddingPx = 140.0f; // Margen fuera de pantalla para que objetos altos/anchos no hagan pop-in.
        float    cellWidthPx = 5.5f;   // Resolucion horizontal por celda ASCII (mas fina = mayor realismo visual).
        float    cellHeightPx = 9.5f;   // Resolucion vertical por celda ASCII.
        float    minZoom = 0.95f;  // Limite inferior que evita sobrecarga cuadratica de chunks visibles.
        float    maxZoom = 2.5f;   // Limite superior soportado sin huecos gracias al splatting 3x3.

        ZBufferStyle   zBufferStyle; // Opciones esteticas alternables en vivo (O, B, G, V).
        EffectLodState effectLod;    // Perfil dinamico de LOD de efectos segun zoom actual.

        EngineSettings settings; // Opciones de configuración global del motor.

        // --- Buffers scratch pre-alojados: garantizan cero malloc/new durante Update() y Render() ---
        std::vector<uint32_t>   visibleInstanceIds;
        std::vector<ChunkCoord> activeChunks;
        std::vector<uint32_t>   physicsQueryIds;
        std::vector<ChunkCoord> physicsQueryChunks;

        FrameTelemetry telemetry;

        /**
         * @brief Devuelve el punto focal del mundo (centro de la camara en Editor, o posicion del personaje controlado).
         * @details Sirve como centro para el LOD radial de viento, pasto interactivo, antorcha y particulas climaticas.
         */
        Vec2 GetFocusWorldPos() const {
            if (editor.IsEnabled()) {
                return MathIso::IsoToWorldGround({ camera.target.x, camera.target.y }, isoConfig);
            }
            if (const CharacterInstance* c = characterSystem.GetControlledCharacter()) {
                return c->actor.position;
            }
            return world.GetPlayerSpawn();
        }

        /**
         * @brief Carga la fuente monoespaciada en GPU con mipmaps trilineales y construye la LUT O(1).
         */
        void LoadTypography() {
            // Puedes anteponer cualquier .ttf propio en assets/fonts/ para distribuirlo junto al ejecutable.
            const char* candidates[] = {
                "assets/fonts/JetBrainsMono-Regular.ttf",
                "assets/fonts/SourceCodePro-Regular.ttf",
                "C:/Windows/Fonts/consola.ttf",
                "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
            };

            customFontLoaded = false;
            for (const char* path : candidates) {
                if (FileExists(path)) {
                    // Horneamos el atlas a 24px (95 caracteres ASCII imprimibles) + mipmaps trilineales
                    // para conservar trazos suaves y limpios al escalar a celdas de 5.5x9.5 px.
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

            // Precalculamos los rectangulos UV de los 95 caracteres ASCII [32..126] una sola vez.
            for (int c = 32; c < 127; ++c) {
                int idx = GetGlyphIndex(renderFont, c);
                glyphLUT[c] = renderFont.recs[idx];
            }
        }

        /**
         * @brief Ajusta progresivamente el nivel de detalle de viento, pasto, particulas y reflejos segun el zoom.
         * @details Por encima de 1.35x corre al 100% de calidad; al alejar hacia minZoom (0.95x) reduce
         *          carga en periferias lejanas para mantener 60 FPS clavados incluso bajo tormenta.
         */
        void UpdateEffectLod() {
            constexpr float lodFullZoom = 1.35f;
            float t = MathIso::Clamp((camera.zoom - minZoom) / (lodFullZoom - minZoom), 0.0f, 1.0f);

            effectLod.densityScale = MathIso::Lerp(0.42f, 1.0f, t);
            effectLod.windEffectRadiusSq = MathIso::Lerp(22.0f * 22.0f, 42.0f * 42.0f, t);
            effectLod.foliageRadiusWorld = MathIso::Lerp(16.0f, 30.0f, t);
            effectLod.particleRenderStride = (camera.zoom < 1.08f) ? 2 : 1;
            effectLod.distantAtomStride = (camera.zoom < 1.12f) ? 2 : 1;
            effectLod.allowReflections = (camera.zoom >= 1.02f);
            effectLod.allowReflections = settings.enableWaterReflections && (camera.zoom >= 1.02f);
        }

        /**
         * @brief Proyecta una coordenada isometrica 2D a pixeles de pantalla aplicando target, zoom y offset de camara.
         */
        Vec2 IsoToScreen(Vec2 isoPt) const {
            return {
                (isoPt.x - camera.target.x) * camera.zoom + camera.offset.x,
                (isoPt.y - camera.target.y) * camera.zoom + camera.offset.y
            };
        }

        /**
         * @brief Transformacion inversa exacta de IsoToScreen (pixeles de pantalla -> espacio isometrico 2D).
         */
        Vec2 ScreenToIso(Vec2 screenPt) const {
            float invZoom = 1.0f / camera.zoom;
            return {
                (screenPt.x - camera.offset.x) * invZoom + camera.target.x,
                (screenPt.y - camera.offset.y) * invZoom + camera.target.y
            };
        }

        /**
         * @brief Calcula el AABB en coordenadas del mundo [minWX..maxWX, minWY..maxWY] que cubre el viewport actual.
         * @details Proyecta las 4 esquinas de la pantalla (ampliadas por viewPaddingPx) hacia el plano del suelo Z=0.
         *          El margen inferior es 2x para capturar las bases de arboles altos cuya copa asoma por abajo.
         */
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

        /**
         * @brief Traduce los limites visibles del mundo al rango entero de chunks para SpatialHashGrid (Capa 1).
         */
        ChunkRange ComputeVisibleChunkRange(float minWX, float maxWX, float minWY, float maxWY) const {
            const float invChunk = 1.0f / spatialGrid.GetChunkSize();
            return {
                static_cast<int32_t>(std::floor(minWX * invChunk)),
                static_cast<int32_t>(std::floor(maxWX * invChunk)),
                static_cast<int32_t>(std::floor(minWY * invChunk)),
                static_cast<int32_t>(std::floor(maxWY * invChunk))
            };
        }

        /**
         * @brief Proyecta el reflejo espejado de un atomo sobre una WaterZone cercana aplicando distorsion de olas.
         * @details Invierte la altura (-Z * 0.80), verifica que el punto reflejado caiga sobre agua descubierta
         *          (sin puente encima) y lo inserta con profundidad -0.10f (sobre el agua -0.15f y detras de solidos).
         */
        void TrySubmitWaterReflection(Vec3 worldPt, const ShadedAtom& shaded,
            float zoom, float timeSec) {
            if (!effectLod.allowReflections || worldPt.z <= 0.12f) return;

            Vec2  refIso = MathIso::WorldToIso({ worldPt.x, worldPt.y, -worldPt.z * 0.80f }, isoConfig);
            float waveRipple = std::sin(refIso.y * 0.22f + timeSec * 3.8f + worldPt.x * 0.6f) * 2.4f;
            refIso.x += waveRipple;

            Vec2 refGround = MathIso::IsoToWorldGround(refIso, isoConfig);
            // Si hay un puente transitable sobre el agua en ese punto, el puente oculta el reflejo.
            if (const SurfaceZone* sz = world.FindSurfaceZoneAt(refGround.x, refGround.y)) {
                if (sz->walkableOverWater) return;
            }
            const WaterZone* wz = world.FindWaterZoneAt(refGround.x, refGround.y);
            if (!wz) return;

            // Tinte espejo oscurecido hacia el color profundo del agua.
            ColorRGBA refCol = MathIso::LerpColor(shaded.color, wz->deepColor, 0.58f);
            refCol.r = static_cast<uint8_t>(refCol.r * 0.82f);
            refCol.g = static_cast<uint8_t>(refCol.g * 0.85f);
            refCol.b = static_cast<uint8_t>(refCol.b * 0.92f);

            // El pequeno sesgo por (x + y) desempata reflejos superpuestos sin salir del rango [-0.115..-0.085].
            float depthRef = MathIso::ComputeDepthKey({ refGround.x, refGround.y, 0.0f })
                - 0.10f
                + MathIso::Clamp((worldPt.x + worldPt.y) * 0.00005f, -0.015f, 0.015f);

            zBuffer.SubmitAtom(refIso.x * zoom, refIso.y * zoom, depthRef,
                shaded.glyph, refCol, FALLBACK_MATERIAL_ID, false, 0, true);
        }

        /**
         * @brief Simula matas organicas multi-atomo de pasto y flores interactivas segun el bioma activo.
         *
         * @details Flujo de optimizacion en cascada:
         *   1. Recorte radial por EffectLodState alrededor del foco.
         *   2. Ruido suave 2D (ValueNoise2D) para agrupar praderas y abrir claros de tierra.
         *   3. Consulta O(1) a World::CanSpawnGroundFoliageAt() (GroundMask): cero computo bajo piedras,
         *      arboles, agua o puentes.
         *   4. Reaccion fisica combinada: campo de viento + empuje radial de todos los personajes cercanos.
         */
        void SubmitGroundFoliageToZBuffer(float minWX, float maxWX, float minWY, float maxWY,
            float zoom, float camTopLeftX, float camTopLeftY,
            uint32_t& atomsEvaluated, uint32_t& outFoliageDrawn) {
            const BiomeDef* biome = weatherSystem.GetActiveBiome();
            if (!biome || biome->groundFoliage.empty()) return;
            if (settings.foliageDensityScale <= 0.001f) return;

            const Vec2 focusPos = GetFocusWorldPos();
            const FoliageDistributionConfig& distCfg = biome->foliageDistribution;
            const float clumpStep = distCfg.clumpStep;
            const float invClumpStep = 1.0f / clumpStep;
            const float patchScale = distCfg.patchScale;
            const float patchSep = distCfg.patchSeparation;
            const float densityScale = effectLod.densityScale * distCfg.globalDensity * settings.foliageDensityScale;

            const float rLod = effectLod.foliageRadiusWorld;
            const float rLodSq = rLod * rLod;
            const float limit = world.GetWorldHalfSize() - 1.0f;

            // Cuantizamos el rango a indices enteros fijos del mundo (mantiene 0 jitter al mover la camara).
            int gx0 = static_cast<int>(std::floor(MathIso::Clamp(std::max(minWX, focusPos.x - rLod), -limit, limit) * invClumpStep));
            int gx1 = static_cast<int>(std::ceil(MathIso::Clamp(std::min(maxWX, focusPos.x + rLod), -limit, limit) * invClumpStep));
            int gy0 = static_cast<int>(std::floor(MathIso::Clamp(std::max(minWY, focusPos.y - rLod), -limit, limit) * invClumpStep));
            int gy1 = static_cast<int>(std::ceil(MathIso::Clamp(std::min(maxWY, focusPos.y + rLod), -limit, limit) * invClumpStep));

            const float screenW = static_cast<float>(screenWidth);
            const float screenH = static_cast<float>(screenHeight);
            const bool  drawFullStem = (zoom >= 1.15f); // En planos alejados dibujamos solo la punta para ahorrar ciclos.
            const auto& allChars = characterSystem.GetInstances();

            constexpr float interactR = 1.75f;
            constexpr float interactRSq = interactR * interactR;

            for (int gy = gy0; gy <= gy1; ++gy) {
                for (int gx = gx0; gx <= gx1; ++gx) {
                    const float fgx = static_cast<float>(gx);
                    const float fgy = static_cast<float>(gy);

                    float cx = (fgx + 0.5f) * clumpStep;
                    float cy = (fgy + 0.5f) * clumpStep;

                    // 1. Culling radial por LOD.
                    float cdx = cx - focusPos.x;
                    float cdy = cy - focusPos.y;
                    if ((cdx * cdx + cdy * cdy) > (rLod + 1.5f) * (rLod + 1.5f)) continue;

                    // 2. Clumping por ValueNoise2D: solo crecen matas donde el ruido supera patchSeparation.
                    float patchNoise = MathIso::ValueNoise2D(cx * patchScale, cy * patchScale, 31.7f);
                    if (patchNoise < patchSep) continue;

                    // 3. Pre-check de pantalla para descartar matas completas fuera del viewport.
                    Vec2  clumpIso = MathIso::WorldToIso({ cx, cy, 0.0f }, isoConfig);
                    float csx = clumpIso.x * zoom - camTopLeftX;
                    float csy = clumpIso.y * zoom - camTopLeftY;
                    if (csx < -cellWidthPx * 4.0f || csx >= screenW + cellWidthPx * 4.0f ||
                        csy < -cellHeightPx * 4.0f || csy >= screenH + cellHeightPx * 3.0f) {
                        continue;
                    }

                    // 4. Seleccion determinista de la especie de follaje para esta mata.
                    float patchBoost = MathIso::Clamp((patchNoise - patchSep) / std::max(1.0f - patchSep, 0.05f), 0.2f, 1.0f);
                    float roll = MathIso::Hash3D(fgx, fgy, 3.0f, 19.3f);
                    float accum = 0.0f;
                    const GroundFoliageDef* chosen = nullptr;

                    for (const GroundFoliageDef& def : biome->groundFoliage) {
                        accum += def.spawnChance * 2.2f * patchBoost * densityScale;
                        if (roll < accum) {
                            chosen = &def;
                            break;
                        }
                    }
                    if (!chosen) continue;

                    // Cantidad de briznas en la mata: mas densa en el corazon de la pradera (patchBoost alto).
                    int span = std::max(0, chosen->maxBlades - chosen->minBlades);
                    int targetCnt = chosen->minBlades + static_cast<int>(std::round(patchBoost * static_cast<float>(span)));
                    int bladeCount = std::max(1, static_cast<int>(std::round(static_cast<float>(targetCnt) * effectLod.densityScale)));

                    for (int b = 0; b < bladeCount; ++b) {
                        float fb = static_cast<float>(b);
                        float ox = (MathIso::Hash3D(fgx, fgy, fb + 1.0f, 4.3f) - 0.5f) * chosen->clumpSpread;
                        float oy = (MathIso::Hash3D(fgx, fgy, fb + 7.0f, 9.1f) - 0.5f) * chosen->clumpSpread;
                        float bx = cx + ox;
                        float by = cy + oy;

                        // 5. Consulta O(1) a la GroundMask: si hay roca, arbol, agua o puente, omitimos la brizna.
                        if (!world.CanSpawnGroundFoliageAt(bx, by)) continue;

                        float fdx = bx - focusPos.x;
                        float fdy = by - focusPos.y;
                        if ((fdx * fdx + fdy * fdy) > rLodSq) continue;

                        float hVar = 0.72f + MathIso::Hash3D(fgx, fgy, fb + 13.0f, 2.9f) * 0.55f;
                        float bladeH = chosen->heightZ * hVar;

                        // 6. Viento continuo + apartamiento fisico radial al caminar jugadores o NPCs encima.
                        Vec2 bend = settings.enableWindSway
                            ? weatherSystem.SampleWindOffset(bx, by, 1.0f, chosen->windWeight * 1.55f)
                            : Vec2{ 0.0f, 0.0f };
                        float flattenZ = 1.0f;

                        if (chosen->interactive) {
                            for (const CharacterInstance& ch : allChars) {
                                float adx = bx - ch.actor.position.x;
                                float ady = by - ch.actor.position.y;
                                float dSq = adx * adx + ady * ady;
                                if (dSq < interactRSq) {
                                    float dist = std::sqrt(dSq);
                                    float push = 1.0f - (dist / interactR);
                                    float invD = (dist > 0.01f) ? (1.0f / dist) : 1.0f;
                                    bend.x += (adx * invD) * push * 0.85f;
                                    bend.y += (ady * invD) * push * 0.85f;
                                    flattenZ = std::min(flattenZ, MathIso::Lerp(1.0f, 0.38f, push));
                                }
                            }
                        }

                        float effH = bladeH * flattenZ;
                        // En isometria 2:1, el desplazamiento horizontal en pantalla es proporcional a (bendX - bendY).
                        float isoBend = bend.x - bend.y;

                        char midGlyph = (isoBend > 0.24f) ? '/' : ((isoBend < -0.24f) ? '\\' : '|');
                        char tipGlyph = '|';
                        if (chosen->isTallBlade) {
                            if (isoBend > 0.12f) tipGlyph = '/';
                            else if (isoBend < -0.12f) tipGlyph = '\\';
                            else                       tipGlyph = '|';
                        }
                        else {
                            float  gh = MathIso::Hash3D(fgx, fgy, fb + 21.0f, 11.1f);
                            size_t gIdx = std::min<size_t>(static_cast<size_t>(gh * chosen->glyphs.size()), chosen->glyphs.size() - 1);
                            tipGlyph = chosen->glyphs[gIdx];
                        }

                        Vec3      tipPos = { bx + bend.x * 0.55f, by + bend.y * 0.55f, effH };
                        ColorRGBA litBase = lightingSystem.ShadeFoliageColor({ bx, by, 0.15f }, chosen->baseColor, true);
                        ColorRGBA litTip = lightingSystem.ShadeFoliageColor(tipPos, chosen->tipColor, true);

                        // Atomo 1: Base anclada al suelo (solo cuando zoom >= 1.15x).
                        if (drawFullStem) {
                            Vec3 basePos = { bx, by, 0.06f };
                            Vec2 baseIso = MathIso::WorldToIso(basePos, isoConfig);
                            zBuffer.SubmitAtom(baseIso.x * zoom, baseIso.y * zoom,
                                MathIso::ComputeDepthKey(basePos),
                                ',', litBase, FALLBACK_MATERIAL_ID, false, 0, true);
                            ++atomsEvaluated;
                        }

                        // Atomo 2: Tallo intermedio (curvatura suave 0.22x).
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

                        // Atomo 3: Punta inclinada o petalo de flor.
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

        /**
         * @brief Rellena UNICAMENTE las celdas que quedaron vacias en el Z-Buffer con el suelo del escenario.
         *
         * @details Al ejecutarse al final de la Etapa 6 (tras insertar objetos, personajes, pasto y particulas):
         *   - Ahorra ~40% de rayos de sombra al saltarse toda celda ya tapada por geometria 3D.
         *   - Garantiza sombras proyectadas 100% continuas sin huecos a cualquier nivel de zoom.
         *   - Resuelve prioridad visual estricta: SurfaceZone (puentes/pisos) > WaterZone > Suelo del Bioma.
         */
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
                    // Salto rapido O(1): si ya hay un objeto, personaje, reflejo o particula aqui, no trabajamos.
                    if (!zBuffer.IsCellEmpty(cx, cy)) continue;

                    const float isoX = (static_cast<float>(origX + cx) + 0.5f) * cellW * invZoom;
                    Vec2 groundW = MathIso::IsoToWorldGround({ isoX, isoY }, isoConfig);

                    if (groundW.x < -limit || groundW.x > limit || groundW.y < -limit || groundW.y > limit) {
                        continue;
                    }

                    ++atomsEvaluated;
                    Vec3        groundPt3D = { groundW.x, groundW.y, 0.0f };
                    const float wcx = static_cast<float>(origX + cx);
                    const float wcy = static_cast<float>(origY + cy);
                    // Ruido anclado a la celda entera del mundo (wcx, wcy): 0 swimming al deslizar la camara.
                    float       noise = MathIso::Hash3D(wcx, wcy, 0.0f, 5.41f);

                    ShadedAtom shaded;
                    int8_t     subX = 0;
                    int8_t     subY = 0;

                    // Prioridad 1: Superficies transitables (puentes sobre agua, patios, pisos -> alineacion recta).
                    if (const SurfaceZone* sz = world.FindSurfaceZoneAt(groundW.x, groundW.y)) {
                        shaded = lightingSystem.ShadeSurfacePoint({ groundW.x, groundW.y, sz->elevationZ }, *sz, noise, true);
                    }
                    // Prioridad 2: Cuerpos de agua con vaiven horizontal sub-pixel.
                    else if (const WaterZone* wz = world.FindWaterZoneAt(groundW.x, groundW.y)) {
                        float df = wz->ComputeDepthFactor(groundW.x, groundW.y);
                        shaded = lightingSystem.ShadeWaterPoint(groundPt3D, wz->deepColor, wz->shallowColor,
                            df, wz->waveSpeed, true);
                        subX = static_cast<int8_t>(std::round(std::sin(groundW.y * 1.4f + windTime * wz->waveSpeed) * 2.0f));
                        subY = static_cast<int8_t>(std::round((noise - 0.5f) * 3.0f));
                    }
                    // Prioridad 3: Terreno base del bioma con desplazamiento organico sub-pixel.
                    else {
                        shaded = lightingSystem.ShadeGroundPoint(groundPt3D, noise, true);
                        if (shaded.glyph != '\0') {
                            float jx = MathIso::Hash3D(wcx, wcy, 1.0f, 8.23f) - 0.5f;
                            float jy = MathIso::Hash3D(wcx, wcy, 2.0f, 14.7f) - 0.5f;
                            bool  isShadow = (shaded.glyph == ':' || shaded.glyph == ';');
                            // Las sombras llevan menor jitter para conservar su silueta compacta y continua.
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

        /**
         * @brief Dibuja un destello emisivo '*' en el origen de cada luz puntual estatica del mapa.
         */
        void SubmitLightMarkersToZBuffer(float zoom, float camTopLeftX, float camTopLeftY) {
            const float screenW = static_cast<float>(screenWidth);
            const float screenH = static_cast<float>(screenHeight);

            for (const PointLight& pl : lightingSystem.GetMapLights()) {
                Vec2  isoPt = MathIso::WorldToIso(pl.position, isoConfig);
                float zoomedX = isoPt.x * zoom;
                float zoomedY = isoPt.y * zoom;
                float sx = zoomedX - camTopLeftX;
                float sy = zoomedY - camTopLeftY;

                if (sx < 0.0f || sx >= screenW || sy < 0.0f || sy >= screenH) continue;

                zBuffer.SubmitAtom(zoomedX, zoomedY, MathIso::ComputeDepthKey(pl.position) + 0.05f,
                    '*', pl.color, FALLBACK_MATERIAL_ID, false, 0, false);
            }
        }

        /**
         * @brief Aplica las reconstrucciones diferidas solicitadas por acciones del Editor Visual (F1).
         */
        void ProcessEditorRequests(EditorContext& ectx) {
            if (ectx.requestWorldRebuild) {
                world.RebuildSpatialGrid(spatialGrid);
                world.RebuildGroundMask(0.5f);
                lightingSystem.BuildShadowHeightfield(world, materialSystem, 0.6f);
                visibleInstanceIds.reserve(world.GetInstances().size());
                activeChunks.reserve(spatialGrid.GetTotalChunks());
            }
            if (ectx.requestLightEmittersSync) {
                particleSystem.ClearWorldState();
                for (const PointLight& pl : lightingSystem.GetMapLights()) {
                    if (pl.flicker && pl.color.r >= 200) {
                        particleSystem.AddWorldEmitter(pl.position, { 0.35f, 0.35f, 0.25f }, "fire_sparks", 14.0f, false);
                    }
                }
            }
        }

        // --- Auxiliares vectoriales para el modo de depuracion visual F4 ---

        void DrawIsoCircle(Vec2 centerWorld, float radius, Color col) const {
            constexpr int   segments = 18;
            constexpr float stepRad = 6.2831853f / static_cast<float>(segments);

            Vec2 prev = IsoToScreen(MathIso::WorldToIso({ centerWorld.x + radius, centerWorld.y, 0.0f }, isoConfig));
            for (int i = 1; i <= segments; ++i) {
                float a = static_cast<float>(i) * stepRad;
                Vec2  curr = IsoToScreen(MathIso::WorldToIso(
                    { centerWorld.x + std::cos(a) * radius, centerWorld.y + std::sin(a) * radius, 0.0f },
                    isoConfig));
                DrawLineV({ prev.x, prev.y }, { curr.x, curr.y }, col);
                prev = curr;
            }
        }

        void DrawIsoOBB(Vec2 center, Vec2 halfSize, float cosRot, float sinRot, Color col) const {
            const std::array<Vec2, 4> localCorners = { {
                { -halfSize.x, -halfSize.y },{ halfSize.x, -halfSize.y },
                { halfSize.x,  halfSize.y },{ -halfSize.x,  halfSize.y }
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

        /**
         * @brief Dibuja overlays tecnicos sobre el viewport cuando F4 esta activo:
         *        Chunks (cian), Agua (azul), Superficies (oro), Colisiones (verde), Luces (naranja),
         *        Personajes (oro/rosa), Vector Solar (amarillo/azul) y Vector de Viento (magenta).
         */
        void RenderDebugOverlays() const {
            if (!debugHUD.AreOverlaysVisible()) return;

            const float cs = spatialGrid.GetChunkSize();
            for (const ChunkCoord& cc : activeChunks) {
                float x0 = static_cast<float>(cc.cx) * cs;
                float y0 = static_cast<float>(cc.cy) * cs;
                DrawIsoOBB({ x0 + cs * 0.5f, y0 + cs * 0.5f }, { cs * 0.5f, cs * 0.5f }, 1.0f, 0.0f, { 0, 220, 240, 100 });
            }

            for (const WaterZone& wz : world.GetWaterZones()) {
                Vec2 center = { (wz.minX + wz.maxX) * 0.5f, (wz.minY + wz.maxY) * 0.5f };
                Vec2 half = { (wz.maxX - wz.minX) * 0.5f, (wz.maxY - wz.minY) * 0.5f };
                DrawIsoOBB(center, half, 1.0f, 0.0f, { 70, 180, 255, 220 });
            }

            for (const SurfaceZone& sz : world.GetSurfaceZones()) {
                Vec2 center = { (sz.minX + sz.maxX) * 0.5f, (sz.minY + sz.maxY) * 0.5f };
                Vec2 half = { (sz.maxX - sz.minX) * 0.5f, (sz.maxY - sz.minY) * 0.5f };
                DrawIsoOBB(center, half, 1.0f, 0.0f, { 245, 215, 140, 220 });
            }

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

            for (const ActiveFrameLight& al : lightingSystem.GetActiveLights()) {
                DrawIsoCircle({ al.position.x, al.position.y }, 1.0f / al.invRadius, { 255, 165, 60, 120 });
            }

            for (const CharacterInstance& ch : characterSystem.GetInstances()) {
                DrawIsoCircle(ch.actor.position, ch.actor.collisionRadius, ch.isPlayer ? GOLD : PINK);
            }

            Vec2            focusPos = GetFocusWorldPos();
            const SunState& sun = lightingSystem.GetSunState();
            Vec2            pStart = IsoToScreen(MathIso::WorldToIso({ focusPos.x, focusPos.y, 2.5f }, isoConfig));
            Vec2            pEnd = IsoToScreen(MathIso::WorldToIso({
                focusPos.x + sun.lightDir.x * 5.0f,
                focusPos.y + sun.lightDir.y * 5.0f,
                2.5f + sun.lightDir.z * 5.0f
                }, isoConfig));
            DrawLineEx({ pStart.x, pStart.y }, { pEnd.x, pEnd.y }, 2.0f, sun.isNight ? SKYBLUE : YELLOW);

            const WindState& wind = weatherSystem.GetWind();
            Vec2             wEnd = IsoToScreen(MathIso::WorldToIso({
                focusPos.x + wind.dirX * (1.2f + wind.speed * 0.6f),
                focusPos.y + wind.dirY * (1.2f + wind.speed * 0.6f),
                2.5f
                }, isoConfig));
            DrawLineEx({ pStart.x, pStart.y }, { wEnd.x, wEnd.y }, 2.0f, MAGENTA);
        }

        /**
         * @brief Sincroniza EngineSettings hacia el Z-Buffer, LightingSystem y limite de FPS en vivo.
         */
        void SyncSettingsToSubsystems() {
            cellWidthPx = settings.cellWidthPx;
            cellHeightPx = settings.cellHeightPx;

            zBufferStyle.enableOutlines = settings.enableOutlines;
            zBufferStyle.enableAsciiBorder = settings.enableAsciiBorder;
            zBufferStyle.enableSubPixelOffset = settings.enableSubPixelOffset;
            zBufferStyle.pixelOutlinePx = settings.pixelOutlinePx;
            zBufferStyle.enableCellBacking = settings.enableCellBacking;
            zBufferStyle.backingDarken = settings.backingDarken;
            zBufferStyle.backingAlpha = settings.backingAlpha;

            lightingSystem.SetShadowsEnabled(settings.enableShadows);
            lightingSystem.SetShadowRaySteps(settings.shadowRaySteps);
            lightingSystem.SetPointLightsEnabled(settings.enablePointLights);
            lightingSystem.SetGIStrengthScale(settings.giStrengthScale);
            lightingSystem.SetAtmosphereInfluence(settings.atmosphericInfluence);

            if (initialized) {
                SetTargetFPS(settings.targetFps);
                zBuffer.Resize(screenWidth, screenHeight, cellWidthPx, cellHeightPx);
            }
        }
    };

    // =========================================================================
    // IMPLEMENTACION DE LA FACHADA PUBLICA (AsciiEngine::Context)
    // =========================================================================

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

            // Centramos la camara en la ventana con zoom inicial en el punto dulce del LOD (1.35x).
            m_impl->camera.offset = { width * 0.5f, height * 0.5f };
            m_impl->camera.target = { 0.0f, 0.0f };
            m_impl->camera.zoom = 1.35f;
        }
        m_impl->SyncSettingsToSubsystems();
        return m_impl->initialized;
    }

    bool Context::LoadRegistry(const std::string& dataRoot) {
        m_impl->dataRootPath = dataRoot;
        const std::filesystem::path root(dataRoot);

        // El orden importa: MaterialSystem debe cargarse primero para que objects/ y characters/ resuelvan sus IDs.
        bool matOk = m_impl->materialSystem.LoadFromFile((root / "materials" / "materials.json").string());
        bool objOk = m_impl->world.LoadObjectsDirectory((root / "objects").string(), m_impl->materialSystem);
        m_impl->characterSystem.LoadTemplatesDirectory((root / "characters").string(), m_impl->materialSystem);
        m_impl->weatherSystem.LoadBiomesDirectory((root / "biomes").string());
        m_impl->weatherSystem.LoadWeathersDirectory((root / "weather").string());
        m_impl->particleSystem.LoadTemplatesDirectory((root / "particles").string());
        return matOk && objOk;
    }

    bool Context::LoadMap(const std::string& mapPath) {
        m_impl->activeMapPath = mapPath;
        bool ok = m_impl->world.LoadMapFromFile(mapPath, m_impl->spatialGrid, &m_impl->materialSystem);

        if (ok) {
            m_impl->viewPaddingPx = m_impl->world.GetViewPaddingPx();
            m_impl->editor.ClearHistory();

            // 1. Instanciamos todos los personajes listados en la seccion "entities" del mapa JSON.
            m_impl->characterSystem.ClearInstances();
            for (const EntitySpawnDef& def : m_impl->world.GetEntitySpawns()) {
                m_impl->characterSystem.SpawnCharacter(
                    def.characterId, def.position, def.facingDeg, def.isPlayer, def.wanderRadius, def.scale
                );
            }
            // Respaldo: si el mapa no define entidades, creamos el explorador por defecto en playerSpawn.
            if (m_impl->characterSystem.GetInstanceCount() == 0) {
                m_impl->characterSystem.SpawnCharacter("hero_human", m_impl->world.GetPlayerSpawn(), 45.0f, true, 0.0f, 1.0f);
            }

            Vec2 focusPos = m_impl->GetFocusWorldPos();
            Vec2 spawnIso = MathIso::WorldToIso({ focusPos.x, focusPos.y, 1.0f }, m_impl->isoConfig);
            m_impl->camera.target = { spawnIso.x, spawnIso.y };

            // 2. Inicializamos bioma, clima y viento desde la configuracion del mapa.
            const WorldWindConfig& wc = m_impl->world.GetWindConfig();
            m_impl->weatherSystem.Configure(
                m_impl->world.GetBiomeId(),
                m_impl->world.GetInitialWeatherId(),
                wc.directionDeg, wc.baseSpeed, wc.gustStrength,
                m_impl->world.GetRandomWeather()
            );

            // 3. Configuramos el ciclo Dia-Noche/GI y horneamos el Heightfield 2D de sombras (celdas 0.6u).
            m_impl->lightingSystem.Configure(
                m_impl->world.GetDayNightConfig(),
                m_impl->world.GetGIConfig(),
                m_impl->world.GetMapLights()
            );
            m_impl->lightingSystem.BuildShadowHeightfield(m_impl->world, m_impl->materialSystem, 0.6f);

            // 4. Asociamos emisores automaticos de chispas de fuego a toda luz calida parpadeante del mapa.
            m_impl->particleSystem.ClearWorldState();
            for (const PointLight& pl : m_impl->world.GetMapLights()) {
                if (pl.flicker && pl.color.r >= 200) {
                    m_impl->particleSystem.AddWorldEmitter(pl.position, { 0.35f, 0.35f, 0.25f }, "fire_sparks", 14.0f, false);
                }
            }

            // 5. Pre-reservamos los buffers de consulta de render y fisica para cero asignaciones en el bucle.
            m_impl->visibleInstanceIds.reserve(m_impl->world.GetInstances().size());
            m_impl->activeChunks.reserve(m_impl->spatialGrid.GetTotalChunks());
            m_impl->physicsQueryIds.reserve(128);
            m_impl->physicsQueryChunks.reserve(16);
        }
        return ok;
    }

    bool Context::SaveMap(const std::string& mapPath) const {
        WorldWindConfig wc = {
            m_impl->weatherSystem.GetWindDirectionDeg(),
            m_impl->weatherSystem.GetWind().speed,
            m_impl->weatherSystem.GetWind().gustStrength
        };
        return m_impl->world.SaveMapToFile(
            mapPath, m_impl->materialSystem,
            m_impl->lightingSystem.GetDayNightConfig(),
            m_impl->lightingSystem.GetGIConfig(),
            m_impl->lightingSystem.GetMapLights(),
            wc,
            m_impl->weatherSystem.GetActiveBiomeId(),
            m_impl->weatherSystem.GetActiveWeatherId(),
            m_impl->weatherSystem.AreRandomTransitionsOn(),
            m_impl->viewPaddingPx,
            m_impl->characterSystem.ExportEntitySpawns()
        );
    }

    void Context::ToggleEditorMode() {
        m_impl->editor.Toggle();
        // Ocultamos el panel F3 al entrar al Editor para despejar el panel izquierdo de plantillas.
        if (m_impl->editor.IsEnabled() && m_impl->debugHUD.IsTelemetryVisible()) {
            m_impl->debugHUD.ToggleTelemetry();
        }
    }

    void Context::ToggleDebugHUD() {
        m_impl->debugHUD.ToggleTelemetry();
    }

    void Context::SpawnParticleBurst(const std::string& templateId, Vec3 worldPos, uint32_t count,
        Vec3 areaHalfExtents) {
        if (m_impl->settings.particleDensityScale <= 0.01f) return;
        uint32_t scaledCount = std::max(1u, static_cast<uint32_t>(std::round(static_cast<float>(count) * m_impl->settings.particleDensityScale)));
        m_impl->particleSystem.SpawnBurst(templateId, worldPos, scaledCount, areaHalfExtents);
    }

    void Context::AddTimedParticleEmitter(const std::string& templateId, Vec3 worldPos, Vec3 areaHalfExtents,
        float spawnRate, float durationSec) {
        if (m_impl->settings.particleDensityScale <= 0.01f || durationSec <= 0.0f) return;
        m_impl->particleSystem.AddWorldEmitter(
            worldPos, areaHalfExtents, templateId,
            spawnRate * m_impl->settings.particleDensityScale, false, durationSec
        );
    }

    void Context::AddTemporaryLight(Vec3 worldPos, ColorRGBA color, float intensity, float radius,
        float durationSec, bool fadeOut) {
        m_impl->lightingSystem.AddTemporaryLight(worldPos, color, intensity, radius, durationSec, fadeOut);
    }

    bool Context::IsRunning() const {
        return m_impl->initialized && !WindowShouldClose();
    }

    float Context::GetFrameTime() const {
        return ::GetFrameTime();
    }

    void Context::Update(float dt) {
        auto simStart = Clock::now();

        // Sincronizamos el Z-Buffer y el centro de camara si el usuario redimensiona la ventana.
        m_impl->screenWidth = GetScreenWidth();
        m_impl->screenHeight = GetScreenHeight();
        m_impl->camera.offset = { m_impl->screenWidth * 0.5f, m_impl->screenHeight * 0.5f };
        m_impl->zBuffer.Resize(m_impl->screenWidth, m_impl->screenHeight, m_impl->cellWidthPx, m_impl->cellHeightPx);

        // Alternar entre Modo Juego y Modo Editor Visual [F1].
        if (IsKeyPressed(KEY_F1)) {
            ToggleEditorMode();
        }

        const bool inEditor = m_impl->editor.IsEnabled();
        Vec2       desiredWorldDir = { 0.0f, 0.0f };
        bool       playerSprint = false;

        // =========================================================================
        // ETAPA 1: ENTRADA DESACOPLADA (MODO EDITOR VS MODO JUEGO)
        // =========================================================================
        if (inEditor) {
            Vec2 camTarget = { m_impl->camera.target.x, m_impl->camera.target.y };
            EditorContext ectx{
                m_impl->world, m_impl->materialSystem, m_impl->spatialGrid,
                m_impl->lightingSystem, m_impl->weatherSystem, m_impl->characterSystem,
                m_impl->isoConfig,
                m_impl->screenWidth, m_impl->screenHeight,
                camTarget,{ m_impl->camera.offset.x, m_impl->camera.offset.y },
                m_impl->camera.zoom, m_impl->minZoom, m_impl->maxZoom,
                m_impl->viewPaddingPx, m_impl->activeMapPath
            };

            m_impl->editor.Update(dt, ectx);
            m_impl->camera.target = { camTarget.x, camTarget.y };
            m_impl->ProcessEditorRequests(ectx);

            // En Modo Editor, F5 recarga las plantillas JSON del disco preservando el escenario editado en RAM.
            if (ectx.requestHotReload || IsKeyPressed(KEY_F5)) {
                LoadRegistry(m_impl->dataRootPath);
                m_impl->world.RebuildGroundMask(0.5f);
                m_impl->lightingSystem.BuildShadowHeightfield(m_impl->world, m_impl->materialSystem, 0.6f);
                m_impl->editor.ShowToast("Plantillas recargadas (F5)", 2.0f);
            }
            if (ectx.requestReloadMap && !m_impl->activeMapPath.empty()) {
                LoadMap(m_impl->activeMapPath);
            }
        }
        else {
            // Traducimos WASD (ejes de pantalla) al plano cartesiano del mundo isometrico.
            Vec2 screenInput = { 0.0f, 0.0f };
            if (IsKeyDown(KEY_W)) screenInput.y -= 1.0f;
            if (IsKeyDown(KEY_S)) screenInput.y += 1.0f;
            if (IsKeyDown(KEY_A)) screenInput.x -= 1.0f;
            if (IsKeyDown(KEY_D)) screenInput.x += 1.0f;

            if (IsKeyPressed(KEY_TAB)) m_impl->characterSystem.CycleControlledCharacter();
            if (IsKeyPressed(KEY_E)) {
                m_impl->characterSystem.TriggerPlayerAttack();
                if (const CharacterInstance* ch = m_impl->characterSystem.GetControlledCharacter()) {
                    float fx = ch->actor.position.x + std::cos(ch->actor.facingAngleRad) * 1.15f;
                    float fy = ch->actor.position.y + std::sin(ch->actor.facingAngleRad) * 1.15f;
                    SpawnParticleBurst("impact_burst", { fx, fy, 1.15f }, 16, { 0.25f, 0.25f, 0.20f });
                    AddTemporaryLight({ fx, fy, 1.45f }, { 255, 200, 115, 255 }, 2.2f, 9.5f, 0.34f, true);
                }
            }

            desiredWorldDir = MathIso::IsoToWorldGround(screenInput, m_impl->isoConfig);
            playerSprint = IsKeyDown(KEY_LEFT_SHIFT);

            float wheel = GetMouseWheelMove();
            if (wheel != 0.0f) {
                m_impl->camera.zoom = MathIso::Clamp(m_impl->camera.zoom + wheel * 0.12f, m_impl->minZoom, m_impl->maxZoom);
            }

            if (IsKeyPressed(KEY_SPACE)) m_impl->lightingSystem.ToggleCycleRunning();
            if (IsKeyDown(KEY_LEFT))     m_impl->lightingSystem.AdjustTimeHours(-dt * 4.5f);
            if (IsKeyDown(KEY_RIGHT))    m_impl->lightingSystem.AdjustTimeHours(dt * 4.5f);
            if (IsKeyPressed(KEY_ONE))   m_impl->weatherSystem.TransitionToWeather("clear_day");
            if (IsKeyPressed(KEY_TWO))   m_impl->weatherSystem.TransitionToWeather("heavy_rain");
            if (IsKeyPressed(KEY_THREE)) m_impl->weatherSystem.TransitionToWeather("blizzard");
            if (IsKeyPressed(KEY_M))     m_impl->weatherSystem.CycleBiome();
            if (IsKeyPressed(KEY_R))     m_impl->weatherSystem.ToggleRandomTransitions();

            // En Modo Juego, F5 recarga tanto plantillas como el mapa JSON conservando la hora actual.
            if (IsKeyPressed(KEY_F5)) {
                float savedHr = m_impl->lightingSystem.GetTimeHours();
                LoadRegistry(m_impl->dataRootPath);
                if (!m_impl->activeMapPath.empty()) {
                    LoadMap(m_impl->activeMapPath);
                    m_impl->lightingSystem.AdjustTimeHours(savedHr - m_impl->lightingSystem.GetTimeHours());
                }
            }
        }

        // Atajos visuales y de diagnostico globales (disponibles tanto en Juego como en Editor).
        if (IsKeyPressed(KEY_H)) { m_impl->settings.enableShadows = !m_impl->settings.enableShadows;                m_impl->SyncSettingsToSubsystems(); }
        if (IsKeyPressed(KEY_O)) { m_impl->settings.enableOutlines = !m_impl->settings.enableOutlines;              m_impl->SyncSettingsToSubsystems(); }
        if (IsKeyPressed(KEY_B)) { m_impl->settings.enableAsciiBorder = !m_impl->settings.enableAsciiBorder;        m_impl->SyncSettingsToSubsystems(); }
        if (IsKeyPressed(KEY_G)) { m_impl->settings.enableSubPixelOffset = !m_impl->settings.enableSubPixelOffset;  m_impl->SyncSettingsToSubsystems(); }
        if (IsKeyPressed(KEY_V)) { m_impl->settings.enableCellBacking = !m_impl->settings.enableCellBacking;        m_impl->SyncSettingsToSubsystems(); }
        if (IsKeyPressed(KEY_T))  m_impl->playerTorchOn = !m_impl->playerTorchOn;
        if (IsKeyPressed(KEY_F3)) m_impl->debugHUD.ToggleTelemetry();
        if (IsKeyPressed(KEY_F4)) m_impl->debugHUD.ToggleOverlays();

        // =========================================================================
        // ETAPA 2: FISICA, COLISIONES, IA DE NPCs Y ANIMACION PROCEDURAL
        // =========================================================================
        // La nieve acumulada reduce la friccion del terreno de 10.0 a 4.5 (deslizamiento invernal).
        float groundFriction = MathIso::Lerp(10.0f, 4.5f, m_impl->weatherSystem.GetModifiers().snowCoverage);
        ParticleSystem* footstepSys = (m_impl->settings.particleDensityScale > 0.01f) ? &m_impl->particleSystem : nullptr;
        m_impl->characterSystem.Update(
            dt, desiredWorldDir, playerSprint, groundFriction,
            m_impl->world, m_impl->spatialGrid,
            m_impl->physicsQueryIds, m_impl->physicsQueryChunks,
            footstepSys, m_impl->weatherSystem.GetModifiers().snowCoverage
        );

        // Seguimiento suave de camara solo en Modo Juego; en Editor la camara es 100% libre.
        if (!inEditor) {
            Vec2  charPos = m_impl->GetFocusWorldPos();
            Vec2  targetIso = MathIso::WorldToIso({ charPos.x, charPos.y, 1.2f }, m_impl->isoConfig);
            float camSmooth = MathIso::Clamp(9.0f * dt, 0.0f, 1.0f);
            m_impl->camera.target.x = MathIso::Lerp(m_impl->camera.target.x, targetIso.x, camSmooth);
            m_impl->camera.target.y = MathIso::Lerp(m_impl->camera.target.y, targetIso.y, camSmooth);
        }

        // =========================================================================
        // ETAPA 3 & 4: LOD DE EFECTOS, CLIMA/VIENTO, CICLO DIA-NOCHE Y PARTICULAS SoA
        // =========================================================================
        m_impl->UpdateEffectLod();
        m_impl->weatherSystem.Update(dt);
        m_impl->lightingSystem.Update(dt, m_impl->weatherSystem.GetModifiers());

        const Vec2      focusPos = m_impl->GetFocusWorldPos();
        const SunState& sun = m_impl->lightingSystem.GetSunState();
        m_impl->particleSystem.Update(
            dt, m_impl->weatherSystem.GetWind(), sun.isNight, focusPos,
            m_impl->weatherSystem.GetWeatherParticleId(),
            m_impl->weatherSystem.GetWeatherParticleRate()* m_impl->effectLod.densityScale* m_impl->settings.particleDensityScale
        );

        auto simEnd = Clock::now();

        // Recopilamos las metricas de simulacion para el Debug HUD (F3).
        auto& t = m_impl->telemetry;
        t.simTimeMs = ElapsedMs(simStart, simEnd);
        t.fps = static_cast<float>(GetFPS());
        t.frameTimeMs = dt * 1000.0f;
        t.cameraWorldPos = focusPos;
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
        t.activeCharacters = m_impl->characterSystem.GetInstanceCount();
        t.controlledCharName = m_impl->characterSystem.GetControlledName();
        t.controlledAnimState = m_impl->characterSystem.GetControlledAnimStateName();
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

        // Preparamos la antorcha movil centrada en el personaje controlado (o en la camara del Editor).
        Vec2              focusPos = m_impl->GetFocusWorldPos();
        PointLight        playerTorch;
        const PointLight* torchPtr = nullptr;
        if (m_impl->playerTorchOn) {
            playerTorch.position = { focusPos.x, focusPos.y, 2.3f };
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
        // ETAPA 6: RASTERIZACION O(N) EN EL ASCII Z-BUFFER WORLD-LOCKED
        // =========================================================================
        auto zbufStart = Clock::now();

        const float zoom = m_impl->camera.zoom;
        const float camTopLeftX = m_impl->camera.target.x * zoom - m_impl->camera.offset.x;
        const float camTopLeftY = m_impl->camera.target.y * zoom - m_impl->camera.offset.y;
        const float screenW = static_cast<float>(m_impl->screenWidth);
        const float screenH = static_cast<float>(m_impl->screenHeight);
        const float windTime = m_impl->weatherSystem.GetWind().time;

        // Limpiamos y anclamos la rejilla del Z-Buffer a la celda entera del mundo mas cercana.
        m_impl->zBuffer.BeginFrame(camTopLeftX, camTopLeftY);

        const Rect2D viewportRect = {
            -m_impl->viewPaddingPx,
            -m_impl->viewPaddingPx,
            screenW + m_impl->viewPaddingPx * 2.0f,
            screenH + m_impl->viewPaddingPx * 2.0f
        };

        // Splatting adaptativo en 3 niveles para mantener superficies selladas desde 0.95x hasta 2.5x.
        const int splatRadius = (zoom > 1.85f) ? 2 : ((zoom > 1.18f) ? 1 : 0);

        uint32_t instancesRendered = 0;
        uint32_t atomsEvaluated = 0;
        uint32_t atomsCulledScreen = 0;
        uint32_t groundFoliageDrawn = 0;

        // 6.1. Destellos emisivos de luces puntuales del mapa.
        m_impl->SubmitLightMarkersToZBuffer(zoom, camTopLeftX, camTopLeftY);

        // 6.2. Personajes modulares articulados (cinematica directa 3D + reflejos en agua).
        m_impl->characterSystem.SubmitToZBuffer(
            m_impl->zBuffer, m_impl->lightingSystem, m_impl->materialSystem,
            m_impl->world, m_impl->isoConfig, viewportRect,
            zoom, camTopLeftX, camTopLeftY, screenW, screenH,
            splatRadius, m_impl->effectLod.allowReflections, windTime,
            atomsEvaluated, atomsCulledScreen
        );

        // 6.3. Instancias estaticas del mundo (Capa 2 Frustum + Flyweight + Viento LOD + Reflejos).
        const auto& instances = m_impl->world.GetInstances();
        const float windRadSq = m_impl->effectLod.windEffectRadiusSq;

        for (uint32_t instId : m_impl->visibleInstanceIds) {
            const MapInstance& inst = instances[instId];
            const BakedPrefab* prefab = m_impl->world.GetPrefab(inst.prefabId);
            if (!prefab) continue;

            // Capa 2 — Frustum Culling 2D en espacio de pantalla usando cotas envolventes horneadas.
            Vec2  baseScreen = m_impl->IsoToScreen(MathIso::WorldToIso(inst.position, m_impl->isoConfig));
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
            if (!viewportRect.Intersects(objRect)) continue;

            ++instancesRendered;

            // LOD radial: desactiva viento/reflejos y activa stride=2 en objetos distantes al alejar camara.
            float        odx = inst.position.x - focusPos.x;
            float        ody = inst.position.y - focusPos.y;
            const bool   inEffectRadius = ((odx * odx + ody * ody) <= windRadSq);
            const size_t atomStep = (!inEffectRadius && m_impl->effectLod.distantAtomStride > 1) ? 2 : 1;

            const float invBoundH = 1.0f / std::max(prefab->boundHeightZ * inst.scale.z, 0.5f);
            const float reflectReach = prefab->boundRadiusXY * maxScaleXY + prefab->boundHeightZ * inst.scale.z * 1.4f;
            const bool  nearWater = m_impl->effectLod.allowReflections && inEffectRadius &&
                m_impl->world.IsNearAnyWater(inst.position.x, inst.position.y, reflectReach);

            const BakedAtom* atoms = prefab->atoms.data();
            const size_t     atomCount = prefab->atoms.size();

            for (size_t i = 0; i < atomCount; i += atomStep) {
                const BakedAtom& a = atoms[i];
                ++atomsEvaluated;

                // Transformacion Local -> Mundo usando escala y cosRot/sinRot precalculados.
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

                // Onda de viento en follaje flexible + perturbacion de normal para brillo vivo en hojas.
                if (m_impl->settings.enableWindSway && inEffectRadius && a.windWeightByte > 0) {
                    float windW = static_cast<float>(a.windWeightByte) * (1.0f / 255.0f);
                    float heightNorm = MathIso::Clamp(sz * invBoundH, 0.0f, 1.0f);
                    Vec2  sway = m_impl->weatherSystem.SampleWindOffset(worldPt.x, worldPt.y, heightNorm, windW);

                    worldPt.x += sway.x;
                    worldPt.y += sway.y;
                    worldNorm = MathIso::Normalize({
                        worldNorm.x + sway.x * 0.45f,
                        worldNorm.y + sway.y * 0.45f,
                        worldNorm.z
                        });
                }

                Vec2  isoPt = MathIso::WorldToIso(worldPt, m_impl->isoConfig);
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

                // Proyeccion de reflejo espejado en agua (submuestreo 50% para preservar presupuesto).
                if (nearWater && ((i & 1) == 0)) {
                    m_impl->TrySubmitWaterReflection(worldPt, shaded, zoom, windTime);
                }
            }
        }

        // 6.4. Pasto y flores interactivas (descarte O(1) bajo objetos/agua/puentes via GroundMask).
        m_impl->SubmitGroundFoliageToZBuffer(minWX, maxWX, minWY, maxWY, zoom, camTopLeftX, camTopLeftY,
            atomsEvaluated, groundFoliageDrawn);

        // 6.5. Particulas activas del pool SoA con stride de LOD.
        m_impl->particleSystem.SubmitToZBuffer(
            m_impl->zBuffer, m_impl->isoConfig, zoom, camTopLeftX, camTopLeftY,
            screenW, screenH, m_impl->effectLod.particleRenderStride
        );

        // 6.6. Terreno modular (SurfaceZone -> WaterZone -> Suelo Bioma) y sombras 2.5D en celdas vacias.
        m_impl->SubmitGroundPlaneToZBuffer(zoom, atomsEvaluated);

        auto zbufEnd = Clock::now();

        // =========================================================================
        // ETAPA 7 & 8: VOLCADO GPU EN 3 PASES + OVERLAYS (F4) + HUD (F3) + EDITOR (F1)
        // =========================================================================
        auto gpuStart = Clock::now();

        BeginDrawing();

        // Fondo continuo estilo terminal oscura modulado por ciclo Dia-Noche y clima.
        const ColorRGBA& bg = m_impl->lightingSystem.GetSunState().backgroundColor;
        ClearBackground({ bg.r, bg.g, bg.b, bg.a });

        // Volcado lineal O(W * H): Pase 0 (Backing) -> Pase 1 (Siluetas) -> Pase 2 (Glifos, Zero Overdraw).
        ZBufferStats zStats = m_impl->zBuffer.FlushToScreen(
            &m_impl->renderFont,
            &m_impl->glyphLUT,
            m_impl->glyphAspectRatio,
            m_impl->zBufferStyle
        );

        m_impl->RenderDebugOverlays();

        auto gpuEnd = Clock::now();

        // Actualizamos contadores de renderizado y memoria antes de dibujar el panel F3.
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
        t.bakedCloudBytes = m_impl->world.GetBakedMemoryBytes() + m_impl->characterSystem.GetBakedMemoryBytes();
        t.heightfieldBytes = m_impl->lightingSystem.GetHeightfieldBytes();
        t.activePointLights = static_cast<uint32_t>(m_impl->lightingSystem.GetActiveLights().size());
        t.gridWidth = m_impl->zBuffer.GetGridWidth();
        t.gridHeight = m_impl->zBuffer.GetGridHeight();

        m_impl->debugHUD.RenderPanel(t, &m_impl->renderFont);

        // UI en modo inmediato (Raygui): los clics de botones se detectan al dibujar el panel;
        // aplicamos inmediatamente cualquier reconstruccion o recarga solicitada desde la barra inferior.
        if (m_impl->editor.IsEnabled()) {
            Vec2 camTarget = { m_impl->camera.target.x, m_impl->camera.target.y };
            EditorContext ectx{
                m_impl->world, m_impl->materialSystem, m_impl->spatialGrid,
                m_impl->lightingSystem, m_impl->weatherSystem, m_impl->characterSystem,
                m_impl->isoConfig,
                m_impl->screenWidth, m_impl->screenHeight,
                camTarget,{ m_impl->camera.offset.x, m_impl->camera.offset.y },
                m_impl->camera.zoom, m_impl->minZoom, m_impl->maxZoom,
                m_impl->viewPaddingPx, m_impl->activeMapPath
            };
            m_impl->editor.Render(ectx, &m_impl->renderFont);
            m_impl->camera.target = { camTarget.x, camTarget.y };
            m_impl->ProcessEditorRequests(ectx);

            if (ectx.requestHotReload) {
                const_cast<Context*>(this)->LoadRegistry(m_impl->dataRootPath);
                m_impl->world.RebuildGroundMask(0.5f);
                m_impl->lightingSystem.BuildShadowHeightfield(m_impl->world, m_impl->materialSystem, 0.6f);
            }
            if (ectx.requestReloadMap && !m_impl->activeMapPath.empty()) {
                const_cast<Context*>(this)->LoadMap(m_impl->activeMapPath);
            }
        }

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

    const EngineSettings& Context::GetSettings() const {
        return m_impl->settings;
    }

    void Context::ApplySettings(const EngineSettings& settings) {
        m_impl->settings = settings;
        m_impl->SyncSettingsToSubsystems();
    }

    void Context::ApplyQualityPreset(QualityPreset preset) {
        m_impl->settings = MakeQualityPreset(preset);
        m_impl->SyncSettingsToSubsystems();
    }

    bool Context::LoadSettings(const std::string& jsonPath) {
        EngineSettings loaded = m_impl->settings;
        if (!LoadEngineSettings(jsonPath, loaded)) {
            return false;
        }
        ApplySettings(loaded);
        return true;
    }

    bool Context::SaveSettings(const std::string& jsonPath) const {
        return SaveEngineSettings(jsonPath, m_impl->settings);
    }

} // namespace AsciiEngine