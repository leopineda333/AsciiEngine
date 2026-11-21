// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file World.hpp
 * @brief Prefabs horneados (Flyweight), instancias del mapa, zonas de agua/suelo, GroundMask O(1) y fisica.
 *
 * @details Arquitectura de memoria centrada en el patron Flyweight:
 *   - BakedPrefab almacena la nube de puntos horneada (BakedAtom) UNA SOLA VEZ en memoria contigua.
 *   - MapInstance ocupa unicamente ~48 bytes (posicion, escala, rotacion + cos/sin precalculados e ID).
 *   - 500 arboles identicos comparten un unico BakedPrefab en RAM (~60 KB en total en vez de >30 MB).
 *   - GroundMask mantiene un mapa de bits 2D (1 byte/celda) para que el pasto y las flores
 *     descarten celdas ocupadas por objetos, agua o puentes en O(1) antes de simular nada.
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

    // Declaraciones adelantadas para evitar dependencias circulares entre headers publicos.
    class MaterialSystem;
    class SpatialHashGrid;
    struct ChunkCoord;

    // Identificador numerico compacto de un prefab horneado (2 bytes: hasta 65,534 prefabs).
    using PrefabId = uint16_t;
    constexpr PrefabId INVALID_PREFAB_ID = 0xFFFF;

    /**
     * @brief Primitivas de colision 2D soportadas sobre el plano del suelo (X, Y).
     */
    enum class ColliderType : uint8_t {
        NONE = 0, // Objeto puramente decorativo (sin respuesta fisica).
        CIRCLE = 1, // Troncos de arbol, rocas, columnas cilindricas.
        BOX = 2, // Muros solidos y estructuras rectangulares con rotacion (OBB).
        ARCH_PILLARS = 3  // Portico/arco: descompuesto en dos pilares OBB con pasillo central transitable.
    };

    /**
     * @brief Parametros fisicos de colision leidos de la seccion "collider" del JSON de cada objeto.
     */
    struct ColliderDef {
        ColliderType type = ColliderType::NONE;
        float        radius = 0.0f;           // Radio en unidades de mundo (usado en CIRCLE).
        Vec2         halfSize = { 0.0f, 0.0f }; // Mitad de (ancho, profundidad) para BOX y ARCH_PILLARS.
        float        openingWidth = 0.0f;           // Ancho del hueco central libre en ARCH_PILLARS.
    };

    /**
     * @brief Atomo individual precalculado durante el Point-Cloud Baking (36 bytes compactos).
     *
     * @details Se almacena en espacio LOCAL del prefab. Durante el renderizado se rota y traslada
     * al espacio del mundo usando el cosRot/sinRot precalculado en cada MapInstance.
     */
    struct BakedAtom {
        Vec3       localPos;                                  // Posicion 3D relativa al origen del prefab.
        Vec3       localNormal;                               // Normal superficial unitaria local.
        float      ao = 1.0f;                 // Oclusion ambiental estatica [0.2 .. 1.0].
        MaterialId materialId = FALLBACK_MATERIAL_ID; // Material propio de esta parte.
        uint8_t    windWeightByte = 0;                    // [0..255] mapeado a [0.0..1.0] para ahorrar 3 bytes/atomo.
        char       structuralOverride = '\0';                 // Si != '\0', fuerza un glifo arquitectonico (ej. '|', '=').
    };

    /**
     * @brief Plantilla geometrica horneada compartida por todas las instancias del mismo tipo (Flyweight).
     */
    struct BakedPrefab {
        std::string            id;                   // Clave unica JSON (ej. "tree_pine").
        std::string            name;                 // Nombre legible mostrado en el Editor Visual.
        ColliderDef            collider;             // Volumen de colision 2D base (sin escalar).
        float                  boundRadiusXY = 1.0f; // Radio envolvente en XY para SpatialHashGrid y Frustum.
        float                  boundHeightZ = 1.0f; // Altura maxima Z para Frustum 2D y calculo de viento.
        std::vector<BakedAtom> atoms;                // Nube de puntos contigua (solo cascara exterior visible).
    };

    /**
     * @brief Instancia ligera colocada en el escenario (~48 bytes).
     *
     * @details Precalcula cosRot y sinRot al cargar o mover la instancia para eliminar por completo
     * llamadas a std::cos/std::sin dentro del bucle critico de renderizado y fisica.
     */
    struct MapInstance {
        Vec3       position = {};
        Vec3       scale = { 1.0f, 1.0f, 1.0f };
        float      rotationDeg = 0.0f;
        float      cosRot = 1.0f;                 // std::cos(rotationDeg * DEG_TO_RAD) precalculado.
        float      sinRot = 0.0f;                 // std::sin(rotationDeg * DEG_TO_RAD) precalculado.
        PrefabId   prefabId = INVALID_PREFAB_ID;    // Indice directo al vector World::m_prefabs.
        MaterialId overrideMaterialId = FALLBACK_MATERIAL_ID; // 0 = conserva materiales originales del prefab.
    };

    /**
     * @brief Zona rectangular de agua definida en el mapa JSON (soporta oleaje y reflejos espejados).
     */
    struct WaterZone {
        float     minX = 0.0f;
        float     maxX = 0.0f;
        float     minY = 0.0f;
        float     maxY = 0.0f;
        ColorRGBA deepColor = { 18, 52, 74, 255 };   // Color en el interior profundo del cuerpo de agua.
        ColorRGBA shallowColor = { 42, 108, 138, 255 }; // Color claro en la zona de orilla/costa.
        float     waveSpeed = 2.6f;                  // Velocidad de animacion de olas (~, =, -).

        /**
         * @brief Comprueba en O(1) si un punto 2D del mundo cae dentro del rectangulo de agua.
         */
        bool Contains(float wx, float wy) const {
            return wx >= minX && wx <= maxX && wy >= minY && wy <= maxY;
        }

        /**
         * @brief Estima la profundidad relativa [0.0 en la orilla .. 1.0 en aguas profundas].
         * @details Mide la distancia minima a cualquiera de los 4 bordes y la normaliza sobre 2.2u.
         * @complexity O(1).
         */
        float ComputeDepthFactor(float wx, float wy) const {
            float dx = std::min(wx - minX, maxX - wx);
            float dy = std::min(wy - minY, maxY - wy);
            float edgeDist = std::min(dx, dy);
            return std::min(1.0f, std::max(0.0f, edgeDist / 2.2f));
        }
    };

    /**
     * @brief Patrones procedurales modulares para objetos de suelo transitables (fantasia, sci-fi, urbano).
     */
    enum class SurfacePattern : uint8_t {
        NOISE = 0, // Variacion organica aleatoria (tierra apisonada, grava).
        PLANKS_X = 1, // Tablones horizontales a lo largo del eje X (puentes de madera, tarimas).
        PLANKS_Y = 2, // Tablones verticales a lo largo del eje Y.
        GRID = 3, // Baldosas, losas de piedra o paneles metalicos con juntas.
        CHECKER = 4  // Patron ajedrezado de dos tonos alternados.
    };

    /**
     * @brief Objeto de suelo transitable definido en JSON (puentes, pisos de casas, pasarelas).
     */
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
        bool           suppressFoliage = true; // true = impide que el pasto/flores atraviesen el piso.
        bool           walkableOverWater = true; // true = tiene prioridad visual sobre WaterZones (puentes).

        bool Contains(float wx, float wy) const {
            return wx >= minX && wx <= maxX && wy >= minY && wy <= maxY;
        }
    };

    /**
     * @brief Mascaras de bits usadas en la grilla 2D de ocupacion del terreno (World::m_groundMask).
     */
    namespace GroundMaskFlags {
        constexpr uint8_t CLEAR = 0;      // Terreno natural despejado (apto para pasto/flores).
        constexpr uint8_t OBJECT_FOOTPRINT = 1 << 0; // Ocupado por base de arbol, roca o estructura.
        constexpr uint8_t WATER = 1 << 1; // Ocupado por un cuerpo de agua y su margen de orilla.
        constexpr uint8_t SURFACE_OVERRIDE = 1 << 2; // Ocupado por un puente o piso artificial.
    }

    /**
     * @brief Parametros iniciales de viento leidos del mapa JSON.
     */
    struct WorldWindConfig {
        float directionDeg = 45.0f;
        float baseSpeed = 2.2f;
        float gustStrength = 1.0f;
    };

    /**
     * @brief Definicion de aparicion inicial de un personaje leida del bloque "entities" del mapa JSON.
     */
    struct EntitySpawnDef {
        std::string characterId;
        Vec2        position = {};
        float       facingDeg = 45.0f;
        float       scale = 1.0f;
        float       wanderRadius = 8.0f;
        bool        isPlayer = false;
    };

    /**
     * @brief Estado cinematica/fisica 2D de cualquier entidad movil controlada por CollisionSolver.
     */
    struct SimpleActor {
        Vec2  position = { 0.0f, 0.0f };
        Vec2  velocity = { 0.0f, 0.0f };
        float collisionRadius = 0.65f;  // Radio circular del cuerpo en el plano XY.
        float moveSpeed = 14.0f;  // Velocidad maxima permitida (unidades/seg).
        float acceleration = 95.0f;  // Aceleracion al aplicar entrada direccional.
        float friction = 10.0f;  // Amortiguamiento del suelo (disminuye bajo nieve).
        float facingAngleRad = 0.785f; // Orientacion suavizada en radianes (~45 deg por defecto).
    };

    class World {
    public:
        /**
         * @brief Escanea todos los .json de objectsDir y hornea sus primitivas SDF en RAM (Flyweight).
         */
        bool LoadObjectsDirectory(const std::string& objectsDir, const MaterialSystem& materials);

        /**
         * @brief Carga un mapa JSON completo (config, luces, zonas, entidades, instancias y scatterGroups).
         * @param materials Puntero opcional usado para resolver overrideMaterialId por nombre.
         */
        bool LoadMapFromFile(const std::string& mapPath, SpatialHashGrid& spatialGrid,
            const MaterialSystem* materials = nullptr);

        /**
         * @brief Serializa el estado completo actual del escenario editado a un archivo JSON formateado.
         * @details Al guardar convierte cualquier instancia procedural previa en instancias explicitas
         * para preservar fielmente cada objeto colocado, movido o eliminado en el Editor Visual.
         */
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

        /**
         * @brief Crea una instancia nueva en el mundo y la indexa inmediatamente en SpatialHashGrid.
         * @return Indice de la nueva instancia en m_instances, o UINT32_MAX si prefabId es invalido.
         */
        uint32_t SpawnInstance(PrefabId prefabId, Vec3 pos, float rotDeg, Vec3 scale,
            SpatialHashGrid& spatialGrid,
            MaterialId overrideMat = FALLBACK_MATERIAL_ID);

        // --- Operaciones de edicion en vivo (Editor Visual + Undo/Redo) ---
        void RebuildSpatialGrid(SpatialHashGrid& spatialGrid) const;
        bool RemoveInstance(size_t index, SpatialHashGrid& spatialGrid);
        bool InsertInstanceAt(size_t index, const MapInstance& inst, SpatialHashGrid& spatialGrid);
        bool UpdateInstanceTransform(size_t index, Vec3 pos, float rotDeg, Vec3 scale,
            MaterialId overrideMat, SpatialHashGrid& spatialGrid);

        /**
         * @brief Construye la mascara 2D de ocupacion estampando objetos, agua y superficies.
         * @details Se ejecuta al cargar el mapa o tras editarlo; libera al bucle de 60 FPS de
         * comprobar colisiones de pasto contra cientos de objetos.
         */
        void RebuildGroundMask(float cellSizeWorld = 0.5f);

        /**
         * @brief Consulta O(1) sobre m_groundMask.
         * @return true SOLO si la celda esta libre (sin roca, arbol, agua ni puente encima).
         * @complexity O(1) — una unica lectura de 1 byte en arreglo contiguo.
         */
        bool CanSpawnGroundFoliageAt(float worldX, float worldY) const;

        PrefabId           GetPrefabId(std::string_view id) const;
        const BakedPrefab* GetPrefab(PrefabId id) const;

        const WaterZone* FindWaterZoneAt(float worldX, float worldY) const;
        const SurfaceZone* FindSurfaceZoneAt(float worldX, float worldY) const;
        bool               IsNearAnyWater(float worldX, float worldY, float margin) const;

        // --- Accesores de lectura y edicion ---
        const std::vector<BakedPrefab>& GetPrefabs() const { return m_prefabs; }
        const std::vector<MapInstance>& GetInstances() const { return m_instances; }
        const std::vector<WaterZone>& GetWaterZones() const { return m_waterZones; }
        const std::vector<SurfaceZone>& GetSurfaceZones() const { return m_surfaceZones; }
        std::vector<MapInstance>& GetMutableInstances() { return m_instances; }
        std::vector<WaterZone>& GetMutableWaterZones() { return m_waterZones; }
        std::vector<SurfaceZone>& GetMutableSurfaceZones() { return m_surfaceZones; }

        const DayNightConfig& GetDayNightConfig() const { return m_dayNightConfig; }
        const GlobalIlluminationConfig& GetGIConfig() const { return m_giConfig; }
        const std::vector<PointLight>& GetMapLights() const { return m_mapLights; }
        const WorldWindConfig& GetWindConfig() const { return m_windConfig; }
        const std::vector<EntitySpawnDef>& GetEntitySpawns() const { return m_entitySpawns; }

        const std::string& GetBiomeId() const { return m_biomeId; }
        const std::string& GetInitialWeatherId() const { return m_initialWeatherId; }
        bool               GetRandomWeather() const { return m_randomWeather; }

        Vec2   GetPlayerSpawn() const { return m_playerSpawn; }
        float  GetWorldHalfSize() const { return m_worldHalfSize; }
        float  GetChunkSize() const { return m_chunkSize; }
        void   SetChunkSize(float cs) { m_chunkSize = std::max(8.0f, cs); }
        float  GetViewPaddingPx() const { return m_viewPaddingPx; }
        size_t GetBakedMemoryBytes() const;
        size_t GetGroundMaskBytes() const { return m_groundMask.capacity() * sizeof(uint8_t); }

    private:
        // Helpers internos para rasterizar huellas geometricas sobre m_groundMask.
        void StampCircleMask(float cx, float cy, float radius, uint8_t flag);
        void StampOBBMask(float cx, float cy, Vec2 halfSize, float cosRot, float sinRot, uint8_t flag);
        void StampRectMask(float minX, float maxX, float minY, float maxY, uint8_t flag);

        std::vector<BakedPrefab>                  m_prefabs;
        std::unordered_map<std::string, PrefabId> m_prefabNameToId;
        std::vector<MapInstance>                  m_instances;
        std::vector<WaterZone>                    m_waterZones;
        std::vector<SurfaceZone>                  m_surfaceZones;

        // Grilla plana 2D de ocupacion del terreno (1 byte por celda -> acceso O(1)).
        float                m_maskOriginX = -140.0f;
        float                m_maskOriginY = -140.0f;
        float                m_maskCellSize = 0.5f;
        float                m_maskInvCellSize = 2.0f;
        int                  m_maskWidth = 0;
        int                  m_maskHeight = 0;
        std::vector<uint8_t> m_groundMask;

        // Configuraciones leidas del JSON del mapa y entregadas a los demas subsistemas al cargar.
        DayNightConfig              m_dayNightConfig;
        GlobalIlluminationConfig    m_giConfig;
        std::vector<PointLight>     m_mapLights;
        WorldWindConfig             m_windConfig;
        std::vector<EntitySpawnDef> m_entitySpawns;

        std::string m_biomeId = "temperate_forest";
        std::string m_initialWeatherId = "clear_day";
        bool        m_randomWeather = true;

        Vec2  m_playerSpawn = { 0.0f, 0.0f };
        float m_worldHalfSize = 140.0f;
        float m_chunkSize = 24.0f;
        float m_viewPaddingPx = 140.0f;
    };

    /**
     * @brief Subsistema de fisica simple 2D y resolucion de colisiones (implementado en CollisionSolver.cpp).
     */
    namespace CollisionSolver {

        /**
         * @brief Integra velocidad/posicion con Euler semi-implicito y resuelve colisiones en O(K).
         * @details Recibe buffers scratch pre-alojados para consultar SpatialHashGrid sin tocar el heap.
         */
        void UpdateActorPhysics(SimpleActor& actor,
            Vec2 desiredWorldDir,
            float dt,
            const World& world,
            SpatialHashGrid& spatialGrid,
            std::vector<uint32_t>& scratchIds,
            std::vector<ChunkCoord>& scratchChunks);

    } // namespace CollisionSolver

} // namespace AsciiEngine