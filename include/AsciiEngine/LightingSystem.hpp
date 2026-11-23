// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file LightingSystem.hpp
 * @brief Sistema de iluminacion dinamica: Ciclo Dia-Noche, GI Hemisferica, Luces Puntuales,
 *        Sombras 2.5D por Heightfield, Nieve climatica y Sombreado de Agua/Superficies.
 *
 * @details Arquitectura enfocada en rendimiento O(1) por atomo sin raytracing 3D ni shaders complejos:
 *   1. Precalculo por frame (Update): las curvas astronomicas (Sol/Luna, GI, color de fondo) se
 *      evaluan UNA SOLA VEZ por frame y se guardan en SunState.
 *   2. Culling de luces (PrepareFrameLights): solo las luces cuyo radio intersecta el area visible
 *      pasan al vector compacto ActiveFrameLight (con parpadeo e inversos ya precalculados).
 *   3. Sombras 2.5D (Heightfield): al cargar o editar el mapa se construye una rejilla 2D con la
 *      altura maxima Z de los objetos opacos; cada sombra se traza en hasta 14 pasos lineales O(1).
 */
#pragma once

#include <AsciiEngine/Types.hpp>
#include <cmath>   // std::fmod usado en SetTimeHours().
#include <cstddef> // size_t para telemetria de memoria.
#include <cstdint>
#include <vector>

namespace AsciiEngine {

    // Declaraciones adelantadas: mantienen este header desacoplado de World.hpp y MaterialSystem.hpp.
    class World;
    class MaterialSystem;
    struct SurfaceZone;

    /**
     * @brief Configuracion del ciclo astronomico de 24 horas leida desde el mapa JSON.
     */
    struct DayNightConfig {
        bool  enabled = true;   // true = el reloj avanza automaticamente cada frame.
        float currentTimeHours = 10.5f;  // Hora actual en el intervalo continuo [0.0 .. 24.0).
        float dayLengthSeconds = 120.0f; // Cuantos segundos reales tarda un dia completo de 24h.
    };

    /**
     * @brief Parametros base de Iluminacion Global Hemisferica (GI).
     *
     * @details Aproxima el rebote indirecto de luz sin coste de radiosity: las caras que apuntan
     * hacia arriba (+Z) reciben tinte de skyColor y las que apuntan abajo (-Z) reciben groundColor.
     */
    struct GlobalIlluminationConfig {
        ColorRGBA skyColor = { 175, 205, 235, 255 }; // Tinte ambiental del cielo despejado.
        ColorRGBA groundColor = { 38, 48, 36, 255 };    // Tinte de rebote terrestre/vegetacion.
        float     ambientStrength = 0.32f;                  // Peso base de la GI sobre la escena.
    };

    /**
     * @brief Fuente de luz puntual estatica (leida del JSON) o dinamica (antorcha del jugador).
     */
    struct PointLight {
        Vec3      position = {};
        ColorRGBA color = { 255, 160, 75, 255 };
        float     intensity = 1.2f;  // Brillo maximo en el origen de la luz.
        float     radius = 10.0f; // Distancia maxima de influencia (atenuacion cae a 0 en radius).
        bool      flicker = false; // true = modula intensidad y radio como fuego/energia viva.
        float     flickerPhase = 0.0f;  // Desfase unico para evitar sincronia entre fogatas.
    };

    /**
     * @brief Estado astronomico y atmosferico precalculado una vez por frame en Update().
     */
    struct SunState {
        Vec3      lightDir = { 0.5f, 0.35f, 0.78f }; // Vector unitario hacia el Sol (dia) o Luna (noche).
        ColorRGBA lightColor = { 255, 248, 230, 255 }; // Tinte de la luz direccional activa.
        float     lightIntensity = 1.0f;                   // Intensidad efectiva tras modular por hora y clima.
        bool      isNight = false;                  // true cuando sin(theta) < 0 (18:00 a 06:00).

        ColorRGBA skyAmbient = { 150, 180, 215, 255 }; // Color de cielo modulado por hora y crepusculo.
        ColorRGBA groundAmbient = { 35, 45, 35, 255 };    // Color de rebote terrestre modulado por hora y nieve.
        float     ambientStrength = 0.32f;                  // Fuerza final de la GI hemisferica del frame.
        ColorRGBA backgroundColor = { 22, 25, 29, 255 };    // Color de fondo terminal modulado continuamente.
        float     shadowDarkness = 0.34f;                  // Factor de atenuacion en sombra [0.25 dia .. 0.55 noche].
    };

    /**
     * @brief Resultado final de sombreado (caracter + color) listo para enviar al AsciiZBuffer.
     */
    struct ShadedAtom {
        char      glyph = '.';
        ColorRGBA color = { 255, 255, 255, 255 };
    };

    /**
     * @brief Version compacta en caliente de una PointLight preparada para el bucle de renderizado.
     *
     * @details Precalcula radiusSq (evita sqrt en rechazos), invRadius (evita division en atenuacion)
     * y rNorm/gNorm/bNorm ya multiplicados por la intensidad con parpadeo aplicado.
     */
    struct ActiveFrameLight {
        Vec3  position;
        float radiusSq;  // radius^2 para descarte rapido antes de std::sqrt.
        float invRadius; // 1.0f / radius para calcular falloff = 1 - d * invRadius.
        float rNorm;     // (color.r / 255.0f) * effIntensity.
        float gNorm;     // (color.g / 255.0f) * effIntensity.
        float bNorm;     // (color.b / 255.0f) * effIntensity.
        float intensity; // Intensidad escalar efectiva del frame.
    };

    /**
     * @brief Luz puntual efimera con tiempo de vida finito (explosiones, impactos, hechizos).
     */
    struct TemporaryLight {
        PointLight light;
        float      remainingLife = 0.0f;
        float      maxLife = 1.0f;
        bool       fadeOut = true; // true = atenuacion lineal de intensidad y radio al agotarse.
    };

    class LightingSystem {
    public:
        /**
         * @brief Inicializa el sistema con la configuracion astronomica, GI y luces del mapa JSON.
         */
        void Configure(const DayNightConfig& dayNight,
            const GlobalIlluminationConfig& gi,
            const std::vector<PointLight>& mapLights);

        /**
         * @brief Rasteriza la altura maxima Z de todos los objetos opacos en una grilla 2D (Heightfield).
         * @details Se ejecuta una sola vez al cargar el mapa o tras editar geometria en el Editor.
         * @param cellSizeWorld Tamano de cada celda en unidades de mundo (0.6u captura troncos finos).
         */
        void BuildShadowHeightfield(const World& world, const MaterialSystem& materials, float cellSizeWorld = 0.6f);

        /**
         * @brief Avanza el reloj astronomico, guarda los modificadores climaticos y recalcula SunState.
         * @complexity O(1).
         */
        void Update(float dt, const WeatherModifiers& weatherMod = {});

        /**
         * @brief Filtra las luces del mapa al rectangulo visible [minWX..maxWX, minWY..maxWY]
         *        y anade opcionalmente una luz dinamica (antorcha del jugador).
         * @details Reutiliza la capacidad de m_activeLights: cero asignaciones en el heap por frame.
         * @complexity O(L) donde L es el total de luces del mapa.
         */
        void PrepareFrameLights(float minWX, float maxWX, float minWY, float maxWY,
            const PointLight* dynamicTorch = nullptr);

        /**
         * @brief Anade una luz puntual temporal que se atenua y elimina sola en O(1) al vencer durationSec.
         */
        void AddTemporaryLight(Vec3 pos, ColorRGBA color, float intensity, float radius,
            float durationSec, bool fadeOut = true);

        void ClearTemporaryLights() { m_tempLights.clear(); }

        /**
         * @brief Traza un rayo 2D en direccion al Sol/Luna sobre el Heightfield para detectar oclusion.
         * @return 1.0f si el punto recibe luz directa; m_sunState.shadowDarkness si cae en sombra.
         * @complexity O(1) acotado a un maximo estricto de 14 pasos con salida temprana por altura.
         */
        float SampleSunShadow(Vec3 worldPt) const;

        /**
         * @brief Evalua el modelo completo de sombreado para objetos 3D y personajes modulares:
         *        Sol/Luna (Lambert) + Sombras 2.5D + GI Hemisferica + Luces Puntuales + Nieve + Contraste.
         * @complexity O(A) donde A es el numero de luces puntuales activas dentro del frustum.
         */
        ShadedAtom ShadeAtom(Vec3 worldPt, Vec3 normal, float ao,
            const MaterialSystem& materials, MaterialId matId,
            bool receiveShadows, char structuralOverride = '\0') const;

        /**
         * @brief Sombreado especializado para el terreno base (Z = 0, normal fija (0,0,1)) usando
         *        los glifos, densidad y colores definidos en el JSON del bioma activo.
         */
        ShadedAtom ShadeGroundPoint(Vec3 worldPt, float terrainNoise, bool receiveShadows) const;

        /**
         * @brief Sombreado animado para cuerpos de agua (oleaje dual, tinte de costa/profundidad,
         *        reflejo del cielo, halos de luces puntuales y destellos especulares en crestas).
         */
        ShadedAtom ShadeWaterPoint(Vec3 worldPt, ColorRGBA deepCol, ColorRGBA shallowCol,
            float depthFactor, float waveSpeed, bool receiveShadows) const;

        /**
         * @brief Sombreado para objetos de suelo transitables (puentes, patios, pisos de casas)
         *        evaluando su SurfacePattern procedural, nieve y sombras.
         */
        ShadedAtom ShadeSurfacePoint(Vec3 worldPt, const SurfaceZone& zone, float noise, bool receiveShadows) const;

        /**
         * @brief Sombreado ligero para pasto interactivo y flores (omite lookup de rampas ASCII).
         */
        ColorRGBA ShadeFoliageColor(Vec3 worldPt, ColorRGBA baseColor, bool receiveShadows) const;

        // --- Controles interactivos y accesores de edicion en vivo (Editor Visual Fase 7) ---
        void AdjustTimeHours(float deltaHours);
        void ToggleCycleRunning() { m_dayNight.enabled = !m_dayNight.enabled; }
        void ToggleShadows() { m_shadowsEnabled = !m_shadowsEnabled; }

        void SetTimeHours(float hours) {
            m_dayNight.currentTimeHours = std::fmod(hours, 24.0f);
            if (m_dayNight.currentTimeHours < 0.0f) m_dayNight.currentTimeHours += 24.0f;
            RecomputeSunState();
        }
        void  SetDayLengthSeconds(float sec) { m_dayNight.dayLengthSeconds = (sec > 5.0f) ? sec : 5.0f; }
        float GetDayLengthSeconds() const { return m_dayNight.dayLengthSeconds; }
        void  SetCycleRunning(bool on) { m_dayNight.enabled = on; }
        void  SetShadowsEnabled(bool on) { m_shadowsEnabled = on; }

        // Ajustes de calidad grafica sincronizados desde EngineSettings (v1.1.0).
        void SetShadowRaySteps(int steps) { m_shadowRaySteps = (steps < 0) ? 0 : ((steps > 20) ? 20 : steps); }
        void SetPointLightsEnabled(bool on) { m_pointLightsEnabled = on; }
        void SetGIStrengthScale(float scale) { m_giStrengthScale = (scale < 0.0f) ? 0.0f : scale; RecomputeSunState(); }
        void SetAtmosphereInfluence(float infl) { m_atmosphereInfluence = (infl < 0.0f) ? 0.0f : ((infl > 1.0f) ? 1.0f : infl); RecomputeSunState(); }

        bool  IsCycleRunning() const { return m_dayNight.enabled; }
        bool  AreShadowsEnabled() const { return m_shadowsEnabled; }
        float GetTimeHours() const { return m_dayNight.currentTimeHours; }

        DayNightConfig                       GetDayNightConfig() const { return m_dayNight; }
        const GlobalIlluminationConfig& GetGIConfig() const { return m_giConfig; }
        GlobalIlluminationConfig& GetMutableGIConfig() { return m_giConfig; }
        std::vector<PointLight>& GetMutableMapLights() { return m_mapLights; }
        const SunState& GetSunState() const { return m_sunState; }
        const std::vector<ActiveFrameLight>& GetActiveLights() const { return m_activeLights; }
        const std::vector<PointLight>& GetMapLights() const { return m_mapLights; }
        size_t                               GetHeightfieldBytes() const;

    private:
        /**
         * @brief Recalcula todas las variables de m_sunState con curvas C1 continuas (sin saltos en 6h/18h).
         */
        void RecomputeSunState();

        /**
         * @brief Consulta O(1) de la elevacion maxima Z registrada en el Heightfield para (worldX, worldY).
         */
        float SampleHeightAt(float worldX, float worldY) const;

        DayNightConfig              m_dayNight;
        GlobalIlluminationConfig    m_giConfig;
        std::vector<PointLight>     m_mapLights;
        std::vector<TemporaryLight> m_tempLights;
        WeatherModifiers            m_weatherMod;

        // Lista pre-reservada de luces visibles en el frame actual (cero asignaciones en Render).
        std::vector<ActiveFrameLight> m_activeLights;

        SunState m_sunState;
        float    m_elapsedTime = 0.0f; // Reloj continuo para parpadeo de luces y animacion de olas.
        bool     m_shadowsEnabled = true;

        int   m_shadowRaySteps = 14;
        bool  m_pointLightsEnabled = true;
        float m_giStrengthScale = 1.0f;
        float m_atmosphereInfluence = 1.0f;

        // Mapa de alturas 2D plano (Heightfield) para raymarching de sombras 2.5D.
        float              m_hfOriginX = -140.0f;
        float              m_hfOriginY = -140.0f;
        float              m_hfCellSize = 0.6f;
        float              m_hfInvCellSize = 1.0f / 0.6f; // Multiplicacion rapida en lugar de division.
        int                m_hfWidth = 0;
        int                m_hfHeight = 0;
        float              m_hfMaxWorldZ = 0.0f;        // Cota superior global para salida temprana de rayos.
        std::vector<float> m_heightfield;
    };

} // namespace AsciiEngine