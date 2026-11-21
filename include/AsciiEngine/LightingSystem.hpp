/**
 * @file LightingSystem.hpp
 * @brief Sistema de iluminacion dinamica: Ciclo Dia-Noche, GI Hemisferica, Luces Puntuales y Sombras 2.5D.
 *
 * @details Emplea un mapa de alturas 2D (Heightfield) construido al cargar el mapa para proyectar
 * sombras direccionales en O(1) sin raytracing 3D ni asignaciones en el bucle principal.
 * Dia-Noche, GI Hemisferica, Luces, Sombras 2.5D, Nieve y Agua.
 */
#pragma once

#include <AsciiEngine/Types.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <cmath>

namespace AsciiEngine {

    class World;
    class MaterialSystem;
    struct SurfaceZone;

    // Configuracion del ciclo astronomico de 24 horas (leida desde map JSON).
    struct DayNightConfig {
        bool  enabled = true;
        float currentTimeHours = 10.5f;  // [0.0 .. 24.0)
        float dayLengthSeconds = 120.0f; // Duracion de un dia completo en segundos reales.
    };

    // Parametros base de iluminacion global hemisferica (cielo vs rebote de suelo).
    struct GlobalIlluminationConfig {
        ColorRGBA skyColor = { 175, 205, 235, 255 };
        ColorRGBA groundColor = { 38, 48, 36, 255 };
        float     ambientStrength = 0.32f;
    };

    // Fuente de luz puntual estatica (JSON) o dinamica (antorcha del jugador).
    struct PointLight {
        Vec3      position = {};
        ColorRGBA color = { 255, 160, 75, 255 };
        float     intensity = 1.2f;
        float     radius = 10.0f;
        bool      flicker = false;
        float     flickerPhase = 0.0f;
    };

    // Estado astronomico y atmosferico precalculado una sola vez por frame en Update().
    struct SunState {
        Vec3      lightDir = { 0.5f, 0.35f, 0.78f }; // Direccion del sol o de la luna.
        ColorRGBA lightColor = { 255, 248, 230, 255 };
        float     lightIntensity = 1.0f;
        bool      isNight = false;
        // Colores ambientales modulados por la hora del dia.
        ColorRGBA skyAmbient = { 150, 180, 215, 255 };
        ColorRGBA groundAmbient = { 10, 10, 10, 255 };
        float     ambientStrength = 0.32f;
        // Color de fondo dinamico modulado desde el gris oscuro neutro base.
        ColorRGBA backgroundColor = { 8, 8, 8, 255 };
        // Oscurecimiento de zonas en sombra (mas suave de noche bajo la luna).
        float     shadowDarkness = 0.34f;
    };

    // Resultado final de sombreado listo para enviar al AsciiZBuffer.
    struct ShadedAtom {
        char      glyph = '.';
        ColorRGBA color = { 255, 255, 255, 255 };
    };

    // Estructura ligera en caliente usada por los atomos durante el frame (con parpadeo ya aplicado).
    struct ActiveFrameLight {
        Vec3  position;
        float radiusSq;
        float invRadius;
        float rNorm;     // Color normalizado [0..1] * intensidad efectiva.
        float gNorm;
        float bNorm;
        float intensity;
    };

    class LightingSystem {
    public:
        // Configura el sistema con los datos cargados del mapa JSON.
        void Configure(const DayNightConfig& dayNight,
            const GlobalIlluminationConfig& gi,
            const std::vector<PointLight>& mapLights);

        void BuildShadowHeightfield(const World& world, const MaterialSystem& materials, float cellSizeWorld = 0.6f);

        // Avanza el ciclo dia-noche aplicando los modificadores climaticos activos (Fase 5).
        void Update(float dt, const WeatherModifiers& weatherMod = {});

        // Filtra las luces estaticas dentro del area visible + anade la antorcha dinamica si se provee.
        void PrepareFrameLights(float minWX, float maxWX, float minWY, float maxWY,
            const PointLight* dynamicTorch = nullptr);

        // Evalua si un punto 3D cae en sombra respecto al sol/luna usando raymarching 2D O(1).
        // Retorna 1.0 si recibe luz directa, o sunState.shadowDarkness si esta ocluido.
        float SampleSunShadow(Vec3 worldPt) const;

        // Evalua el modelo completo (Sol/Luna + Sombras 2.5D + GI Hemisferica + Luces Puntuales)
        // y mapea la luminancia final a la rampa ASCII y colorizacion de 3 paradas del material.
        ShadedAtom ShadeAtom(Vec3 worldPt, Vec3 normal, float ao,
            const MaterialSystem& materials, MaterialId matId,
            bool receiveShadows, char structuralOverride = '\0') const;

        // Sombreado rapido especializado para el terreno base (Z = 0, normal = (0, 0, 1)).
        ShadedAtom ShadeGroundPoint(Vec3 worldPt, float terrainNoise, bool receiveShadows) const;

        // Sombreado de superficie de agua con oleaje ASCII, brillos de luz y sombras.
        ShadedAtom ShadeWaterPoint(Vec3 worldPt, ColorRGBA deepCol, ColorRGBA shallowCol,
            float depthFactor, float waveSpeed, bool receiveShadows) const;

        // Sombreado para objetos de suelo transitables (puentes, pisos de casas, rejillas sci-fi).
        ShadedAtom ShadeSurfacePoint(Vec3 worldPt, const SurfaceZone& zone, float noise, bool receiveShadows) const;

        // Sombreado liviano para pasto y flores interactivas (soporta sombras, nieve y luces puntuales).
        ColorRGBA ShadeFoliageColor(Vec3 worldPt, ColorRGBA baseColor, bool receiveShadows) const;

        void AdjustTimeHours(float deltaHours);
        void ToggleCycleRunning() { m_dayNight.enabled = !m_dayNight.enabled; }
        void ToggleShadows() { m_shadowsEnabled = !m_shadowsEnabled; }

        // Setters/getters de edicion en vivo para el Editor de Mapas (Fase 7).
        void SetTimeHours(float hours) {
            m_dayNight.currentTimeHours = std::fmod(hours, 24.0f);
            if (m_dayNight.currentTimeHours < 0.0f) m_dayNight.currentTimeHours += 24.0f;
            RecomputeSunState();
        }
        void  SetDayLengthSeconds(float sec) { m_dayNight.dayLengthSeconds = (sec > 5.0f) ? sec : 5.0f; }
        float GetDayLengthSeconds() const { return m_dayNight.dayLengthSeconds; }
        void  SetCycleRunning(bool on) { m_dayNight.enabled = on; }
        void  SetShadowsEnabled(bool on) { m_shadowsEnabled = on; }

        DayNightConfig                  GetDayNightConfig() const { return m_dayNight; }
        const GlobalIlluminationConfig& GetGIConfig() const { return m_giConfig; }
        GlobalIlluminationConfig& GetMutableGIConfig() { return m_giConfig; }
        std::vector<PointLight>& GetMutableMapLights() { return m_mapLights; }

        bool  IsCycleRunning() const { return m_dayNight.enabled; }
        bool  AreShadowsEnabled() const { return m_shadowsEnabled; }
        float GetTimeHours() const { return m_dayNight.currentTimeHours; }

        const SunState& GetSunState() const { return m_sunState; }
        const std::vector<ActiveFrameLight>& GetActiveLights() const { return m_activeLights; }
        const std::vector<PointLight>& GetMapLights() const { return m_mapLights; }
        size_t                               GetHeightfieldBytes() const;

    private:
        void RecomputeSunState();
        float SampleHeightAt(float worldX, float worldY) const;

        DayNightConfig           m_dayNight;
        GlobalIlluminationConfig m_giConfig;
        std::vector<PointLight>  m_mapLights;
        WeatherModifiers         m_weatherMod;

        // Lista reutilizable de luces visibles este frame (cero asignaciones en el bucle).
        std::vector<ActiveFrameLight> m_activeLights;

        SunState m_sunState;
        float    m_elapsedTime = 0.0f;
        bool     m_shadowsEnabled = true;

        // Mapa de alturas 2D (Heightfield) para sombras 2.5D rapidas.
        float              m_hfOriginX = -140.0f;
        float              m_hfOriginY = -140.0f;
        float              m_hfCellSize = 0.6f;
        float              m_hfInvCellSize = 1.0f / 0.6f;
        int                m_hfWidth = 0;
        int                m_hfHeight = 0;
        float              m_hfMaxWorldZ = 0.0f; // Permite salida temprana al trazar rayos.
        std::vector<float> m_heightfield;
    };

} // namespace AsciiEngine