// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file LightingSystem.cpp
 * @brief Implementacion comentada linea por linea de la iluminacion dinamica, sombras 2.5D y agua.
 */
#include <AsciiEngine/LightingSystem.hpp>
#include <AsciiEngine/MathIso.hpp>
#include <AsciiEngine/MaterialSystem.hpp>
#include <AsciiEngine/World.hpp>
#include "raylib.h" // Solo para TraceLog() en diagnostico al construir el Heightfield.

#include <algorithm> // std::max, std::min
#include <cmath>     // std::sin, std::cos, std::sqrt, std::floor, std::ceil, std::fmod

namespace AsciiEngine {

    namespace {

        constexpr float TWO_PI_F = 6.28318530717958647692f;

        /**
         * @brief Acota un flotante a [0.0, 255.0] y lo convierte con seguridad a canal de color uint8_t.
         */
        inline uint8_t ToByte(float v) {
            return static_cast<uint8_t>(MathIso::Clamp(v, 0.0f, 255.0f));
        }

        /**
         * @brief Funcion Hermite C1 continua (Smoothstep) en [0.0, 1.0].
         * @details Garantiza derivada 0 en los extremos e0 y e1, eliminando cambios bruscos de
         * iluminacion, sombras y fondo al cruzar el amanecer (6:00) y el anochecer (18:00).
         */
        inline float Smoothstep(float e0, float e1, float x) {
            float t = MathIso::Clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

    } // namespace anonimo

    void LightingSystem::Configure(const DayNightConfig& dayNight,
        const GlobalIlluminationConfig& gi,
        const std::vector<PointLight>& mapLights) {
        m_dayNight = dayNight;
        m_giConfig = gi;
        m_mapLights = mapLights;

        // Pre-reservamos la lista de luces activas (+4 margen para antorcha dinamica u otras luces moviles).
        m_activeLights.reserve(m_mapLights.size() + 4);
        RecomputeSunState();
    }

    void LightingSystem::BuildShadowHeightfield(const World& world, const MaterialSystem& materials, float cellSizeWorld) {
        // 1. Configuramos resolucion de la grilla (por defecto 0.6u -> captura troncos de radio 0.48u).
        m_hfCellSize = std::max(0.35f, cellSizeWorld);
        m_hfInvCellSize = 1.0f / m_hfCellSize;

        // 2. Dimensionamos la rejilla cuadrada cubriendo todo el mundo + 4u de holgura perimetral.
        const float halfSize = world.GetWorldHalfSize() + 4.0f;
        m_hfOriginX = -halfSize;
        m_hfOriginY = -halfSize;
        m_hfWidth = static_cast<int>(std::ceil((halfSize * 2.0f) * m_hfInvCellSize));
        m_hfHeight = m_hfWidth;
        m_hfMaxWorldZ = 0.0f;

        // 3. Inicializamos toda la rejilla a altura 0.0 (nivel del suelo plano).
        m_heightfield.assign(static_cast<size_t>(m_hfWidth) * static_cast<size_t>(m_hfHeight), 0.0f);

        // 4. Proyectamos todos los atomos opacos de cada instancia sobre la grilla 2D guardando la Z maxima.
        for (const MapInstance& inst : world.GetInstances()) {
            const BakedPrefab* prefab = world.GetPrefab(inst.prefabId);
            if (!prefab) continue;

            for (const BakedAtom& a : prefab->atoms) {
                MaterialId matId = (inst.overrideMaterialId != FALLBACK_MATERIAL_ID)
                    ? inst.overrideMaterialId
                    : a.materialId;
                // Materiales translucidos o emisivos (occludesLight == false) no proyectan sombra.
                if (!materials.Get(matId).occludesLight) continue;

                // Transformacion Local -> Mundo usando escala y cos/sin precalculados de la instancia.
                float sx = a.localPos.x * inst.scale.x;
                float sy = a.localPos.y * inst.scale.y;
                float wx = inst.position.x + (sx * inst.cosRot - sy * inst.sinRot);
                float wy = inst.position.y + (sx * inst.sinRot + sy * inst.cosRot);
                float wz = inst.position.z + (a.localPos.z * inst.scale.z);

                int gx = static_cast<int>(std::floor((wx - m_hfOriginX) * m_hfInvCellSize));
                int gy = static_cast<int>(std::floor((wy - m_hfOriginY) * m_hfInvCellSize));

                if (gx >= 0 && gx < m_hfWidth && gy >= 0 && gy < m_hfHeight) {
                    size_t idx = static_cast<size_t>(gy) * static_cast<size_t>(m_hfWidth) + static_cast<size_t>(gx);
                    // Conservamos la elevacion maxima de la columna (ej. copa de un pino o dintel de un arco).
                    if (wz > m_heightfield[idx]) m_heightfield[idx] = wz;
                    if (wz > m_hfMaxWorldZ)      m_hfMaxWorldZ = wz;
                }
            }
        }

        TraceLog(LOG_INFO, "LIGHTING: Heightfield 2D (%dx%d, %.1f KB, maxZ=%.1f) construido.",
            m_hfWidth, m_hfHeight, static_cast<float>(GetHeightfieldBytes()) / 1024.0f, m_hfMaxWorldZ);
    }

    void LightingSystem::AdjustTimeHours(float deltaHours) {
        // Modulo continuo en [0.0, 24.0) que soporta desplazamientos negativos (tecla Izquierda).
        m_dayNight.currentTimeHours = std::fmod(m_dayNight.currentTimeHours + deltaHours, 24.0f);
        if (m_dayNight.currentTimeHours < 0.0f) {
            m_dayNight.currentTimeHours += 24.0f;
        }
        RecomputeSunState();
    }

    void LightingSystem::Update(float dt, const WeatherModifiers& weatherMod) {
        m_elapsedTime += dt;
        m_weatherMod = weatherMod;

        // Si el ciclo automatico esta activo, avanzamos la hora proporcionalmente a dayLengthSeconds.
        if (m_dayNight.enabled && m_dayNight.dayLengthSeconds > 1.0f) {
            float hoursPerSec = 24.0f / m_dayNight.dayLengthSeconds;
            m_dayNight.currentTimeHours = std::fmod(m_dayNight.currentTimeHours + hoursPerSec * dt, 24.0f);
        }

        RecomputeSunState();
    }

    void LightingSystem::RecomputeSunState() {
        // 1. Convertimos la hora [0..24) a angulo orbital theta:
        //    06:00 -> theta = 0 (amanecer) | 12:00 -> pi/2 (cenit)
        //    18:00 -> theta = pi (ocaso)   | 00:00 -> 3pi/2 (medianoche)
        float theta = ((m_dayNight.currentTimeHours - 6.0f) / 24.0f) * TWO_PI_F;
        float sinT = std::sin(theta);
        float cosT = std::cos(theta);

        // 2. Factores C1 continuos para evitar cualquier salto visual al cruzar el horizonte:
        //    - dayBlend:     0.0 en noche profunda -> 1.0 en dia pleno (transicion suave alrededor de sinT = 0).
        //    - twilightBand: parabola 4*b*(1-b) que vale 0 de dia/noche y alcanza 1.0 justo en el crepusculo.
        //    - sunWeight / moonWeight: caen exactamente a 0.0 antes de que cambie el signo de sinT.
        float dayBlend = Smoothstep(-0.22f, 0.26f, sinT);
        float twilightBand = 4.0f * dayBlend * (1.0f - dayBlend);
        float sunWeight = Smoothstep(0.02f, 0.32f, sinT);
        float moonWeight = Smoothstep(0.02f, 0.32f, -sinT);

        m_sunState.isNight = (sinT < 0.0f);

        // Moduladores climaticos atenuados por m_atmosphereInfluence [0 = luz neutra .. 1 = clima completo].
        const float effSunMul = MathIso::Lerp(1.0f, m_weatherMod.sunMultiplier, m_atmosphereInfluence);
        const float effAmbMul = MathIso::Lerp(1.0f, m_weatherMod.ambientMultiplier, m_atmosphereInfluence);
        const float effBgBlend = m_weatherMod.bgTintBlend * m_atmosphereInfluence;

        // 3. Direccion, color e intensidad de la fuente direccional activa (Sol de dia, Luna de noche).
        //    Como sunWeight == moonWeight == 0 en sinT == 0, cambiar lightDir aqui es 100% imperceptible.
        if (sinT >= 0.0f) {
            // Elevacion minima 0.20f impide sombras infinitas rasantes al horizonte.
            float sunElev = MathIso::Clamp(sinT, 0.20f, 1.0f);
            m_sunState.lightDir = MathIso::Normalize({ cosT, 0.35f, sunElev });

            const ColorRGBA sunHorizon = { 255, 162, 92, 255 };  // Ambar calido de amanecer/atardecer.
            const ColorRGBA sunNoon = { 255, 248, 236, 255 }; // Blanco solar natural al mediodia.
            m_sunState.lightColor = MathIso::LerpColor(sunHorizon, sunNoon, Smoothstep(0.05f, 0.45f, sinT));
            m_sunState.lightIntensity = sunWeight * 0.92f * effSunMul;
        }
        else {
            float moonElev = MathIso::Clamp(-sinT, 0.26f, 1.0f);
            // La luna orbita en oposicion al sol (-cosT) proyectando luz fria azulada.
            m_sunState.lightDir = MathIso::Normalize({ -cosT, 0.35f, moonElev });
            m_sunState.lightColor = { 130, 168, 230, 255 };
            m_sunState.lightIntensity = moonWeight * 0.34f * effSunMul;
        }

        // 4. Fondo continuo estilo consola/terminal oscura (maximo contraste para los glifos ASCII).
        const ColorRGBA bgNight = { 6,  8, 12, 255 }; // Negro azulado nocturno.
        const ColorRGBA bgTwilight = { 16, 14, 20, 255 }; // Violeta carbon en crepusculo.
        const ColorRGBA bgDay = { 22, 25, 29, 255 }; // Gris carbon neutro de dia.

        ColorRGBA bg = MathIso::LerpColor(bgNight, bgDay, dayBlend);
        bg = MathIso::LerpColor(bg, bgTwilight, twilightBand * 0.65f);

        // 5. Tinte atmosferico del clima (tormenta/ventisca) escalado continuamente con dayBlend
        //    para evitar saltos bruscos al cambiar de dia con lluvia activa.
        if (effBgBlend > 0.01f) {
            float tintScale = MathIso::Lerp(0.32f, 1.0f, dayBlend);
            ColorRGBA scaledTint = {
                ToByte(static_cast<float>(m_weatherMod.bgTint.r) * tintScale),
                ToByte(static_cast<float>(m_weatherMod.bgTint.g) * tintScale),
                ToByte(static_cast<float>(m_weatherMod.bgTint.b) * MathIso::Lerp(0.42f, 1.0f, dayBlend)),
                255
            };
            bg = MathIso::LerpColor(bg, scaledTint, m_weatherMod.bgTintBlend);
        }

        // 6. Colores ambientales de GI hemisferica (cielo vs suelo) con matiz calido en crepusculo.
        ColorRGBA skyNight = { 32, 48, 82, 255 };
        ColorRGBA skyTwil = { 175, 118, 102, 255 };
        ColorRGBA skyCol = MathIso::LerpColor(skyNight, m_giConfig.skyColor, dayBlend);
        skyCol = MathIso::LerpColor(skyCol, skyTwil, twilightBand * 0.55f);

        ColorRGBA gndNight = { 14, 18, 24, 255 };
        ColorRGBA gndCol = MathIso::LerpColor(gndNight, m_giConfig.groundColor, dayBlend);

        // 7. Contribucion continua de nieve acumulada sobre el fondo y el rebote luminoso del suelo.
        if (m_weatherMod.snowCoverage > 0.05f) {
            ColorRGBA snowBg = MathIso::LerpColor({ 12, 16, 24, 255 }, { 34, 40, 48, 255 }, dayBlend);
            bg = MathIso::LerpColor(bg, snowBg, m_weatherMod.snowCoverage * 0.45f);
            gndCol = MathIso::LerpColor(gndCol, { 118, 134, 155, 255 }, m_weatherMod.snowCoverage * 0.55f);
        }

        m_sunState.backgroundColor = bg;
        m_sunState.skyAmbient = skyCol;
        m_sunState.groundAmbient = gndCol;
        m_sunState.ambientStrength = m_giConfig.ambientStrength * MathIso::Lerp(0.42f, 0.72f, dayBlend) * effAmbMul * m_giStrengthScale;
        m_sunState.shadowDarkness = MathIso::Lerp(0.55f, 0.25f, dayBlend);
    }

    void LightingSystem::PrepareFrameLights(float minWX, float maxWX, float minWY, float maxWY,
        const PointLight* dynamicTorch) {
        // Reutilizamos el buffer ya reservado: cero asignaciones en el bucle principal.
        m_activeLights.clear();
        if (!m_pointLightsEnabled) return;

        auto pushLight = [&](const PointLight& src) {
            if (src.radius <= 0.1f || src.intensity <= 0.01f) return;

            float effIntensity = src.intensity;
            float effRadius = src.radius;

            // Parpadeo organico: superposicion de dos ondas armonicas con frecuencias primas (10.5 y 19.3)
            // para evitar un ritmo mecanico repetitivo en antorchas y braseros.
            if (src.flicker) {
                float f1 = std::sin(m_elapsedTime * 10.5f + src.flickerPhase);
                float f2 = std::cos(m_elapsedTime * 19.3f + src.flickerPhase * 1.7f);
                float mod = 1.0f + (f1 * 0.08f + f2 * 0.04f);
                effIntensity *= mod;
                effRadius *= (0.96f + mod * 0.04f);
            }

            // Precalculamos constantes calientes para acelerar el bucle por atomo.
            ActiveFrameLight al;
            al.position = src.position;
            al.radiusSq = effRadius * effRadius;
            al.invRadius = 1.0f / effRadius;
            al.intensity = effIntensity;
            al.rNorm = (static_cast<float>(src.color.r) / 255.0f) * effIntensity;
            al.gNorm = (static_cast<float>(src.color.g) / 255.0f) * effIntensity;
            al.bNorm = (static_cast<float>(src.color.b) / 255.0f) * effIntensity;
            m_activeLights.push_back(al);
            };

        // 1. Culling AABB 2D de luces del mapa: solo incluimos las cuyo circulo de influencia toca la pantalla.
        for (const PointLight& ml : m_mapLights) {
            if (ml.position.x + ml.radius < minWX || ml.position.x - ml.radius > maxWX ||
                ml.position.y + ml.radius < minWY || ml.position.y - ml.radius > maxWY) {
                continue;
            }
            pushLight(ml);
        }

        // 2. Luz dinamica del jugador (antorcha tecla [T]) si esta encendida.
        if (dynamicTorch) {
            pushLight(*dynamicTorch);
        }
    }

    float LightingSystem::SampleHeightAt(float worldX, float worldY) const {
        int gx = static_cast<int>(std::floor((worldX - m_hfOriginX) * m_hfInvCellSize));
        int gy = static_cast<int>(std::floor((worldY - m_hfOriginY) * m_hfInvCellSize));
        if (gx < 0 || gx >= m_hfWidth || gy < 0 || gy >= m_hfHeight) {
            return 0.0f;
        }
        return m_heightfield[static_cast<size_t>(gy) * static_cast<size_t>(m_hfWidth) + static_cast<size_t>(gx)];
    }

    float LightingSystem::SampleSunShadow(Vec3 worldPt) const {
        // Descarte rapido si las sombras estan apagadas, no hay Heightfield o es plena penumbra de horizonte.
        if (!m_shadowsEnabled || m_shadowRaySteps <= 0 || m_heightfield.empty() || m_sunState.lightIntensity < 0.05f) {
            return 1.0f;
        }

        const Vec3& dir = m_sunState.lightDir;
        float horizLenSq = dir.x * dir.x + dir.y * dir.y;
        if (horizLenSq < 1e-4f) {
            return 1.0f; // Luz exactamente vertical desde el cenit: no proyecta sombra lateral.
        }

        // Normalizamos la proyeccion horizontal del rayo para avanzar pasos uniformes en el plano XY,
        // y calculamos cuanto sube el rayo en Z por cada unidad horizontal recorrida (dzPerUnit).
        float invHoriz = 1.0f / std::sqrt(horizLenSq);
        float stepDirX = dir.x * invHoriz;
        float stepDirY = dir.y * invHoriz;
        float dzPerUnit = dir.z * invHoriz;

        // Raymarching 2D acotado (maximo 14 pasos x 0.65u = ~9.8u de alcance de sombra):
        // kStartDist y kSelfBias evitan el "shadow acne" (que un objeto se proyecte sombra sobre si mismo).
        constexpr int   kMaxSteps = 14;
        constexpr float kStepSize = 0.65f;
        constexpr float kStartDist = 0.70f;
        constexpr float kSelfBias = 0.35f;

        for (int s = 0; s < m_shadowRaySteps; ++s) {
            float dist = kStartDist + static_cast<float>(s) * kStepSize;
            float rayZ = worldPt.z + dist * dzPerUnit + kSelfBias;

            // Salida temprana: si el rayo ya supera el techo maximo del mapa, ningun objeto puede taparlo.
            if (rayZ > m_hfMaxWorldZ) {
                return 1.0f;
            }

            // Si la columna del mapa en (sx, sy) es mas alta que el rayo, la luz directa esta bloqueada.
            if (SampleHeightAt(worldPt.x + stepDirX * dist, worldPt.y + stepDirY * dist) > rayZ) {
                return m_sunState.shadowDarkness;
            }
        }
        return 1.0f;
    }

    ShadedAtom LightingSystem::ShadeAtom(Vec3 worldPt, Vec3 normal, float ao,
        const MaterialSystem& materials, MaterialId matId,
        bool receiveShadows, char structuralOverride) const {
        // 1. Iluminacion directa Lambertiana (max(0, Dot(N, L))) modulada por sombras 2.5D.
        //    Optimizacion: solo trazamos el rayo de sombra si la cara realmente mira hacia la luz (ndotl > 0.06).
        float ndotl = std::max(0.0f, MathIso::Dot(normal, m_sunState.lightDir));
        float shadowFactor = (receiveShadows && ndotl > 0.06f) ? SampleSunShadow(worldPt) : 1.0f;
        float sunContrib = ndotl * m_sunState.lightIntensity * shadowFactor;

        // Acumulador de energia luminica en espacio RGB normalizado [0..1+].
        float lightR = sunContrib * (static_cast<float>(m_sunState.lightColor.r) / 255.0f);
        float lightG = sunContrib * (static_cast<float>(m_sunState.lightColor.g) / 255.0f);
        float lightB = sunContrib * (static_cast<float>(m_sunState.lightColor.b) / 255.0f);

        // 2. Iluminacion Global Hemisferica (GI):
        //    Mapeamos normal.z de [-1..+1] a [0..1] (upFactor) para mezclar rebote del suelo con cielo.
        //    El factor (0.45 + 0.85 * upFactor) mantiene volumen 3D legible incluso bajo lluvia o crepusculo.
        float upFactor = (normal.z * 0.5f) + 0.5f;
        ColorRGBA hemisCol = MathIso::LerpColor(m_sunState.groundAmbient, m_sunState.skyAmbient, upFactor);
        float ambEnergy = m_sunState.ambientStrength * (0.45f + 0.85f * upFactor) * ao;

        lightR += (static_cast<float>(hemisCol.r) / 255.0f) * ambEnergy;
        lightG += (static_cast<float>(hemisCol.g) / 255.0f) * ambEnergy;
        lightB += (static_cast<float>(hemisCol.b) / 255.0f) * ambEnergy;

        // 3. Acumulacion de luces puntuales activas en el frustum (estaticas + antorcha del jugador).
        float pointEnergy = 0.0f;
        for (const ActiveFrameLight& pl : m_activeLights) {
            float dx = pl.position.x - worldPt.x;
            float dy = pl.position.y - worldPt.y;
            float dz = pl.position.z - worldPt.z;
            float distSq = dx * dx + dy * dy + dz * dz;

            // Descarte rapido sin raiz cuadrada si el atomo esta fuera del radio de la luz.
            if (distSq >= pl.radiusSq) continue;

            float dist = std::sqrt(distSq);
            float invDist = 1.0f / (dist + 0.0001f);
            // Wrap Lambert minimo de 0.20f para que las antorchas iluminen suavemente bordes laterales.
            float pNdotL = std::max(0.20f, (normal.x * dx + normal.y * dy + normal.z * dz) * invDist);
            // Atenuacion cuadratica suave: (1 - d / R)^2 vale 1 en el centro y cae a 0 con derivada 0 en R.
            float falloff = 1.0f - (dist * pl.invRadius);
            float atten = falloff * falloff * pNdotL;

            lightR += pl.rNorm * atten;
            lightG += pl.gNorm * atten;
            lightB += pl.bNorm * atten;
            pointEnergy += pl.intensity * atten;
        }

        // 4. Luminancia escalar con curva de contraste centrada en 0.45 (pendiente 1.32x):
        //    Separa claramente las 3 paradas del material (dark -> normal -> lit) y evita tonos lavados.
        float relief = (upFactor * 0.24f + sunContrib * 0.85f + pointEnergy * 0.90f + ambEnergy * 0.65f) * ao;
        float luma = MathIso::Clamp((relief - 0.45f) * 1.32f + 0.45f, 0.0f, 1.0f);

        char glyph = (structuralOverride != '\0') ? structuralOverride : materials.SampleGlyph(matId, luma);
        ColorRGBA baseCol = materials.SampleColor(matId, luma);

        // 5. Acumulacion de nieve blanca sobre caras que miran hacia el cielo (normal.z > 0.52).
        if (m_weatherMod.snowCoverage > 0.05f && normal.z > 0.52f) {
            float snowBlend = m_weatherMod.snowCoverage * MathIso::Clamp((normal.z - 0.52f) / 0.35f, 0.0f, 1.0f);
            baseCol = MathIso::LerpColor(baseCol, { 232, 240, 250, 255 }, snowBlend * 0.85f);
        }

        // 6. Separacion de Cromaticidad y Exposicion:
        //    Como baseCol ya fue oscurecido por SampleColor(luma), multiplicar directamente por lightRGB
        //    aplicaria un "doble oscurecimiento" que volveria grises los objetos en tormenta o crepusculo.
        //    Normalizando por maxLight extraemos el tinte cromatico puro de la luz (55% de influencia)
        //    y acotamos la exposicion a [0.56 .. 1.14] para preservar la identidad del material.
        float maxLight = std::max({ lightR, lightG, lightB, 0.001f });
        float chromaR = MathIso::Lerp(1.0f, lightR / maxLight, 0.55f);
        float chromaG = MathIso::Lerp(1.0f, lightG / maxLight, 0.55f);
        float chromaB = MathIso::Lerp(1.0f, lightB / maxLight, 0.55f);
        float exposure = MathIso::Clamp(0.56f + maxLight * 0.56f, 0.56f, 1.14f);

        return {
            glyph,
            {
                ToByte(static_cast<float>(baseCol.r) * chromaR * exposure),
                ToByte(static_cast<float>(baseCol.g) * chromaG * exposure),
                ToByte(static_cast<float>(baseCol.b) * chromaB * exposure),
                255
            }
        };
    }

    ShadedAtom LightingSystem::ShadeGroundPoint(Vec3 worldPt, float terrainNoise, bool receiveShadows) const {
        // Normal fija hacia arriba (0, 0, 1): Dot(normal, lightDir) se reduce directamente a lightDir.z.
        float sunNdotL = std::max(0.0f, m_sunState.lightDir.z);
        float shadowFactor = (receiveShadows && sunNdotL > 0.05f) ? SampleSunShadow(worldPt) : 1.0f;

        // Acumulamos energia de luces puntuales y antorcha proyectadas sobre el suelo.
        float ptR = 0.0f, ptG = 0.0f, ptB = 0.0f, pointBoost = 0.0f;
        for (const ActiveFrameLight& pl : m_activeLights) {
            float dx = pl.position.x - worldPt.x;
            float dy = pl.position.y - worldPt.y;
            float dz = pl.position.z - worldPt.z;
            float distSq = dx * dx + dy * dy + dz * dz;
            if (distSq >= pl.radiusSq) continue;

            float dist = std::sqrt(distSq);
            float falloff = 1.0f - (dist * pl.invRadius);
            float atten = falloff * falloff;

            ptR += pl.rNorm * atten;
            ptG += pl.gNorm * atten;
            ptB += pl.bNorm * atten;
            pointBoost += pl.intensity * atten;
        }

        // CASO 1: Sombra proyectada continua (se desvanece suavemente a 0 cuando el Sol/Luna cruzan el horizonte).
        float dirVisibility = Smoothstep(0.04f, 0.24f, m_sunState.lightIntensity);
        float castStrength = (1.0f - shadowFactor) * dirVisibility;

        if (castStrength > 0.14f && pointBoost < 0.28f) {
            const ColorRGBA& bg = m_sunState.backgroundColor;
            float shadowScale = MathIso::Lerp(0.88f, 0.36f, castStrength);
            float shadeR = static_cast<float>(bg.r) * shadowScale + ptR * 55.0f;
            float shadeG = static_cast<float>(bg.g) * shadowScale + ptG * 50.0f;
            float shadeB = static_cast<float>(bg.b) * (shadowScale + 0.06f) + ptB * 48.0f;

            // Seleccionamos el glifo de sombra definido por el JSON del bioma activo.
            uint8_t sCount = std::max<uint8_t>(1, m_weatherMod.shadowGlyphCount);
            uint8_t sIdx = std::min<uint8_t>(static_cast<uint8_t>(terrainNoise * sCount), sCount - 1);
            return { m_weatherMod.shadowGlyphs[sIdx],{ ToByte(shadeR), ToByte(shadeG), ToByte(shadeB), 255 } };
        }

        // CASO 2: Suelo libre — densidad data-driven del bioma (sube hasta 82% cuando nieva).
        float effDensity = MathIso::Lerp(m_weatherMod.groundDensity, 0.82f, m_weatherMod.snowCoverage);
        float skipThreshold = 1.0f - effDensity;
        // Las celdas iluminadas por una fogata o antorcha (pointBoost >= 0.10) siempre se dibujan.
        if (pointBoost < 0.10f && terrainNoise < skipThreshold) {
            return { '\0',{ 0, 0, 0, 0 } };
        }

        float sunContrib = sunNdotL * m_sunState.lightIntensity * shadowFactor;
        float lightR = sunContrib * (static_cast<float>(m_sunState.lightColor.r) / 255.0f) + ptR * 0.90f;
        float lightG = sunContrib * (static_cast<float>(m_sunState.lightColor.g) / 255.0f) + ptG * 0.90f;
        float lightB = sunContrib * (static_cast<float>(m_sunState.lightColor.b) / 255.0f) + ptB * 0.90f;

        float amb = m_sunState.ambientStrength * 0.78f;
        lightR += (static_cast<float>(m_sunState.skyAmbient.r) / 255.0f) * amb;
        lightG += (static_cast<float>(m_sunState.skyAmbient.g) / 255.0f) * amb;
        lightB += (static_cast<float>(m_sunState.skyAmbient.b) / 255.0f) * amb;

        // Mezclamos la paleta base/alterna del bioma con nieve blanca segun snowCoverage.
        ColorRGBA biomeCol = MathIso::LerpColor(m_weatherMod.biomeAltGroundColor, m_weatherMod.biomeGroundColor, terrainNoise);
        ColorRGBA gnd = MathIso::LerpColor(biomeCol, { 220, 232, 245, 255 }, m_weatherMod.snowCoverage);

        char glyph = '.';
        if (pointBoost > 0.55f) {
            glyph = '+'; // Brillo intenso justo debajo de una fogata o antorcha.
        }
        else if (m_weatherMod.snowCoverage > 0.55f) {
            glyph = (terrainNoise > 0.78f) ? '*' : ((terrainNoise > 0.45f) ? ':' : '.');
        }
        else {
            // Reescalamos terrainNoise de [skipThreshold..1] a [0..1] para aprovechar toda la rampa del bioma.
            uint8_t gCount = std::max<uint8_t>(1, m_weatherMod.groundGlyphCount);
            float   normT = (effDensity > 0.01f) ? MathIso::Clamp((terrainNoise - skipThreshold) / effDensity, 0.0f, 0.999f) : terrainNoise;
            uint8_t gIdx = std::min<uint8_t>(static_cast<uint8_t>(normT * gCount), gCount - 1);
            glyph = m_weatherMod.groundGlyphs[gIdx];
        }

        return {
            glyph,
            {
                ToByte(static_cast<float>(gnd.r) * MathIso::Clamp(0.22f + lightR * 0.92f, 0.22f, 1.15f)),
                ToByte(static_cast<float>(gnd.g) * MathIso::Clamp(0.22f + lightG * 0.92f, 0.22f, 1.15f)),
                ToByte(static_cast<float>(gnd.b) * MathIso::Clamp(0.22f + lightB * 0.92f, 0.22f, 1.15f)),
                255
            }
        };
    }

    ShadedAtom LightingSystem::ShadeSurfacePoint(Vec3 worldPt, const SurfaceZone& zone,
        float noise, bool receiveShadows) const {
        float sunNdotL = std::max(0.0f, m_sunState.lightDir.z);
        float shadowFactor = (receiveShadows && sunNdotL > 0.05f) ? SampleSunShadow(worldPt) : 1.0f;

        // 1. Evaluacion del patron arquitectonico modular del objeto de suelo (frecuencia = 2 celdas/unidad).
        bool   useSecondary = false;
        size_t glyphIdx = 0;
        const size_t gLen = std::max<size_t>(1, zone.glyphs.size());

        int ix = static_cast<int>(std::floor(worldPt.x * 2.0f));
        int iy = static_cast<int>(std::floor(worldPt.y * 2.0f));

        switch (zone.pattern) {
        case SurfacePattern::PLANKS_X:
            // Alterna tablas claras y oscuras a lo largo de Y (ideal para puentes orientados en Y).
            useSecondary = ((iy & 1) == 0);
            glyphIdx = static_cast<size_t>(std::abs(iy)) % gLen;
            break;
        case SurfacePattern::PLANKS_Y:
            useSecondary = ((ix & 1) == 0);
            glyphIdx = static_cast<size_t>(std::abs(ix)) % gLen;
            break;
        case SurfacePattern::GRID:
            // Dibuja juntas cada 3 celdas (baldosas de piedra o paneles metalicos sci-fi).
            useSecondary = ((std::abs(ix) % 3 == 0) || (std::abs(iy) % 3 == 0));
            glyphIdx = useSecondary ? (gLen - 1) : (static_cast<size_t>(noise * gLen) % gLen);
            break;
        case SurfacePattern::CHECKER:
            useSecondary = (((ix + iy) & 1) == 0);
            glyphIdx = useSecondary ? 0 : std::min<size_t>(1, gLen - 1);
            break;
        default:
            useSecondary = (noise > 0.5f);
            glyphIdx = std::min<size_t>(static_cast<size_t>(noise * gLen), gLen - 1);
            break;
        }

        // 2. Color base de la superficie + ligera acumulacion de escarcha/nieve en ventiscas.
        ColorRGBA baseCol = useSecondary ? zone.secondaryColor : zone.primaryColor;
        if (m_weatherMod.snowCoverage > 0.10f) {
            baseCol = MathIso::LerpColor(baseCol, { 215, 226, 238, 255 }, m_weatherMod.snowCoverage * 0.55f);
        }

        // 3. Iluminacion combinada: Sol/Luna + GI del cielo + luces puntuales activas.
        float sunContrib = sunNdotL * m_sunState.lightIntensity * shadowFactor;
        float lightR = sunContrib * (static_cast<float>(m_sunState.lightColor.r) / 255.0f);
        float lightG = sunContrib * (static_cast<float>(m_sunState.lightColor.g) / 255.0f);
        float lightB = sunContrib * (static_cast<float>(m_sunState.lightColor.b) / 255.0f);

        float amb = m_sunState.ambientStrength * 0.85f;
        lightR += (static_cast<float>(m_sunState.skyAmbient.r) / 255.0f) * amb;
        lightG += (static_cast<float>(m_sunState.skyAmbient.g) / 255.0f) * amb;
        lightB += (static_cast<float>(m_sunState.skyAmbient.b) / 255.0f) * amb;

        for (const ActiveFrameLight& pl : m_activeLights) {
            float dx = pl.position.x - worldPt.x;
            float dy = pl.position.y - worldPt.y;
            float dz = pl.position.z - worldPt.z;
            float distSq = dx * dx + dy * dy + dz * dz;
            if (distSq >= pl.radiusSq) continue;

            float dist = std::sqrt(distSq);
            float falloff = 1.0f - (dist * pl.invRadius);
            float atten = falloff * falloff;

            lightR += pl.rNorm * atten;
            lightG += pl.gNorm * atten;
            lightB += pl.bNorm * atten;
        }

        return {
            zone.glyphs[glyphIdx],
            {
                ToByte(static_cast<float>(baseCol.r) * MathIso::Clamp(0.16f + lightR * 0.90f, 0.16f, 1.12f)),
                ToByte(static_cast<float>(baseCol.g) * MathIso::Clamp(0.16f + lightG * 0.90f, 0.16f, 1.12f)),
                ToByte(static_cast<float>(baseCol.b) * MathIso::Clamp(0.16f + lightB * 0.90f, 0.16f, 1.12f)),
                255
            }
        };
    }

    ColorRGBA LightingSystem::ShadeFoliageColor(Vec3 worldPt, ColorRGBA baseColor, bool receiveShadows) const {
        // Sombreado optimizado para cientos de briznas de pasto/flores por frame.
        float sunNdotL = std::max(0.0f, m_sunState.lightDir.z);
        float shadowFactor = (receiveShadows && sunNdotL > 0.05f) ? SampleSunShadow(worldPt) : 1.0f;

        if (m_weatherMod.snowCoverage > 0.15f) {
            baseColor = MathIso::LerpColor(baseColor, { 222, 234, 246, 255 }, m_weatherMod.snowCoverage * 0.65f);
        }

        float sunContrib = sunNdotL * m_sunState.lightIntensity * shadowFactor;
        float lightR = sunContrib * (static_cast<float>(m_sunState.lightColor.r) / 255.0f);
        float lightG = sunContrib * (static_cast<float>(m_sunState.lightColor.g) / 255.0f);
        float lightB = sunContrib * (static_cast<float>(m_sunState.lightColor.b) / 255.0f);

        float amb = m_sunState.ambientStrength * 0.90f;
        lightR += (static_cast<float>(m_sunState.skyAmbient.r) / 255.0f) * amb;
        lightG += (static_cast<float>(m_sunState.skyAmbient.g) / 255.0f) * amb;
        lightB += (static_cast<float>(m_sunState.skyAmbient.b) / 255.0f) * amb;

        for (const ActiveFrameLight& pl : m_activeLights) {
            float dx = pl.position.x - worldPt.x;
            float dy = pl.position.y - worldPt.y;
            float dz = pl.position.z - worldPt.z;
            float distSq = dx * dx + dy * dy + dz * dz;
            if (distSq >= pl.radiusSq) continue;

            float dist = std::sqrt(distSq);
            float falloff = 1.0f - (dist * pl.invRadius);
            float atten = falloff * falloff;

            lightR += pl.rNorm * atten;
            lightG += pl.gNorm * atten;
            lightB += pl.bNorm * atten;
        }

        return {
            ToByte(static_cast<float>(baseColor.r) * MathIso::Clamp(0.18f + lightR * 0.92f, 0.18f, 1.15f)),
            ToByte(static_cast<float>(baseColor.g) * MathIso::Clamp(0.18f + lightG * 0.92f, 0.18f, 1.15f)),
            ToByte(static_cast<float>(baseColor.b) * MathIso::Clamp(0.18f + lightB * 0.92f, 0.18f, 1.15f)),
            255
        };
    }

    ShadedAtom LightingSystem::ShadeWaterPoint(Vec3 worldPt, ColorRGBA deepCol, ColorRGBA shallowCol,
        float depthFactor, float waveSpeed, bool receiveShadows) const {
        float sunNdotL = std::max(0.0f, m_sunState.lightDir.z);
        float shadowFactor = (receiveShadows && sunNdotL > 0.05f) ? SampleSunShadow(worldPt) : 1.0f;

        // 1. Interferencia de dos ondas viajeras diagonales en sentidos opuestos -> oleaje organico [-1..+1].
        float w1 = std::sin(worldPt.x * 1.15f + worldPt.y * 0.65f + m_elapsedTime * waveSpeed);
        float w2 = std::cos(worldPt.x * 0.55f - worldPt.y * 1.35f - m_elapsedTime * (waveSpeed * 0.75f));
        float wave = (w1 + w2) * 0.5f;

        // 2. Color base interpolado entre orilla somera (shallowCol) y centro hondo (deepCol),
        //    mas un 22% de reflejo del cielo ambiental del ciclo dia-noche.
        ColorRGBA waterCol = MathIso::LerpColor(shallowCol, deepCol, depthFactor);
        waterCol = MathIso::LerpColor(waterCol, m_sunState.skyAmbient, 0.22f);

        float sunContrib = sunNdotL * m_sunState.lightIntensity * shadowFactor;
        float lightR = sunContrib * (static_cast<float>(m_sunState.lightColor.r) / 255.0f) + 0.24f;
        float lightG = sunContrib * (static_cast<float>(m_sunState.lightColor.g) / 255.0f) + 0.26f;
        float lightB = sunContrib * (static_cast<float>(m_sunState.lightColor.b) / 255.0f) + 0.32f;

        // 3. Reflejos de luces puntuales cercanas modulados por la pendiente de la ola (efecto titileo).
        float ptBoost = 0.0f;
        for (const ActiveFrameLight& pl : m_activeLights) {
            float dx = pl.position.x - worldPt.x;
            float dy = pl.position.y - worldPt.y;
            float dz = pl.position.z - worldPt.z;
            float distSq = dx * dx + dy * dy + dz * dz;
            if (distSq >= pl.radiusSq) continue;

            float dist = std::sqrt(distSq);
            float falloff = 1.0f - (dist * pl.invRadius);
            float atten = falloff * falloff * (0.85f + 0.35f * wave);

            lightR += pl.rNorm * atten;
            lightG += pl.gNorm * atten;
            lightB += pl.bNorm * atten;
            ptBoost += pl.intensity * atten;
        }

        // 4. Destello especular en la cresta de las olas (wave > 0.62) iluminado por Sol o antorchas.
        float glint = (wave > 0.62f) ? ((wave - 0.62f) / 0.38f) : 0.0f;
        float specBoost = glint * (sunContrib * 0.75f + ptBoost * 0.55f);

        // 5. Seleccion de glifo segun profundidad (orilla) y altura instantanea de la onda.
        char glyph = '~';
        if (depthFactor < 0.22f)    glyph = (wave > 0.0f) ? '-' : '.'; // Espuma suave en la costa.
        else if (specBoost > 0.38f) glyph = '+';                       // Destello especular en cresta.
        else if (wave > 0.15f)      glyph = '~';                       // Cresta de onda.
        else if (wave > -0.35f)     glyph = '=';                       // Cuerpo de agua en reposo.
        else                        glyph = '-';                       // Valle entre olas.

        return {
            glyph,
            {
                ToByte(static_cast<float>(waterCol.r) * MathIso::Clamp(lightR, 0.16f, 1.2f) + specBoost * 115.0f),
                ToByte(static_cast<float>(waterCol.g) * MathIso::Clamp(lightG, 0.16f, 1.2f) + specBoost * 125.0f),
                ToByte(static_cast<float>(waterCol.b) * MathIso::Clamp(lightB, 0.18f, 1.2f) + specBoost * 135.0f),
                255
            }
        };
    }

    size_t LightingSystem::GetHeightfieldBytes() const {
        return m_heightfield.capacity() * sizeof(float);
    }

} // namespace AsciiEngine