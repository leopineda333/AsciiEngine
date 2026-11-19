/**
 * @file CullingSystem.cpp
 * @brief Implementacion del SpatialHashGrid con deduplicacion O(1) orientada a cache.
 */
#include <AsciiEngine/CullingSystem.hpp>
#include <algorithm>
#include <cmath>

namespace AsciiEngine {

    SpatialHashGrid::SpatialHashGrid(float chunkSize) {
        Reset(chunkSize, 0);
    }

    void SpatialHashGrid::Reset(float chunkSize, size_t expectedInstanceCount) {
        m_chunkSize = (chunkSize > 1.0f) ? chunkSize : 24.0f;
        m_invChunkSize = 1.0f / m_chunkSize;

        m_chunks.clear();
        m_visitedStamp.assign(expectedInstanceCount, 0);
        m_currentStamp = 1;
    }

    int32_t SpatialHashGrid::WorldToChunkCoord(float worldCoord) const {
        return static_cast<int32_t>(std::floor(worldCoord * m_invChunkSize));
    }

    void SpatialHashGrid::InsertInstance(uint32_t instanceId, float worldX, float worldY, float worldRadius) {
        // Garantizamos espacio en el arreglo de sellos si se agregan instancias dinamicamente.
        if (instanceId >= m_visitedStamp.size()) {
            m_visitedStamp.resize(static_cast<size_t>(instanceId) + 1, 0);
        }

        // Calculamos todos los chunks que toca la caja envolvente 2D del objeto.
        int32_t minCX = WorldToChunkCoord(worldX - worldRadius);
        int32_t maxCX = WorldToChunkCoord(worldX + worldRadius);
        int32_t minCY = WorldToChunkCoord(worldY - worldRadius);
        int32_t maxCY = WorldToChunkCoord(worldY + worldRadius);

        for (int32_t cy = minCY; cy <= maxCY; ++cy) {
            for (int32_t cx = minCX; cx <= maxCX; ++cx) {
                m_chunks[PackKey(cx, cy)].push_back(instanceId);
            }
        }
    }

    void SpatialHashGrid::QueryChunks(const ChunkRange& range,
        std::vector<uint32_t>& outInstances,
        std::vector<ChunkCoord>& outActiveChunks) {
        // clear() conserva la capacidad reservada: cero asignaciones en el heap.
        outInstances.clear();
        outActiveChunks.clear();

        // Incrementamos el sello de consulta para deduplicar instancias en O(1).
        ++m_currentStamp;
        if (m_currentStamp == 0) {
            // Manejo seguro de overflow cada ~4 mil millones de frames.
            std::fill(m_visitedStamp.begin(), m_visitedStamp.end(), 0);
            m_currentStamp = 1;
        }

        for (int32_t cy = range.minCY; cy <= range.maxCY; ++cy) {
            for (int32_t cx = range.minCX; cx <= range.maxCX; ++cx) {
                auto it = m_chunks.find(PackKey(cx, cy));
                if (it == m_chunks.end()) {
                    continue; // Chunk vacio: salto inmediato.
                }

                outActiveChunks.push_back({ cx, cy });

                for (uint32_t id : it->second) {
                    // Si la instancia ya fue agregada por un chunk vecino, la omitimos en O(1).
                    if (m_visitedStamp[id] != m_currentStamp) {
                        m_visitedStamp[id] = m_currentStamp;
                        outInstances.push_back(id);
                    }
                }
            }
        }
    }

    size_t SpatialHashGrid::GetMemoryBytes() const {
        size_t total = m_visitedStamp.capacity() * sizeof(uint32_t);
        for (const auto& [key, list] : m_chunks) {
            total += sizeof(int64_t) + sizeof(std::vector<uint32_t>) + (list.capacity() * sizeof(uint32_t));
        }
        return total;
    }

} // namespace AsciiEngine