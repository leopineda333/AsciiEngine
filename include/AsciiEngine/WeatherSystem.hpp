// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file WeatherSystem.hpp
 * @brief Sistema de Biomas (suelo y follaje data-driven), Clima de Markov y Campo Vectorial de Viento.
 *
 * @details Encapsula tres responsabilidades acopladas entre si pero 100% dirigidas por archivos JSON:
 *   1. Biomas (assets/data/biomes/*.json): definen la paleta, glifos y densidad del terreno,
 *      la distribucion de praderas/claros (foliageDistribution), las especies de pasto/flores
 *      interactivas (groundFoliage) y las probabilidades de transicion climatica.
 *   2. Climas (assets/data/weather/*.json): definen atenuacion solar/ambiental, nieve objetivo,
 *      rangos de viento y emisor de particulas atmosfericas (lluvia, ventisca).
 *   3. Campo de Viento: mantiene direccion, velocidad y fases integradas continuamente para
 *      animar arboles, pasto interactivo y particulas sin saltos bruscos durante transiciones.
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

    /**
     * @brief Regla ponderada de transicion dentro de la Matriz de Markov del bioma.
     */
    struct WeatherTransitionRule {
        std::string targetWeather;      // ID del clima destino (ej. "heavy_rain", "blizzard").
        float       probability = 1.0f; // Peso relativo (no exige sumar 1.0 exacto; se normaliza al tirar).
    };

    /**
     * @brief Control modular de densidad y separacion espacial de praderas por bioma (100% JSON).
     */
    struct FoliageDistributionConfig {
        float clumpStep = 1.45f; // Separacion base de la grilla de cumulos en unidades de mundo.
        float patchScale = 0.14f; // Frecuencia del ValueNoise2D (menor = praderas mas amplias).
        float patchSeparation = 0.36f; // Umbral [0.0..0.9]: mayor valor abre claros de tierra mas grandes.
        float globalDensity = 1.0f;  // Multiplicador global de abundancia de follaje del bioma.
    };

    /**
     * @brief Especie de vegetacion o detalle a ras del suelo definida en el JSON del bioma.
     * @details Agnóstica al genero: sirve igual para pasto y flores en fantasia que para cables,
     *          hongos bioluminiscentes o chatarra flexible en sci-fi.
     */
    struct GroundFoliageDef {
        std::string id;
        bool        isTallBlade = true;   // true: selecciona /, |, \ segun inclinacion; false: usa glyphs.
        std::string glyphs = "*ov+"; // Caracteres de flor o detalle superior cuando isTallBlade == false.
        ColorRGBA   baseColor = { 34, 82, 42, 255 };
        ColorRGBA   tipColor = { 86, 156, 78, 255 };
        float       spawnChance = 0.20f;  // Probabilidad base de elegir esta especie en una mata valida.
        float       heightZ = 0.90f;  // Altura nominal en unidades de mundo (varia por brizna).
        float       windWeight = 0.90f;  // Sensibilidad al campo vectorial de viento.
        int         minBlades = 2;      // Minimo de instancias generadas dentro de una misma mata.
        int         maxBlades = 4;      // Maximo de instancias generadas dentro de una misma mata.
        float       clumpSpread = 1.15f;  // Radio de dispersion espacial de las briznas en la mata.
        bool        interactive = true;   // true: se aparta y aplasta al paso de jugadores y NPCs.
    };

    /**
     * @brief Definicion completa en RAM de un bioma leido desde assets/data/biomes/*.json.
     */
    struct BiomeDef {
        std::string id;
        std::string name;

        // Estilo data-driven del terreno base (consumido por LightingSystem via WeatherModifiers).
        ColorRGBA   baseGroundColor = { 74, 98, 72, 255 };
        ColorRGBA   altGroundColor = { 58, 78, 56, 255 };
        std::string groundGlyphs = ".,'";
        std::string shadowGlyphs = ":;,";
        float       groundDensity = 0.35f;

        float       baseSnowCoverage = 0.0f;  // Piso minimo de nieve del bioma (ej. 0.75 en Tundra Helada).
        float       windMultiplier = 1.0f;  // Factor multiplicativo sobre la velocidad del viento del clima.
        float       minStateSeconds = 35.0f; // Duracion minima de un estado climatico antes de sortear otro.
        float       maxStateSeconds = 70.0f; // Duracion maxima de un estado climatico.

        std::vector<WeatherTransitionRule> weatherTransitions;
        FoliageDistributionConfig          foliageDistribution;
        std::vector<GroundFoliageDef>      groundFoliage;
    };

    /**
     * @brief Definicion completa en RAM de un clima leido desde assets/data/weather/*.json.
     */
    struct WeatherDef {
        std::string id;
        std::string name;
        float       transitionDuration = 4.0f;  // Segundos de interpolacion suave (Smoothstep) al entrar.
        ColorRGBA   bgTint = { 50, 54, 60, 255 };
        float       bgTintBlend = 0.0f;  // Peso [0..1] del tinte atmosferico sobre el fondo.
        float       sunMultiplier = 1.0f;  // Multiplicador de intensidad solar/lunar.
        float       ambientMultiplier = 1.0f;  // Multiplicador de fuerza de GI hemisferica.
        float       windMinSpeed = 1.5f;  // Rango de velocidad de viento sorteado al iniciar el clima.
        float       windMaxSpeed = 3.0f;
        float       windGustStrength = 1.0f;
        float       snowTargetCoverage = 0.0f;  // Cobertura de nieve objetivo [0.0..1.0].
        float       snowChangeRate = 0.12f; // Velocidad de acumulacion o derretimiento (unidades/seg).
        std::string particleTemplateId;          // Plantilla en ParticleSystem (vacio si despejado).
        float       weatherParticleRate = 0.0f;  // Particulas por segundo a emitir alrededor del foco.
    };

    class WeatherSystem {
    public:
        /**
         * @brief Escanea y carga todos los biomas .json dentro de assets/data/biomes/.
         */
        bool LoadBiomesDirectory(const std::string& biomesDir);

        /**
         * @brief Escanea y carga todos los climas .json dentro de assets/data/weather/.
         */
        bool LoadWeathersDirectory(const std::string& weatherDir);

        /**
         * @brief Aplica de inmediato el bioma, clima y viento inicial leidos del mapa JSON (sin fade inicial).
         */
        void Configure(std::string_view biomeId, std::string_view initialWeatherId,
            float windDirDeg, float windBaseSpeed, float windGustStrength,
            bool randomTransitions);

        /**
         * @brief Inicia una transicion suave (Smoothstep) desde el estado actual hacia 'weatherId'.
         * @details Captura una instantanea de partida para permitir encadenar cambios incluso a mitad
         *          de una transicion previa sin saltos visuales ni de viento.
         */
        void TransitionToWeather(std::string_view weatherId);

        /**
         * @brief Rota al siguiente bioma cargado (tecla [M] o boton en el Editor).
         */
        void CycleBiome();

        void ToggleRandomTransitions() { m_randomTransitions = !m_randomTransitions; }

        /**
         * @brief Avanza el temporizador de Markov, interpola modificadores/nieve e integra fases del viento.
         * @complexity O(1).
         */
        void Update(float dt);

        /**
         * @brief Evalua el desplazamiento XY por viento para un punto del mundo segun su altura relativa.
         * @param heightNorm Altura normalizada [0.0 en la base anclada .. 1.0 en la copa/punta].
         * @param windWeight Flexibilidad del material o especie [0.0 rigido .. 1.0+ muy flexible].
         * @complexity O(1).
         */
        Vec2 SampleWindOffset(float worldX, float worldY, float heightNorm, float windWeight) const;

        // --- Consultas de solo lectura para LightingSystem, Engine y DebugHUD ---
        const BiomeDef* GetActiveBiome() const { return m_biomes.empty() ? nullptr : &m_biomes[m_activeBiomeIdx]; }
        const WeatherModifiers& GetModifiers() const { return m_currentMods; }
        const WindState& GetWind() const { return m_wind; }
        const std::string& GetWeatherParticleId() const { return m_activeParticleId; }
        float                   GetWeatherParticleRate() const { return m_activeParticleRate; }
        const std::string& GetActiveBiomeName() const { return m_activeBiomeName; }
        const std::string& GetActiveWeatherName() const { return m_activeWeatherName; }
        bool                    AreRandomTransitionsOn() const { return m_randomTransitions; }
        float                   GetNextTransitionSec() const { return m_stateTimer; }

        // --- Accesores de edicion en vivo para el Editor Visual (Fase 7) ---
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

    private:
        /**
         * @brief Copia los glifos de suelo, glifos de sombra y densidad del bioma al struct POD m_currentMods.
         */
        void ApplyBiomeGroundStyle(const BiomeDef& b);

        /**
         * @brief Sortea el siguiente clima usando muestreo proporcional (ruleta) sobre la Matriz de Markov.
         */
        void RollNextWeather();

        /**
         * @brief Reinicia el temporizador aleatorio entre minStateSeconds y maxStateSeconds del bioma activo.
         */
        void ResetStateTimer();

        std::vector<BiomeDef>                   m_biomes;
        std::unordered_map<std::string, size_t> m_biomeMap;
        std::vector<WeatherDef>                 m_weathers;
        std::unordered_map<std::string, size_t> m_weatherMap;

        size_t m_activeBiomeIdx = 0;
        size_t m_targetWeatherIdx = 0;

        // Instantaneas origen/actual y objetivos de interpolacion para transiciones fluidas.
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

        // Generador Mersenne Twister con semilla fija inicial para transiciones deterministas en pruebas.
        std::mt19937 m_rng{ 4201u };
    };

} // namespace AsciiEngine