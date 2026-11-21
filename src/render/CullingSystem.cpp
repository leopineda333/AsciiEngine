// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file CullingSystem.cpp
 * @brief Implementacion comentada linea por linea del SpatialHashGrid con deduplicacion O(1).
 */
#include <AsciiEngine/CullingSystem.hpp>
#include <algorithm> // std::fill
#include <cmath>     // std::floor

namespace AsciiEngine {

    SpatialHashGrid::SpatialHashGrid(float chunkSize) {
        Reset(chunkSize, 0);
    }

    void SpatialHashGrid::Reset(float chunkSize, size_t expectedInstanceCount) {
        // Evitamos tamanos de chunk degenerados que dispararian la cantidad de celdas.
        m_chunkSize = (chunkSize > 1.0f) ? chunkSize : 24.0f;
        m_invChunkSize = 1.0f / m_chunkSize;

        // Vaciamos la tabla hash y dimensionamos el arreglo de sellos a la cantidad esperada.
        m_chunks.clear();
        m_visitedStamp.assign(expectedInstanceCount, 0);
        m_currentStamp = 1;
    }

    int32_t SpatialHashGrid::WorldToChunkCoord(float worldCoord) const {
        // std::floor es imprescindible para coordenadas negativas:
        //   -0.5 / 24 -> floor(-0.02) = -1 (chunk -1), mientras que cast entero truncaria a 0
        //   y haria que el chunk 0 midiera el doble de ancho alrededor del origen.
        return static_cast<int32_t>(std::floor(worldCoord * m_invChunkSize));
    }

    void SpatialHashGrid::InsertInstance(uint32_t instanceId, float worldX, float worldY, float worldRadius) {
        // 1. Si se insertan instancias dinamicamente (ej. desde el Editor), ampliamos el arreglo de sellos.
        if (instanceId >= m_visitedStamp.size()) {
            m_visitedStamp.resize(static_cast<size_t>(instanceId) + 1, 0);
        }

        // 2. Calculamos el rectangulo de chunks que toca la caja envolvente 2D [pos - r .. pos + r].
        int32_t minCX = WorldToChunkCoord(worldX - worldRadius);
        int32_t maxCX = WorldToChunkCoord(worldX + worldRadius);
        int32_t minCY = WorldToChunkCoord(worldY - worldRadius);
        int32_t maxCY = WorldToChunkCoord(worldY + worldRadius);

        // 3. Registramos el ID en cada chunk solapado.
        for (int32_t cy = minCY; cy <= maxCY; ++cy) {
            for (int32_t cx = minCX; cx <= maxCX; ++cx) {
                m_chunks[PackKey(cx, cy)].push_back(instanceId);
            }
        }
    }

    void SpatialHashGrid::QueryChunks(const ChunkRange& range,
        std::vector<uint32_t>& outInstances,
        std::vector<ChunkCoord>& outActiveChunks) {
        // 1. clear() deja size() en 0 pero conserva capacity(): cero asignaciones en el bucle principal.
        outInstances.clear();
        outActiveChunks.clear();

        // 2. Avanzamos el sello de generacion en 1 en lugar de limpiar m_visitedStamp con std::fill.
        //    Esto hace que preparar cada consulta (render + fisica de cada personaje) cueste O(1).
        ++m_currentStamp;
        if (m_currentStamp == 0) {
            // Solo ocurre tras 2^32 (~4,294 millones) de consultas: reiniciamos el arreglo de forma segura.
            std::fill(m_visitedStamp.begin(), m_visitedStamp.end(), 0);
            m_currentStamp = 1;
        }

        // 3. Recorremos unicamente los chunks dentro del frustum o radio de colision solicitado.
        for (int32_t cy = range.minCY; cy <= range.maxCY; ++cy) {
            for (int32_t cx = range.minCX; cx <= range.maxCX; ++cx) {
                // Busqueda en la tabla hash dispersa: si el chunk no tiene objetos, saltamos al siguiente.
                auto it = m_chunks.find(PackKey(cx, cy));
                if (it == m_chunks.end()) {
                    continue;
                }

                // Registramos la coordenada del chunk activo (usada para telemetria F3 y overlays F4).
                outActiveChunks.push_back({ cx, cy });

                // Recorremos la lista contigua de IDs dentro de este chunk.
                for (uint32_t id : it->second) {
                    // Deduplicacion O(1): si la instancia cruza varios chunks, solo entra una vez.
                    if (m_visitedStamp[id] != m_currentStamp) {
                        m_visitedStamp[id] = m_currentStamp;
                        outInstances.push_back(id);
                    }
                }
            }
        }
    }

    size_t SpatialHashGrid::GetMemoryBytes() const {
        // Sumamos el arreglo de sellos + el almacenamiento aproximado de cada bucket ocupado.
        size_t total = m_visitedStamp.capacity() * sizeof(uint32_t);
        for (const auto& [key, list] : m_chunks) {
            total += sizeof(int64_t) + sizeof(std::vector<uint32_t>) + (list.capacity() * sizeof(uint32_t));
        }
        return total;
    }

} // namespace AsciiEngine