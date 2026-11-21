// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file MaterialSystem.cpp
 * @brief Implementacion comentada linea por linea del parser JSON de materiales y muestreadores ASCII.
 */
#include <AsciiEngine/MaterialSystem.hpp>
#include <AsciiEngine/MathIso.hpp>
#include "json.hpp"   // Dependencia privada: nunca se expone en include/AsciiEngine/.
#include "raylib.h"   // Usado exclusivamente aqui para TraceLog() en mensajes de diagnostico.

#include <fstream>    // std::ifstream para leer el archivo JSON desde disco.

namespace AsciiEngine {

    using json = nlohmann::json;

    // Namespace anonimo: funciones auxiliares privadas a esta unidad de traduccion (.cpp).
    namespace {

        /**
         * @brief Convierte un caracter hexadecimal ASCII ('0'-'9', 'a'-'f', 'A'-'F') a su nibble [0..15].
         * @details Evita std::stoi o std::stringstream, eliminando asignaciones dinamicas y excepciones.
         */
        uint8_t HexCharToNibble(char c) {
            if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(10 + (c - 'a'));
            if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(10 + (c - 'A'));
            return 0; // Caracter no hexadecimal -> devuelve 0 de forma segura.
        }

        /**
         * @brief Decodifica una cadena "#RRGGBB" o "RRGGBB" en un ColorRGBA opaco (a = 255).
         * @details Usa std::string_view para leer sin copiar memoria del string original.
         */
        ColorRGBA ParseHexColor(std::string_view hex, const ColorRGBA& fallback) {
            // Si el texto inicia con '#', avanzamos el inicio de la vista 1 caracter.
            if (!hex.empty() && hex.front() == '#') {
                hex.remove_prefix(1);
            }
            // Necesitamos al menos 6 digitos hexadecimales (2 por cada canal R, G, B).
            if (hex.size() < 6) {
                return fallback;
            }
            // Cada par de nibbles se combina mediante desplazamiento de 4 bits: (alto << 4) | bajo.
            // Ejemplo: "A5" -> (10 << 4) | 5 = 160 + 5 = 165.
            uint8_t r = static_cast<uint8_t>((HexCharToNibble(hex[0]) << 4) | HexCharToNibble(hex[1]));
            uint8_t g = static_cast<uint8_t>((HexCharToNibble(hex[2]) << 4) | HexCharToNibble(hex[3]));
            uint8_t b = static_cast<uint8_t>((HexCharToNibble(hex[4]) << 4) | HexCharToNibble(hex[5]));
            return { r, g, b, 255 };
        }

    } // namespace anonimo

    MaterialSystem::MaterialSystem() {
        // Garantizamos que incluso antes de llamar a LoadFromFile(), Get(0) nunca acceda a memoria vacia.
        RegisterFallbackMaterial();
    }

    void MaterialSystem::RegisterFallbackMaterial() {
        m_materials.clear();
        m_nameToId.clear();

        // Indice 0 reservado: magenta brillante estilo "missing texture" para detectar al instante
        // cualquier materialId mal escrito en un objeto, personaje o mapa JSON.
        Material fallback;
        fallback.name = "fallback_missing";
        fallback.asciiRamp = ".:?#@";
        fallback.darkColor = { 90, 10, 90, 255 };
        fallback.normalColor = { 210, 30, 210, 255 };
        fallback.litColor = { 255, 160, 255, 255 };
        fallback.gridMode = GridMode::REGULAR;

        m_materials.push_back(fallback);
        m_nameToId[fallback.name] = FALLBACK_MATERIAL_ID;
    }

    bool MaterialSystem::LoadFromFile(const std::string& jsonPath) {
        // 1. Abrimos el archivo en modo lectura.
        std::ifstream file(jsonPath);
        if (!file.is_open()) {
            TraceLog(LOG_ERROR, "MATERIAL_SYSTEM: No se pudo abrir %s", jsonPath.c_str());
            return false;
        }

        // 2. Parseamos el JSON capturando posibles errores de sintaxis (ej. comas sobrantes al editar a mano).
        json root;
        try {
            // Parametros: (stream, callback=nullptr, allow_exceptions=true, ignore_comments=true).
            // ignore_comments=true permite documentar los archivos JSON con comentarios estilo //.
            root = json::parse(file, nullptr, true, true);
        }
        catch (const json::parse_error& e) {
            TraceLog(LOG_ERROR, "MATERIAL_SYSTEM: Error de sintaxis en %s -> %s", jsonPath.c_str(), e.what());
            return false;
        }

        // 3. Validamos que exista el arreglo raiz esperado "materials".
        if (!root.contains("materials") || !root["materials"].is_array()) {
            TraceLog(LOG_ERROR, "MATERIAL_SYSTEM: Falta arreglo raiz 'materials' en %s", jsonPath.c_str());
            return false;
        }

        // 4. Limpiamos el registro previo (soporte Hot-Reload) y pre-reservamos la capacidad exacta
        //    para evitar realojamientos multiples de memoria durante push_back.
        RegisterFallbackMaterial();
        m_materials.reserve(root["materials"].size() + 1);

        // 5. Recorremos cada definicion del arreglo JSON y construimos su struct Material.
        for (const auto& entry : root["materials"]) {
            Material mat;
            mat.name = entry.value("id", "");
            mat.asciiRamp = entry.value("asciiRamp", ".:-=+*#%@");

            // Si falta nombre o rampa, descartamos la entrada sin abortar la carga del resto.
            if (mat.name.empty() || mat.asciiRamp.empty()) {
                TraceLog(LOG_WARNING, "MATERIAL_SYSTEM: Material invalido omitido (id o rampa vacios).");
                continue;
            }

            // Paradas de color: dark (sombra), normal (medio), lit (brillo maximo).
            if (entry.contains("colors")) {
                const auto& c = entry["colors"];
                mat.darkColor = ParseHexColor(c.value("dark", "#202020"), mat.darkColor);
                mat.normalColor = ParseHexColor(c.value("normal", "#808080"), mat.normalColor);
                mat.litColor = ParseHexColor(c.value("lit", "#E0E0E0"), mat.litColor);
            }

            // Modo de muestreo espacial al hornear geometria (REGULAR por defecto).
            std::string modeStr = entry.value("gridMode", "REGULAR");
            mat.gridMode = (modeStr == "NOISY") ? GridMode::NOISY : GridMode::REGULAR;

            // Parametros opcionales de dispersion para materiales organicos (follaje, piedra, pelaje).
            if (entry.contains("noisySettings")) {
                const auto& ns = entry["noisySettings"];
                mat.jitter = MathIso::Clamp(ns.value("jitter", 0.35f), 0.0f, 0.5f);
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

            // Asignamos como MaterialId el indice que ocupara en m_materials y movemos el struct sin copiarlo.
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
            // Devolver el fallback en lugar de lanzar excepcion permite depurar visualmente en escena.
            return FALLBACK_MATERIAL_ID;
        }
        return it->second;
    }

    const Material& MaterialSystem::Get(MaterialId id) const {
        // Comprobacion defensiva de limites: impide lecturas fuera de memoria si un ID queda obsoleto.
        if (id >= m_materials.size()) {
            return m_materials[FALLBACK_MATERIAL_ID];
        }
        return m_materials[id];
    }

    char MaterialSystem::SampleGlyph(MaterialId id, float luminance) const {
        const Material& mat = Get(id);

        // 1. Acotamos luminancia a [0.0, 1.0].
        float luma = MathIso::Clamp(luminance, 0.0f, 1.0f);

        // 2. Escalamos [0.0, 1.0] al indice entero [0 .. N-1] redondeando al entero mas cercano (+0.5f).
        size_t maxIdx = mat.asciiRamp.size() - 1;
        size_t idx = static_cast<size_t>(std::floor(luma * static_cast<float>(maxIdx) + 0.5f));

        // 3. std::min protege contra cualquier redondeo flotante en el extremo 1.0.
        return mat.asciiRamp[std::min(idx, maxIdx)];
    }

    ColorRGBA MaterialSystem::SampleColor(MaterialId id, float luminance) const {
        const Material& mat = Get(id);
        float luma = MathIso::Clamp(luminance, 0.0f, 1.0f);

        // Interpolacion por tramos (piecewise linear) sobre las 3 paradas de color:
        // - Mitad oscura  [0.0, 0.5): reescalamos luma * 2.0 a [0.0, 1.0) para ir de darkColor -> normalColor.
        // - Mitad clara   [0.5, 1.0]: reescalamos (luma - 0.5) * 2.0 a [0.0, 1.0] para ir de normalColor -> litColor.
        if (luma < 0.5f) {
            return MathIso::LerpColor(mat.darkColor, mat.normalColor, luma * 2.0f);
        }
        return MathIso::LerpColor(mat.normalColor, mat.litColor, (luma - 0.5f) * 2.0f);
    }

} // namespace AsciiEngine