/**
 * @file World.hpp
 * @brief Definicion de Prefabs horneados (Flyweight), Instancias, Config de iluminacion y Colisiones.
 * @details Mantiene desacoplados los datos geometricos (BakedPrefab) de su colocacion
 * espacial (MapInstance). 500 arboles identicos comparten una sola nube de puntos en RAM
 * y la lectura de `biomeId`, `weather`, `wind` y `waterZones` en `World`.
 */
#pragma once

#include <AsciiEngine/Types.hpp>
#include <AsciiEngine/LightingSystem.hpp>
#include <algorithm>
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
        CIRCLE = 1,
        BOX = 2,
        ARCH_PILLARS = 3
    };

    // Parametros fisicos de colision leidos del JSON del objeto.
    struct ColliderDef {
        ColliderType type = ColliderType::NONE;
        float        radius = 0.0f;
        Vec2         halfSize = { 0.0f, 0.0f };
        float        openingWidth = 0.0f;
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
        float      cosRot = 1.0f;
        float      sinRot = 0.0f;
        PrefabId   prefabId = INVALID_PREFAB_ID;
        MaterialId overrideMaterialId = FALLBACK_MATERIAL_ID;
    };

    // Zona rectangular de agua definida en el mapa JSON (soporta oleaje y reflejos).
    struct WaterZone {
        float     minX = 0.0f;
        float     maxX = 0.0f;
        float     minY = 0.0f;
        float     maxY = 0.0f;
        ColorRGBA deepColor = { 18, 52, 74, 255 };
        ColorRGBA shallowColor = { 42, 108, 138, 255 };
        float     waveSpeed = 2.6f;

        bool Contains(float wx, float wy) const {
            return wx >= minX && wx <= maxX && wy >= minY && wy <= maxY;
        }

        float ComputeDepthFactor(float wx, float wy) const {
            float dx = std::min(wx - minX, maxX - wx);
            float dy = std::min(wy - minY, maxY - wy);
            float edgeDist = std::min(dx, dy);
            return std::min(1.0f, std::max(0.0f, edgeDist / 2.2f));
        }
    };

    // Patrones procedurales modulares para objetos de suelo transitables (puentes, casas, cubiertas sci-fi).
    enum class SurfacePattern : uint8_t {
        NOISE = 0,
        PLANKS_X = 1, // Tablas alineadas al eje X (puentes / tarimas).
        PLANKS_Y = 2, // Tablas alineadas al eje Y.
        GRID = 3, // Baldosas, losas de piedra o paneles metalicos sci-fi.
        CHECKER = 4  // Patron ajedrezado o losetas alternadas.
    };

    // Objeto de suelo transitable definido en JSON (colocable en el futuro Editor).
    struct SurfaceZone {
        std::string    id;
        float          minX = 0.0f;
        float          maxX = 0.0f;
        float          minY = 0.0f;
        float          maxY = 0.0f;
        float          elevationZ = 0.05f;
        ColorRGBA      primaryColor = { 118, 86, 58, 255 };
        ColorRGBA      secondaryColor = { 86, 60, 40, 255 };
        std::string    glyphs = "=#-";
        SurfacePattern pattern = SurfacePattern::GRID;
        bool           suppressFoliage = true; // Impide que crezca pasto/flores atravesando el piso.
        bool           walkableOverWater = true; // Permite crear puentes sobre WaterZones.

        bool Contains(float wx, float wy) const {
            return wx >= minX && wx <= maxX && wy >= minY && wy <= maxY;
        }
    };

    // Bitflags de la grilla 2D de ocupacion del terreno (1 byte por celda).
    namespace GroundMaskFlags {
        constexpr uint8_t CLEAR = 0;
        constexpr uint8_t OBJECT_FOOTPRINT = 1 << 0; // Hay arbol, roca o estructura encima.
        constexpr uint8_t WATER = 1 << 1; // Celda cubierta por agua.
        constexpr uint8_t SURFACE_OVERRIDE = 1 << 2; // Celda cubierta por piso/puente artificial.
    }

    struct WorldWindConfig {
        float directionDeg = 45.0f;
        float baseSpeed = 2.2f;
        float gustStrength = 1.0f;
    };

    // Definicion de aparicion de un personaje en el mapa JSON (seccion "entities").
    struct EntitySpawnDef {
        std::string characterId;
        Vec2        position = {};
        float       facingDeg = 45.0f;
        float       scale = 1.0f;
        float       wanderRadius = 8.0f;
        bool        isPlayer = false;
    };

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
        bool LoadObjectsDirectory(const std::string& objectsDir, const MaterialSystem& materials);
        // Carga instancias explicitas, grupos procedurales (scatterGroups) y spawn desde un mapa JSON.
        bool LoadMapFromFile(const std::string& mapPath, SpatialHashGrid& spatialGrid,
            const MaterialSystem* materials = nullptr);

        // Serializa el estado completo del mapa editado a un archivo JSON legible.
        bool SaveMapToFile(const std::string& mapPath,
            const MaterialSystem& materials,
            const DayNightConfig& dayNight,
            const GlobalIlluminationConfig& gi,
            const std::vector<PointLight>& lights,
            const WorldWindConfig& wind,
            const std::string& biomeId,
            const std::string& weatherId,
            bool randomWeather,
            float viewPaddingPx,
            const std::vector<EntitySpawnDef>& entities) const;

        // Operaciones de edicion del Editor de Mapas (mantienen coherente SpatialHashGrid).
        void RebuildSpatialGrid(SpatialHashGrid& spatialGrid) const;
        bool RemoveInstance(size_t index, SpatialHashGrid& spatialGrid);
        bool InsertInstanceAt(size_t index, const MapInstance& inst, SpatialHashGrid& spatialGrid);
        bool UpdateInstanceTransform(size_t index, Vec3 pos, float rotDeg, Vec3 scale,
            MaterialId overrideMat, SpatialHashGrid& spatialGrid);

        std::vector<MapInstance>& GetMutableInstances() { return m_instances; }
        std::vector<WaterZone>& GetMutableWaterZones() { return m_waterZones; }
        std::vector<SurfaceZone>& GetMutableSurfaceZones() { return m_surfaceZones; }
        void                      SetChunkSize(float cs) { m_chunkSize = std::max(8.0f, cs); }
        float                     GetViewPaddingPx() const { return m_viewPaddingPx; }

        // Crea una instancia en el mundo y la registra automaticamente en el SpatialHashGrid.
        uint32_t SpawnInstance(PrefabId prefabId, Vec3 pos, float rotDeg, Vec3 scale,
            SpatialHashGrid& spatialGrid,
            MaterialId overrideMat = FALLBACK_MATERIAL_ID);

        // Construye la mascara 2D O(1) estampando objetos, agua y superficies transitables.
        void RebuildGroundMask(float cellSizeWorld = 0.5f);

        // Consulta O(1): devuelve true SOLO si no hay objeto, agua ni superficie que suprima pasto.
        bool CanSpawnGroundFoliageAt(float worldX, float worldY) const;

        const std::vector<EntitySpawnDef>& GetEntitySpawns() const { return m_entitySpawns; }

        PrefabId GetPrefabId(std::string_view id) const;
        const BakedPrefab* GetPrefab(PrefabId id) const;

        // Consultas rapidas sobre zonas de agua y superficies para sombreado de piso y culling de reflejos.
        const WaterZone* FindWaterZoneAt(float worldX, float worldY) const;
        const SurfaceZone* FindSurfaceZoneAt(float worldX, float worldY) const;
        bool IsNearAnyWater(float worldX, float worldY, float margin) const;

        const std::vector<BakedPrefab>& GetPrefabs() const      { return m_prefabs; }
        const std::vector<MapInstance>& GetInstances() const    { return m_instances; }
        const std::vector<WaterZone>&   GetWaterZones() const   { return m_waterZones; }
        const std::vector<SurfaceZone>& GetSurfaceZones() const { return m_surfaceZones; }

        // Datos de iluminacion leidos del mapa JSON (entregados a LightingSystem).
        const DayNightConfig&           GetDayNightConfig() const   { return m_dayNightConfig; }
        const GlobalIlluminationConfig& GetGIConfig() const         { return m_giConfig; }
        const std::vector<PointLight>&  GetMapLights() const        { return m_mapLights; }
        const WorldWindConfig&          GetWindConfig() const       { return m_windConfig; }

        const std::string& GetBiomeId() const           { return m_biomeId; }
        const std::string& GetInitialWeatherId() const  { return m_initialWeatherId; }
        bool               GetRandomWeather() const     { return m_randomWeather; }

        Vec2   GetPlayerSpawn() const       { return m_playerSpawn; }
        float  GetWorldHalfSize() const     { return m_worldHalfSize; }
        float  GetChunkSize() const         { return m_chunkSize; }
        size_t GetBakedMemoryBytes() const;
        size_t GetGroundMaskBytes() const   { return m_groundMask.capacity() * sizeof(uint8_t); }

    private:

        void StampCircleMask(float cx, float cy, float radius, uint8_t flag);
        void StampOBBMask(float cx, float cy, Vec2 halfSize, float cosRot, float sinRot, uint8_t flag);
        void StampRectMask(float minX, float maxX, float minY, float maxY, uint8_t flag);


        std::vector<BakedPrefab>                  m_prefabs;
        std::unordered_map<std::string, PrefabId> m_prefabNameToId;
        std::vector<MapInstance>                  m_instances;
        std::vector<WaterZone>                    m_waterZones;
        std::vector<SurfaceZone>                  m_surfaceZones;
        std::vector<EntitySpawnDef>               m_entitySpawns;

        // Mascara 2D de ocupacion del suelo (1 byte por celda -> acceso O(1)).
        float                m_maskOriginX = -140.0f;
        float                m_maskOriginY = -140.0f;
        float                m_maskCellSize = 0.5f;
        float                m_viewPaddingPx = 140.0f;
        float                m_maskInvCellSize = 2.0f;
        int                  m_maskWidth = 0;
        int                  m_maskHeight = 0;
        std::vector<uint8_t> m_groundMask;

        DayNightConfig           m_dayNightConfig;
        GlobalIlluminationConfig m_giConfig;
        std::vector<PointLight>  m_mapLights;
        WorldWindConfig          m_windConfig;

        std::string m_biomeId = "temperate_forest";
        std::string m_initialWeatherId = "clear_day";
        bool        m_randomWeather = true;

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
    }

} // namespace AsciiEngine