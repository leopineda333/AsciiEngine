// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file WeatherSystem.cpp
 * @brief Implementacion comentada linea por linea de Biomas, Transiciones de Markov y Campo de Viento.
 */
#include <AsciiEngine/WeatherSystem.hpp>
#include <AsciiEngine/MathIso.hpp>
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

        constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;

        /**
         * @brief Convierte un digito hexadecimal ASCII en su valor numerico [0..15].
         */
        uint8_t HexNibble(char c) {
            if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(10 + (c - 'a'));
            if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(10 + (c - 'A'));
            return 0;
        }

        /**
         * @brief Decodifica una cadena "#RRGGBB" en ColorRGBA opaco sin asignaciones dinamicas.
         */
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

        // Recorremos dinamicamente cualquier archivo .json en assets/data/biomes/.
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
                b.baseSnowCoverage = MathIso::Clamp(root.value("baseSnowCoverage", 0.0f), 0.0f, 1.0f);
                b.windMultiplier = std::max(0.1f, root.value("windMultiplier", 1.0f));
                b.minStateSeconds = std::max(5.0f, root.value("minStateSeconds", 35.0f));
                b.maxStateSeconds = std::max(b.minStateSeconds, root.value("maxStateSeconds", 70.0f));

                // 1. Bloque modular 'ground': define paleta, caracteres ASCII y densidad del terreno.
                if (root.contains("ground") && root["ground"].is_object()) {
                    const auto& g = root["ground"];
                    b.baseGroundColor = ParseHex(g.value("baseColor", "#4A6248"), b.baseGroundColor);
                    b.altGroundColor = ParseHex(g.value("altColor", "#3A4E38"), b.altGroundColor);
                    b.groundGlyphs = g.value("glyphs", ".,'");
                    b.shadowGlyphs = g.value("shadowGlyphs", ":;,");
                    b.groundDensity = MathIso::Clamp(g.value("density", 0.35f), 0.05f, 1.0f);
                }

                // 2. Bloque 'foliageDistribution': ajusta tamano de praderas, claros y densidad global.
                if (root.contains("foliageDistribution") && root["foliageDistribution"].is_object()) {
                    const auto& fd = root["foliageDistribution"];
                    b.foliageDistribution.clumpStep = MathIso::Clamp(fd.value("clumpStep", 1.45f), 0.55f, 4.0f);
                    b.foliageDistribution.patchScale = MathIso::Clamp(fd.value("patchScale", 0.14f), 0.02f, 0.80f);
                    b.foliageDistribution.patchSeparation = MathIso::Clamp(fd.value("patchSeparation", 0.36f), 0.0f, 0.90f);
                    b.foliageDistribution.globalDensity = MathIso::Clamp(fd.value("globalDensity", 1.0f), 0.05f, 3.0f);
                }

                // 3. Lista libre de especies de follaje interactivo a ras del suelo.
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

                // 4. Fila del bioma en la Matriz de Markov de transiciones climaticas.
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

    void WeatherSystem::ApplyBiomeGroundStyle(const BiomeDef& b) {
        // Copiamos los caracteres del bioma a arreglos POD fijos char[8] dentro de m_currentMods
        // para que LightingSystem pueda leerlos por frame sin tocar memoria dinamica.
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

    void WeatherSystem::Configure(std::string_view biomeId, std::string_view initialWeatherId,
        float windDirDeg, float windBaseSpeed, float windGustStrength,
        bool randomTransitions) {
        m_randomTransitions = randomTransitions;

        // 1. Convertimos la direccion angular del viento en un vector unitario 2D (dirX, dirY).
        float rad = windDirDeg * DEG_TO_RAD;
        m_wind.dirX = std::cos(rad);
        m_wind.dirY = std::sin(rad);
        m_wind.speed = windBaseSpeed;
        m_wind.gustStrength = windGustStrength;

        // 2. Resolvemos y aplicamos el bioma inicial.
        auto bIt = m_biomeMap.find(std::string(biomeId));
        m_activeBiomeIdx = (bIt != m_biomeMap.end()) ? bIt->second : 0;

        if (!m_biomes.empty()) {
            const BiomeDef& b = m_biomes[m_activeBiomeIdx];
            m_activeBiomeName = b.name;
            m_currentMods.biomeGroundColor = b.baseGroundColor;
            m_currentMods.biomeAltGroundColor = b.altGroundColor;
            m_currentMods.snowCoverage = b.baseSnowCoverage;
            ApplyBiomeGroundStyle(b);
        }

        // 3. Resolvemos y aplicamos el clima inicial de forma instantanea (sin transicion de entrada).
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

        // Marcamos la transicion como completada (elapsed == duration) y sorteamos el primer reloj Markov.
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

        // 1. Congelamos el estado actual como origen ('m_from*') para garantizar continuidad C0
        //    incluso si el usuario dispara otra transicion antes de terminar la anterior.
        m_fromMods = m_currentMods;
        m_fromWindSpeed = m_wind.speed;
        m_fromWindGust = m_wind.gustStrength;
        m_fromParticleRate = m_activeParticleRate;

        // 2. Establecemos el nuevo clima objetivo y sorteamos una velocidad dentro de su windSpeedRange.
        m_targetWeatherIdx = it->second;
        const WeatherDef& w = m_weathers[m_targetWeatherIdx];
        const float windMult = !m_biomes.empty() ? m_biomes[m_activeBiomeIdx].windMultiplier : 1.0f;

        std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
        m_targetWindSpeed = MathIso::Lerp(w.windMinSpeed, w.windMaxSpeed, dist01(m_rng)) * windMult;
        m_targetWindGust = w.windGustStrength;
        m_transitionDuration = w.transitionDuration;
        m_transitionElapsed = 0.0f;
        m_activeWeatherName = w.name;

        // Si el nuevo clima usa particulas, cambiamos el ID de inmediato y subimos la tasa desde 0.
        // Si es despejado (vacio), mantenemos el ID previo mientras la tasa baja suavemente a 0.
        if (!w.particleTemplateId.empty()) {
            m_activeParticleId = w.particleTemplateId;
        }

        ResetStateTimer();
    }

    void WeatherSystem::CycleBiome() {
        if (m_biomes.empty()) return;
        SetBiomeByIndex((m_activeBiomeIdx + 1) % m_biomes.size());
    }

    void WeatherSystem::SetBiomeByIndex(size_t idx) {
        if (m_biomes.empty()) return;
        m_activeBiomeIdx = idx % m_biomes.size();
        const BiomeDef& b = m_biomes[m_activeBiomeIdx];
        m_activeBiomeName = b.name;
        ApplyBiomeGroundStyle(b);

        // Relanzamos la transicion del clima actual para interpolar suavemente el color del suelo del bioma.
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

        // 1. Sumamos todos los pesos de la fila del bioma (permite pesos arbitrarios sin exigir suma = 1.0).
        float totalProb = 0.0f;
        for (const auto& rule : b.weatherTransitions) totalProb += rule.probability;
        if (totalProb <= 0.0f) {
            ResetStateTimer();
            return;
        }

        // 2. Muestreo por ruleta (Fitness Proportionate Selection): tiramos un valor en [0, totalProb]
        //    y avanzamos acumulando pesos hasta cruzar 'roll'.
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
        // Limitamos dt a 50 ms para evitar saltos de interpolacion al arrastrar la ventana.
        const float safeDt = std::min(dt, 0.05f);

        if (!m_weathers.empty()) {
            // 1. Temporizador autonomo de Cadena de Markov.
            if (m_randomTransitions) {
                m_stateTimer -= safeDt;
                if (m_stateTimer <= 0.0f) {
                    RollNextWeather();
                }
            }

            // 2. Interpolacion Smoothstep (3t^2 - 2t^3) entre instantanea origen y clima/bioma destino.
            const WeatherDef& w = m_weathers[m_targetWeatherIdx];
            ColorRGBA targetGround = !m_biomes.empty() ? m_biomes[m_activeBiomeIdx].baseGroundColor : ColorRGBA{ 74, 98, 72, 255 };
            ColorRGBA targetAltGround = !m_biomes.empty() ? m_biomes[m_activeBiomeIdx].altGroundColor : ColorRGBA{ 58, 78, 56, 255 };
            float     biomeSnowFloor = !m_biomes.empty() ? m_biomes[m_activeBiomeIdx].baseSnowCoverage : 0.0f;

            m_transitionElapsed = std::min(m_transitionElapsed + safeDt, m_transitionDuration);
            float t = MathIso::Clamp(m_transitionElapsed / std::max(m_transitionDuration, 0.01f), 0.0f, 1.0f);
            float smoothT = t * t * (3.0f - 2.0f * t);

            m_currentMods.biomeGroundColor = MathIso::LerpColor(m_fromMods.biomeGroundColor, targetGround, smoothT);
            m_currentMods.biomeAltGroundColor = MathIso::LerpColor(m_fromMods.biomeAltGroundColor, targetAltGround, smoothT);
            m_currentMods.bgTint = MathIso::LerpColor(m_fromMods.bgTint, w.bgTint, smoothT);
            m_currentMods.bgTintBlend = MathIso::Lerp(m_fromMods.bgTintBlend, w.bgTintBlend, smoothT);
            m_currentMods.sunMultiplier = MathIso::Lerp(m_fromMods.sunMultiplier, w.sunMultiplier, smoothT);
            m_currentMods.ambientMultiplier = MathIso::Lerp(m_fromMods.ambientMultiplier, w.ambientMultiplier, smoothT);

            m_wind.speed = MathIso::Lerp(m_fromWindSpeed, m_targetWindSpeed, smoothT);
            m_wind.gustStrength = MathIso::Lerp(m_fromWindGust, m_targetWindGust, smoothT);

            m_activeParticleRate = MathIso::Lerp(m_fromParticleRate, w.weatherParticleRate, smoothT);
            if (t >= 1.0f && w.particleTemplateId.empty()) {
                m_activeParticleId.clear();
            }

            // 3. Acumulacion o derretimiento progresivo de nieve (el piso minimo lo fija el bioma).
            float targetSnow = std::max(biomeSnowFloor, w.snowTargetCoverage);
            if (m_currentMods.snowCoverage < targetSnow) {
                m_currentMods.snowCoverage = std::min(targetSnow, m_currentMods.snowCoverage + w.snowChangeRate * safeDt);
            }
            else if (m_currentMods.snowCoverage > targetSnow) {
                m_currentMods.snowCoverage = std::max(targetSnow, m_currentMods.snowCoverage - w.snowChangeRate * safeDt);
            }
        }

        // 4. Integracion continua de fases del viento:
        //    Sumar (dt * frecuencia_actual) en lugar de calcular (time * frecuencia_actual) garantiza
        //    que cambiar m_wind.speed nunca provoque aceleraciones violentas en las copas ni en el pasto.
        //    El modulo cada 100*2pi (628.3185) evita perdida de precision flotante en sesiones largas.
        m_wind.time += safeDt;
        m_wind.phase1 += safeDt * (1.45f + m_wind.speed * 0.34f);
        m_wind.phase2 += safeDt * (2.35f + m_wind.speed * 0.48f);
        if (m_wind.phase1 > 628.3185f) m_wind.phase1 -= 628.3185f;
        if (m_wind.phase2 > 628.3185f) m_wind.phase2 -= 628.3185f;
    }

    Vec2 WeatherSystem::SampleWindOffset(float worldX, float worldY, float heightNorm, float windWeight) const {
        // 1. Proyectamos la posicion sobre la direccion del viento para generar una onda viajera 2D
        //    que recorre el bosque en oleadas en vez de mover todos los arboles sincronizados.
        float spatialPhase = (worldX * m_wind.dirX + worldY * m_wind.dirY) * 0.35f;
        float wave1 = std::sin(m_wind.phase1 + spatialPhase);
        float wave2 = std::cos(m_wind.phase2 + worldY * 0.28f); // Armonico transversal organico.
        float gust = wave1 + 0.38f * wave2;

        // 2. Flexibilidad cuadratica (heightNorm^2):
        //    Un tronco o tallo empotrado en el suelo (viga en voladizo) se deforma de forma no lineal:
        //    en la base (0.0) el desplazamiento es 0 y crece suavemente hacia la punta (1.0).
        float bend = (heightNorm * heightNorm) * windWeight * (0.045f * m_wind.speed * m_wind.gustStrength);
        // Acotamos el desplazamiento maximo a +-0.85u para evitar desgarros en tormentas extremas.
        float disp = MathIso::Clamp((0.30f + gust * 0.55f) * bend, -0.85f, 0.85f);

        return { m_wind.dirX * disp, m_wind.dirY * disp };
    }

    std::string WeatherSystem::GetActiveBiomeId() const {
        return m_biomes.empty() ? "temperate_forest" : m_biomes[m_activeBiomeIdx].id;
    }

    std::string WeatherSystem::GetActiveWeatherId() const {
        return m_weathers.empty() ? "clear_day" : m_weathers[m_targetWeatherIdx].id;
    }

    void WeatherSystem::SetWindDirectionDeg(float deg) {
        float rad = deg * DEG_TO_RAD;
        m_wind.dirX = std::cos(rad);
        m_wind.dirY = std::sin(rad);
    }

    float WeatherSystem::GetWindDirectionDeg() const {
        float deg = std::atan2(m_wind.dirY, m_wind.dirX) / DEG_TO_RAD;
        if (deg < 0.0f) deg += 360.0f;
        return deg;
    }

    void WeatherSystem::SetWindBaseSpeed(float speed) {
        float clamped = MathIso::Clamp(speed, 0.0f, 15.0f);
        m_wind.speed = clamped;
        m_fromWindSpeed = clamped;
        m_targetWindSpeed = clamped;
    }

    void WeatherSystem::SetWindGustStrength(float gust) {
        float clamped = MathIso::Clamp(gust, 0.0f, 3.0f);
        m_wind.gustStrength = clamped;
        m_fromWindGust = clamped;
        m_targetWindGust = clamped;
    }

} // namespace AsciiEngine