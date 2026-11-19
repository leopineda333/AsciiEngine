/**
 * @file MaterialSystem.cpp
 * @brief Parser JSON y evaluador de rampas ASCII / colorizacion de 3 paradas.
 */
#include <AsciiEngine/MaterialSystem.hpp>
#include <AsciiEngine/MathIso.hpp>
#include "json.hpp"
#include "raylib.h" // Solo para TraceLog en diagnosticos de carga.

#include <fstream>

namespace AsciiEngine {

    using json = nlohmann::json;

    namespace {

        // Convierte un digito hexadecimal ASCII ('0'-'9', 'A'-'F', 'a'-'f') a su valor [0..15].
        // Sin excepciones ni asignaciones dinamicas.
        uint8_t HexCharToNibble(char c) {
            if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(10 + (c - 'a'));
            if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(10 + (c - 'A'));
            return 0;
        }

        // Parsea un color hexadecimal "#RRGGBB" o "RRGGBB" a ColorRGBA.
        ColorRGBA ParseHexColor(std::string_view hex, const ColorRGBA& fallback) {
            if (!hex.empty() && hex.front() == '#') {
                hex.remove_prefix(1);
            }
            if (hex.size() < 6) {
                return fallback;
            }
            uint8_t r = static_cast<uint8_t>((HexCharToNibble(hex[0]) << 4) | HexCharToNibble(hex[1]));
            uint8_t g = static_cast<uint8_t>((HexCharToNibble(hex[2]) << 4) | HexCharToNibble(hex[3]));
            uint8_t b = static_cast<uint8_t>((HexCharToNibble(hex[4]) << 4) | HexCharToNibble(hex[5]));
            return { r, g, b, 255 };
        }

    } // namespace anonimo

    MaterialSystem::MaterialSystem() {
        RegisterFallbackMaterial();
    }

    void MaterialSystem::RegisterFallbackMaterial() {
        m_materials.clear();
        m_nameToId.clear();

        // Indice 0: Material magenta brillante para detectar al instante IDs mal escritos en JSON.
        Material fallback;
        fallback.name = "fallback_missing";
        fallback.asciiRamp = " .:?#@";
        fallback.darkColor = { 90, 10, 90, 255 };
        fallback.normalColor = { 210, 30, 210, 255 };
        fallback.litColor = { 255, 160, 255, 255 };
        fallback.gridMode = GridMode::REGULAR;

        m_materials.push_back(fallback);
        m_nameToId[fallback.name] = FALLBACK_MATERIAL_ID;
    }

    bool MaterialSystem::LoadFromFile(const std::string& jsonPath) {
        std::ifstream file(jsonPath);
        if (!file.is_open()) {
            TraceLog(LOG_ERROR, "MATERIAL_SYSTEM: No se pudo abrir %s", jsonPath.c_str());
            return false;
        }

        json root;
        try {
            // El 4to parametro 'true' permite comentarios estilo // dentro del JSON.
            root = json::parse(file, nullptr, true, true);
        }
        catch (const json::parse_error& e) {
            TraceLog(LOG_ERROR, "MATERIAL_SYSTEM: Error de sintaxis en %s -> %s", jsonPath.c_str(), e.what());
            return false;
        }

        if (!root.contains("materials") || !root["materials"].is_array()) {
            TraceLog(LOG_ERROR, "MATERIAL_SYSTEM: Falta arreglo raiz 'materials' en %s", jsonPath.c_str());
            return false;
        }

        // Reiniciamos conservando siempre el material fallback en el indice 0.
        RegisterFallbackMaterial();
        m_materials.reserve(root["materials"].size() + 1);

        for (const auto& entry : root["materials"]) {
            Material mat;
            mat.name = entry.value("id", "");
            mat.asciiRamp = entry.value("asciiRamp", " .:-=+*#%@");

            if (mat.name.empty() || mat.asciiRamp.empty()) {
                TraceLog(LOG_WARNING, "MATERIAL_SYSTEM: Material invalido omitido (id o rampa vacios).");
                continue;
            }

            // Colorizacion de 3 paradas: dark -> normal -> lit.
            if (entry.contains("colors")) {
                const auto& c = entry["colors"];
                mat.darkColor = ParseHexColor(c.value("dark", "#202020"), mat.darkColor);
                mat.normalColor = ParseHexColor(c.value("normal", "#808080"), mat.normalColor);
                mat.litColor = ParseHexColor(c.value("lit", "#E0E0E0"), mat.litColor);
            }

            // Modo de grilla espacial (REGULAR o NOISY).
            std::string modeStr = entry.value("gridMode", "REGULAR");
            mat.gridMode = (modeStr == "NOISY") ? GridMode::NOISY : GridMode::REGULAR;

            if (entry.contains("noisySettings")) {
                const auto& ns = entry["noisySettings"];
                mat.jitter = MathIso::Clamp(ns.value("jitter", 0.35f), 0.0f, 0.8f);
                mat.density = MathIso::Clamp(ns.value("density", 0.80f), 0.05f, 1.0f);
                mat.windWeight = MathIso::Clamp(ns.value("windWeight", 0.0f), 0.0f, 1.0f);
            }

            // Propiedades fisicas y de renderizado.
            if (entry.contains("properties")) {
                const auto& p = entry["properties"];
                mat.roughness = p.value("roughness", 0.8f);
                mat.reflectivity = p.value("reflectivity", 0.0f);
                mat.occludesLight = p.value("occludesLight", true);
                mat.outline = p.value("outline", true);
            }

            MaterialId newId = static_cast<MaterialId>(m_materials.size());
            m_nameToId[mat.name] = newId;
            m_materials.push_back(std::move(mat));
        }

        TraceLog(LOG_INFO, "MATERIAL_SYSTEM: %zu materiales cargados desde %s", m_materials.size() - 1, jsonPath.c_str());
        return true;
    }

    MaterialId MaterialSystem::GetId(std::string_view materialName) const {
        auto it = m_nameToId.find(std::string(materialName));
        if (it == m_nameToId.end()) {
            return FALLBACK_MATERIAL_ID;
        }
        return it->second;
    }

    const Material& MaterialSystem::Get(MaterialId id) const {
        if (id >= m_materials.size()) {
            return m_materials[FALLBACK_MATERIAL_ID];
        }
        return m_materials[id];
    }

    char MaterialSystem::SampleGlyph(MaterialId id, float luminance) const {
        const Material& mat = Get(id);
        float luma = MathIso::Clamp(luminance, 0.0f, 1.0f);

        // Mapea [0.0, 1.0] al indice entero [0, asciiRamp.size() - 1].
        size_t maxIdx = mat.asciiRamp.size() - 1;
        size_t idx = static_cast<size_t>(std::floor(luma * static_cast<float>(maxIdx) + 0.5f));
        return mat.asciiRamp[std::min(idx, maxIdx)];
    }

    ColorRGBA MaterialSystem::SampleColor(MaterialId id, float luminance) const {
        const Material& mat = Get(id);
        float luma = MathIso::Clamp(luminance, 0.0f, 1.0f);

        // Interpolacion en dos tramos:
        // [0.0, 0.5) mezcla dark -> normal; [0.5, 1.0] mezcla normal -> lit.
        if (luma < 0.5f) {
            return MathIso::LerpColor(mat.darkColor, mat.normalColor, luma * 2.0f);
        }
        return MathIso::LerpColor(mat.normalColor, mat.litColor, (luma - 0.5f) * 2.0f);
    }

} // namespace AsciiEngine