// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file Settings.cpp
 * @brief Implementacion de presets de calidad y persistencia JSON para EngineSettings.
 */
#include <AsciiEngine/Settings.hpp>
#include <AsciiEngine/MathIso.hpp>
#include "json.hpp" // Dependencia privada: nunca se expone en headers publicos.
#include "raylib.h" // Solo para TraceLog() en diagnosticos de carga/guardado.

#include <algorithm>
#include <fstream>

namespace AsciiEngine {

    using json = nlohmann::json;

    namespace {

        /**
         * @brief Acota todos los campos de EngineSettings a rangos seguros para prevenir bloqueos o NaN.
         */
        void SanitizeSettings(EngineSettings& s) {
            s.cellWidthPx = MathIso::Clamp(s.cellWidthPx, 3.5f, 12.0f);
            s.cellHeightPx = MathIso::Clamp(s.cellHeightPx, 6.0f, 20.0f);
            s.targetFps = std::clamp(s.targetFps, 0, 240);
            s.pixelOutlinePx = MathIso::Clamp(s.pixelOutlinePx, 0.5f, 2.5f);
            s.backingDarken = MathIso::Clamp(s.backingDarken, 0.10f, 0.85f);
            s.shadowRaySteps = std::clamp(s.shadowRaySteps, 0, 20);
            s.giStrengthScale = MathIso::Clamp(s.giStrengthScale, 0.0f, 1.6f);
            s.atmosphericInfluence = MathIso::Clamp(s.atmosphericInfluence, 0.0f, 1.0f);
            s.foliageDensityScale = MathIso::Clamp(s.foliageDensityScale, 0.0f, 1.5f);
            s.particleDensityScale = MathIso::Clamp(s.particleDensityScale, 0.0f, 1.5f);
        }

    } // namespace anonimo

    EngineSettings MakeQualityPreset(QualityPreset preset) {
        EngineSettings s; // Por defecto coincide con HIGH.

        switch (preset) {
        case QualityPreset::LOW:
            s.cellWidthPx = 7.5f;
            s.cellHeightPx = 13.0f;
            s.enableOutlines = false;
            s.enableCellBacking = true;
            s.backingAlpha = 190;
            s.enableShadows = false;
            s.shadowRaySteps = 0;
            s.enableWaterReflections = false;
            s.enableWindSway = false;
            s.foliageDensityScale = 0.0f;
            s.particleDensityScale = 0.45f;
            break;

        case QualityPreset::MEDIUM:
            s.cellWidthPx = 6.5f;
            s.cellHeightPx = 11.0f;
            s.enableOutlines = true;
            s.enableShadows = true;
            s.shadowRaySteps = 8;
            s.enableWaterReflections = true;
            s.enableWindSway = true;
            s.foliageDensityScale = 0.65f;
            s.particleDensityScale = 0.75f;
            break;

        case QualityPreset::HIGH:
            // Valores por defecto calibrados de v1.0.0 (5.5x9.5, 14 pasos de sombra, 100% densidad).
            break;

        case QualityPreset::ULTRA:
            s.cellWidthPx = 4.8f;
            s.cellHeightPx = 8.2f;
            s.enableOutlines = true;
            s.enableShadows = true;
            s.shadowRaySteps = 18;
            s.enableWaterReflections = true;
            s.enableWindSway = true;
            s.foliageDensityScale = 1.25f;
            s.particleDensityScale = 1.25f;
            break;
        }

        SanitizeSettings(s);
        return s;
    }

    bool LoadEngineSettings(const std::string& jsonPath, EngineSettings& outSettings) {
        std::ifstream file(jsonPath);
        if (!file.is_open()) {
            TraceLog(LOG_INFO, "SETTINGS: No existe %s; se usaran valores por defecto.", jsonPath.c_str());
            return false;
        }

        json root;
        try {
            root = json::parse(file, nullptr, true, true);
        }
        catch (const json::parse_error& e) {
            TraceLog(LOG_ERROR, "SETTINGS: Error parseando %s -> %s", jsonPath.c_str(), e.what());
            return false;
        }

        // Si el JSON incluye un preset base ("LOW", "MEDIUM", "HIGH", "ULTRA"), partimos de el
        // antes de aplicar sobrescrituras individuales.
        if (root.contains("preset") && root["preset"].is_string()) {
            std::string p = root["preset"].get<std::string>();
            if (p == "LOW")    outSettings = MakeQualityPreset(QualityPreset::LOW);
            else if (p == "MEDIUM") outSettings = MakeQualityPreset(QualityPreset::MEDIUM);
            else if (p == "ULTRA")  outSettings = MakeQualityPreset(QualityPreset::ULTRA);
            else                    outSettings = MakeQualityPreset(QualityPreset::HIGH);
        }

        if (root.contains("display") && root["display"].is_object()) {
            const auto& d = root["display"];
            outSettings.cellWidthPx = d.value("cellWidthPx", outSettings.cellWidthPx);
            outSettings.cellHeightPx = d.value("cellHeightPx", outSettings.cellHeightPx);
            outSettings.targetFps = d.value("targetFps", outSettings.targetFps);
        }

        if (root.contains("style") && root["style"].is_object()) {
            const auto& st = root["style"];
            outSettings.enableOutlines = st.value("enableOutlines", outSettings.enableOutlines);
            outSettings.enableAsciiBorder = st.value("enableAsciiBorder", outSettings.enableAsciiBorder);
            outSettings.enableSubPixelOffset = st.value("enableSubPixelOffset", outSettings.enableSubPixelOffset);
            outSettings.pixelOutlinePx = st.value("pixelOutlinePx", outSettings.pixelOutlinePx);
            outSettings.enableCellBacking = st.value("enableCellBacking", outSettings.enableCellBacking);
            outSettings.backingDarken = st.value("backingDarken", outSettings.backingDarken);
            outSettings.backingAlpha = static_cast<uint8_t>(std::clamp(st.value("backingAlpha", static_cast<int>(outSettings.backingAlpha)), 0, 255));
        }

        if (root.contains("lighting") && root["lighting"].is_object()) {
            const auto& l = root["lighting"];
            outSettings.enableShadows = l.value("enableShadows", outSettings.enableShadows);
            outSettings.shadowRaySteps = l.value("shadowRaySteps", outSettings.shadowRaySteps);
            outSettings.enablePointLights = l.value("enablePointLights", outSettings.enablePointLights);
            outSettings.giStrengthScale = l.value("giStrengthScale", outSettings.giStrengthScale);
            outSettings.atmosphericInfluence = l.value("atmosphericInfluence", outSettings.atmosphericInfluence);
        }

        if (root.contains("effects") && root["effects"].is_object()) {
            const auto& e = root["effects"];
            outSettings.enableWaterReflections = e.value("enableWaterReflections", outSettings.enableWaterReflections);
            outSettings.enableWindSway = e.value("enableWindSway", outSettings.enableWindSway);
            outSettings.foliageDensityScale = e.value("foliageDensityScale", outSettings.foliageDensityScale);
            outSettings.particleDensityScale = e.value("particleDensityScale", outSettings.particleDensityScale);
        }

        SanitizeSettings(outSettings);
        TraceLog(LOG_INFO, "SETTINGS: Configuracion cargada desde %s.", jsonPath.c_str());
        return true;
    }

    bool SaveEngineSettings(const std::string& jsonPath, const EngineSettings& settings) {
        EngineSettings clean = settings;
        SanitizeSettings(clean);

        json root = {
            { "display",{
                { "cellWidthPx",  clean.cellWidthPx },
                { "cellHeightPx", clean.cellHeightPx },
                { "targetFps",    clean.targetFps }
            } },
            { "style",{
                { "enableOutlines",       clean.enableOutlines },
                { "enableAsciiBorder",    clean.enableAsciiBorder },
                { "enableSubPixelOffset", clean.enableSubPixelOffset },
                { "pixelOutlinePx",       clean.pixelOutlinePx },
                { "enableCellBacking",    clean.enableCellBacking },
                { "backingDarken",        clean.backingDarken },
                { "backingAlpha",         static_cast<int>(clean.backingAlpha) }
            } },
            { "lighting",{
                { "enableShadows",        clean.enableShadows },
                { "shadowRaySteps",       clean.shadowRaySteps },
                { "enablePointLights",    clean.enablePointLights },
                { "giStrengthScale",      clean.giStrengthScale },
                { "atmosphericInfluence", clean.atmosphericInfluence }
            } },
            { "effects",{
                { "enableWaterReflections", clean.enableWaterReflections },
                { "enableWindSway",         clean.enableWindSway },
                { "foliageDensityScale",    clean.foliageDensityScale },
                { "particleDensityScale",   clean.particleDensityScale }
            } }
        };

        std::ofstream out(jsonPath);
        if (!out.is_open()) {
            TraceLog(LOG_ERROR, "SETTINGS: No se pudo guardar %s.", jsonPath.c_str());
            return false;
        }
        out << root.dump(2);
        TraceLog(LOG_INFO, "SETTINGS: Configuracion guardada en %s.", jsonPath.c_str());
        return true;
    }

} // namespace AsciiEngine