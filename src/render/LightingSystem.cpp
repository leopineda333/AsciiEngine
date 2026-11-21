/**
 * @file LightingSystem.cpp
 * @brief Implementacion de iluminacion dinamica, sombras 2.5D, nieve climatica y agua animada.
 */
#include <AsciiEngine/LightingSystem.hpp>
#include <AsciiEngine/MathIso.hpp>
#include <AsciiEngine/MaterialSystem.hpp>
#include <AsciiEngine/World.hpp>
#include "raylib.h"

#include <algorithm>
#include <cmath>

namespace AsciiEngine {

    namespace {

        constexpr float TWO_PI_F = 6.28318530717958647692f;

        inline uint8_t ToByte(float v) {
            return static_cast<uint8_t>(MathIso::Clamp(v, 0.0f, 255.0f));
        }

        // Curva Hermite C1 continua para transiciones sin saltos en amanecer, ocaso y sombras.
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

        m_activeLights.reserve(m_mapLights.size() + 4);
        RecomputeSunState();
    }

    void LightingSystem::BuildShadowHeightfield(const World& world, const MaterialSystem& materials, float cellSizeWorld) {
        // Celdas de 0.6u capturan troncos delgados sin disparar consumo de memoria (~750 KB).
        m_hfCellSize = std::max(0.35f, cellSizeWorld);
        m_hfInvCellSize = 1.0f / m_hfCellSize;

        const float halfSize = world.GetWorldHalfSize() + 4.0f;
        m_hfOriginX = -halfSize;
        m_hfOriginY = -halfSize;
        m_hfWidth = static_cast<int>(std::ceil((halfSize * 2.0f) * m_hfInvCellSize));
        m_hfHeight = m_hfWidth;
        m_hfMaxWorldZ = 0.0f;

        m_heightfield.assign(static_cast<size_t>(m_hfWidth) * static_cast<size_t>(m_hfHeight), 0.0f);

        for (const MapInstance& inst : world.GetInstances()) {
            const BakedPrefab* prefab = world.GetPrefab(inst.prefabId);
            if (!prefab) continue;

            for (const BakedAtom& a : prefab->atoms) {
                MaterialId matId = (inst.overrideMaterialId != FALLBACK_MATERIAL_ID)
                    ? inst.overrideMaterialId
                    : a.materialId;
                if (!materials.Get(matId).occludesLight) continue;

                float sx = a.localPos.x * inst.scale.x;
                float sy = a.localPos.y * inst.scale.y;
                float wx = inst.position.x + (sx * inst.cosRot - sy * inst.sinRot);
                float wy = inst.position.y + (sx * inst.sinRot + sy * inst.cosRot);
                float wz = inst.position.z + (a.localPos.z * inst.scale.z);

                int gx = static_cast<int>(std::floor((wx - m_hfOriginX) * m_hfInvCellSize));
                int gy = static_cast<int>(std::floor((wy - m_hfOriginY) * m_hfInvCellSize));

                if (gx >= 0 && gx < m_hfWidth && gy >= 0 && gy < m_hfHeight) {
                    size_t idx = static_cast<size_t>(gy) * static_cast<size_t>(m_hfWidth) + static_cast<size_t>(gx);
                    if (wz > m_heightfield[idx]) m_heightfield[idx] = wz;
                    if (wz > m_hfMaxWorldZ)      m_hfMaxWorldZ = wz;
                }
            }
        }

        TraceLog(LOG_INFO, "LIGHTING: Heightfield 2D (%dx%d, %.1f KB, maxZ=%.1f) construido.",
            m_hfWidth, m_hfHeight, static_cast<float>(GetHeightfieldBytes()) / 1024.0f, m_hfMaxWorldZ);
    }

    void LightingSystem::AdjustTimeHours(float deltaHours) {
        m_dayNight.currentTimeHours = std::fmod(m_dayNight.currentTimeHours + deltaHours, 24.0f);
        if (m_dayNight.currentTimeHours < 0.0f) m_dayNight.currentTimeHours += 24.0f;
        RecomputeSunState();
    }

    void LightingSystem::Update(float dt, const WeatherModifiers& weatherMod) {
        m_elapsedTime += dt;
        m_weatherMod = weatherMod;

        if (m_dayNight.enabled && m_dayNight.dayLengthSeconds > 1.0f) {
            float hoursPerSec = 24.0f / m_dayNight.dayLengthSeconds;
            m_dayNight.currentTimeHours = std::fmod(m_dayNight.currentTimeHours + hoursPerSec * dt, 24.0f);
        }

        RecomputeSunState();
    }

    void LightingSystem::RecomputeSunState() {
        float theta = ((m_dayNight.currentTimeHours - 6.0f) / 24.0f) * TWO_PI_F;
        float sinT = std::sin(theta);
        float cosT = std::cos(theta);

        // Factores 100% continuos en todo el ciclo de 24h (eliminan saltos en 6:00 y 18:00).
        float dayBlend = Smoothstep(-0.22f, 0.26f, sinT);              // 0 noche profunda -> 1 dia pleno.
        float twilightBand = 4.0f * dayBlend * (1.0f - dayBlend);          // Pico 1.0 justo en amanecer/ocaso.
        float sunWeight = Smoothstep(0.02f, 0.32f, sinT);             // 0.0 estricto al cruzar el horizonte.
        float moonWeight = Smoothstep(0.02f, 0.32f, -sinT);             // 0.0 estricto al cruzar el horizonte.

        m_sunState.isNight = (sinT < 0.0f);

        // La direccion cambia justo cuando sunWeight == 0 y moonWeight == 0 (cero teletransporte visual).
        if (sinT >= 0.0f) {
            float sunElev = MathIso::Clamp(sinT, 0.20f, 1.0f);
            m_sunState.lightDir = MathIso::Normalize({ cosT, 0.35f, sunElev });

            const ColorRGBA sunHorizon = { 255, 162, 92, 255 };
            const ColorRGBA sunNoon = { 255, 248, 236, 255 };
            m_sunState.lightColor = MathIso::LerpColor(sunHorizon, sunNoon, Smoothstep(0.05f, 0.45f, sinT));
            m_sunState.lightIntensity = sunWeight * 0.92f * m_weatherMod.sunMultiplier;
        }
        else {
            float moonElev = MathIso::Clamp(-sinT, 0.26f, 1.0f);
            m_sunState.lightDir = MathIso::Normalize({ -cosT, 0.35f, moonElev });
            m_sunState.lightColor = { 130, 168, 230, 255 };
            m_sunState.lightIntensity = moonWeight * 0.34f * m_weatherMod.sunMultiplier;
        }

        // Fondo continuo estilo terminal oscura (noche casi negra -> crepusculo sutil -> dia gris carbon).
        const ColorRGBA bgNight = { 6,  8, 12, 255 };
        const ColorRGBA bgTwilight = { 16, 14, 20, 255 };
        const ColorRGBA bgDay = { 22, 25, 29, 255 };

        ColorRGBA bg = MathIso::LerpColor(bgNight, bgDay, dayBlend);
        bg = MathIso::LerpColor(bg, bgTwilight, twilightBand * 0.65f);

        // Tinte climatico escalado continuamente con dayBlend (corrige el salto brusco en dia 2/3 con lluvia).
        if (m_weatherMod.bgTintBlend > 0.01f) {
            float tintScale = MathIso::Lerp(0.32f, 1.0f, dayBlend);
            ColorRGBA scaledTint = {
                ToByte(static_cast<float>(m_weatherMod.bgTint.r) * tintScale),
                ToByte(static_cast<float>(m_weatherMod.bgTint.g) * tintScale),
                ToByte(static_cast<float>(m_weatherMod.bgTint.b) * MathIso::Lerp(0.42f, 1.0f, dayBlend)),
                255
            };
            bg = MathIso::LerpColor(bg, scaledTint, m_weatherMod.bgTintBlend);
        }

        // Iluminacion hemisferica continua (cielo vs suelo) con matiz calido en amanecer/atardecer.
        ColorRGBA skyNight = { 32, 48, 82, 255 };
        ColorRGBA skyTwil = { 175, 118, 102, 255 };
        ColorRGBA skyCol = MathIso::LerpColor(skyNight, m_giConfig.skyColor, dayBlend);
        skyCol = MathIso::LerpColor(skyCol, skyTwil, twilightBand * 0.55f);

        ColorRGBA gndNight = { 14, 18, 24, 255 };
        ColorRGBA gndCol = MathIso::LerpColor(gndNight, m_giConfig.groundColor, dayBlend);

        // Contribucion continua de nieve (sin rama booleana isNight).
        if (m_weatherMod.snowCoverage > 0.05f) {
            ColorRGBA snowBg = MathIso::LerpColor({ 12, 16, 24, 255 }, { 34, 40, 48, 255 }, dayBlend);
            bg = MathIso::LerpColor(bg, snowBg, m_weatherMod.snowCoverage * 0.45f);
            gndCol = MathIso::LerpColor(gndCol, { 118, 134, 155, 255 }, m_weatherMod.snowCoverage * 0.55f);
        }

        m_sunState.backgroundColor = bg;
        m_sunState.skyAmbient = skyCol;
        m_sunState.groundAmbient = gndCol;
        m_sunState.ambientStrength = m_giConfig.ambientStrength * MathIso::Lerp(0.42f, 0.72f, dayBlend) * m_weatherMod.ambientMultiplier;
        m_sunState.shadowDarkness = MathIso::Lerp(0.55f, 0.25f, dayBlend);
    }

    void LightingSystem::PrepareFrameLights(float minWX, float maxWX, float minWY, float maxWY,
        const PointLight* dynamicTorch) {
        m_activeLights.clear();

        auto pushLight = [&](const PointLight& src) {
            if (src.radius <= 0.1f || src.intensity <= 0.01f) return;

            float effIntensity = src.intensity;
            float effRadius = src.radius;

            if (src.flicker) {
                float f1 = std::sin(m_elapsedTime * 10.5f + src.flickerPhase);
                float f2 = std::cos(m_elapsedTime * 19.3f + src.flickerPhase * 1.7f);
                float mod = 1.0f + (f1 * 0.08f + f2 * 0.04f);
                effIntensity *= mod;
                effRadius *= (0.96f + mod * 0.04f);
            }

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

        for (const PointLight& ml : m_mapLights) {
            if (ml.position.x + ml.radius < minWX || ml.position.x - ml.radius > maxWX ||
                ml.position.y + ml.radius < minWY || ml.position.y - ml.radius > maxWY) {
                continue;
            }
            pushLight(ml);
        }

        if (dynamicTorch) pushLight(*dynamicTorch);
    }

    float LightingSystem::SampleHeightAt(float worldX, float worldY) const {
        int gx = static_cast<int>(std::floor((worldX - m_hfOriginX) * m_hfInvCellSize));
        int gy = static_cast<int>(std::floor((worldY - m_hfOriginY) * m_hfInvCellSize));
        if (gx < 0 || gx >= m_hfWidth || gy < 0 || gy >= m_hfHeight) return 0.0f;
        return m_heightfield[static_cast<size_t>(gy) * static_cast<size_t>(m_hfWidth) + static_cast<size_t>(gx)];
    }

    float LightingSystem::SampleSunShadow(Vec3 worldPt) const {
        if (!m_shadowsEnabled || m_heightfield.empty() || m_sunState.lightIntensity < 0.05f) {
            return 1.0f;
        }

        const Vec3& dir = m_sunState.lightDir;
        float horizLenSq = dir.x * dir.x + dir.y * dir.y;
        if (horizLenSq < 1e-4f) return 1.0f;

        float invHoriz = 1.0f / std::sqrt(horizLenSq);
        float stepDirX = dir.x * invHoriz;
        float stepDirY = dir.y * invHoriz;
        float dzPerUnit = dir.z * invHoriz;

        // Paso fino de 0.65u (14 pasos max) para capturar troncos y proyectar sombras continuas.
        constexpr int   kMaxSteps = 14;
        constexpr float kStepSize = 0.65f;
        constexpr float kStartDist = 0.70f;
        constexpr float kSelfBias = 0.35f;

        for (int s = 0; s < kMaxSteps; ++s) {
            float dist = kStartDist + static_cast<float>(s) * kStepSize;
            float rayZ = worldPt.z + dist * dzPerUnit + kSelfBias;
            if (rayZ > m_hfMaxWorldZ) return 1.0f;

            if (SampleHeightAt(worldPt.x + stepDirX * dist, worldPt.y + stepDirY * dist) > rayZ) {
                return m_sunState.shadowDarkness;
            }
        }
        return 1.0f;
    }

    ShadedAtom LightingSystem::ShadeAtom(Vec3 worldPt, Vec3 normal, float ao,
        const MaterialSystem& materials, MaterialId matId,
        bool receiveShadows, char structuralOverride) const {
        // 1. Luz directa Sol/Luna (Lambert) con sombras 2.5D.
        float ndotl = std::max(0.0f, MathIso::Dot(normal, m_sunState.lightDir));
        float shadowFactor = (receiveShadows && ndotl > 0.06f) ? SampleSunShadow(worldPt) : 1.0f;
        float sunContrib = ndotl * m_sunState.lightIntensity * shadowFactor;

        float lightR = sunContrib * (static_cast<float>(m_sunState.lightColor.r) / 255.0f);
        float lightG = sunContrib * (static_cast<float>(m_sunState.lightColor.g) / 255.0f);
        float lightB = sunContrib * (static_cast<float>(m_sunState.lightColor.b) / 255.0f);

        // 2. Relieve hemisferico (cielo arriba vs suelo abajo): mantiene volumen 3D incluso bajo lluvia o crepusculo.
        float upFactor = (normal.z * 0.5f) + 0.5f;
        ColorRGBA hemisCol = MathIso::LerpColor(m_sunState.groundAmbient, m_sunState.skyAmbient, upFactor);
        float ambEnergy = m_sunState.ambientStrength * (0.45f + 0.85f * upFactor) * ao;

        lightR += (static_cast<float>(hemisCol.r) / 255.0f) * ambEnergy;
        lightG += (static_cast<float>(hemisCol.g) / 255.0f) * ambEnergy;
        lightB += (static_cast<float>(hemisCol.b) / 255.0f) * ambEnergy;

        // 3. Luces puntuales activas.
        float pointEnergy = 0.0f;
        for (const ActiveFrameLight& pl : m_activeLights) {
            float dx = pl.position.x - worldPt.x;
            float dy = pl.position.y - worldPt.y;
            float dz = pl.position.z - worldPt.z;
            float distSq = dx * dx + dy * dy + dz * dz;
            if (distSq >= pl.radiusSq) continue;

            float dist = std::sqrt(distSq);
            float invDist = 1.0f / (dist + 0.0001f);
            float pNdotL = std::max(0.20f, (normal.x * dx + normal.y * dy + normal.z * dz) * invDist);
            float falloff = 1.0f - (dist * pl.invRadius);
            float atten = falloff * falloff * pNdotL;

            lightR += pl.rNorm * atten;
            lightG += pl.gNorm * atten;
            lightB += pl.bNorm * atten;
            pointEnergy += pl.intensity * atten;
        }

        // 4. Luminancia para elegir glifo y parada del material (dark -> normal -> lit).
        // Agregamos relieve direccional sutil por upFactor para que los objetos no se aplanen en crepusculo.
        float relief = (upFactor * 0.24f + sunContrib * 0.85f + pointEnergy * 0.90f + ambEnergy * 0.65f) * ao;
        float luma = MathIso::Clamp((relief - 0.45f) * 1.32f + 0.45f, 0.0f, 1.0f);

        char glyph = (structuralOverride != '\0') ? structuralOverride : materials.SampleGlyph(matId, luma);
        ColorRGBA baseCol = materials.SampleColor(matId, luma);

        // Nieve sobre caras superiores.
        if (m_weatherMod.snowCoverage > 0.05f && normal.z > 0.52f) {
            float snowBlend = m_weatherMod.snowCoverage * MathIso::Clamp((normal.z - 0.52f) / 0.35f, 0.0f, 1.0f);
            baseCol = MathIso::LerpColor(baseCol, { 232, 240, 250, 255 }, snowBlend * 0.85f);
        }

        // 5. Separamos cromaticidad de exposicion (evita doble oscurecimiento que mataba el color en lluvia/ocaso).
        float maxLight = std::max({ lightR, lightG, lightB, 0.001f });
        float chromaR = MathIso::Lerp(1.0f, lightR / maxLight, 0.55f);
        float chromaG = MathIso::Lerp(1.0f, lightG / maxLight, 0.55f);
        float chromaB = MathIso::Lerp(1.0f, lightB / maxLight, 0.55f);

        // Exposicion acotada [0.58 .. 1.12]: preserva la paleta real del material incluso de noche/tormenta.
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
        float sunNdotL = std::max(0.0f, m_sunState.lightDir.z);
        float shadowFactor = (receiveShadows && sunNdotL > 0.05f) ? SampleSunShadow(worldPt) : 1.0f;

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

        // Intensidad suave de sombra en suelo: se desvanece a 0 justo al cruzar el horizonte (sin parpadeos).
        float dirVisibility = Smoothstep(0.04f, 0.24f, m_sunState.lightIntensity);
        float castStrength = (1.0f - shadowFactor) * dirVisibility;

        if (castStrength > 0.14f && pointBoost < 0.28f) {
            const ColorRGBA& bg = m_sunState.backgroundColor;
            float shadowScale = MathIso::Lerp(0.88f, 0.36f, castStrength);
            float shadeR = static_cast<float>(bg.r) * shadowScale + ptR * 55.0f;
            float shadeG = static_cast<float>(bg.g) * shadowScale + ptG * 50.0f;
            float shadeB = static_cast<float>(bg.b) * (shadowScale + 0.06f) + ptB * 48.0f;

            uint8_t sCount = std::max<uint8_t>(1, m_weatherMod.shadowGlyphCount);
            uint8_t sIdx = std::min<uint8_t>(static_cast<uint8_t>(terrainNoise * sCount), sCount - 1);
            return { m_weatherMod.shadowGlyphs[sIdx],{ ToByte(shadeR), ToByte(shadeG), ToByte(shadeB), 255 } };
        }

        float effDensity = MathIso::Lerp(m_weatherMod.groundDensity, 0.82f, m_weatherMod.snowCoverage);
        float skipThreshold = 1.0f - effDensity;
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

        ColorRGBA biomeCol = MathIso::LerpColor(m_weatherMod.biomeAltGroundColor, m_weatherMod.biomeGroundColor, terrainNoise);
        ColorRGBA gnd = MathIso::LerpColor(biomeCol, { 220, 232, 245, 255 }, m_weatherMod.snowCoverage);

        char glyph = '.';
        if (pointBoost > 0.55f) {
            glyph = '+';
        }
        else if (m_weatherMod.snowCoverage > 0.55f) {
            glyph = (terrainNoise > 0.78f) ? '*' : ((terrainNoise > 0.45f) ? ':' : '.');
        }
        else {
            uint8_t gCount = std::max<uint8_t>(1, m_weatherMod.groundGlyphCount);
            float normT = (effDensity > 0.01f) ? MathIso::Clamp((terrainNoise - skipThreshold) / effDensity, 0.0f, 0.999f) : terrainNoise;
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

        // Evaluacion del patron modular del objeto de suelo (tablas, baldosas, rejillas).
        bool useSecondary = false;
        size_t glyphIdx = 0;
        const size_t gLen = std::max<size_t>(1, zone.glyphs.size());

        int ix = static_cast<int>(std::floor(worldPt.x * 2.0f));
        int iy = static_cast<int>(std::floor(worldPt.y * 2.0f));

        switch (zone.pattern) {
        case SurfacePattern::PLANKS_X:
            useSecondary = ((iy & 1) == 0);
            glyphIdx = static_cast<size_t>(std::abs(iy)) % gLen;
            break;
        case SurfacePattern::PLANKS_Y:
            useSecondary = ((ix & 1) == 0);
            glyphIdx = static_cast<size_t>(std::abs(ix)) % gLen;
            break;
        case SurfacePattern::GRID:
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

        ColorRGBA baseCol = useSecondary ? zone.secondaryColor : zone.primaryColor;
        if (m_weatherMod.snowCoverage > 0.10f) {
            baseCol = MathIso::LerpColor(baseCol, { 215, 226, 238, 255 }, m_weatherMod.snowCoverage * 0.55f);
        }

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

        // Onda dual sobre la superficie del agua para animar glifos (~, =, -) y destellos especulares.
        float w1 = std::sin(worldPt.x * 1.15f + worldPt.y * 0.65f + m_elapsedTime * waveSpeed);
        float w2 = std::cos(worldPt.x * 0.55f - worldPt.y * 1.35f - m_elapsedTime * (waveSpeed * 0.75f));
        float wave = (w1 + w2) * 0.5f; // [-1 .. +1]

        // Color base interpolado entre costa poco profunda y centro hondo.
        ColorRGBA waterCol = MathIso::LerpColor(shallowCol, deepCol, depthFactor);

        // El agua refleja sutilmente el cielo ambiental del ciclo dia-noche.
        waterCol = MathIso::LerpColor(waterCol, m_sunState.skyAmbient, 0.22f);

        float sunContrib = sunNdotL * m_sunState.lightIntensity * shadowFactor;
        float lightR = sunContrib * (static_cast<float>(m_sunState.lightColor.r) / 255.0f) + 0.24f;
        float lightG = sunContrib * (static_cast<float>(m_sunState.lightColor.g) / 255.0f) + 0.26f;
        float lightB = sunContrib * (static_cast<float>(m_sunState.lightColor.b) / 255.0f) + 0.32f;

        // Reflejo de luces puntuales cercanas (antorchas / cristales en la orilla).
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

        // Destello especular en la cresta de las olas (mas brillante si recibe sol o luz cercana).
        float glint = (wave > 0.62f) ? ((wave - 0.62f) / 0.38f) : 0.0f;
        float specBoost = glint * (sunContrib * 0.75f + ptBoost * 0.55f);

        char glyph = '~';
        if (depthFactor < 0.22f)    glyph = (wave > 0.0f) ? '-' : '.'; // Espuma suave en la orilla.
        else if (specBoost > 0.38f) glyph = '+';                        // Destello en cresta.
        else if (wave > 0.15f)      glyph = '~';
        else if (wave > -0.35f)     glyph = '=';
        else                        glyph = '-';

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