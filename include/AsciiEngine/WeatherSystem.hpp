/**
 * @file WeatherSystem.hpp
 * @brief Sistema de Biomas (suelo + follaje data-driven), Clima Aleatorio (Transiciones de Markov) y Campo Vectorial de Viento.
 */
#pragma once

#include <AsciiEngine/Types.hpp>
#include <cstddef>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace AsciiEngine {

    struct WeatherTransitionRule {
        std::string targetWeather;
        float       probability = 1.0f;
    };

    // Control modular de distribucion de praderas y claros por bioma (100% JSON).
    struct FoliageDistributionConfig {
        float clumpStep = 1.45f; // Separacion base de la grilla de matas (unidades de mundo).
        float patchScale = 0.14f; // Escala del ruido (menor = praderas mas amplias, mayor = islas pequenas).
        float patchSeparation = 0.36f; // Umbral [0.0..0.9]: mayor = mas espacio vacio entre areas de follaje.
        float globalDensity = 1.0f;  // Multiplicador general de instancias por area.
    };

    // Especie de follaje o detalle a ras del suelo definida 100% en JSON (pasto, flores, cables, hongos).
    struct GroundFoliageDef {
        std::string id;
        bool        isTallBlade = true;
        std::string glyphs = "*ov+";
        ColorRGBA   baseColor = { 34, 82, 42, 255 };
        ColorRGBA   tipColor = { 86, 156, 78, 255 };
        float       spawnChance = 0.20f;
        float       heightZ = 0.90f;
        float       windWeight = 0.90f;
        int         minBlades = 2;     // Minimo de instancias dentro de cada mata/area.
        int         maxBlades = 4;     // Maximo de instancias dentro de cada mata/area.
        float       clumpSpread = 1.15f; // Dispersion espacial de las instancias dentro de la mata.
        bool        interactive = true;
    };

    struct BiomeDef {
        std::string id;
        std::string name;

        // Apariencia data-driven del suelo base del bioma.
        ColorRGBA   baseGroundColor = { 74, 98, 72, 255 };
        ColorRGBA   altGroundColor = { 58, 78, 56, 255 };
        std::string groundGlyphs = ".,'";
        std::string shadowGlyphs = ":;,";
        float       groundDensity = 0.35f;

        float       baseSnowCoverage = 0.0f;
        float       windMultiplier = 1.0f;
        float       minStateSeconds = 35.0f;
        float       maxStateSeconds = 70.0f;

        std::vector<WeatherTransitionRule> weatherTransitions;
        std::vector<GroundFoliageDef>      groundFoliage;
        FoliageDistributionConfig          foliageDistribution;
    };

    struct WeatherDef {
        std::string id;
        std::string name;
        float       transitionDuration = 4.0f;
        ColorRGBA   bgTint = { 50, 54, 60, 255 };
        float       bgTintBlend = 0.0f;
        float       sunMultiplier = 1.0f;
        float       ambientMultiplier = 1.0f;
        float       windMinSpeed = 1.5f;
        float       windMaxSpeed = 3.0f;
        float       windGustStrength = 1.0f;
        float       snowTargetCoverage = 0.0f;
        float       snowChangeRate = 0.12f;
        std::string particleTemplateId;
        float       weatherParticleRate = 0.0f;
    };

    class WeatherSystem {
    public:
        bool LoadBiomesDirectory(const std::string& biomesDir);
        bool LoadWeathersDirectory(const std::string& weatherDir);

        void Configure(std::string_view biomeId, std::string_view initialWeatherId,
            float windDirDeg, float windBaseSpeed, float windGustStrength,
            bool randomTransitions);

        // Inicia una interpolacion suave hacia otro clima definido en JSON.
        void TransitionToWeather(std::string_view weatherId);

        // Cambia al siguiente bioma cargado (tecla M).
        void CycleBiome();

        void ToggleRandomTransitions() { m_randomTransitions = !m_randomTransitions; }

        // Avanza transiciones climaticas, acumulacion de nieve, temporizador de Markov y campo de viento.
        void Update(float dt);

        // Accesores de edicion en vivo para el Editor de Mapas (Fase 7).
        const std::vector<BiomeDef>& GetBiomes() const { return m_biomes; }
        const std::vector<WeatherDef>& GetWeathers() const { return m_weathers; }
        std::string GetActiveBiomeId() const;
        std::string GetActiveWeatherId() const;
        void        SetBiomeByIndex(size_t idx);
        void        SetRandomTransitions(bool on) { m_randomTransitions = on; }
        void        SetWindDirectionDeg(float deg);
        float       GetWindDirectionDeg() const;
        void        SetWindBaseSpeed(float speed);
        void        SetWindGustStrength(float gust);

        // Evalua el desplazamiento XY por onda de viento para un atomo de follaje segun su altura [0..1].
        Vec2 SampleWindOffset(float worldX, float worldY, float heightNorm, float windWeight) const;

        const BiomeDef* GetActiveBiome() const                  { return m_biomes.empty() ? nullptr : &m_biomes[m_activeBiomeIdx]; }
        const WeatherModifiers& GetModifiers() const            { return m_currentMods; }
        const WindState& GetWind() const                        { return m_wind; }
        const std::string& GetWeatherParticleId() const         { return m_activeParticleId; }
        float                   GetWeatherParticleRate() const  { return m_activeParticleRate; }
        const std::string& GetActiveBiomeName() const           { return m_activeBiomeName; }
        const std::string& GetActiveWeatherName() const         { return m_activeWeatherName; }
        bool                    AreRandomTransitionsOn() const  { return m_randomTransitions; }
        float                   GetNextTransitionSec() const    { return m_stateTimer; }

    private:
        void ApplyBiomeGroundStyle(const BiomeDef& b);
        void RollNextWeather();
        void ResetStateTimer();

        std::vector<BiomeDef>                    m_biomes;
        std::unordered_map<std::string, size_t>  m_biomeMap;
        std::vector<WeatherDef>                  m_weathers;
        std::unordered_map<std::string, size_t>  m_weatherMap;

        size_t m_activeBiomeIdx = 0;
        size_t m_targetWeatherIdx = 0;

        // Instantanea de inicio y destino para interpolar transiciones sin saltos bruscos.
        WeatherModifiers m_fromMods;
        WeatherModifiers m_currentMods;
        float            m_fromWindSpeed = 2.2f;
        float            m_targetWindSpeed = 2.2f;
        float            m_fromWindGust = 1.0f;
        float            m_targetWindGust = 1.0f;
        float            m_fromParticleRate = 0.0f;

        float m_transitionElapsed = 1.0f;
        float m_transitionDuration = 1.0f;
        bool  m_randomTransitions = true;
        float m_stateTimer = 45.0f;

        WindState   m_wind;
        std::string m_activeParticleId;
        float       m_activeParticleRate = 0.0f;
        std::string m_activeBiomeName = "Default";
        std::string m_activeWeatherName = "Clear";

        std::mt19937 m_rng{ 4201u };
    };

} // namespace AsciiEngine