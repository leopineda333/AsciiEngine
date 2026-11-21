/**
 * @file CharacterSystem.hpp
 * @brief Sistema agnostico de personajes modulares 100% data-driven con animacion procedural.
 *
 * @details Ninguna anatomia (humanoide, cuadrupedo, robot) esta hardcodeada en C++.
 * Cada personaje es un arbol de secciones arbitrarias ordenado topologicamente al cargar
 * para evaluarlo en O(S) lineal sin recursion y con cero asignaciones por frame.
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

    class MaterialSystem;
    class LightingSystem;
    class AsciiZBuffer;
    class SpatialHashGrid;
    struct ChunkCoord;

    using CharacterTemplateId = uint16_t;
    constexpr CharacterTemplateId INVALID_CHARACTER_ID = 0xFFFF;

    // Estados de animacion soportados por las pistas procedurales del JSON.
    enum class AnimState : uint8_t {
        IDLE = 0,
        WALK = 1,
        RUN = 2,
        ATTACK = 3,
        COUNT = 4
    };

    // Eje de oscilacion o rotacion local para pistas de animacion procedural.
    enum class AnimAxis : uint8_t {
        NONE = 0,
        X = 1, // Adelante/atras en espacio local del personaje (pitch / balanceo lateral).
        Y = 2, // Izquierda/derecha en espacio local (swing de piernas/brazos/alas).
        Z = 3  // Arriba/abajo en espacio local (respiracion / giro yaw / helices).
    };

    // Pista de animacion procedural asignable a cualquier seccion y estado en JSON.
    struct AnimTrack {
        bool     active = false;
        AnimAxis posAxis = AnimAxis::NONE; // Traslacion armonica sinusoidal.
        float    posAmplitude = 0.0f;
        AnimAxis rotAxis = AnimAxis::NONE; // Rotacion armonica alrededor del pivote.
        float    rotAmpRad = 0.0f;
        float    frequency = 4.0f;           // Radianes/seg base.
        float    phaseOffset = 0.0f;           // Desfase (ej. pi para piernas opuestas).
        bool     spinContinuous = false;          // true = giro continuo (helices, anillos, radares).
    };

    // Seccion horneada de un personaje (compartida en RAM por todas las instancias via Flyweight).
    struct BakedCharacterSection {
        std::string            id;
        int16_t                parentIdx = -1; // -1 si es raiz; siempre < propio indice tras orden topologico.
        MaterialId             materialId = FALLBACK_MATERIAL_ID;
        Vec3                   localOffset = {}; // Posicion del pivote respecto al pivote del padre.
        std::vector<BakedAtom> atoms;            // Nube de puntos local ya desplazada por pivotOffset.
        std::array<AnimTrack, static_cast<size_t>(AnimState::COUNT)> tracks = {};
    };

    // Plantilla completa de criatura/personaje cargada desde assets/data/characters/*.json.
    struct CharacterTemplate {
        std::string id;
        std::string name;
        float       collisionRadius = 0.65f;
        float       walkSpeed = 13.0f;
        float       runSpeed = 22.0f;
        float       baseElevationZ = 0.0f;  // > 0 para drones o criaturas voladoras.
        float       boundRadiusXY = 1.2f;
        float       boundHeightZ = 2.4f;
        std::vector<BakedCharacterSection> sections;
    };

    // Instancia viva de un personaje en el mundo (controlado por jugador o IA de patrulla simple).
    struct CharacterInstance {
        CharacterTemplateId templateId = INVALID_CHARACTER_ID;
        SimpleActor         actor;
        AnimState           animState = AnimState::IDLE;
        float               animTime = 0.0f;
        float               attackTimer = 0.0f;
        float               scale = 1.0f;
        bool                isPlayer = false;

        // Estado ligero de patrulla/deambulacion para NPCs no controlados.
        Vec2  homePos = {};
        Vec2  wanderTarget = {};
        float wanderRadius = 8.0f;
        float aiWaitTimer = 1.0f;
    };

    // Transformacion mundial temporal de una seccion (usada en el buffer scratch pre-alojado).
    struct SectionWorldPose {
        Vec3  position;
        float rot[9]; // Matriz de rotacion 3x3 row-major (36 bytes).
    };

    class CharacterSystem {
    public:
        CharacterSystem();

        // Escanea y hornea todas las plantillas .json dentro de assets/data/characters/.
        bool LoadTemplatesDirectory(const std::string& charactersDir, const MaterialSystem& materials);

        // Limpia las instancias activas al cambiar de mapa.
        void ClearInstances();

        // Instancia un personaje a partir de su templateId JSON.
        bool SpawnCharacter(std::string_view templateId, Vec2 worldPos, float facingDeg,
            bool isPlayer, float wanderRadius = 8.0f, float scale = 1.0f);

        // Dispara la animacion ATTACK en el personaje controlado actualmente (tecla E).
        void TriggerPlayerAttack();

        // Rota el control del jugador al siguiente personaje vivo en la escena (tecla TAB).
        void CycleControlledCharacter();

        // Integra fisica, colisiones, IA simple de NPCs y maquinas de estado de animacion.
        void Update(float dt, Vec2 playerDesiredWorldDir, bool playerSprint, float groundFriction,
            const World& world, SpatialHashGrid& spatialGrid,
            std::vector<uint32_t>& scratchIds, std::vector<ChunkCoord>& scratchChunks);

        // Evalua jerarquias de secciones, ilumina y envia todos los personajes visibles al Z-Buffer.
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

        const CharacterInstance* GetControlledCharacter() const;
        const std::vector<CharacterInstance>& GetInstances() const { return m_instances; }
        uint32_t                              GetInstanceCount() const { return static_cast<uint32_t>(m_instances.size()); }
        std::string                           GetControlledName() const;
        const char*                           GetControlledAnimStateName() const;
        size_t                                GetBakedMemoryBytes() const;

        // Accesores de edicion y exportacion para el Editor de Mapas (Fase 7).
        const std::vector<CharacterTemplate>& GetTemplates() const { return m_templates; }
        std::vector<CharacterInstance>&       GetMutableInstances() { return m_instances; }
        bool                                  RemoveCharacter(size_t index);
        std::vector<EntitySpawnDef>           ExportEntitySpawns() const;

    private:
        CharacterTemplateId FindTemplateId(std::string_view id) const;

        std::vector<CharacterTemplate>                       m_templates;
        std::unordered_map<std::string, CharacterTemplateId> m_templateMap;
        std::vector<CharacterInstance>                       m_instances;
        size_t                                               m_controlledIdx = 0;

        // Buffer pre-alojado reutilizable para evaluar poses padre->hijo sin asignaciones en heap.
        mutable std::vector<SectionWorldPose> m_poseScratch;
        std::mt19937 m_rng{ 9001u };
    };

} // namespace AsciiEngine