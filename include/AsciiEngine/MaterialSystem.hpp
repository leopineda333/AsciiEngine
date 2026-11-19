/**
 * @file MaterialSystem.hpp
 * @brief Registro data-driven de materiales ASCII con colorizacion de 3 paradas y modos de grilla.
 */
#pragma once

#include <AsciiEngine/Types.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace AsciiEngine {

    // Definicion completa de un material cargado desde JSON.
    struct Material {
        std::string name;             // ID legible (ej. "metal_iron", "foliage_pine").
        std::string asciiRamp;        // Glifos ordenados de sombra profunda a brillo maximo.

        // Colorizacion de 3 paradas para dar riqueza cromatica sin depender de texturas.
        ColorRGBA darkColor = { 30, 30, 30, 255 };
        ColorRGBA normalColor = { 140, 140, 140, 255 };
        ColorRGBA litColor = { 240, 240, 240, 255 };

        // Configuracion de muestreo espacial (REGULAR vs NOISY).
        GridMode gridMode = GridMode::REGULAR;
        float jitter = 0.0f; // Amplitud de desplazamiento en modo NOISY [0.0, 0.5].
        float density = 1.0f; // Probabilidad de conservar el atomo en modo NOISY [0.0, 1.0].
        float windWeight = 0.0f; // Influencia del campo de viento sobre este material [0.0, 1.0].

        // Propiedades fisicas y de renderizado.
        float roughness = 0.8f;
        float reflectivity = 0.0f;
        bool  occludesLight = true;
        bool  outline = true;
    };

    class MaterialSystem {
    public:
        MaterialSystem();

        // Carga y valida los materiales desde un archivo JSON. Soporta Hot-Reloading.
        bool LoadFromFile(const std::string& jsonPath);

        // Resuelve un nombre de material a su MaterialId numerico O(1) promedio (usar al cargar assets).
        MaterialId GetId(std::string_view materialName) const;

        // Acceso directo O(1) por indice contiguo en RAM (usar en el bucle de renderizado).
        const Material& Get(MaterialId id) const;

        // Devuelve la cantidad total de materiales cargados (incluyendo el fallback en indice 0).
        size_t Count() const { return m_materials.size(); }

        // Selecciona el caracter ASCII correspondiente a una luminancia [0.0, 1.0].
        char SampleGlyph(MaterialId id, float luminance) const;

        // Calcula el color final interpolando dark -> normal -> lit segun la luminancia [0.0, 1.0].
        ColorRGBA SampleColor(MaterialId id, float luminance) const;

    private:
        void RegisterFallbackMaterial();

        // Almacenamiento contiguo orientado a cache (indice = MaterialId).
        std::vector<Material> m_materials;

        // Diccionario usado exclusivamente en tiempo de carga o en el Editor.
        std::unordered_map<std::string, MaterialId> m_nameToId;
    };

} // namespace AsciiEngine