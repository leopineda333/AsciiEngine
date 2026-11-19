/**
 * @file CullingSystem.hpp
 * @brief Sistema de Culling Espacial (Capa 1: Spatial Hash Grid) y Frustum 2D (Capa 2).
 *
 * @details Permite que un mapa con miles de instancias procese unicamente aquellas
 * que habitan los chunks visibles dentro del viewport actual, con cero asignaciones por frame.
 */
#pragma once

#include <AsciiEngine/Types.hpp>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace AsciiEngine {

    // Coordenada discreta de un chunk 2D en el mundo cartesiano.
    struct ChunkCoord {
        int32_t cx = 0;
        int32_t cy = 0;
    };

    // Rango rectangular de chunks activos que intersectan el frustum de la camara.
    struct ChunkRange {
        int32_t minCX = 0;
        int32_t maxCX = -1;
        int32_t minCY = 0;
        int32_t maxCY = -1;
    };

    class SpatialHashGrid {
    public:
        explicit SpatialHashGrid(float chunkSize = 24.0f);

        // Cambia el tamano de chunk y limpia el indice espacial.
        void Reset(float chunkSize, size_t expectedInstanceCount = 0);

        // Registra una instancia en todos los chunks que solapa su radio de influencia en XY.
        void InsertInstance(uint32_t instanceId, float worldX, float worldY, float worldRadius);

        // Consulta todas las instancias unicas dentro del rango de chunks visible.
        // Reutiliza la capacidad de outInstances y outActiveChunks (cero asignaciones en el bucle).
        void QueryChunks(const ChunkRange& range,
            std::vector<uint32_t>& outInstances,
            std::vector<ChunkCoord>& outActiveChunks);

        float    GetChunkSize() const { return m_chunkSize; }
        uint32_t GetTotalChunks() const { return static_cast<uint32_t>(m_chunks.size()); }
        size_t   GetMemoryBytes() const;

    private:
        // Empaqueta (cx, cy) de 32 bits en una clave unica sin colisiones de 64 bits.
        static int64_t PackKey(int32_t cx, int32_t cy) {
            return (static_cast<int64_t>(cx) << 32) | static_cast<uint32_t>(cy);
        }

        int32_t WorldToChunkCoord(float worldCoord) const;

        float m_chunkSize = 24.0f;
        float m_invChunkSize = 1.0f / 24.0f;

        // Mapa de clave de chunk -> lista contigua de IDs de instancias dentro de ese chunk.
        std::unordered_map<int64_t, std::vector<uint32_t>> m_chunks;

        // Arreglo de sellos para deduplicacion O(1) cuando una instancia cruza varios chunks.
        std::vector<uint32_t> m_visitedStamp;
        uint32_t m_currentStamp = 1;
    };

} // namespace AsciiEngine