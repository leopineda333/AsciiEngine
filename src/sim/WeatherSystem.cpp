/**
 * @file WeatherSystem.cpp
 * @brief Implementacion de Biomas, Transiciones de Markov de Clima y Campo de Viento.
 */
#include <AsciiEngine/WeatherSystem.hpp>
#include <AsciiEngine/MathIso.hpp>
#include "json.hpp"
#include "raylib.h" // Solo para TraceLog en diagnosticos.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace AsciiEngine {

    using json = nlohmann::json;
    namespace fs = std::filesystem;

    namespace {

        constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;

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

    } // namespace anonimo

    bool WeatherSystem::LoadBiomesDirectory(const std::string& biomesDir) {
        m_biomes.clear();
        m_biomeMap.clear();

        if (!fs::exists(biomesDir) || !fs::is_directory(biomesDir)) {
            TraceLog(LOG_WARNING, "WEATHER: No existe directorio de biomas: %s", biomesDir.c_str());
            return false;
        }

        for (const auto& entry : fs::directory_iterator(biomesDir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;

            std::ifstream file(entry.path());
            if (!file.is_open()) continue;

            try {
                json root = json::parse(file, nullptr, true, true);
                BiomeDef b;
                b.id = root.value("id", entry.path().stem().string());
                b.name = root.value("name", b.id);
                b.baseGroundColor = ParseHex(root.value("baseGroundColor", "#4A6248"), b.baseGroundColor);

                b.altGroundColor = ParseHex(root.value("altGroundColor", "#3A4E38"), b.baseGroundColor);

                // Bloque modular 'ground' para caracteres ASCII y densidad del piso del bioma.
                if (root.contains("ground") && root["ground"].is_object()) {
                    const auto& g = root["ground"];
                    b.baseGroundColor = ParseHex(g.value("baseColor", "#4A6248"), b.baseGroundColor);
                    b.altGroundColor = ParseHex(g.value("altColor", "#3A4E38"), b.altGroundColor);
                    b.groundGlyphs = g.value("glyphs", ".,'");
                    b.shadowGlyphs = g.value("shadowGlyphs", ":;,");
                    b.groundDensity = MathIso::Clamp(g.value("density", 0.35f), 0.05f, 1.0f);
                }

                // Lista libre de pasto interactivo, flores o detalles a ras del suelo.
                if (root.contains("groundFoliage") && root["groundFoliage"].is_array()) {
                    for (const auto& gf : root["groundFoliage"]) {
                        GroundFoliageDef def;
                        def.id = gf.value("id", "foliage");
                        def.isTallBlade = gf.value("isTallBlade", true);
                        def.glyphs = gf.value("glyphs", "*ov+");
                        if (def.glyphs.empty()) def.glyphs = "|";
                        def.baseColor = ParseHex(gf.value("baseColor", "#22522A"), def.baseColor);
                        def.tipColor = ParseHex(gf.value("tipColor", "#569C4E"), def.tipColor);
                        def.spawnChance = MathIso::Clamp(gf.value("spawnChance", 0.20f), 0.0f, 1.0f);
                        def.heightZ = MathIso::Clamp(gf.value("heightZ", 0.65f), 0.1f, 2.5f);
                        def.windWeight = MathIso::Clamp(gf.value("windWeight", 0.9f), 0.0f, 2.0f);
                        def.interactive = gf.value("interactive", true);
                        b.groundFoliage.push_back(std::move(def));
                    }
                }

                if (root.contains("foliageDistribution") && root["foliageDistribution"].is_object()) {
                    const auto& fd = root["foliageDistribution"];
                    b.foliageDistribution.clumpStep = MathIso::Clamp(fd.value("clumpStep", 1.45f), 0.55f, 4.0f);
                    b.foliageDistribution.patchScale = MathIso::Clamp(fd.value("patchScale", 0.14f), 0.02f, 0.80f);
                    b.foliageDistribution.patchSeparation = MathIso::Clamp(fd.value("patchSeparation", 0.36f), 0.0f, 0.90f);
                    b.foliageDistribution.globalDensity = MathIso::Clamp(fd.value("globalDensity", 1.0f), 0.05f, 3.0f);
                }

                if (root.contains("groundFoliage") && root["groundFoliage"].is_array()) {
                    for (const auto& gf : root["groundFoliage"]) {
                        GroundFoliageDef def;
                        def.id = gf.value("id", "foliage");
                        def.isTallBlade = gf.value("isTallBlade", true);
                        def.glyphs = gf.value("glyphs", "*ov+");
                        if (def.glyphs.empty()) def.glyphs = "|";
                        def.baseColor = ParseHex(gf.value("baseColor", "#22522A"), def.baseColor);
                        def.tipColor = ParseHex(gf.value("tipColor", "#569C4E"), def.tipColor);
                        def.spawnChance = MathIso::Clamp(gf.value("spawnChance", 0.20f), 0.0f, 1.0f);
                        def.heightZ = MathIso::Clamp(gf.value("heightZ", 0.90f), 0.1f, 2.5f);
                        def.windWeight = MathIso::Clamp(gf.value("windWeight", 0.9f), 0.0f, 2.0f);
                        def.minBlades = std::max(1, gf.value("minBlades", def.isTallBlade ? 2 : 1));
                        def.maxBlades = std::max(def.minBlades, gf.value("maxBlades", def.isTallBlade ? 4 : 2));
                        def.clumpSpread = MathIso::Clamp(gf.value("clumpSpread", 1.15f), 0.15f, 3.0f);
                        def.interactive = gf.value("interactive", true);
                        b.groundFoliage.push_back(std::move(def));
                    }
                }

                b.baseSnowCoverage = MathIso::Clamp(root.value("baseSnowCoverage", 0.0f), 0.0f, 1.0f);
                b.windMultiplier = std::max(0.1f, root.value("windMultiplier", 1.0f));
                b.minStateSeconds = std::max(5.0f, root.value("minStateSeconds", 35.0f));
                b.maxStateSeconds = std::max(b.minStateSeconds, root.value("maxStateSeconds", 70.0f));

                if (root.contains("weatherTransitions") && root["weatherTransitions"].is_array()) {
                    for (const auto& tr : root["weatherTransitions"]) {
                        WeatherTransitionRule rule;
                        rule.targetWeather = tr.value("targetWeather", "");
                        rule.probability = std::max(0.0f, tr.value("probability", 0.0f));
                        if (!rule.targetWeather.empty() && rule.probability > 0.0f) {
                            b.weatherTransitions.push_back(std::move(rule));
                        }
                    }
                }

                m_biomeMap[b.id] = m_biomes.size();
                m_biomes.push_back(std::move(b));
            }
            catch (const json::parse_error& e) {
                TraceLog(LOG_ERROR, "WEATHER: Error parseando bioma %s -> %s",
                    entry.path().string().c_str(), e.what());
            }
        }

        TraceLog(LOG_INFO, "WEATHER: %zu biomas cargados.", m_biomes.size());
        return !m_biomes.empty();
    }

    void WeatherSystem::ApplyBiomeGroundStyle(const BiomeDef& b) {
        m_currentMods.groundGlyphCount = static_cast<uint8_t>(std::min<size_t>(b.groundGlyphs.size(), 8));
        for (uint8_t i = 0; i < m_currentMods.groundGlyphCount; ++i) {
            m_currentMods.groundGlyphs[i] = b.groundGlyphs[i];
        }
        if (m_currentMods.groundGlyphCount == 0) {
            m_currentMods.groundGlyphs[0] = '.';
            m_currentMods.groundGlyphCount = 1;
        }

        m_currentMods.shadowGlyphCount = static_cast<uint8_t>(std::min<size_t>(b.shadowGlyphs.size(), 8));
        for (uint8_t i = 0; i < m_currentMods.shadowGlyphCount; ++i) {
            m_currentMods.shadowGlyphs[i] = b.shadowGlyphs[i];
        }
        if (m_currentMods.shadowGlyphCount == 0) {
            m_currentMods.shadowGlyphs[0] = ':';
            m_currentMods.shadowGlyphCount = 1;
        }
        m_currentMods.groundDensity = b.groundDensity;
    }

    bool WeatherSystem::LoadWeathersDirectory(const std::string& weatherDir) {
        m_weathers.clear();
        m_weatherMap.clear();

        if (!fs::exists(weatherDir) || !fs::is_directory(weatherDir)) {
            TraceLog(LOG_WARNING, "WEATHER: No existe directorio de climas: %s", weatherDir.c_str());
            return false;
        }

        for (const auto& entry : fs::directory_iterator(weatherDir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;

            std::ifstream file(entry.path());
            if (!file.is_open()) continue;

            try {
                json root = json::parse(file, nullptr, true, true);
                WeatherDef w;
                w.id = root.value("id", entry.path().stem().string());
                w.name = root.value("name", w.id);
                w.transitionDuration = std::max(0.25f, root.value("transitionDuration", 4.0f));
                w.bgTint = ParseHex(root.value("bgTint", "#32363C"), w.bgTint);
                w.bgTintBlend = MathIso::Clamp(root.value("bgTintBlend", 0.0f), 0.0f, 1.0f);
                w.sunMultiplier = MathIso::Clamp(root.value("sunMultiplier", 1.0f), 0.1f, 1.5f);
                w.ambientMultiplier = MathIso::Clamp(root.value("ambientMultiplier", 1.0f), 0.2f, 2.0f);
                w.windGustStrength = std::max(0.0f, root.value("windGustStrength", 1.0f));
                w.snowTargetCoverage = MathIso::Clamp(root.value("snowTargetCoverage", 0.0f), 0.0f, 1.0f);
                w.snowChangeRate = std::max(0.01f, root.value("snowChangeRate", 0.12f));
                w.particleTemplateId = root.value("particleTemplateId", "");
                w.weatherParticleRate = std::max(0.0f, root.value("weatherParticleRate", 0.0f));

                if (root.contains("windSpeedRange")) {
                    w.windMinSpeed = std::max(0.0f, root["windSpeedRange"].value("min", 1.5f));
                    w.windMaxSpeed = std::max(w.windMinSpeed, root["windSpeedRange"].value("max", 3.0f));
                }

                m_weatherMap[w.id] = m_weathers.size();
                m_weathers.push_back(std::move(w));
            }
            catch (const json::parse_error& e) {
                TraceLog(LOG_ERROR, "WEATHER: Error parseando clima %s -> %s",
                    entry.path().string().c_str(), e.what());
            }
        }

        TraceLog(LOG_INFO, "WEATHER: %zu climas cargados.", m_weathers.size());
        return !m_weathers.empty();
    }

    void WeatherSystem::Configure(std::string_view biomeId, std::string_view initialWeatherId,
        float windDirDeg, float windBaseSpeed, float windGustStrength,
        bool randomTransitions) {
        m_randomTransitions = randomTransitions;

        // Direccion unitaria del campo vectorial de viento.
        float rad = windDirDeg * DEG_TO_RAD;
        m_wind.dirX = std::cos(rad);
        m_wind.dirY = std::sin(rad);
        m_wind.speed = windBaseSpeed;
        m_wind.gustStrength = windGustStrength;

        // Resolvemos bioma inicial.
        auto bIt = m_biomeMap.find(std::string(biomeId));
        m_activeBiomeIdx = (bIt != m_biomeMap.end()) ? bIt->second : 0;

        if (!m_biomes.empty()) {
            const BiomeDef& b = m_biomes[m_activeBiomeIdx];
            m_activeBiomeName = b.name;
            m_currentMods.biomeGroundColor = b.baseGroundColor;
            m_currentMods.snowCoverage = b.baseSnowCoverage;
            m_currentMods.biomeAltGroundColor = b.altGroundColor;
            ApplyBiomeGroundStyle(b);
        }

        // Resolvemos clima inicial aplicandolo de forma inmediata (sin fade inicial).
        auto wIt = m_weatherMap.find(std::string(initialWeatherId));
        m_targetWeatherIdx = (wIt != m_weatherMap.end()) ? wIt->second : 0;

        if (!m_weathers.empty()) {
            const WeatherDef& w = m_weathers[m_targetWeatherIdx];
            const float windMult = !m_biomes.empty() ? m_biomes[m_activeBiomeIdx].windMultiplier : 1.0f;

            m_activeWeatherName = w.name;
            m_currentMods.bgTint = w.bgTint;
            m_currentMods.bgTintBlend = w.bgTintBlend;
            m_currentMods.sunMultiplier = w.sunMultiplier;
            m_currentMods.ambientMultiplier = w.ambientMultiplier;
            m_fromMods = m_currentMods;

            m_targetWindSpeed = MathIso::Lerp(w.windMinSpeed, w.windMaxSpeed, 0.5f) * windMult;
            m_fromWindSpeed = m_targetWindSpeed;
            m_wind.speed = m_targetWindSpeed;
            m_targetWindGust = w.windGustStrength;
            m_fromWindGust = m_targetWindGust;
            m_wind.gustStrength = m_targetWindGust;
            m_activeParticleId = w.particleTemplateId;
            m_activeParticleRate = w.weatherParticleRate;
            m_fromParticleRate = w.weatherParticleRate;
        }

        m_transitionElapsed = 1.0f;
        m_transitionDuration = 1.0f;
        ResetStateTimer();
    }

    void WeatherSystem::TransitionToWeather(std::string_view weatherId) {
        if (m_weathers.empty()) return;

        auto it = m_weatherMap.find(std::string(weatherId));
        if (it == m_weatherMap.end()) {
            TraceLog(LOG_WARNING, "WEATHER: Clima no encontrado: %.*s",
                static_cast<int>(weatherId.size()), weatherId.data());
            return;
        }

        // Capturamos el estado actual como punto de partida para interpolacion sin saltos.
        m_fromMods = m_currentMods;
        m_fromWindSpeed = m_wind.speed;
        m_fromWindGust = m_wind.gustStrength;
        m_fromParticleRate = m_activeParticleRate;

        m_targetWeatherIdx = it->second;
        const WeatherDef& w = m_weathers[m_targetWeatherIdx];
        const float windMult = !m_biomes.empty() ? m_biomes[m_activeBiomeIdx].windMultiplier : 1.0f;

        std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
        m_targetWindSpeed = MathIso::Lerp(w.windMinSpeed, w.windMaxSpeed, dist01(m_rng)) * windMult;
        m_targetWindGust = w.windGustStrength;
        m_transitionDuration = w.transitionDuration;
        m_transitionElapsed = 0.0f;
        m_activeWeatherName = w.name;

        if (!w.particleTemplateId.empty()) {
            m_activeParticleId = w.particleTemplateId;
        }

        ResetStateTimer();
    }

    void WeatherSystem::CycleBiome() {
        if (m_biomes.empty()) return;

        m_activeBiomeIdx = (m_activeBiomeIdx + 1) % m_biomes.size();
        const BiomeDef& b = m_biomes[m_activeBiomeIdx];
        m_activeBiomeName = b.name;
        ApplyBiomeGroundStyle(b);

        // Re-iniciamos la transicion hacia el clima actual para mezclar el color de suelo del nuevo bioma.
        if (!m_weathers.empty()) {
            TransitionToWeather(m_weathers[m_targetWeatherIdx].id);
        }
    }

    void WeatherSystem::ResetStateTimer() {
        if (m_biomes.empty()) {
            m_stateTimer = 45.0f;
            return;
        }
        const BiomeDef& b = m_biomes[m_activeBiomeIdx];
        std::uniform_real_distribution<float> dist(b.minStateSeconds, b.maxStateSeconds);
        m_stateTimer = dist(m_rng);
    }

    void WeatherSystem::RollNextWeather() {
        if (m_biomes.empty()) return;

        const BiomeDef& b = m_biomes[m_activeBiomeIdx];
        if (b.weatherTransitions.empty()) {
            ResetStateTimer();
            return;
        }

        float totalProb = 0.0f;
        for (const auto& rule : b.weatherTransitions) totalProb += rule.probability;
        if (totalProb <= 0.0f) {
            ResetStateTimer();
            return;
        }

        // Muestreo proporcional sobre la fila del bioma en la Matriz de Markov.
        std::uniform_real_distribution<float> dist(0.0f, totalProb);
        float roll = dist(m_rng);
        float accum = 0.0f;

        for (const auto& rule : b.weatherTransitions) {
            accum += rule.probability;
            if (roll <= accum) {
                TransitionToWeather(rule.targetWeather);
                return;
            }
        }
        TransitionToWeather(b.weatherTransitions.back().targetWeather);
    }

    void WeatherSystem::Update(float dt) {
        const float safeDt = std::min(dt, 0.05f);
        // Integracion continua de fase: cambiar m_wind.speed nunca dispara saltos de frecuencia.
        m_wind.time += safeDt;
        m_wind.phase1 += safeDt * (1.45f + m_wind.speed * 0.34f);
        m_wind.phase2 += safeDt * (2.35f + m_wind.speed * 0.48f);
        if (m_wind.phase1 > 628.3185f) m_wind.phase1 -= 628.3185f;
        if (m_wind.phase2 > 628.3185f) m_wind.phase2 -= 628.3185f;

        if (m_weathers.empty()) return;

        // 1. Temporizador de transiciones aleatorias por bioma (Markov).
        if (m_randomTransitions) {
            m_stateTimer -= safeDt;
            if (m_stateTimer <= 0.0f) {
                RollNextWeather();
            }
        }

        // 2. Interpolacion suave de atmosfera, viento y tasa de particulas.
        const WeatherDef& w = m_weathers[m_targetWeatherIdx];
        ColorRGBA targetGround = !m_biomes.empty()
            ? m_biomes[m_activeBiomeIdx].baseGroundColor
            : ColorRGBA{ 74, 98, 72, 255 };
        float biomeSnowFloor = !m_biomes.empty() ? m_biomes[m_activeBiomeIdx].baseSnowCoverage : 0.0f;

        ColorRGBA targetAltGround = !m_biomes.empty() ? m_biomes[m_activeBiomeIdx].altGroundColor : ColorRGBA{ 58, 78, 56, 255 };

        m_transitionElapsed = std::min(m_transitionElapsed + safeDt, m_transitionDuration);
        float t = MathIso::Clamp(m_transitionElapsed / std::max(m_transitionDuration, 0.01f), 0.0f, 1.0f);
        // Smoothstep (3t^2 - 2t^3) para entradas y salidas organicas.
        float smoothT = t * t * (3.0f - 2.0f * t);

        m_currentMods.biomeGroundColor = MathIso::LerpColor(m_fromMods.biomeGroundColor, targetGround, smoothT);
        m_currentMods.biomeAltGroundColor = MathIso::LerpColor(m_fromMods.biomeAltGroundColor, targetAltGround, smoothT);
        m_currentMods.bgTint = MathIso::LerpColor(m_fromMods.bgTint, w.bgTint, smoothT);
        m_currentMods.bgTintBlend = MathIso::Lerp(m_fromMods.bgTintBlend, w.bgTintBlend, smoothT);
        m_currentMods.sunMultiplier = MathIso::Lerp(m_fromMods.sunMultiplier, w.sunMultiplier, smoothT);
        m_currentMods.ambientMultiplier = MathIso::Lerp(m_fromMods.ambientMultiplier, w.ambientMultiplier, smoothT);

        m_wind.speed = MathIso::Lerp(m_fromWindSpeed, m_targetWindSpeed, smoothT);
        m_wind.gustStrength = MathIso::Lerp(m_fromWindGust, m_targetWindGust, smoothT);

        // Si el clima destino no tiene particulas, bajamos la tasa a 0 suavemente.
        m_activeParticleRate = MathIso::Lerp(m_fromParticleRate, w.weatherParticleRate, smoothT);
        if (t >= 1.0f && w.particleTemplateId.empty()) {
            m_activeParticleId.clear();
        }

        // 3. Acumulacion o derretimiento progresivo de nieve sobre suelo y copas.
        float targetSnow = std::max(biomeSnowFloor, w.snowTargetCoverage);
        if (m_currentMods.snowCoverage < targetSnow) {
            m_currentMods.snowCoverage = std::min(targetSnow, m_currentMods.snowCoverage + w.snowChangeRate * safeDt);
        }
        else if (m_currentMods.snowCoverage > targetSnow) {
            m_currentMods.snowCoverage = std::max(targetSnow, m_currentMods.snowCoverage - w.snowChangeRate * safeDt);
        }
    }

    Vec2 WeatherSystem::SampleWindOffset(float worldX, float worldY, float heightNorm, float windWeight) const {
        float spatialPhase = (worldX * m_wind.dirX + worldY * m_wind.dirY) * 0.35f;
        float wave1 = std::sin(m_wind.phase1 + spatialPhase);
        float wave2 = std::cos(m_wind.phase2 + worldY * 0.28f);
        float gust = wave1 + 0.38f * wave2;

        float bend = (heightNorm * heightNorm) * windWeight * (0.045f * m_wind.speed * m_wind.gustStrength);
        float disp = MathIso::Clamp((0.30f + gust * 0.55f) * bend, -0.85f, 0.85f);

        return { m_wind.dirX * disp, m_wind.dirY * disp };
    }

} // namespace AsciiEngine