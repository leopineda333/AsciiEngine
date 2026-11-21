// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file CharacterSystem.hpp
 * @brief Sistema agnostico de personajes modulares 100% data-driven con cinematica directa y animacion procedural.
 *
 * @details Arquitectura sin anatomias hardcodeadas (sirve igual para humanoides, cuadrupedos, mechs o drones):
 *   1. Patron Flyweight: cada seccion (brazo, pata, cabeza, rotor) hornea su nube de puntos SDF UNA SOLA VEZ
 *      dentro de CharacterTemplate. Multiples lobos o soldados comparten los mismos atomos en RAM.
 *   2. Ordenamiento topologico al cargar: las secciones se reordenan para garantizar parentIdx < indice propio.
 *      Asi la cinematica directa (Forward Kinematics) se evalua en un bucle lineal O(S) sin recursion.
 *   3. Cero asignaciones en Render(): las poses mundiales temporales de las articulaciones se escriben sobre
 *      un buffer scratch pre-alojado (m_poseScratch).
 */
#pragma once

#include <AsciiEngine/Types.hpp>
#include <AsciiEngine/MathIso.hpp>
#include <AsciiEngine/World.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace AsciiEngine {

    // Declaraciones adelantadas para evitar dependencias circulares y mantener los headers ligeros.
    class MaterialSystem;
    class LightingSystem;
    class AsciiZBuffer;
    class SpatialHashGrid;
    struct ChunkCoord;

    // Identificador compacto de plantilla de personaje (2 bytes: hasta 65,534 anatomias distintas).
    using CharacterTemplateId = uint16_t;
    constexpr CharacterTemplateId INVALID_CHARACTER_ID = 0xFFFF;

    /**
     * @brief Estados discretos de la maquina de animacion procedural.
     */
    enum class AnimState : uint8_t {
        IDLE = 0, // Reposo (respiracion, levitacion, giro de cabeza o cola).
        WALK = 1, // Marcha normal.
        RUN = 2, // Carrera (si no tiene pista propia en JSON, hereda WALK acelerado).
        ATTACK = 3, // Accion/ataque temporal disparado con tecla [E].
        COUNT = 4  // Centinela para dimensionar std::array<AnimTrack, COUNT>.
    };

    /**
     * @brief Eje local sobre el cual oscila o rota una seccion en su pista procedural.
     * @details Convencion local del personaje antes de aplicar su facingAngleRad:
     *   +X = frente (hacia donde mira) | +Y = izquierda | +Z = arriba.
     */
    enum class AnimAxis : uint8_t {
        NONE = 0,
        X = 1, // Roll / balanceo lateral (ej. alas de dron).
        Y = 2, // Pitch / vaiven adelante-atras (ej. piernas, brazos, capa).
        Z = 3  // Yaw / giro horizontal o traslacion vertical (ej. helices, respiracion).
    };

    /**
     * @brief Pista de animacion sinusoidal o rotacion continua asignable a cualquier seccion y estado.
     */
    struct AnimTrack {
        bool     active = false;          // false si esta seccion no definio pista para este estado.
        AnimAxis posAxis = AnimAxis::NONE; // Eje de traslacion armonica: delta = sin(phase) * posAmplitude.
        float    posAmplitude = 0.0f;           // Amplitud de traslacion en unidades de mundo.
        AnimAxis rotAxis = AnimAxis::NONE; // Eje de rotacion local alrededor del pivote de la seccion.
        float    rotAmpRad = 0.0f;           // Amplitud angular maxima en radianes (convertida desde rotAmpDeg).
        float    frequency = 4.0f;           // Velocidad angular en radianes por segundo.
        float    phaseOffset = 0.0f;           // Desfase en radianes (ej. PI = 3.14159 para extremidad opuesta).
        bool     spinContinuous = false;          // true = angulo = phase continuo (rotores, radares, ruedas).
    };

    /**
     * @brief Seccion articulada horneada en RAM (compartida por todas las instancias via Flyweight).
     */
    struct BakedCharacterSection {
        std::string            id;                                     // Nombre unico dentro del personaje (ej. "torso").
        int16_t                parentIdx = -1;                       // -1 si es raiz; siempre < propio indice.
        MaterialId             materialId = FALLBACK_MATERIAL_ID;     // Material asignado a esta pieza.
        Vec3                   localOffset = {};                       // Posicion de la articulacion respecto al padre.
        std::vector<BakedAtom> atoms;                                  // Nube local ya desplazada por pivotOffset.
        std::array<AnimTrack, static_cast<size_t>(AnimState::COUNT)> tracks = {}; // 1 pista fija por cada AnimState.
    };

    /**
     * @brief Plantilla completa de criatura/personaje leida desde assets/data/characters/*.json.
     */
    struct CharacterTemplate {
        std::string id;                    // Clave JSON unica (ej. "wolf_quadruped").
        std::string name;                  // Nombre legible para el Debug HUD y Editor Visual.
        float       collisionRadius = 0.65f;
        float       walkSpeed = 13.0f;
        float       runSpeed = 22.0f;
        float       baseElevationZ = 0.0f; // > 0 para entidades levitantes o voladoras (ej. drones).
        float       boundRadiusXY = 1.2f; // Radio envolvente en XY precalculado para Frustum Culling 2D.
        float       boundHeightZ = 2.4f; // Altura maxima Z precalculada para Frustum y reflejos.
        std::vector<BakedCharacterSection> sections; // Arreglo ordenado topologicamente (padres antes que hijos).
    };

    /**
     * @brief Instancia viva en el escenario (controlada por el jugador o por IA simple de patrulla).
     */
    struct CharacterInstance {
        CharacterTemplateId templateId = INVALID_CHARACTER_ID;
        SimpleActor         actor;                         // Estado fisico 2D (posicion, velocidad, radio, angulo).
        AnimState           animState = AnimState::IDLE; // Estado actual de la maquina de animacion.
        float               animTime = 0.0f;            // Reloj interno de fase (avanza mas rapido en RUN).
        float               attackTimer = 0.0f;            // Tiempo restante en estado ATTACK (> 0 bloquea cambio).
        float               scale = 1.0f;            // Factor de escala uniforme de la instancia.
        bool                isPlayer = false;           // true = recibe entrada WASD/Shift/E del usuario.

        // Estado ligero de deambulacion autonoma cuando isPlayer == false.
        Vec2  homePos = {};   // Punto central de patrulla.
        Vec2  wanderTarget = {};   // Destino temporal actual.
        float wanderRadius = 8.0f; // Radio maximo de paseo alrededor de homePos (0 = estatico).
        float aiWaitTimer = 1.0f; // Pausa aleatoria en segundos al llegar a wanderTarget.
    };

    /**
     * @brief Pose mundial temporal de una seccion durante el pase de renderizado (48 bytes).
     * @details Almacenada en m_poseScratch para propagar transformaciones padre -> hijo en O(S).
     */
    struct SectionWorldPose {
        Vec3  position; // Posicion mundial 3D del pivote de esta seccion.
        float rot[9];   // Matriz de rotacion 3x3 acumulada en orden row-major (fila por fila).
    };

    class CharacterSystem {
    public:
        /**
         * @brief Pre-reserva el buffer scratch de poses para evitar asignaciones en tiempo de render.
         */
        CharacterSystem();

        /**
         * @brief Escanea assets/data/characters/*.json, hornea cada seccion y ordena topologicamente sus jerarquias.
         */
        bool LoadTemplatesDirectory(const std::string& charactersDir, const MaterialSystem& materials);

        /**
         * @brief Elimina todas las instancias vivas (al cargar un mapa nuevo o reiniciar la escena).
         */
        void ClearInstances();

        /**
         * @brief Crea una instancia viva a partir de una plantilla cargada.
         * @param isPlayer Si es true, transfiere el control del jugador a esta nueva instancia.
         */
        bool SpawnCharacter(std::string_view templateId, Vec2 worldPos, float facingDeg,
            bool isPlayer, float wanderRadius = 8.0f, float scale = 1.0f);

        /**
         * @brief Dispara el estado ATTACK durante 0.48s en el personaje controlado actualmente (tecla [E]).
         */
        void TriggerPlayerAttack();

        /**
         * @brief Transfiere el control del jugador al siguiente personaje vivo en la escena (tecla [TAB]).
         */
        void CycleControlledCharacter();

        /**
         * @brief Actualiza entrada/IA, fisica de colisiones, maquina de estados y relojes de animacion.
         * @complexity O(C * K) donde C es el numero de personajes y K las colisiones en chunks vecinos.
         */
        void Update(float dt, Vec2 playerDesiredWorldDir, bool playerSprint, float groundFriction,
            const World& world, SpatialHashGrid& spatialGrid,
            std::vector<uint32_t>& scratchIds, std::vector<ChunkCoord>& scratchChunks);

        /**
         * @brief Evalua cinematica directa 3D, ilumina y envia al Z-Buffer todos los personajes visibles
         *        (incluyendo sus reflejos ondulantes si caminan junto a una WaterZone).
         * @complexity O(C * (S + A)) donde S son las secciones y A los atomos de los personajes visibles.
         */
        void SubmitToZBuffer(AsciiZBuffer& zBuffer,
            const LightingSystem& lighting,
            const MaterialSystem& materials,
            const World& world,
            const MathIso::IsoConfig& isoConfig,
            const Rect2D& viewportRect,
            float zoom, float camTopLeftX, float camTopLeftY,
            float screenW, float screenH,
            int splatRadius, bool allowReflections, float windTime,
            uint32_t& atomsEvaluated, uint32_t& atomsCulled) const;

        // --- Consultas de estado y telemetria ---
        const CharacterInstance* GetControlledCharacter() const;
        const std::vector<CharacterInstance>& GetInstances() const { return m_instances; }
        uint32_t                              GetInstanceCount() const { return static_cast<uint32_t>(m_instances.size()); }
        std::string                           GetControlledName() const;
        const char* GetControlledAnimStateName() const;
        size_t                                GetBakedMemoryBytes() const;

        // --- Accesores de edicion y serializacion para el Editor Visual (Fase 7) ---
        const std::vector<CharacterTemplate>& GetTemplates() const { return m_templates; }
        std::vector<CharacterInstance>& GetMutableInstances() { return m_instances; }
        bool                                  RemoveCharacter(size_t index);
        std::vector<EntitySpawnDef>           ExportEntitySpawns() const;

    private:
        CharacterTemplateId FindTemplateId(std::string_view id) const;

        std::vector<CharacterTemplate>                       m_templates;
        std::unordered_map<std::string, CharacterTemplateId> m_templateMap;
        std::vector<CharacterInstance>                       m_instances;
        size_t                                               m_controlledIdx = 0;

        // Marcado 'mutable' para reutilizar su memoria contigua dentro de SubmitToZBuffer() const.
        mutable std::vector<SectionWorldPose> m_poseScratch;
        std::mt19937 m_rng{ 9001u };
    };

} // namespace AsciiEngine