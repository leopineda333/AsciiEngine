// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file MaterialSystem.hpp
 * @brief Registro data-driven de materiales ASCII con colorizacion de 3 paradas y modos de grilla.
 *
 * @details Aplica Data-Oriented Design: los identificadores de texto ("metal_iron", "foliage_pine")
 * solo se consultan en un hash map al cargar los JSON para traducirse a un MaterialId entero (uint16_t).
 * En tiempo de renderizado, leer un material cuesta una sola indexacion contigua O(1) en RAM.
 */
#pragma once

#include <AsciiEngine/Types.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace AsciiEngine {

    /**
     * @brief Definicion completa en memoria de un material leido desde materials.json.
     */
    struct Material {
        std::string name;             // Identificador legible unico (ej. "stone_granite").
        std::string asciiRamp;        // Caracteres ordenados de menor a mayor densidad visual (sombra -> brillo).

        // Paleta de 3 paradas: da riqueza tonal a sombras y luces altas sin depender de texturas.
        ColorRGBA darkColor = { 30, 30, 30, 255 };    // Color en luminancia 0.0 (sombra profunda).
        ColorRGBA normalColor = { 140, 140, 140, 255 }; // Color en luminancia 0.5 (tono medio real).
        ColorRGBA litColor = { 240, 240, 240, 255 }; // Color en luminancia 1.0 (luz directa maxima).

        // Configuracion usada durante el Point-Cloud Baking (WorldLoader / CharacterSystem).
        GridMode gridMode = GridMode::REGULAR; // REGULAR = paso uniforme; NOISY = desplazamiento organico.
        float    jitter = 0.0f;              // Amplitud de perturbacion espacial en modo NOISY [0.0, 0.5].
        float    density = 1.0f;              // Probabilidad de conservar cada atomo en modo NOISY [0.05, 1.0].
        float    windWeight = 0.0f;              // Susceptibilidad por defecto al campo de viento [0.0, 1.0].

        // Propiedades opticas y de renderizado consumidas por LightingSystem y AsciiZBuffer.
        float roughness = 0.8f;  // Rugosidad superficial [0.0 espejo .. 1.0 mate].
        float reflectivity = 0.0f;  // Nivel de reflectividad base [0.0 .. 1.0].
        bool  occludesLight = true;  // true = el material se registra en el Heightfield de sombras 2.5D.
        bool  outline = true;  // true = el AsciiZBuffer dibuja contorno en sus siluetas.
    };

    class MaterialSystem {
    public:
        /**
         * @brief Inicializa el registro asegurando que el indice 0 contenga siempre el material fallback.
         */
        MaterialSystem();

        /**
         * @brief Carga y valida todos los materiales desde un archivo JSON.
         * @details Soporta Hot-Reloading (F5): reconstruye el vector conservando el fallback en el indice 0.
         * @return true si el archivo se abrio y parseo correctamente.
         */
        bool LoadFromFile(const std::string& jsonPath);

        /**
         * @brief Traduce el nombre de un material a su MaterialId numerico.
         * @details Llamar SOLO en tiempo de carga o en el Editor (evitar en el bucle de render).
         * @return FALLBACK_MATERIAL_ID (0) si el nombre no existe, evitando crashes por typos en JSON.
         * @complexity O(1) promedio (busqueda en tabla hash).
         */
        MaterialId GetId(std::string_view materialName) const;

        /**
         * @brief Acceso directo por indice contiguo al material en RAM.
         * @details Seguro ante indices fuera de rango: retorna el material fallback si id >= size().
         * @complexity O(1) estricto (una sola lectura de arreglo contiguo).
         */
        const Material& Get(MaterialId id) const;

        /**
         * @brief Cantidad total de materiales registrados (incluye el material fallback en el indice 0).
         */
        size_t Count() const { return m_materials.size(); }

        /**
         * @brief Mapea una luminancia [0.0, 1.0] al caracter correspondiente de la asciiRamp del material.
         * @complexity O(1).
         */
        char SampleGlyph(MaterialId id, float luminance) const;

        /**
         * @brief Interpola el color de 3 paradas (dark -> normal -> lit) segun una luminancia [0.0, 1.0].
         * @complexity O(1).
         */
        ColorRGBA SampleColor(MaterialId id, float luminance) const;

    private:
        /**
         * @brief Limpia los contenedores y registra en el indice 0 un material magenta de diagnostico.
         */
        void RegisterFallbackMaterial();

        // Vector contiguo indexado por MaterialId: optimo para localidad de cache L1/L2 en Render().
        std::vector<Material> m_materials;

        // Indice inverso nombre -> MaterialId, usado unicamente al cargar JSONs o en el Editor.
        std::unordered_map<std::string, MaterialId> m_nameToId;
    };

} // namespace AsciiEngine