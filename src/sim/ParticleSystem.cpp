// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file ParticleSystem.cpp
 * @brief Implementacion comentada linea por linea del pool SoA de particulas y emisores climaticos.
 */
#include <AsciiEngine/ParticleSystem.hpp>
#include <AsciiEngine/AsciiZBuffer.hpp>
#include "json.hpp" // Dependencia privada: nunca se expone en headers publicos.
#include "raylib.h" // Solo para TraceLog() en diagnosticos de carga.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace AsciiEngine {

    using json = nlohmann::json;
    namespace fs = std::filesystem;

    namespace {

        uint8_t HexNibble(char c) {
            if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(10 + (c - 'a'));
            if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(10 + (c - 'A'));
            return 0;
        }

        ColorRGBA ParseHex(std::string_view hex, ColorRGBA fallback) {
            if (!hex.empty() && hex.front() == '#') hex.remove_prefix(1);
            if (hex.size() < 6) return fallback;
            return {
                static_cast<uint8_t>((HexNibble(hex[0]) << 4) | HexNibble(hex[1])),
                static_cast<uint8_t>((HexNibble(hex[2]) << 4) | HexNibble(hex[3])),
                static_cast<uint8_t>((HexNibble(hex[4]) << 4) | HexNibble(hex[5])),
                255
            };
        }

        Vec3 ReadVec3(const json& obj, const char* key, Vec3 def) {
            if (!obj.contains(key)) return def;
            const auto& v = obj[key];
            if (v.is_array() && v.size() >= 3) {
                return { v[0].get<float>(), v[1].get<float>(), v[2].get<float>() };
            }
            return { v.value("x", def.x), v.value("y", def.y), v.value("z", def.z) };
        }

    } // namespace anonimo

    ParticleSystem::ParticleSystem() {
        // Pre-alojamos toda la memoria del pool una sola vez al crear el motor.
        // Durante la partida, ni nacer ni morir particulas provoca una sola llamada a malloc/new.
        m_posX.resize(MAX_PARTICLES, 0.0f);
        m_posY.resize(MAX_PARTICLES, 0.0f);
        m_posZ.resize(MAX_PARTICLES, 0.0f);
        m_velX.resize(MAX_PARTICLES, 0.0f);
        m_velY.resize(MAX_PARTICLES, 0.0f);
        m_velZ.resize(MAX_PARTICLES, 0.0f);
        m_age.resize(MAX_PARTICLES, 0.0f);
        m_maxAge.resize(MAX_PARTICLES, 1.0f);
        m_templateIdx.resize(MAX_PARTICLES, 0);
    }

    bool ParticleSystem::LoadTemplatesDirectory(const std::string& particlesDir) {
        m_templates.clear();
        m_templateMap.clear();
        m_activeCount = 0;

        if (!fs::exists(particlesDir) || !fs::is_directory(particlesDir)) {
            TraceLog(LOG_WARNING, "PARTICLES: No existe directorio: %s", particlesDir.c_str());
            return false;
        }

        for (const auto& entry : fs::directory_iterator(particlesDir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;

            std::ifstream file(entry.path());
            if (!file.is_open()) continue;

            try {
                json root = json::parse(file, nullptr, true, true);
                ParticleTemplate pt;
                pt.id = root.value("id", entry.path().stem().string());
                pt.spawnRate = root.value("spawnRate", 20.0f);
                pt.gravityZ = root.value("gravityZ", -2.0f);
                pt.windInfluence = root.value("windInfluence", 0.5f);
                pt.collideWithFloor = root.value("collideWithFloor", true);
                pt.spawnHighVolume = root.value("spawnHighVolume", false);

                if (root.contains("lifetime")) {
                    pt.lifeMin = std::max(0.1f, root["lifetime"].value("min", 0.5f));
                    pt.lifeMax = std::max(pt.lifeMin, root["lifetime"].value("max", 1.2f));
                }
                if (root.contains("velocity")) {
                    pt.velMin = ReadVec3(root["velocity"], "min", { -0.5f, -0.5f, 0.5f });
                    pt.velMax = ReadVec3(root["velocity"], "max", { 0.5f,  0.5f, 1.5f });
                }

                // Secuencia de caracteres ASCII a lo largo de la vida de la particula (ej. "*", "+", ".").
                if (root.contains("asciiOverLife") && root["asciiOverLife"].is_array()) {
                    for (const auto& ch : root["asciiOverLife"]) {
                        std::string s = ch.get<std::string>();
                        if (!s.empty()) pt.asciiOverLife.push_back(s[0]);
                    }
                }
                if (pt.asciiOverLife.empty()) pt.asciiOverLife.push_back('*');

                // Gradiente de colores interpolado a lo largo de la vida de la particula.
                if (root.contains("colorOverLife") && root["colorOverLife"].is_array()) {
                    for (const auto& col : root["colorOverLife"]) {
                        pt.colorOverLife.push_back(ParseHex(col.get<std::string>(), { 255, 255, 255, 255 }));
                    }
                }
                if (pt.colorOverLife.empty()) pt.colorOverLife.push_back({ 255, 255, 255, 255 });

                uint16_t idx = static_cast<uint16_t>(m_templates.size());
                m_templateMap[pt.id] = idx;
                m_templates.push_back(std::move(pt));
            }
            catch (const json::parse_error& e) {
                TraceLog(LOG_ERROR, "PARTICLES: Error en %s -> %s", entry.path().string().c_str(), e.what());
            }
        }

        TraceLog(LOG_INFO, "PARTICLES: %zu plantillas cargadas (Pool SoA: %.1f KB).",
            m_templates.size(), static_cast<float>(GetMemoryBytes()) / 1024.0f);
        return !m_templates.empty();
    }

    void ParticleSystem::ClearWorldState() {
        m_worldEmitters.clear();
        m_activeCount = 0; // Basta resetear el contador; los arreglos SoA conservan su memoria.
        m_weatherAccum = 0.0f;
        m_ambientNightAcc = 0.0f;
    }

    bool ParticleSystem::FindTemplateIndex(std::string_view id, uint16_t& outIdx) const {
        auto it = m_templateMap.find(std::string(id));
        if (it == m_templateMap.end()) return false;
        outIdx = it->second;
        return true;
    }

    void ParticleSystem::AddWorldEmitter(Vec3 position, Vec3 areaHalfExtents,
        std::string_view templateId, float rateOverride,
        bool nightOnly) {
        uint16_t idx = 0;
        if (!FindTemplateIndex(templateId, idx)) return;

        WorldParticleEmitter em;
        em.position = position;
        em.areaHalfExtents = areaHalfExtents;
        em.templateIdx = idx;
        em.spawnRate = (rateOverride > 0.0f) ? rateOverride : m_templates[idx].spawnRate;
        em.nightOnly = nightOnly;
        m_worldEmitters.push_back(em);
    }

    void ParticleSystem::SpawnOne(uint16_t templateIdx, Vec3 pos) {
        // Proteccion rigida de techo de memoria: si el pool esta lleno, descartamos silenciosamente.
        if (m_activeCount >= MAX_PARTICLES || templateIdx >= m_templates.size()) {
            return;
        }

        const ParticleTemplate& pt = m_templates[templateIdx];
        auto randRange = [&](float a, float b) -> float {
            if (b <= a) return a;
            std::uniform_real_distribution<float> d(a, b);
            return d(m_rng);
            };

        // Escribimos directamente al final del bloque vivo e incrementamos m_activeCount en O(1).
        const size_t i = m_activeCount++;
        m_posX[i] = pos.x;
        m_posY[i] = pos.y;
        m_posZ[i] = pos.z;
        m_velX[i] = randRange(pt.velMin.x, pt.velMax.x);
        m_velY[i] = randRange(pt.velMin.y, pt.velMax.y);
        m_velZ[i] = randRange(pt.velMin.z, pt.velMax.z);
        m_age[i] = 0.0f;
        m_maxAge[i] = randRange(pt.lifeMin, pt.lifeMax);
        m_templateIdx[i] = templateIdx;
    }

    void ParticleSystem::Update(float dt, const WindState& wind, bool isNight,
        Vec2 cameraWorldCenter,
        std::string_view weatherParticleId, float weatherSpawnRate) {
        const float safeDt = std::min(dt, 0.05f);
        auto randSym = [&](float half) -> float {
            std::uniform_real_distribution<float> d(-half, half);
            return d(m_rng);
            };

        // 1. Emisores fijos del escenario (ej. chispas sobre braseros/antorchas).
        for (WorldParticleEmitter& em : m_worldEmitters) {
            if (em.nightOnly && !isNight) continue;

            // Acumulador fraccionario: asegura la misma tasa de emision a 30, 60 o 144 FPS.
            em.accumulator += em.spawnRate * safeDt;
            int toSpawn = static_cast<int>(em.accumulator);
            em.accumulator -= static_cast<float>(toSpawn);

            for (int s = 0; s < toSpawn && m_activeCount < MAX_PARTICLES; ++s) {
                Vec3 p = {
                    em.position.x + randSym(em.areaHalfExtents.x),
                    em.position.y + randSym(em.areaHalfExtents.y),
                    em.position.z + randSym(em.areaHalfExtents.z)
                };
                SpawnOne(em.templateIdx, p);
            }
        }

        // 2. Emisor climatico volumetrico centrado alrededor del foco de camara (lluvia / ventisca).
        //    Al emitir solo en un volumen local de 72x72u alrededor de la camara, evitamos desperdiciar
        //    el pool en zonas fuera de pantalla.
        uint16_t weatherIdx = 0;
        if (weatherSpawnRate > 0.1f && !weatherParticleId.empty() && FindTemplateIndex(weatherParticleId, weatherIdx)) {
            m_weatherAccum += weatherSpawnRate * safeDt;
            // Tope de 80 particulas por frame para evitar picos subitos tras pausar la ventana.
            int toSpawn = std::min(static_cast<int>(m_weatherAccum), 80);
            m_weatherAccum -= static_cast<float>(toSpawn);

            const ParticleTemplate& pt = m_templates[weatherIdx];
            for (int s = 0; s < toSpawn && m_activeCount < MAX_PARTICLES; ++s) {
                float zSpawn = pt.spawnHighVolume
                    ? (11.0f + std::abs(randSym(7.0f))) // Nace alto en el cielo (Z en [11..18]).
                    : (0.8f + std::abs(randSym(3.0f)));
                Vec3 p = {
                    cameraWorldCenter.x + randSym(36.0f),
                    cameraWorldCenter.y + randSym(36.0f),
                    zSpawn
                };
                SpawnOne(weatherIdx, p);
            }
        }

        // 3. Luciernagas ambientales automaticas durante noches despejadas (sin lluvia ni ventisca).
        uint16_t fireflyIdx = 0;
        if (isNight && weatherSpawnRate < 5.0f && FindTemplateIndex("fireflies", fireflyIdx)) {
            m_ambientNightAcc += 14.0f * safeDt;
            int toSpawn = static_cast<int>(m_ambientNightAcc);
            m_ambientNightAcc -= static_cast<float>(toSpawn);

            for (int s = 0; s < toSpawn && m_activeCount < MAX_PARTICLES; ++s) {
                Vec3 p = {
                    cameraWorldCenter.x + randSym(26.0f),
                    cameraWorldCenter.y + randSym(26.0f),
                    0.6f + std::abs(randSym(2.8f))
                };
                SpawnOne(fireflyIdx, p);
            }
        }

        // 4. Integracion fisica lineal SoA + borrado O(1) Swap-with-Last.
        const float windForceX = wind.dirX * wind.speed;
        const float windForceY = wind.dirY * wind.speed;

        size_t i = 0;
        while (i < m_activeCount) {
            m_age[i] += safeDt;

            const ParticleTemplate& pt = m_templates[m_templateIdx[i]];
            bool dead = (m_age[i] >= m_maxAge[i]);

            if (!dead) {
                // Aplicamos gravedad vertical (Z) y empuje del campo vectorial de viento (XY).
                m_velZ[i] += pt.gravityZ * safeDt;
                float vx = m_velX[i] + windForceX * pt.windInfluence;
                float vy = m_velY[i] + windForceY * pt.windInfluence;

                m_posX[i] += vx * safeDt;
                m_posY[i] += vy * safeDt;
                m_posZ[i] += m_velZ[i] * safeDt;

                // Colision simple contra el plano del terreno (Z = 0).
                if (m_posZ[i] <= 0.0f) {
                    if (pt.collideWithFloor) {
                        // Detenemos la gota/copo en el suelo y le dejamos 60 ms de vida para que
                        // muestre su ultimo glifo de impacto (ej. '.' en lluvia) antes de desaparecer.
                        m_posZ[i] = 0.0f;
                        m_age[i] = std::max(m_age[i], m_maxAge[i] - 0.06f);
                        m_velX[i] = 0.0f;
                        m_velY[i] = 0.0f;
                        m_velZ[i] = 0.0f;
                    }
                    else {
                        dead = true;
                    }
                }
            }

            if (dead) {
                // Swap-with-Last O(1): movemos la ultima particula viva al hueco libre 'i' y
                // NO incrementamos 'i' para evaluar la particula recien trasladada en la siguiente iteracion.
                size_t last = --m_activeCount;
                if (i != last) {
                    m_posX[i] = m_posX[last];
                    m_posY[i] = m_posY[last];
                    m_posZ[i] = m_posZ[last];
                    m_velX[i] = m_velX[last];
                    m_velY[i] = m_velY[last];
                    m_velZ[i] = m_velZ[last];
                    m_age[i] = m_age[last];
                    m_maxAge[i] = m_maxAge[last];
                    m_templateIdx[i] = m_templateIdx[last];
                }
            }
            else {
                ++i;
            }
        }
    }

    void ParticleSystem::SubmitToZBuffer(AsciiZBuffer& zBuffer, const MathIso::IsoConfig& isoConfig,
        float zoom, float camTopLeftX, float camTopLeftY,
        float screenW, float screenH, int renderStride) const {
        // LOD de zoom alejado: renderStride = 2 recorre solo la mitad de las particulas en O(P/2).
        const size_t step = static_cast<size_t>(std::max(1, renderStride));

        for (size_t i = 0; i < m_activeCount; i += step) {
            Vec3  worldPt = { m_posX[i], m_posY[i], m_posZ[i] };
            Vec2  isoPt = MathIso::WorldToIso(worldPt, isoConfig);
            float zoomedX = isoPt.x * zoom;
            float zoomedY = isoPt.y * zoom;

            // Pre-check rapido de Capa 3 antes de calcular vida, glifo y gradiente de color.
            float sx = zoomedX - camTopLeftX;
            float sy = zoomedY - camTopLeftY;
            if (sx < 0.0f || sx >= screenW || sy < 0.0f || sy >= screenH) {
                continue;
            }

            const ParticleTemplate& pt = m_templates[m_templateIdx[i]];
            // Progreso normalizado de vida en [0.0 .. 0.999] para indexar arreglos sin desbordar.
            float lifeT = MathIso::Clamp(m_age[i] / std::max(m_maxAge[i], 0.001f), 0.0f, 0.999f);

            // 1. Glifo correspondiente a la fase actual de vida.
            size_t glyphIdx = static_cast<size_t>(lifeT * static_cast<float>(pt.asciiOverLife.size()));
            char   glyph = pt.asciiOverLife[std::min(glyphIdx, pt.asciiOverLife.size() - 1)];

            // 2. Interpolacion lineal por tramos sobre la paleta colorOverLife del JSON.
            ColorRGBA col = pt.colorOverLife[0];
            if (pt.colorOverLife.size() > 1) {
                float  scaled = lifeT * static_cast<float>(pt.colorOverLife.size() - 1);
                size_t c0 = static_cast<size_t>(scaled);
                size_t c1 = std::min(c0 + 1, pt.colorOverLife.size() - 1);
                col = MathIso::LerpColor(pt.colorOverLife[c0], pt.colorOverLife[c1], scaled - static_cast<float>(c0));
            }

            // 3. Enviamos con FALLBACK_MATERIAL_ID y wantsOutline=false para que sean etereas/emisivas
            //    (sin respaldo volumetrico en Pase 0 ni contorno negro en Pase 1).
            //    El sesgo +0.02f asegura que chispas y gotas resalten ligeramente sobre objetos adyacentes.
            float depth = MathIso::ComputeDepthKey(worldPt) + 0.02f;
            zBuffer.SubmitAtom(zoomedX, zoomedY, depth, glyph, col, FALLBACK_MATERIAL_ID, false, 0, false);
        }
    }

    size_t ParticleSystem::GetMemoryBytes() const {
        // 8 arreglos de float (posXYZ, velXYZ, age, maxAge) + 1 arreglo de uint16_t (templateIdx).
        return MAX_PARTICLES * (sizeof(float) * 8 + sizeof(uint16_t));
    }

} // namespace AsciiEngine