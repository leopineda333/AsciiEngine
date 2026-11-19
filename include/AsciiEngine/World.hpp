/**
 * @file World.hpp
 * @brief Definicion de Prefabs horneados (Flyweight), Instancias del mapa y Solver de Colisiones.
 *
 * @details Mantiene desacoplados los datos geometricos (BakedPrefab) de su colocacion
 * espacial (MapInstance). 500 arboles identicos comparten una sola nube de puntos en RAM.
 */
#pragma once

#include <AsciiEngine/Types.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace AsciiEngine {

    // Declaraciones adelantadas para evitar dependencias circulares de headers.
    class MaterialSystem;
    class SpatialHashGrid;
    struct ChunkCoord;

    // Identificador numerico compacto de un prefab (2 bytes).
    using PrefabId = uint16_t;
    constexpr PrefabId INVALID_PREFAB_ID = 0xFFFF;

    // Formas de colision 2D soportadas en el plano del suelo (X, Y).
    enum class ColliderType : uint8_t {
        NONE = 0,
        CIRCLE = 1, // Troncos de arbol, rocas, columnas.
        BOX = 2, // Muros solidos orientados (OBB con rotacion).
        ARCH_PILLARS = 3  // Puerta con arco: dos pilares laterales solidos y paso central libre.
    };

    // Parametros fisicos de colision leidos del JSON del objeto.
    struct ColliderDef {
        ColliderType type = ColliderType::NONE;
        float        radius = 0.0f;            // Usado en CIRCLE.
        Vec2         halfSize = { 0.0f, 0.0f };  // Mitad de (ancho, profundidad) en BOX / ARCH_PILLARS.
        float        openingWidth = 0.0f;            // Ancho del hueco transitable en ARCH_PILLARS.
    };

    // Atomo individual precalculado durante el Point-Cloud Baking (32 bytes compactos).
    struct BakedAtom {
        Vec3       localPos;
        Vec3       localNormal;
        float      ao = 1.0f;
        MaterialId materialId = FALLBACK_MATERIAL_ID;
        uint8_t    windWeightByte = 0;    // [0..255] mapea a [0.0..1.0] para ahorrar RAM.
        char       structuralOverride = '\0'; // Si != '\0', fuerza un glifo especifico (ej. '|', '=').
    };

    // Plantilla horneada compartida en RAM por todas las instancias de ese tipo (Flyweight).
    struct BakedPrefab {
        std::string            id;
        std::string            name;
        ColliderDef            collider;
        float                  boundRadiusXY = 1.0f; // Radio maximo en XY para SpatialHash y Frustum.
        float                  boundHeightZ = 1.0f; // Altura maxima en Z para Frustum y Sombras.
        std::vector<BakedAtom> atoms;
    };

    // Instancia ligera colocada en el mundo (48 bytes). Precalcula cos/sin de rotacion al cargar.
    struct MapInstance {
        Vec3       position = {};
        Vec3       scale = { 1.0f, 1.0f, 1.0f };
        float      rotationDeg = 0.0f;
        float      cosRot = 1.0f; // Precalculado: evita std::cos() por atomo cada frame.
        float      sinRot = 0.0f; // Precalculado: evita std::sin() por atomo cada frame.
        PrefabId   prefabId = INVALID_PREFAB_ID;
        MaterialId overrideMaterialId = FALLBACK_MATERIAL_ID; // 0 = usa materiales propios del prefab.
    };

    // Entidad fisica simple (en Fase 3 representa al jugador de prueba para validar colisiones).
    struct SimpleActor {
        Vec2  position = { 0.0f, 0.0f };
        Vec2  velocity = { 0.0f, 0.0f };
        float collisionRadius = 0.65f;
        float moveSpeed = 14.0f;
        float acceleration = 95.0f;
        float friction = 10.0f;
        float facingAngleRad = 0.785f; // ~45 grados (mirando al frente isometrico).
    };

    class World {
    public:
        // Escanea y hornea todos los archivos .json dentro de assets/data/objects/.
        bool LoadObjectsDirectory(const std::string& objectsDir, const MaterialSystem& materials);

        // Carga instancias explicitas, grupos procedurales (scatterGroups) y spawn desde un mapa JSON.
        bool LoadMapFromFile(const std::string& mapPath, SpatialHashGrid& spatialGrid);

        // Crea una instancia en el mundo y la registra automaticamente en el SpatialHashGrid.
        uint32_t SpawnInstance(PrefabId prefabId, Vec3 pos, float rotDeg, Vec3 scale,
            SpatialHashGrid& spatialGrid,
            MaterialId overrideMat = FALLBACK_MATERIAL_ID);

        PrefabId GetPrefabId(std::string_view id) const;
        const BakedPrefab* GetPrefab(PrefabId id) const;

        const std::vector<BakedPrefab>& GetPrefabs() const { return m_prefabs; }
        const std::vector<MapInstance>& GetInstances() const { return m_instances; }

        Vec2  GetPlayerSpawn() const { return m_playerSpawn; }
        float GetWorldHalfSize() const { return m_worldHalfSize; }
        float GetChunkSize() const { return m_chunkSize; }

        // Reporta el consumo real en bytes de todas las nubes horneadas (para el Debug HUD F3).
        size_t GetBakedMemoryBytes() const;

    private:
        std::vector<BakedPrefab>                  m_prefabs;
        std::unordered_map<std::string, PrefabId> m_prefabNameToId;
        std::vector<MapInstance>                  m_instances;

        Vec2  m_playerSpawn = { 0.0f, 0.0f };
        float m_worldHalfSize = 140.0f;
        float m_chunkSize = 24.0f;
    };

    // Subsistemas de fisica simple y resolucion de colisiones (implementado en CollisionSolver.cpp).
    namespace CollisionSolver {

        // Integra movimiento con Euler semi-implicito y resuelve colisiones contra los chunks cercanos.
        // Recibe buffers temporales pre-alojados para garantizar cero asignaciones por frame.
        void UpdateActorPhysics(SimpleActor& actor,
            Vec2 desiredWorldDir,
            float dt,
            const World& world,
            SpatialHashGrid& spatialGrid,
            std::vector<uint32_t>& scratchIds,
            std::vector<ChunkCoord>& scratchChunks);

    } // namespace CollisionSolver

} // namespace AsciiEngine