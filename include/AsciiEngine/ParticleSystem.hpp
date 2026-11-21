/**
 * @file ParticleSystem.hpp
 * @brief Sistema de particulas 100% data-driven con Pool SoA de tamano fijo.
 *
 * @details Pre-aloja 2,500 particulas en arreglos contiguos (Structure of Arrays) al iniciar
 * el motor (~83 KB fijos en RAM). Utiliza borrado swap-with-last en O(1) para garantizar
 * cero asignaciones dinamicas en el bucle principal durante tormentas o ventiscas.
 */
#pragma once

#include <AsciiEngine/Types.hpp>
#include <AsciiEngine/MathIso.hpp>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace AsciiEngine {

    class AsciiZBuffer;

    // Plantilla de comportamiento de particulas cargada desde assets/data/particles/*.json.
    struct ParticleTemplate {
        std::string id;
        float       spawnRate = 20.0f;
        float       lifeMin = 0.5f;
        float       lifeMax = 1.2f;
        Vec3        velMin = { -0.5f, -0.5f, 0.5f };
        Vec3        velMax = { 0.5f,  0.5f, 1.5f };
        float       gravityZ = -2.0f;
        float       windInfluence = 0.5f;
        bool        collideWithFloor = true;
        bool        spawnHighVolume = false; // true para lluvia/nieve que nace en el cielo.

        std::vector<char>      asciiOverLife;
        std::vector<ColorRGBA> colorOverLife;
    };

    // Emisor fijo en el mundo (ej. chispas sobre fogatas o braseros del mapa).
    struct WorldParticleEmitter {
        Vec3     position = {};
        Vec3     areaHalfExtents = {};
        uint16_t templateIdx = 0;
        float    spawnRate = 10.0f;
        float    accumulator = 0.0f;
        bool     nightOnly = false;
    };

    class ParticleSystem {
    public:
        static constexpr size_t MAX_PARTICLES = 2500;

        ParticleSystem();

        // Escanea y carga todas las plantillas .json dentro de assets/data/particles/.
        bool LoadTemplatesDirectory(const std::string& particlesDir);

        // Reinicia particulas activas y emisores al cargar un mapa nuevo.
        void ClearWorldState();

        // Registra un emisor fijo en el escenario (si rateOverride <= 0 usa el del JSON).
        void AddWorldEmitter(Vec3 position, Vec3 areaHalfExtents,
            std::string_view templateId,
            float rateOverride = 0.0f,
            bool nightOnly = false);

        // Avanza emisores fijos, clima volumetrico, luciernagas nocturnas y fisica SoA.
        void Update(float dt, const WindState& wind, bool isNight,
            Vec2 cameraWorldCenter,
            std::string_view weatherParticleId, float weatherSpawnRate);

        // Proyecta e inserta las particulas visibles directamente en el AsciiZBuffer.
        void SubmitToZBuffer(AsciiZBuffer& zBuffer, const MathIso::IsoConfig& isoConfig,
            float zoom, float camTopLeftX, float camTopLeftY,
            float screenW, float screenH, int renderStride = 1) const;

        uint32_t GetActiveCount() const { return static_cast<uint32_t>(m_activeCount); }
        uint32_t GetCapacity() const { return static_cast<uint32_t>(MAX_PARTICLES); }
        size_t   GetMemoryBytes() const;

    private:
        bool FindTemplateIndex(std::string_view id, uint16_t& outIdx) const;
        void SpawnOne(uint16_t templateIdx, Vec3 pos);

        // Arreglos SoA contiguos (Structure of Arrays) para recorrido lineal amigable con cache.
        std::vector<float>    m_posX;
        std::vector<float>    m_posY;
        std::vector<float>    m_posZ;
        std::vector<float>    m_velX;
        std::vector<float>    m_velY;
        std::vector<float>    m_velZ;
        std::vector<float>    m_age;
        std::vector<float>    m_maxAge;
        std::vector<uint16_t> m_templateIdx;

        size_t m_activeCount = 0;

        // Registro de plantillas JSON y emisores del mapa.
        std::vector<ParticleTemplate>              m_templates;
        std::unordered_map<std::string, uint16_t>  m_templateMap;
        std::vector<WorldParticleEmitter>          m_worldEmitters;

        float        m_weatherAccum = 0.0f;
        float        m_ambientNightAcc = 0.0f;
        std::mt19937 m_rng{ 1337u };
    };

} // namespace AsciiEngine