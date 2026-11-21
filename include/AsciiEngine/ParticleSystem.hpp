// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file ParticleSystem.hpp
 * @brief Sistema de particulas 100% data-driven con Pool SoA (Structure of Arrays) de tamano fijo.
 *
 * @details Decisiones clave de Data-Oriented Design:
 *   1. Cero asignaciones en el bucle principal: pre-aloja 2,500 particulas (~85 KB fijos) una sola
 *      vez al arrancar el motor.
 *   2. Layout SoA (arreglos separados por atributo): durante la integracion fisica, la CPU carga
 *      bloques contiguos de floats en cache L1 (ideal para auto-vectorizacion SIMD).
 *   3. Borrado O(1) Swap-with-Last: cuando una particula muere, se copia la ultima particula viva
 *      en su ranura y se decrementa m_activeCount, manteniendo [0..m_activeCount) siempre compacto.
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

    /**
     * @brief Plantilla de comportamiento de particulas leida desde assets/data/particles/*.json.
     */
    struct ParticleTemplate {
        std::string id;
        float       spawnRate = 20.0f; // Tasa por defecto si el emisor no especifica override.
        float       lifeMin = 0.5f;  // Tiempo de vida minimo en segundos.
        float       lifeMax = 1.2f;  // Tiempo de vida maximo en segundos.
        Vec3        velMin = { -0.5f, -0.5f, 0.5f };
        Vec3        velMax = { 0.5f,  0.5f, 1.5f };
        float       gravityZ = -2.0f; // Aceleracion vertical (negativa cae; positiva flota como chispas).
        float       windInfluence = 0.5f;  // Fraccion del vector de viento aplicada a la velocidad XY.
        bool        collideWithFloor = true;  // true: deja un breve destello/impacto en Z=0 antes de morir.
        bool        spawnHighVolume = false; // true: nace alto en el cielo (lluvia/nieve) sobre la camara.

        // Secuencia de caracteres y gradiente de color interpolados segun progreso de vida [0..1].
        std::vector<char>      asciiOverLife;
        std::vector<ColorRGBA> colorOverLife;
    };

    /**
     * @brief Emisor estatico vinculado a un punto del escenario (ej. chispas sobre braseros/antorchas).
     */
    struct WorldParticleEmitter {
        Vec3     position = {};
        Vec3     areaHalfExtents = {};    // Volumen aleatorio alrededor de position donde nacen particulas.
        uint16_t templateIdx = 0;     // Indice directo a ParticleSystem::m_templates.
        float    spawnRate = 10.0f; // Particulas por segundo.
        float    accumulator = 0.0f;  // Residuo fraccionario entre frames (independiente de los FPS).
        bool     nightOnly = false; // true: solo emite durante el periodo nocturno.
    };

    class ParticleSystem {
    public:
        // Tope rigido del pool pre-alojado: 2,500 particulas x 34 bytes = ~85 KB constantes en RAM.
        static constexpr size_t MAX_PARTICLES = 2500;

        /**
         * @brief Pre-aloja todos los arreglos SoA una unica vez al instanciar el subsistema.
         */
        ParticleSystem();

        /**
         * @brief Escanea y carga todas las plantillas .json dentro de assets/data/particles/.
         */
        bool LoadTemplatesDirectory(const std::string& particlesDir);

        /**
         * @brief Limpia particulas vivas y emisores fijos al cargar un mapa nuevo o sincronizar luces.
         */
        void ClearWorldState();

        /**
         * @brief Registra un emisor fijo en el mundo usando el ID de una plantilla cargada.
         */
        void AddWorldEmitter(Vec3 position, Vec3 areaHalfExtents,
            std::string_view templateId,
            float rateOverride = 0.0f,
            bool nightOnly = false);

        /**
         * @brief Emite nuevas particulas (emisores fijos, clima y luciernagas) e integra fisica SoA.
         * @complexity O(P) donde P = m_activeCount (acotado superiormente por MAX_PARTICLES).
         */
        void Update(float dt, const WindState& wind, bool isNight,
            Vec2 cameraWorldCenter,
            std::string_view weatherParticleId, float weatherSpawnRate);

        /**
         * @brief Proyecta y envia las particulas vivas dentro del viewport al AsciiZBuffer.
         * @param renderStride 1 = dibuja todas; 2 = dibuja la mitad (LOD de zoom alejado).
         * @complexity O(P / renderStride).
         */
        void SubmitToZBuffer(AsciiZBuffer& zBuffer, const MathIso::IsoConfig& isoConfig,
            float zoom, float camTopLeftX, float camTopLeftY,
            float screenW, float screenH, int renderStride = 1) const;

        uint32_t GetActiveCount() const { return static_cast<uint32_t>(m_activeCount); }
        uint32_t GetCapacity() const { return static_cast<uint32_t>(MAX_PARTICLES); }
        size_t   GetMemoryBytes() const;

    private:
        bool FindTemplateIndex(std::string_view id, uint16_t& outIdx) const;

        /**
         * @brief Inicializa una nueva particula en la ranura m_activeCount++ en O(1).
         */
        void SpawnOne(uint16_t templateIdx, Vec3 pos);

        // --- Arreglos SoA contiguos (Structure of Arrays): 8 floats (32B) + 1 uint16_t (2B) = 34B/particula ---
        std::vector<float>    m_posX;
        std::vector<float>    m_posY;
        std::vector<float>    m_posZ;
        std::vector<float>    m_velX;
        std::vector<float>    m_velY;
        std::vector<float>    m_velZ;
        std::vector<float>    m_age;
        std::vector<float>    m_maxAge;
        std::vector<uint16_t> m_templateIdx;

        // Frontera activa: las particulas vivas viven siempre en el rango compacto [0 .. m_activeCount).
        size_t m_activeCount = 0;

        // Registro de plantillas JSON y emisores fijos del mapa.
        std::vector<ParticleTemplate>             m_templates;
        std::unordered_map<std::string, uint16_t> m_templateMap;
        std::vector<WorldParticleEmitter>         m_worldEmitters;

        // Acumuladores fraccionarios para emision independiente de framerate en clima y luciernagas.
        float        m_weatherAccum = 0.0f;
        float        m_ambientNightAcc = 0.0f;
        std::mt19937 m_rng{ 1337u };
    };

} // namespace AsciiEngine