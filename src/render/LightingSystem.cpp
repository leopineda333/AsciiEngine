/**
 * @file LightingSystem.cpp
 * @brief Implementacion del ciclo Dia-Noche, iluminacion GI/puntual con curva de contraste y Heightfield 2.5D.
 */
#include <AsciiEngine/LightingSystem.hpp>
#include <AsciiEngine/MathIso.hpp>
#include <AsciiEngine/MaterialSystem.hpp>
#include <AsciiEngine/World.hpp>
#include "raylib.h" // Solo para TraceLog en diagnosticos.

#include <algorithm>
#include <cmath>

namespace AsciiEngine {

    namespace {

        constexpr float PI_F = 3.14159265358979323846f;
        constexpr float TWO_PI_F = 6.28318530717958647692f;

        inline uint8_t ToByte(float v) {
            return static_cast<uint8_t>(MathIso::Clamp(v, 0.0f, 255.0f));
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
                if (!materials.Get(matId).occludesLight) {
                    continue;
                }

                float sx = a.localPos.x * inst.scale.x;
                float sy = a.localPos.y * inst.scale.y;
                float wx = inst.position.x + (sx * inst.cosRot - sy * inst.sinRot);
                float wy = inst.position.y + (sx * inst.sinRot + sy * inst.cosRot);
                float wz = inst.position.z + (a.localPos.z * inst.scale.z);

                int gx = static_cast<int>(std::floor((wx - m_hfOriginX) * m_hfInvCellSize));
                int gy = static_cast<int>(std::floor((wy - m_hfOriginY) * m_hfInvCellSize));

                if (gx >= 0 && gx < m_hfWidth && gy >= 0 && gy < m_hfHeight) {
                    size_t idx = static_cast<size_t>(gy) * static_cast<size_t>(m_hfWidth) + static_cast<size_t>(gx);
                    if (wz > m_heightfield[idx]) {
                        m_heightfield[idx] = wz;
                    }
                    if (wz > m_hfMaxWorldZ) {
                        m_hfMaxWorldZ = wz;
                    }
                }
            }
        }

        TraceLog(LOG_INFO, "LIGHTING: Heightfield 2D (%dx%d, %.1f KB, maxZ=%.1f) construido.",
            m_hfWidth, m_hfHeight, static_cast<float>(GetHeightfieldBytes()) / 1024.0f, m_hfMaxWorldZ);
    }

    void LightingSystem::AdjustTimeHours(float deltaHours) {
        m_dayNight.currentTimeHours = std::fmod(m_dayNight.currentTimeHours + deltaHours, 24.0f);
        if (m_dayNight.currentTimeHours < 0.0f) {
            m_dayNight.currentTimeHours += 24.0f;
        }
        RecomputeSunState();
    }

    void LightingSystem::Update(float dt) {
        m_elapsedTime += dt;

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

        // Fondos calibrados con mayor contraste para que los glifos conserven saturacion al mediodia.
        const ColorRGBA bgNight = { 11, 13, 19, 255 };
        const ColorRGBA bgDusk = { 30, 27, 34, 255 };
        const ColorRGBA bgDay = { 50, 54, 60, 255 };

        if (sinT >= 0.0f) {
            // --- PERIODO DIURNO (SOL) ---
            m_sunState.isNight = false;
            float sunElev = MathIso::Clamp(sinT, 0.16f, 1.0f);
            m_sunState.lightDir = MathIso::Normalize({ cosT, 0.35f, sunElev });

            float dayFactor = MathIso::Clamp(sinT * 1.45f, 0.0f, 1.0f);
            float goldenFactor = MathIso::Clamp(1.0f - (sinT * 3.0f), 0.0f, 1.0f);

            const ColorRGBA sunHorizon = { 255, 158, 86, 255 };
            const ColorRGBA sunNoon = { 255, 246, 232, 255 };
            m_sunState.lightColor = MathIso::LerpColor(sunNoon, sunHorizon, goldenFactor);

            // Intensidad contenida (0.86 max) para evitar recortar (clip) las zonas iluminadas a blanco.
            m_sunState.lightIntensity = MathIso::Lerp(0.38f, 0.86f, dayFactor);
            m_sunState.backgroundColor = MathIso::LerpColor(bgDusk, bgDay, dayFactor);
            m_sunState.skyAmbient = MathIso::LerpColor({ 155, 105, 92, 255 }, m_giConfig.skyColor, dayFactor);
            m_sunState.groundAmbient = MathIso::LerpColor({ 20, 22, 24, 255 }, m_giConfig.groundColor, dayFactor);
            m_sunState.ambientStrength = m_giConfig.ambientStrength * MathIso::Lerp(0.50f, 0.68f, dayFactor);
            m_sunState.shadowDarkness = MathIso::Lerp(0.42f, 0.24f, dayFactor);
        }
        else {
            // --- PERIODO NOCTURNO (LUNA OPUESTA) ---
            m_sunState.isNight = true;
            float moonSin = -sinT;
            float moonElev = MathIso::Clamp(moonSin, 0.24f, 1.0f);
            m_sunState.lightDir = MathIso::Normalize({ -cosT, 0.35f, moonElev });

            float nightDepth = MathIso::Clamp(moonSin * 2.2f, 0.0f, 1.0f);

            m_sunState.lightColor = { 120, 155, 220, 255 };
            m_sunState.lightIntensity = MathIso::Lerp(0.18f, 0.30f, nightDepth);
            m_sunState.backgroundColor = MathIso::LerpColor(bgDusk, bgNight, nightDepth);
            m_sunState.skyAmbient = { 24, 38, 68, 255 };
            m_sunState.groundAmbient = { 10, 13, 18, 255 };
            m_sunState.ambientStrength = m_giConfig.ambientStrength * 0.38f;
            m_sunState.shadowDarkness = 0.50f;
        }
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
        if (!m_shadowsEnabled || m_heightfield.empty() || m_sunState.lightIntensity < 0.05f) {
            return 1.0f;
        }

        const Vec3& dir = m_sunState.lightDir;
        float horizLenSq = dir.x * dir.x + dir.y * dir.y;
        if (horizLenSq < 1e-4f) {
            return 1.0f;
        }

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

            if (rayZ > m_hfMaxWorldZ) {
                return 1.0f;
            }

            float sx = worldPt.x + stepDirX * dist;
            float sy = worldPt.y + stepDirY * dist;

            if (SampleHeightAt(sx, sy) > rayZ) {
                return m_sunState.shadowDarkness;
            }
        }

        return 1.0f;
    }

    ShadedAtom LightingSystem::ShadeAtom(Vec3 worldPt, Vec3 normal, float ao,
        const MaterialSystem& materials, MaterialId matId,
        bool receiveShadows, char structuralOverride) const {
        // 1. Luz directa Sol/Luna (Lambert).
        float ndotl = std::max(0.0f, MathIso::Dot(normal, m_sunState.lightDir));
        float shadowFactor = (receiveShadows && ndotl > 0.06f) ? SampleSunShadow(worldPt) : 1.0f;
        float sunContrib = ndotl * m_sunState.lightIntensity * shadowFactor;

        float lightR = sunContrib * (static_cast<float>(m_sunState.lightColor.r) / 255.0f);
        float lightG = sunContrib * (static_cast<float>(m_sunState.lightColor.g) / 255.0f);
        float lightB = sunContrib * (static_cast<float>(m_sunState.lightColor.b) / 255.0f);
        float totalEnergy = sunContrib;

        // 2. Iluminacion Global Hemisferica (cielo vs suelo).
        float upFactor = (normal.z * 0.5f) + 0.5f;
        ColorRGBA hemisCol = MathIso::LerpColor(m_sunState.groundAmbient, m_sunState.skyAmbient, upFactor);
        float ambEnergy = m_sunState.ambientStrength * ao;

        lightR += (static_cast<float>(hemisCol.r) / 255.0f) * ambEnergy;
        lightG += (static_cast<float>(hemisCol.g) / 255.0f) * ambEnergy;
        lightB += (static_cast<float>(hemisCol.b) / 255.0f) * ambEnergy;
        totalEnergy += ambEnergy;

        // 3. Luces puntuales activas en el frustum.
        for (const ActiveFrameLight& pl : m_activeLights) {
            float dx = pl.position.x - worldPt.x;
            float dy = pl.position.y - worldPt.y;
            float dz = pl.position.z - worldPt.z;
            float distSq = dx * dx + dy * dy + dz * dz;
            if (distSq >= pl.radiusSq) continue;

            float dist = std::sqrt(distSq);
            float invDist = 1.0f / (dist + 0.0001f);
            float pNdotL = std::max(0.18f, (normal.x * dx + normal.y * dy + normal.z * dz) * invDist);

            float falloff = 1.0f - (dist * pl.invRadius);
            float atten = falloff * falloff * pNdotL;

            lightR += pl.rNorm * atten;
            lightG += pl.gNorm * atten;
            lightB += pl.bNorm * atten;
            totalEnergy += pl.intensity * atten;
        }

        // 4. Curva de contraste centrada en 0.5: sombras mas profundas y volumen mas definido.
        float rawLuma = MathIso::Clamp(totalEnergy * ao, 0.0f, 1.0f);
        float luma = MathIso::Clamp((rawLuma - 0.5f) * 1.28f + 0.5f, 0.0f, 1.0f);

        char glyph = (structuralOverride != '\0') ? structuralOverride : materials.SampleGlyph(matId, luma);
        ColorRGBA baseCol = materials.SampleColor(matId, luma);

        // Tinte acotado a 1.08 max (evita desaturar/lavar el litColor) y piso bajo (0.14) para sombras ricas.
        float tintR = MathIso::Clamp(0.14f + lightR * 0.82f, 0.14f, 1.08f);
        float tintG = MathIso::Clamp(0.14f + lightG * 0.82f, 0.14f, 1.08f);
        float tintB = MathIso::Clamp(0.14f + lightB * 0.82f, 0.14f, 1.08f);

        ColorRGBA finalCol = {
            ToByte(static_cast<float>(baseCol.r) * tintR),
            ToByte(static_cast<float>(baseCol.g) * tintG),
            ToByte(static_cast<float>(baseCol.b) * tintB),
            255
        };

        return { glyph, finalCol };
    }

    ShadedAtom LightingSystem::ShadeGroundPoint(Vec3 worldPt, float terrainNoise, bool receiveShadows) const {
        float sunNdotL = std::max(0.0f, m_sunState.lightDir.z);
        float shadowFactor = (receiveShadows && sunNdotL > 0.05f) ? SampleSunShadow(worldPt) : 1.0f;

        // Acumulamos aportes de luces puntuales cercanas (fogatas / antorcha).
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

        // --- CASO 1: CELDA EN SOMBRA PROYECTADA ---
        // Se rellena el 100% de las celdas en sombra con glifos mas oscuros que el fondo
        // para formar siluetas continuas en lugar de puntos dispersos.
        if (shadowFactor < 0.85f && pointBoost < 0.28f) {
            const ColorRGBA& bg = m_sunState.backgroundColor;
            float shadowScale = m_sunState.isNight ? 0.45f : 0.34f;
            float shadeR = static_cast<float>(bg.r) * shadowScale + ptR * 55.0f;
            float shadeG = static_cast<float>(bg.g) * shadowScale + ptG * 50.0f;
            float shadeB = static_cast<float>(bg.b) * (shadowScale + 0.06f) + ptB * 48.0f;

            // Variacion sutil de textura dentro de la sombra sin romper su densidad.
            char shadowGlyph = (terrainNoise > 0.55f) ? ':' : ((terrainNoise > 0.20f) ? ';' : ',');
            return { shadowGlyph,{ ToByte(shadeR), ToByte(shadeG), ToByte(shadeB), 255 } };
        }

        // --- CASO 2: SUELO ILUMINADO POR SOL O LUZ PUNTUAL ---
        // En suelo despejado dibujamos solo ~35% de celdas (o donde haya luz puntual) para
        // dar textura organica limpia sin ensuciar la escena ni competir con las siluetas.
        if (pointBoost < 0.10f && terrainNoise < 0.65f) {
            return { '\0',{ 0, 0, 0, 0 } };
        }

        float sunContrib = sunNdotL * m_sunState.lightIntensity * shadowFactor;
        float lightR = sunContrib * (static_cast<float>(m_sunState.lightColor.r) / 255.0f) + ptR * 0.90f;
        float lightG = sunContrib * (static_cast<float>(m_sunState.lightColor.g) / 255.0f) + ptG * 0.90f;
        float lightB = sunContrib * (static_cast<float>(m_sunState.lightColor.b) / 255.0f) + ptB * 0.90f;

        float amb = m_sunState.ambientStrength * 0.70f;
        lightR += (static_cast<float>(m_sunState.skyAmbient.r) / 255.0f) * amb;
        lightG += (static_cast<float>(m_sunState.skyAmbient.g) / 255.0f) * amb;
        lightB += (static_cast<float>(m_sunState.skyAmbient.b) / 255.0f) * amb;

        // Paleta terrosa/boscosa limpia sin neblina blanca anadida.
        float baseR = 72.0f + terrainNoise * 18.0f;
        float baseG = 94.0f + terrainNoise * 22.0f;
        float baseB = 72.0f + terrainNoise * 12.0f;

        char glyph = '.';
        if (pointBoost > 0.55f)       glyph = '+';
        else if (terrainNoise > 0.85f) glyph = '\'';
        else if (terrainNoise > 0.72f) glyph = ',';

        ColorRGBA col = {
            ToByte(baseR * MathIso::Clamp(lightR, 0.12f, 1.15f)),
            ToByte(baseG * MathIso::Clamp(lightG, 0.12f, 1.15f)),
            ToByte(baseB * MathIso::Clamp(lightB, 0.12f, 1.15f)),
            255
        };

        return { glyph, col };
    }

    size_t LightingSystem::GetHeightfieldBytes() const {
        return m_heightfield.capacity() * sizeof(float);
    }

} // namespace AsciiEngine