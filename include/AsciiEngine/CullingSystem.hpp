// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file CullingSystem.hpp
 * @brief Culling Espacial Capa 1 mediante Spatial Hash Grid 2D con deduplicacion O(1).
 *
 * @details Divide el plano cartesiano (X, Y) en celdas cuadradas llamadas "chunks" (ej. 24x24u).
 * En lugar de recorrer todas las instancias del mapa en cada frame de renderizado o fisica,
 * tanto el renderizador como CollisionSolver consultan unicamente los chunks que intersectan
 * su area de interes, manteniendo coste practicamente constante aunque el mapa crezca a miles de objetos.
 */
#pragma once

#include <AsciiEngine/Types.hpp>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace AsciiEngine {

    /**
     * @brief Coordenada discreta (columna, fila) de un chunk en el mundo 2D.
     */
    struct ChunkCoord {
        int32_t cx = 0;
        int32_t cy = 0;
    };

    /**
     * @brief Rango rectangular cerrado [minCX..maxCX] x [minCY..maxCY] de chunks a consultar.
     */
    struct ChunkRange {
        int32_t minCX = 0;
        int32_t maxCX = -1;
        int32_t minCY = 0;
        int32_t maxCY = -1;
    };

    class SpatialHashGrid {
    public:
        /**
         * @brief Crea una grilla espacial con el tamano de chunk indicado (en unidades de mundo).
         */
        explicit SpatialHashGrid(float chunkSize = 24.0f);

        /**
         * @brief Reinicia todos los chunks y pre-asigna el arreglo de sellos de deduplicacion.
         * @param chunkSize Tamano de lado de cada chunk en unidades de mundo (minimo efectivo > 1.0).
         * @param expectedInstanceCount Cantidad estimada de instancias para evitar realojamientos.
         */
        void Reset(float chunkSize, size_t expectedInstanceCount = 0);

        /**
         * @brief Registra el ID de una instancia en TODOS los chunks que solapa su radio en XY.
         * @details Insertarla en todos los chunks que toca garantiza que un objeto grande en la
         * frontera entre dos chunks nunca desaparezca prematuramente al salir uno de los chunks.
         * @complexity O(K) donde K es el numero de chunks cubiertos (tipicamente 1 a 4).
         */
        void InsertInstance(uint32_t instanceId, float worldX, float worldY, float worldRadius);

        /**
         * @brief Recolecta todos los IDs de instancias unicos contenidos en 'range'.
         * @details Reutiliza la capacidad de los vectores de salida (outInstances, outActiveChunks)
         * y usa un sello incremental (m_currentStamp) para evitar duplicados sin std::unordered_set
         * ni asignaciones dinamicas en el bucle principal.
         * @complexity O(C + M) donde C es el numero de chunks en el rango y M las referencias en ellos.
         */
        void QueryChunks(const ChunkRange& range,
            std::vector<uint32_t>& outInstances,
            std::vector<ChunkCoord>& outActiveChunks);

        float    GetChunkSize() const { return m_chunkSize; }
        uint32_t GetTotalChunks() const { return static_cast<uint32_t>(m_chunks.size()); }

        /**
         * @brief Calcula la memoria aproximada en bytes ocupada por la tabla hash y sus listas.
         */
        size_t   GetMemoryBytes() const;

    private:
        /**
         * @brief Empaqueta dos enteros con signo de 32 bits (cx, cy) en una clave unica de 64 bits.
         * @details Coloca 'cx' en los 32 bits superiores y los bits crudos de 'cy' (cast a uint32_t
         * para evitar extension de signo cuando cy < 0) en los 32 bits inferiores. Cero colisiones.
         */
        static int64_t PackKey(int32_t cx, int32_t cy) {
            return (static_cast<int64_t>(cx) << 32) | static_cast<uint32_t>(cy);
        }

        /**
         * @brief Convierte una coordenada continua del mundo a su indice discreto de chunk.
         */
        int32_t WorldToChunkCoord(float worldCoord) const;

        float m_chunkSize = 24.0f;
        float m_invChunkSize = 1.0f / 24.0f; // Inverso precalculado para reemplazar division por multiplicacion.

        // Tabla hash dispersa: solo ocupa memoria para chunks que realmente contienen objetos.
        std::unordered_map<int64_t, std::vector<uint32_t>> m_chunks;

        // Deduplicacion O(1) basada en sellos temporales (generation stamps):
        // m_visitedStamp[id] == m_currentStamp indica que 'id' ya fue agregado en esta consulta.
        std::vector<uint32_t> m_visitedStamp;
        uint32_t              m_currentStamp = 1;
    };

} // namespace AsciiEngine