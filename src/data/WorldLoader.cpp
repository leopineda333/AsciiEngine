// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file WorldLoader.cpp
 * @brief Implementacion comentada linea por linea del Point-Cloud Baker (Flyweight), cargador/serializador
 *        de mapas JSON y constructor de la mascara de ocupacion 2D (GroundMask).
 */
#include <AsciiEngine/World.hpp>
#include <AsciiEngine/MathIso.hpp>
#include <AsciiEngine/MaterialSystem.hpp>
#include <AsciiEngine/CullingSystem.hpp>
#include "json.hpp" // Dependencia privada: nunca se expone en headers publicos.
#include "raylib.h" // Solo para TraceLog() en mensajes de carga/guardado.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace AsciiEngine {

    using json = nlohmann::json;
    namespace fs = std::filesystem;

    namespace {

        constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;

        /**
         * @brief Lee un Vec3 flexible desde JSON (acepta tanto [x, y, z] como { "x":.., "y":.., "z":.. }).
         */
        Vec3 ReadVec3(const json& obj, const char* key, Vec3 def = { 0.0f, 0.0f, 0.0f }) {
            if (!obj.contains(key)) return def;
            const auto& v = obj[key];
            if (v.is_array() && v.size() >= 3) {
                return { v[0].get<float>(), v[1].get<float>(), v[2].get<float>() };
            }
            if (v.is_object()) {
                return { v.value("x", def.x), v.value("y", def.y), v.value("z", def.z) };
            }
            return def;
        }

        /**
         * @brief Convierte un digito hexadecimal ASCII en su valor numerico [0..15].
         */
        uint8_t HexNibble(char c) {
            if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(10 + (c - 'a'));
            if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(10 + (c - 'A'));
            return 0;
        }

        /**
         * @brief Lee un color desde JSON soportando formato hexadecimal "#RRGGBB" o arreglo [r, g, b].
         */
        ColorRGBA ReadColor(const json& obj, const char* key, ColorRGBA def) {
            if (!obj.contains(key)) return def;
            const auto& c = obj[key];
            if (c.is_string()) {
                std::string s = c.get<std::string>();
                size_t off = (!s.empty() && s[0] == '#') ? 1 : 0;
                if (s.size() >= off + 6) {
                    return {
                        static_cast<uint8_t>((HexNibble(s[off + 0]) << 4) | HexNibble(s[off + 1])),
                        static_cast<uint8_t>((HexNibble(s[off + 2]) << 4) | HexNibble(s[off + 3])),
                        static_cast<uint8_t>((HexNibble(s[off + 4]) << 4) | HexNibble(s[off + 5])),
                        255
                    };
                }
            }
            else if (c.is_array() && c.size() >= 3) {
                return { c[0].get<uint8_t>(), c[1].get<uint8_t>(), c[2].get<uint8_t>(), 255 };
            }
            return def;
        }

        /**
         * @brief Convierte un ColorRGBA a texto hexadecimal "#RRGGBB" para guardar mapas JSON legibles.
         */
        std::string ColorToHex(const ColorRGBA& c) {
            static constexpr char kHex[] = "0123456789ABCDEF";
            std::string out = "#000000";
            out[1] = kHex[(c.r >> 4) & 0xF]; out[2] = kHex[c.r & 0xF];
            out[3] = kHex[(c.g >> 4) & 0xF]; out[4] = kHex[c.g & 0xF];
            out[5] = kHex[(c.b >> 4) & 0xF]; out[6] = kHex[c.b & 0xF];
            return out;
        }

        /**
         * @brief Aplica porosidad (density) y perturbacion espacial (jitter) si el material usa GridMode::NOISY.
         * @return false si el atomo es descartado por porosidad (genera copas organicas o piedra rugosa).
         */
        bool ApplyMaterialGridNoise(Vec3& p, float step, const Material& mat, float seed) {
            if (mat.gridMode != GridMode::NOISY) {
                return true;
            }
            // Descarte probabilistico determinista segun mat.density [0.05..1.0].
            if (MathIso::Hash3D(p.x, p.y, p.z, seed + 11.7f) > mat.density) {
                return false;
            }
            // Desplazamos el punto en las 3 dimensiones alrededor de su posicion regular usando 3 semillas distintas.
            float jAmp = mat.jitter * step * 2.0f;
            p.x += (MathIso::Hash3D(p.x, p.y, p.z, seed + 1.3f) - 0.5f) * jAmp;
            p.y += (MathIso::Hash3D(p.x, p.y, p.z, seed + 2.7f) - 0.5f) * jAmp;
            p.z += (MathIso::Hash3D(p.x, p.y, p.z, seed + 5.1f) - 0.5f) * jAmp;
            return true;
        }

        /**
         * @brief Empaqueta un atomo horneado y actualiza los radios envolventes (XY y altura Z) del prefab.
         */
        void PushBakedAtom(BakedPrefab& prefab, Vec3 localPos, Vec3 localNormal, float ao,
            MaterialId matId, float windWeight, char overrideChar = '\0') {
            BakedAtom atom;
            atom.localPos = localPos;
            atom.localNormal = MathIso::Normalize(localNormal);
            atom.ao = MathIso::Clamp(ao, 0.2f, 1.0f);
            atom.materialId = matId;
            // Comprimimos el peso del viento [0.0..1.0] en 1 solo byte [0..255] para mantener compacto BakedAtom.
            atom.windWeightByte = static_cast<uint8_t>(MathIso::Clamp(windWeight, 0.0f, 1.0f) * 255.0f);
            atom.structuralOverride = overrideChar;

            // Expandimos las cotas envolventes del prefab (+0.35u de holgura para que Frustum Culling no corte bordes).
            float xyDist = std::sqrt(localPos.x * localPos.x + localPos.y * localPos.y);
            prefab.boundRadiusXY = std::max(prefab.boundRadiusXY, xyDist + 0.35f);
            prefab.boundHeightZ = std::max(prefab.boundHeightZ, localPos.z + 0.35f);

            prefab.atoms.push_back(atom);
        }

        /**
         * @brief Hornea una parte geometrica SDF del JSON dentro de la nube de puntos contigua del prefab.
         *
         * @details Optimizacion de cascara vacia (Hollow Shell Baking): para todas las primitivas
         * (ELLIPSOID, CYLINDER, CONE, BOX, ARCH) descartamos el volumen solido interior que nunca seria
         * visible desde afuera, reduciendo entre un 60% y un 80% la cantidad de atomos procesados por frame.
         */
        void BakePrefabPart(BakedPrefab& prefab, const json& part, const MaterialSystem& materials, float partSeed) {
            const std::string shape = part.value("shape", "BOX");
            const std::string matName = part.value("materialId", "");
            const MaterialId  matId = materials.GetId(matName);
            const Material& mat = materials.Get(matId);

            const Vec3  offset = ReadVec3(part, "offset");
            const float step = MathIso::Clamp(part.value("step", 0.25f), 0.10f, 2.0f);
            const float windW = part.value("windWeight", mat.windWeight);
            const json  dims = part.contains("dimensions") ? part["dimensions"] : json::object();

            if (shape == "ELLIPSOID") {
                Vec3 radii = {
                    dims.value("rx", dims.value("radius", 1.5f)),
                    dims.value("ry", dims.value("radius", 1.5f)),
                    dims.value("rz", dims.value("radius", 1.5f))
                };
                for (float z = -radii.z; z <= radii.z; z += step) {
                    for (float y = -radii.y; y <= radii.y; y += step) {
                        for (float x = -radii.x; x <= radii.x; x += step) {
                            Vec3 p = { x, y, z };
                            if (!ApplyMaterialGridNoise(p, step, mat, partSeed)) continue;

                            float m = MathIso::EllipsoidMetric(p, radii);
                            // m > 1.0: fuera del elipsoide; m < 0.62: interior solido invisible.
                            if (m > 1.0f || m < 0.62f) continue;

                            Vec3 norm = MathIso::EllipsoidNormal(p, radii);
                            // AO basado en norm.z: la parte inferior de una roca recibe menos luz del cielo.
                            float ao = 0.45f + 0.55f * norm.z;
                            PushBakedAtom(prefab, { offset.x + p.x, offset.y + p.y, offset.z + p.z },
                                norm, ao, matId, windW);
                        }
                    }
                }
            }
            else if (shape == "CYLINDER") {
                const float r = dims.value("radius", 0.5f);
                const float h = dims.value("height", 3.0f);
                const float rSqMax = r * r;
                const float rInner = std::max(0.0f, r - step * 1.35f);
                const float rSqMin = rInner * rInner;

                for (float z = 0.0f; z <= h; z += step) {
                    const bool isTop = (z >= h - step * 0.9f);
                    for (float y = -r; y <= r; y += step) {
                        for (float x = -r; x <= r; x += step) {
                            Vec3 p = { x, y, z };
                            if (!ApplyMaterialGridNoise(p, step, mat, partSeed)) continue;

                            float rSq = p.x * p.x + p.y * p.y;
                            // Descartamos puntos fuera del radio o dentro del nucleo hueco (salvo en la tapa superior).
                            if (rSq > rSqMax) continue;
                            if (!isTop && rSq < rSqMin) continue;

                            Vec3 norm = isTop ? Vec3{ 0.0f, 0.0f, 1.0f } : Vec3{ p.x, p.y, 0.15f };
                            float hNorm = z / std::max(h, 0.01f);
                            float ao = 0.45f + 0.55f * hNorm;
                            // El viento actua proporcionalmente a la altura del tronco (0 en la base anclada al suelo).
                            PushBakedAtom(prefab, { offset.x + p.x, offset.y + p.y, offset.z + p.z },
                                norm, ao, matId, windW * hNorm);
                        }
                    }
                }
            }
            else if (shape == "CONE") {
                const float baseR = dims.value("radius", 1.8f);
                const float h = dims.value("height", 4.0f);
                // Pendiente de la normal lateral perpendicular a la generatriz del cono.
                const float slopeZ = baseR / std::max(h, 0.01f);

                for (float z = 0.0f; z <= h; z += step) {
                    const float t = z / std::max(h, 0.01f); // 0 en la base -> 1 en la punta.
                    const float curR = baseR * (1.0f - t);

                    // En la punta del cono (curR casi 0) colocamos un unico atomo vertical.
                    if (curR < 0.05f) {
                        PushBakedAtom(prefab, { offset.x, offset.y, offset.z + z },
                            { 0.0f, 0.0f, 1.0f }, 1.0f, matId, windW);
                        continue;
                    }

                    const float rSqMax = curR * curR;
                    const float rInner = std::max(0.0f, curR - step * 1.45f);
                    const float rSqMin = rInner * rInner;

                    for (float y = -curR; y <= curR; y += step) {
                        for (float x = -curR; x <= curR; x += step) {
                            Vec3 p = { x, y, z };
                            if (!ApplyMaterialGridNoise(p, step, mat, partSeed)) continue;

                            float rSq = p.x * p.x + p.y * p.y;
                            if (rSq > rSqMax) continue;
                            if (z > step && rSq < rSqMin) continue;

                            float distXY = std::sqrt(rSq);
                            Vec3 norm = (distXY > 1e-4f)
                                ? Vec3{ p.x / distXY, p.y / distXY, slopeZ }
                            : Vec3{ 0.0f, 0.0f, 1.0f };

                            // Mas luz ambiental y flexibilidad al viento conforme sube hacia la copa.
                            float ao = 0.45f + 0.55f * t;
                            PushBakedAtom(prefab, { offset.x + p.x, offset.y + p.y, offset.z + p.z },
                                norm, ao, matId, windW * (0.35f + 0.65f * t));
                        }
                    }
                }
            }
            else if (shape == "BOX" || shape == "ARCH") {
                const float sx = dims.value("width", dims.value("x", 2.0f));
                const float sy = dims.value("depth", dims.value("y", 1.0f));
                const float sz = dims.value("height", dims.value("z", 2.0f));
                const float hx = sx * 0.5f;
                const float hy = sy * 0.5f;

                // Parametros del hueco central transitable cuando la primitiva es ARCH.
                const bool  isArch = (shape == "ARCH");
                const float openHalfW = dims.value("openingWidth", sx * 0.42f) * 0.5f;
                const float openHeight = dims.value("openingHeight", sz * 0.68f);

                for (float z = 0.0f; z <= sz; z += step) {
                    const bool isTop = (z >= sz - step * 0.9f);
                    for (float y = -hy; y <= hy; y += step) {
                        const bool isFaceYPos = (y >= hy - step * 0.9f);
                        const bool isFaceYNeg = (y <= -hy + step * 0.9f);
                        for (float x = -hx; x <= hx; x += step) {
                            // Recortamos el pasillo central libre en el arco.
                            if (isArch && std::abs(x) < openHalfW && z < openHeight) {
                                continue;
                            }

                            const bool isFaceXPos = (x >= hx - step * 0.9f);
                            const bool isFaceXNeg = (x <= -hx + step * 0.9f);

                            // Conservamos tambien las jambas internas y la parte inferior del dintel
                            // para que el grosor arquitectonico del portico se lea en 3D.
                            const bool isArchInnerSide = isArch && (z < openHeight) &&
                                (std::abs(std::abs(x) - openHalfW) <= step * 1.05f);
                            const bool isArchLintel = isArch && (std::abs(x) < openHalfW) &&
                                (z >= openHeight && z <= openHeight + step * 1.05f);

                            if (!isTop && !isFaceXPos && !isFaceXNeg && !isFaceYPos && !isFaceYNeg &&
                                !isArchInnerSide && !isArchLintel) {
                                continue; // Interior solido invisible descartado.
                            }

                            Vec3 p = { x, y, z };
                            if (!ApplyMaterialGridNoise(p, step, mat, partSeed)) continue;

                            Vec3 norm = { 0.0f, 0.0f, 1.0f };
                            char structural = '\0';

                            // Asignamos normales exactas por cara y glifos estructurales en remates y aristas.
                            if (isTop) {
                                norm = { 0.0f, 0.0f, 1.0f };
                                structural = '='; // Remate superior plano nitido.
                            }
                            else if ((isFaceXPos || isFaceXNeg) && (isFaceYPos || isFaceYNeg)) {
                                norm = { isFaceXPos ? 1.0f : -1.0f, isFaceYPos ? 1.0f : -1.0f, 0.1f };
                                structural = '|'; // Aristas verticales marcadas en las 4 esquinas.
                            }
                            else if (isArchInnerSide) norm = { (x > 0.0f) ? -1.0f : 1.0f, 0.0f, 0.0f };
                            else if (isArchLintel)    norm = { 0.0f, 0.0f, -1.0f };
                            else if (isFaceXPos)      norm = { 1.0f,  0.0f, 0.0f };
                            else if (isFaceXNeg)      norm = { -1.0f,  0.0f, 0.0f };
                            else if (isFaceYPos)      norm = { 0.0f,  1.0f, 0.0f };
                            else                      norm = { 0.0f, -1.0f, 0.0f };

                            float ao = 0.50f + 0.50f * (z / std::max(sz, 0.01f));
                            PushBakedAtom(prefab, { offset.x + p.x, offset.y + p.y, offset.z + p.z },
                                norm, ao, matId, windW, structural);
                        }
                    }
                }
            }
        }

        /**
         * @brief Parsea el bloque "collider" del JSON de un prefab.
         */
        ColliderDef ParseCollider(const json& root) {
            ColliderDef col;
            if (!root.contains("collider")) return col;

            const auto& c = root["collider"];
            const std::string typeStr = c.value("type", "NONE");

            if (typeStr == "CIRCLE") {
                col.type = ColliderType::CIRCLE;
                col.radius = c.value("radius", 0.5f);
            }
            else if (typeStr == "BOX") {
                col.type = ColliderType::BOX;
                col.halfSize = { c.value("width", 1.0f) * 0.5f, c.value("depth", 1.0f) * 0.5f };
            }
            else if (typeStr == "ARCH_PILLARS") {
                col.type = ColliderType::ARCH_PILLARS;
                col.halfSize = { c.value("width", 6.0f) * 0.5f, c.value("depth", 1.2f) * 0.5f };
                col.openingWidth = c.value("openingWidth", 2.6f);
            }
            return col;
        }

    } // namespace anonimo

    bool World::LoadObjectsDirectory(const std::string& objectsDir, const MaterialSystem& materials) {
        m_prefabs.clear();
        m_prefabNameToId.clear();

        if (!fs::exists(objectsDir) || !fs::is_directory(objectsDir)) {
            TraceLog(LOG_ERROR, "WORLD: No existe el directorio de objetos: %s", objectsDir.c_str());
            return false;
        }

        size_t totalBakedAtoms = 0;

        // Escaneamos dinamicamente cualquier .json en assets/data/objects/ sin listas hardcodeadas.
        for (const auto& entry : fs::directory_iterator(objectsDir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") {
                continue;
            }

            std::ifstream file(entry.path());
            if (!file.is_open()) continue;

            json root;
            try {
                root = json::parse(file, nullptr, true, true);
            }
            catch (const json::parse_error& e) {
                TraceLog(LOG_ERROR, "WORLD: Error parseando %s -> %s",
                    entry.path().string().c_str(), e.what());
                continue;
            }

            BakedPrefab prefab;
            prefab.id = root.value("id", entry.path().stem().string());
            prefab.name = root.value("name", prefab.id);
            prefab.collider = ParseCollider(root);

            if (root.contains("parts") && root["parts"].is_array()) {
                float seedCounter = 1.0f;
                for (const auto& part : root["parts"]) {
                    BakePrefabPart(prefab, part, materials, seedCounter * 3.17f);
                    seedCounter += 1.0f;
                }
            }

            // Liberamos capacidad sobrante del vector: la nube horneada es inmutable durante el juego.
            prefab.atoms.shrink_to_fit();
            totalBakedAtoms += prefab.atoms.size();

            PrefabId newId = static_cast<PrefabId>(m_prefabs.size());
            m_prefabNameToId[prefab.id] = newId;
            m_prefabs.push_back(std::move(prefab));
        }

        TraceLog(LOG_INFO, "WORLD: %zu prefabs horneados (%zu atomos compartidos en RAM, %.1f KB).",
            m_prefabs.size(), totalBakedAtoms, static_cast<float>(GetBakedMemoryBytes()) / 1024.0f);
        return !m_prefabs.empty();
    }

    uint32_t World::SpawnInstance(PrefabId prefabId, Vec3 pos, float rotDeg, Vec3 scale,
        SpatialHashGrid& spatialGrid, MaterialId overrideMat) {
        const BakedPrefab* prefab = GetPrefab(prefabId);
        if (!prefab) return UINT32_MAX;

        // Precalculamos cos/sin una sola vez para no repetir trigonometria por atomo cada frame.
        float rad = rotDeg * DEG_TO_RAD;
        MapInstance inst;
        inst.position = pos;
        inst.scale = scale;
        inst.rotationDeg = rotDeg;
        inst.cosRot = std::cos(rad);
        inst.sinRot = std::sin(rad);
        inst.prefabId = prefabId;
        inst.overrideMaterialId = overrideMat;

        uint32_t id = static_cast<uint32_t>(m_instances.size());
        m_instances.push_back(inst);

        // Indexamos la instancia en todos los chunks que intersecta su radio envolvente escalado.
        float maxScaleXY = std::max(scale.x, scale.y);
        spatialGrid.InsertInstance(id, pos.x, pos.y, prefab->boundRadiusXY * maxScaleXY);
        return id;
    }

    bool World::LoadMapFromFile(const std::string& mapPath, SpatialHashGrid& spatialGrid,
        const MaterialSystem* materials) {
        std::ifstream file(mapPath);
        if (!file.is_open()) {
            TraceLog(LOG_ERROR, "WORLD: No se pudo abrir el mapa %s", mapPath.c_str());
            return false;
        }

        json root;
        try {
            root = json::parse(file, nullptr, true, true);
        }
        catch (const json::parse_error& e) {
            TraceLog(LOG_ERROR, "WORLD: Error parseando mapa %s -> %s", mapPath.c_str(), e.what());
            return false;
        }

        // 1. Reiniciamos todo el estado del mapa previo a valores por defecto.
        m_instances.clear();
        m_mapLights.clear();
        m_waterZones.clear();
        m_surfaceZones.clear();
        m_entitySpawns.clear();
        m_dayNightConfig = DayNightConfig{};
        m_giConfig = GlobalIlluminationConfig{};
        m_windConfig = WorldWindConfig{};
        m_biomeId = "temperate_forest";
        m_initialWeatherId = "clear_day";
        m_randomWeather = true;
        m_viewPaddingPx = 140.0f;

        // 2. Configuracion global del mundo (limites, culling, ciclo dia-noche, GI, viento y clima).
        if (root.contains("worldConfig")) {
            const auto& wc = root["worldConfig"];
            m_worldHalfSize = wc.value("worldHalfSize", 140.0f);
            m_biomeId = wc.value("biomeId", "temperate_forest");

            if (wc.contains("culling")) {
                m_chunkSize = wc["culling"].value("chunkSize", 24.0f);
                m_viewPaddingPx = wc["culling"].value("viewPadding", 140.0f);
            }
            if (wc.contains("playerSpawn")) {
                const auto& ps = wc["playerSpawn"];
                m_playerSpawn = { ps.value("x", 0.0f), ps.value("y", 0.0f) };
            }
            if (wc.contains("dayNightCycle")) {
                const auto& dn = wc["dayNightCycle"];
                m_dayNightConfig.enabled = dn.value("enabled", true);
                m_dayNightConfig.currentTimeHours = dn.value("currentTimeHours", 10.5f);
                m_dayNightConfig.dayLengthSeconds = dn.value("dayLengthSeconds", 120.0f);
            }
            if (wc.contains("globalIllumination")) {
                const auto& gi = wc["globalIllumination"];
                m_giConfig.skyColor = ReadColor(gi, "skyColor", m_giConfig.skyColor);
                m_giConfig.groundColor = ReadColor(gi, "groundColor", m_giConfig.groundColor);
                m_giConfig.ambientStrength = gi.value("ambientStrength", 0.32f);
            }
            if (wc.contains("wind")) {
                const auto& w = wc["wind"];
                m_windConfig.directionDeg = w.value("directionDeg", 45.0f);
                m_windConfig.baseSpeed = w.value("baseSpeed", 2.2f);
                m_windConfig.gustStrength = w.value("gustStrength", 1.0f);
            }
            if (wc.contains("weather")) {
                const auto& wt = wc["weather"];
                m_initialWeatherId = wt.value("currentWeatherId", "clear_day");
                m_randomWeather = wt.value("randomTransitionsEnabled", true);
            }
        }

        // 3. Zonas de agua (estanques, rios).
        if (root.contains("waterZones") && root["waterZones"].is_array()) {
            for (const auto& wz : root["waterZones"]) {
                WaterZone zone;
                zone.minX = wz.value("minX", -5.0f);
                zone.maxX = wz.value("maxX", 5.0f);
                zone.minY = wz.value("minY", -5.0f);
                zone.maxY = wz.value("maxY", 5.0f);
                zone.deepColor = ReadColor(wz, "deepColor", zone.deepColor);
                zone.shallowColor = ReadColor(wz, "shallowColor", zone.shallowColor);
                zone.waveSpeed = wz.value("waveSpeed", 2.6f);
                m_waterZones.push_back(zone);
            }
        }

        // 4. Superficies transitables (puentes, pisos de casas, plataformas sci-fi).
        if (root.contains("surfaceZones") && root["surfaceZones"].is_array()) {
            for (const auto& sz : root["surfaceZones"]) {
                SurfaceZone zone;
                zone.id = sz.value("id", "surface");
                zone.minX = sz.value("minX", -2.0f);
                zone.maxX = sz.value("maxX", 2.0f);
                zone.minY = sz.value("minY", -2.0f);
                zone.maxY = sz.value("maxY", 2.0f);
                zone.elevationZ = sz.value("elevationZ", 0.05f);
                zone.primaryColor = ReadColor(sz, "primaryColor", zone.primaryColor);
                zone.secondaryColor = ReadColor(sz, "secondaryColor", zone.secondaryColor);
                zone.glyphs = sz.value("glyphs", "=#-");
                if (zone.glyphs.empty()) zone.glyphs = "=";
                zone.suppressFoliage = sz.value("suppressFoliage", true);
                zone.walkableOverWater = sz.value("walkableOverWater", true);

                std::string pat = sz.value("pattern", "GRID");
                if (pat == "PLANKS_X") zone.pattern = SurfacePattern::PLANKS_X;
                else if (pat == "PLANKS_Y") zone.pattern = SurfacePattern::PLANKS_Y;
                else if (pat == "CHECKER")  zone.pattern = SurfacePattern::CHECKER;
                else if (pat == "NOISE")    zone.pattern = SurfacePattern::NOISE;
                else                        zone.pattern = SurfacePattern::GRID;

                m_surfaceZones.push_back(std::move(zone));
            }
        }

        // 5. Fuentes de luz estaticas y parpadeantes del mapa.
        if (root.contains("lights") && root["lights"].is_array()) {
            float phaseCounter = 0.0f;
            for (const auto& l : root["lights"]) {
                PointLight pl;
                pl.position = ReadVec3(l, "pos", { 0.0f, 0.0f, 2.0f });
                pl.color = ReadColor(l, "color", { 255, 160, 75, 255 });
                pl.intensity = l.value("intensity", 1.2f);
                pl.radius = l.value("radius", 10.0f);
                pl.flicker = l.value("flicker", false);
                pl.flickerPhase = phaseCounter * 2.13f; // Desfase unico para que no parpadeen al unisono.
                phaseCounter += 1.0f;
                m_mapLights.push_back(pl);
            }
        }

        // 6. Personajes y criaturas definidas en la seccion "entities".
        if (root.contains("entities") && root["entities"].is_array()) {
            for (const auto& e : root["entities"]) {
                EntitySpawnDef def;
                def.characterId = e.value("characterId", "");
                if (def.characterId.empty()) continue;

                Vec3 p3 = ReadVec3(e, "pos", { m_playerSpawn.x, m_playerSpawn.y, 0.0f });
                def.position = { p3.x, p3.y };
                def.facingDeg = e.value("facingDeg", 45.0f);
                def.scale = e.value("scale", 1.0f);
                def.wanderRadius = e.value("wanderRadius", 8.0f);
                def.isPlayer = e.value("isPlayer", false);
                m_entitySpawns.push_back(std::move(def));
            }
        }

        // 7. Pre-reservamos m_instances y SpatialHashGrid para evitar realojamientos al poblar el mapa.
        size_t estimatedCount = root.contains("instances") ? root["instances"].size() : 0;
        if (root.contains("scatterGroups")) {
            for (const auto& sg : root["scatterGroups"]) {
                estimatedCount += sg.value("count", 0u);
            }
        }
        m_instances.reserve(estimatedCount);
        spatialGrid.Reset(m_chunkSize, estimatedCount);

        // 8. Instancias colocadas explicitamente en el JSON (soporta escala uniforme o vectorial y material override).
        if (root.contains("instances") && root["instances"].is_array()) {
            for (const auto& item : root["instances"]) {
                PrefabId pid = GetPrefabId(item.value("prefabId", ""));
                if (pid == INVALID_PREFAB_ID) continue;

                Vec3  pos = ReadVec3(item, "pos");
                float rot = item.value("rotDeg", 0.0f);
                Vec3  scale = { 1.0f, 1.0f, 1.0f };
                if (item.contains("scale")) {
                    if (item["scale"].is_number()) {
                        float s = item["scale"].get<float>();
                        scale = { s, s, s };
                    }
                    else {
                        scale = ReadVec3(item, "scale", { 1.0f, 1.0f, 1.0f });
                    }
                }

                MaterialId ovMat = FALLBACK_MATERIAL_ID;
                if (materials && item.contains("overrideMaterialId")) {
                    std::string ovName = item.value("overrideMaterialId", "");
                    if (!ovName.empty()) {
                        ovMat = materials->GetId(ovName);
                    }
                }
                SpawnInstance(pid, pos, rot, scale, spatialGrid, ovMat);
            }
        }

        // 9. Grupos de dispersion procedural determinista (ej. 500 pinos generados desde 1 bloque JSON).
        if (root.contains("scatterGroups") && root["scatterGroups"].is_array()) {
            for (const auto& sg : root["scatterGroups"]) {
                PrefabId pid = GetPrefabId(sg.value("prefabId", ""));
                if (pid == INVALID_PREFAB_ID) continue;

                const uint32_t count = sg.value("count", 0u);
                const float    minX = sg.value("minX", -110.0f);
                const float    maxX = sg.value("maxX", 110.0f);
                const float    minY = sg.value("minY", -110.0f);
                const float    maxY = sg.value("maxY", 110.0f);
                const float    clearRadius = sg.value("clearCenterRadius", 12.0f);
                const float    scaleMin = sg.value("scaleMin", 0.85f);
                const float    scaleMax = sg.value("scaleMax", 1.25f);
                const float    seed = sg.value("seed", 1.0f);

                for (uint32_t i = 0; i < count; ++i) {
                    float fi = static_cast<float>(i);
                    float hx = MathIso::Hash3D(fi, seed, 1.1f, seed * 7.3f);
                    float hy = MathIso::Hash3D(fi, seed, 2.3f, seed * 13.7f);
                    float hr = MathIso::Hash3D(fi, seed, 3.7f, seed * 5.9f);
                    float hs = MathIso::Hash3D(fi, seed, 5.1f, seed * 11.1f);

                    float wx = MathIso::Lerp(minX, maxX, hx);
                    float wy = MathIso::Lerp(minY, maxY, hy);

                    // Respetamos el claro central alrededor del spawn, los lagos y las superficies artificiales.
                    float dx = wx - m_playerSpawn.x;
                    float dy = wy - m_playerSpawn.y;
                    if ((dx * dx + dy * dy) < (clearRadius * clearRadius)) continue;
                    if (IsNearAnyWater(wx, wy, 1.5f)) continue;
                    if (FindSurfaceZoneAt(wx, wy)) continue;

                    float s = MathIso::Lerp(scaleMin, scaleMax, hs);
                    SpawnInstance(pid, { wx, wy, 0.0f }, hr * 360.0f, { s, s, s }, spatialGrid);
                }
            }
        }

        // 10. Estampamos la mascara 2D de ocupacion para que el follaje del suelo corra en O(1).
        RebuildGroundMask(0.5f);

        TraceLog(LOG_INFO, "WORLD: Mapa '%s' cargado con %zu instancias en %u chunks.",
            mapPath.c_str(), m_instances.size(), spatialGrid.GetTotalChunks());
        return true;
    }

    bool World::SaveMapToFile(const std::string& mapPath,
        const MaterialSystem& materials,
        const DayNightConfig& dayNight,
        const GlobalIlluminationConfig& gi,
        const std::vector<PointLight>& lights,
        const WorldWindConfig& wind,
        const std::string& biomeId,
        const std::string& weatherId,
        bool randomWeather,
        float viewPaddingPx,
        const std::vector<EntitySpawnDef>& entities) const {
        json root;

        // 1. Configuracion global del mundo.
        root["worldConfig"] = {
            { "worldHalfSize", m_worldHalfSize },
            { "biomeId", biomeId },
            { "playerSpawn",{ { "x", m_playerSpawn.x },{ "y", m_playerSpawn.y } } },
            { "culling",{ { "chunkSize", m_chunkSize },{ "viewPadding", viewPaddingPx } } },
            { "dayNightCycle",{
                { "enabled", dayNight.enabled },
                { "currentTimeHours", dayNight.currentTimeHours },
                { "dayLengthSeconds", dayNight.dayLengthSeconds }
            } },
            { "globalIllumination",{
                { "skyColor", ColorToHex(gi.skyColor) },
                { "groundColor", ColorToHex(gi.groundColor) },
                { "ambientStrength", gi.ambientStrength }
            } },
            { "wind",{
                { "directionDeg", wind.directionDeg },
                { "baseSpeed", wind.baseSpeed },
                { "gustStrength", wind.gustStrength }
            } },
            { "weather",{
                { "currentWeatherId", weatherId },
                { "randomTransitionsEnabled", randomWeather }
            } }
        };

        // 2. Zonas de agua.
        json waterArr = json::array();
        for (const WaterZone& wz : m_waterZones) {
            waterArr.push_back({
                { "minX", wz.minX },{ "maxX", wz.maxX },
                { "minY", wz.minY },{ "maxY", wz.maxY },
                { "deepColor", ColorToHex(wz.deepColor) },
                { "shallowColor", ColorToHex(wz.shallowColor) },
                { "waveSpeed", wz.waveSpeed }
                });
        }
        root["waterZones"] = waterArr;

        // 3. Superficies transitables.
        json surfArr = json::array();
        for (const SurfaceZone& sz : m_surfaceZones) {
            const char* patStr = "GRID";
            if (sz.pattern == SurfacePattern::PLANKS_X) patStr = "PLANKS_X";
            else if (sz.pattern == SurfacePattern::PLANKS_Y) patStr = "PLANKS_Y";
            else if (sz.pattern == SurfacePattern::CHECKER)  patStr = "CHECKER";
            else if (sz.pattern == SurfacePattern::NOISE)    patStr = "NOISE";

            surfArr.push_back({
                { "id", sz.id },
                { "minX", sz.minX },{ "maxX", sz.maxX },
                { "minY", sz.minY },{ "maxY", sz.maxY },
                { "elevationZ", sz.elevationZ },
                { "primaryColor", ColorToHex(sz.primaryColor) },
                { "secondaryColor", ColorToHex(sz.secondaryColor) },
                { "glyphs", sz.glyphs },
                { "pattern", patStr },
                { "suppressFoliage", sz.suppressFoliage },
                { "walkableOverWater", sz.walkableOverWater }
                });
        }
        root["surfaceZones"] = surfArr;

        // 4. Luces puntuales.
        json lightArr = json::array();
        for (const PointLight& pl : lights) {
            lightArr.push_back({
                { "pos",{ { "x", pl.position.x },{ "y", pl.position.y },{ "z", pl.position.z } } },
                { "color", ColorToHex(pl.color) },
                { "intensity", pl.intensity },
                { "radius", pl.radius },
                { "flicker", pl.flicker }
                });
        }
        root["lights"] = lightArr;

        // 5. Entidades / personajes colocados.
        json entArr = json::array();
        for (const EntitySpawnDef& e : entities) {
            entArr.push_back({
                { "characterId", e.characterId },
                { "pos",{ { "x", e.position.x },{ "y", e.position.y },{ "z", 0.0f } } },
                { "facingDeg", e.facingDeg },
                { "isPlayer", e.isPlayer },
                { "wanderRadius", e.wanderRadius },
                { "scale", e.scale }
                });
        }
        root["entities"] = entArr;

        // 6. Todas las instancias explicitas (preserva cada edicion exacta hecha con el Editor Visual).
        json instArr = json::array();
        for (const MapInstance& inst : m_instances) {
            const BakedPrefab* p = GetPrefab(inst.prefabId);
            if (!p) continue;
            json entry = {
                { "prefabId", p->id },
                { "pos",{ { "x", inst.position.x },{ "y", inst.position.y },{ "z", inst.position.z } } },
                { "rotDeg", inst.rotationDeg },
                { "scale",{ { "x", inst.scale.x },{ "y", inst.scale.y },{ "z", inst.scale.z } } }
            };
            if (inst.overrideMaterialId != FALLBACK_MATERIAL_ID) {
                entry["overrideMaterialId"] = materials.Get(inst.overrideMaterialId).name;
            }
            instArr.push_back(std::move(entry));
        }
        root["instances"] = instArr;

        // Escribimos con sangria de 2 espacios para que el archivo siga siendo facil de editar a mano.
        std::ofstream out(mapPath);
        if (!out.is_open()) {
            TraceLog(LOG_ERROR, "WORLD: No se pudo escribir el mapa JSON en %s", mapPath.c_str());
            return false;
        }
        out << root.dump(2);
        TraceLog(LOG_INFO, "WORLD: Mapa guardado en %s (%zu instancias, %zu luces, %zu entidades).",
            mapPath.c_str(), m_instances.size(), lights.size(), entities.size());
        return true;
    }

    void World::RebuildSpatialGrid(SpatialHashGrid& spatialGrid) const {
        // Reindexamos todas las instancias desde cero cuando el Editor borra, inserta o cambia chunkSize.
        spatialGrid.Reset(m_chunkSize, m_instances.size());
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_instances.size()); ++i) {
            const MapInstance& inst = m_instances[i];
            const BakedPrefab* prefab = GetPrefab(inst.prefabId);
            if (!prefab) continue;
            float maxScaleXY = std::max(inst.scale.x, inst.scale.y);
            spatialGrid.InsertInstance(i, inst.position.x, inst.position.y, prefab->boundRadiusXY * maxScaleXY);
        }
    }

    bool World::RemoveInstance(size_t index, SpatialHashGrid& spatialGrid) {
        if (index >= m_instances.size()) return false;
        m_instances.erase(m_instances.begin() + static_cast<ptrdiff_t>(index));
        // Como borrar desplaza los indices posteriores, reindexamos SpatialHashGrid para mantener coherencia.
        RebuildSpatialGrid(spatialGrid);
        return true;
    }

    bool World::InsertInstanceAt(size_t index, const MapInstance& inst, SpatialHashGrid& spatialGrid) {
        if (index > m_instances.size()) index = m_instances.size();
        m_instances.insert(m_instances.begin() + static_cast<ptrdiff_t>(index), inst);
        RebuildSpatialGrid(spatialGrid);
        return true;
    }

    bool World::UpdateInstanceTransform(size_t index, Vec3 pos, float rotDeg, Vec3 scale,
        MaterialId overrideMat, SpatialHashGrid& spatialGrid) {
        if (index >= m_instances.size()) return false;
        MapInstance& inst = m_instances[index];
        float rad = rotDeg * DEG_TO_RAD;
        inst.position = pos;
        inst.rotationDeg = rotDeg;
        inst.cosRot = std::cos(rad);
        inst.sinRot = std::sin(rad);
        inst.scale = scale;
        inst.overrideMaterialId = overrideMat;
        RebuildSpatialGrid(spatialGrid);
        return true;
    }

    PrefabId World::GetPrefabId(std::string_view id) const {
        auto it = m_prefabNameToId.find(std::string(id));
        return (it != m_prefabNameToId.end()) ? it->second : INVALID_PREFAB_ID;
    }

    const BakedPrefab* World::GetPrefab(PrefabId id) const {
        if (id >= m_prefabs.size()) return nullptr;
        return &m_prefabs[id];
    }

    size_t World::GetBakedMemoryBytes() const {
        size_t total = 0;
        for (const BakedPrefab& p : m_prefabs) {
            total += p.atoms.capacity() * sizeof(BakedAtom);
        }
        return total;
    }

    const WaterZone* World::FindWaterZoneAt(float worldX, float worldY) const {
        for (const WaterZone& wz : m_waterZones) {
            if (wz.Contains(worldX, worldY)) return &wz;
        }
        return nullptr;
    }

    const SurfaceZone* World::FindSurfaceZoneAt(float worldX, float worldY) const {
        for (const SurfaceZone& sz : m_surfaceZones) {
            if (sz.Contains(worldX, worldY)) return &sz;
        }
        return nullptr;
    }

    bool World::IsNearAnyWater(float worldX, float worldY, float margin) const {
        for (const WaterZone& wz : m_waterZones) {
            if (worldX >= wz.minX - margin && worldX <= wz.maxX + margin &&
                worldY >= wz.minY - margin && worldY <= wz.maxY + margin) {
                return true;
            }
        }
        return false;
    }

    void World::StampRectMask(float minX, float maxX, float minY, float maxY, uint8_t flag) {
        // Convertimos el rectangulo del mundo a indices discretos acotados dentro de m_groundMask.
        int gx0 = std::max(0, static_cast<int>(std::floor((minX - m_maskOriginX) * m_maskInvCellSize)));
        int gx1 = std::min(m_maskWidth - 1, static_cast<int>(std::floor((maxX - m_maskOriginX) * m_maskInvCellSize)));
        int gy0 = std::max(0, static_cast<int>(std::floor((minY - m_maskOriginY) * m_maskInvCellSize)));
        int gy1 = std::min(m_maskHeight - 1, static_cast<int>(std::floor((maxY - m_maskOriginY) * m_maskInvCellSize)));

        for (int gy = gy0; gy <= gy1; ++gy) {
            size_t rowOff = static_cast<size_t>(gy) * static_cast<size_t>(m_maskWidth);
            for (int gx = gx0; gx <= gx1; ++gx) {
                m_groundMask[rowOff + static_cast<size_t>(gx)] |= flag;
            }
        }
    }

    void World::StampCircleMask(float cx, float cy, float radius, uint8_t flag) {
        int gx0 = std::max(0, static_cast<int>(std::floor((cx - radius - m_maskOriginX) * m_maskInvCellSize)));
        int gx1 = std::min(m_maskWidth - 1, static_cast<int>(std::floor((cx + radius - m_maskOriginX) * m_maskInvCellSize)));
        int gy0 = std::max(0, static_cast<int>(std::floor((cy - radius - m_maskOriginY) * m_maskInvCellSize)));
        int gy1 = std::min(m_maskHeight - 1, static_cast<int>(std::floor((cy + radius - m_maskOriginY) * m_maskInvCellSize)));

        const float rSq = radius * radius;
        for (int gy = gy0; gy <= gy1; ++gy) {
            float wy = m_maskOriginY + (static_cast<float>(gy) + 0.5f) * m_maskCellSize;
            float dy = wy - cy;
            size_t rowOff = static_cast<size_t>(gy) * static_cast<size_t>(m_maskWidth);
            for (int gx = gx0; gx <= gx1; ++gx) {
                float wx = m_maskOriginX + (static_cast<float>(gx) + 0.5f) * m_maskCellSize;
                float dx = wx - cx;
                // Evitamos sqrt comparando distancias al cuadrado.
                if ((dx * dx + dy * dy) <= rSq) {
                    m_groundMask[rowOff + static_cast<size_t>(gx)] |= flag;
                }
            }
        }
    }

    void World::StampOBBMask(float cx, float cy, Vec2 halfSize, float cosRot, float sinRot, uint8_t flag) {
        // Calculamos el radio de la circunferencia circunscrita del OBB para acotar las celdas candidatas.
        float boundR = std::sqrt(halfSize.x * halfSize.x + halfSize.y * halfSize.y) + m_maskCellSize;
        int gx0 = std::max(0, static_cast<int>(std::floor((cx - boundR - m_maskOriginX) * m_maskInvCellSize)));
        int gx1 = std::min(m_maskWidth - 1, static_cast<int>(std::floor((cx + boundR - m_maskOriginX) * m_maskInvCellSize)));
        int gy0 = std::max(0, static_cast<int>(std::floor((cy - boundR - m_maskOriginY) * m_maskInvCellSize)));
        int gy1 = std::min(m_maskHeight - 1, static_cast<int>(std::floor((cy + boundR - m_maskOriginY) * m_maskInvCellSize)));

        for (int gy = gy0; gy <= gy1; ++gy) {
            float wy = m_maskOriginY + (static_cast<float>(gy) + 0.5f) * m_maskCellSize;
            float ry = wy - cy;
            size_t rowOff = static_cast<size_t>(gy) * static_cast<size_t>(m_maskWidth);
            for (int gx = gx0; gx <= gx1; ++gx) {
                float wx = m_maskOriginX + (static_cast<float>(gx) + 0.5f) * m_maskCellSize;
                float rx = wx - cx;
                // Rotacion inversa (-theta) al espacio local del rectangulo para comprobar |lx| <= hx y |ly| <= hy.
                float lx = rx * cosRot + ry * sinRot;
                float ly = -rx * sinRot + ry * cosRot;
                if (std::abs(lx) <= halfSize.x && std::abs(ly) <= halfSize.y) {
                    m_groundMask[rowOff + static_cast<size_t>(gx)] |= flag;
                }
            }
        }
    }

    void World::RebuildGroundMask(float cellSizeWorld) {
        m_maskCellSize = std::max(0.25f, cellSizeWorld);
        m_maskInvCellSize = 1.0f / m_maskCellSize;

        const float halfSize = m_worldHalfSize + 4.0f;
        m_maskOriginX = -halfSize;
        m_maskOriginY = -halfSize;
        m_maskWidth = static_cast<int>(std::ceil((halfSize * 2.0f) * m_maskInvCellSize));
        m_maskHeight = m_maskWidth;

        // 1 byte por celda: para un mundo de 280x280u a paso 0.5u -> 560x560 = ~313 KB contiguos.
        m_groundMask.assign(static_cast<size_t>(m_maskWidth) * static_cast<size_t>(m_maskHeight), GroundMaskFlags::CLEAR);

        // 1. Estampamos huellas de objetos solidos (troncos, rocas, muros y pilares de arcos).
        for (const MapInstance& inst : m_instances) {
            const BakedPrefab* prefab = GetPrefab(inst.prefabId);
            if (!prefab) continue;

            const ColliderDef& col = prefab->collider;
            if (col.type == ColliderType::CIRCLE) {
                float r = col.radius * ((inst.scale.x + inst.scale.y) * 0.5f) + 0.45f;
                StampCircleMask(inst.position.x, inst.position.y, r, GroundMaskFlags::OBJECT_FOOTPRINT);
            }
            else if (col.type == ColliderType::BOX) {
                Vec2 hs = { col.halfSize.x * inst.scale.x + 0.35f, col.halfSize.y * inst.scale.y + 0.35f };
                StampOBBMask(inst.position.x, inst.position.y, hs, inst.cosRot, inst.sinRot, GroundMaskFlags::OBJECT_FOOTPRINT);
            }
            else if (col.type == ColliderType::ARCH_PILLARS) {
                // Estampamos ambos pilares laterales respetando el hueco central transitable.
                float totalW = (col.halfSize.x * 2.0f) * inst.scale.x;
                float openW = col.openingWidth * inst.scale.x;
                float pillarW = std::max(0.2f, (totalW - openW) * 0.5f);
                float offsetDist = (openW * 0.5f) + (pillarW * 0.5f);
                Vec2  pillarHalf = { pillarW * 0.5f + 0.3f, col.halfSize.y * inst.scale.y + 0.3f };

                StampOBBMask(inst.position.x - inst.cosRot * offsetDist, inst.position.y - inst.sinRot * offsetDist,
                    pillarHalf, inst.cosRot, inst.sinRot, GroundMaskFlags::OBJECT_FOOTPRINT);
                StampOBBMask(inst.position.x + inst.cosRot * offsetDist, inst.position.y + inst.sinRot * offsetDist,
                    pillarHalf, inst.cosRot, inst.sinRot, GroundMaskFlags::OBJECT_FOOTPRINT);
            }
            else {
                // Objeto decorativo sin colision: huella conservadora en su base.
                float r = prefab->boundRadiusXY * std::max(inst.scale.x, inst.scale.y) * 0.45f;
                StampCircleMask(inst.position.x, inst.position.y, r, GroundMaskFlags::OBJECT_FOOTPRINT);
            }
        }

        // 2. Estampamos lagos y rios con un pequeno margen costero.
        for (const WaterZone& wz : m_waterZones) {
            StampRectMask(wz.minX - 0.35f, wz.maxX + 0.35f, wz.minY - 0.35f, wz.maxY + 0.35f, GroundMaskFlags::WATER);
        }

        // 3. Estampamos puentes, patios y pisos que suprimen vegetacion natural debajo.
        for (const SurfaceZone& sz : m_surfaceZones) {
            if (sz.suppressFoliage) {
                StampRectMask(sz.minX - 0.2f, sz.maxX + 0.2f, sz.minY - 0.2f, sz.maxY + 0.2f, GroundMaskFlags::SURFACE_OVERRIDE);
            }
        }
    }

    bool World::CanSpawnGroundFoliageAt(float worldX, float worldY) const {
        if (m_groundMask.empty()) return true;
        int gx = static_cast<int>(std::floor((worldX - m_maskOriginX) * m_maskInvCellSize));
        int gy = static_cast<int>(std::floor((worldY - m_maskOriginY) * m_maskInvCellSize));
        if (gx < 0 || gx >= m_maskWidth || gy < 0 || gy >= m_maskHeight) {
            return false;
        }
        return m_groundMask[static_cast<size_t>(gy) * static_cast<size_t>(m_maskWidth) + static_cast<size_t>(gx)] == GroundMaskFlags::CLEAR;
    }

} // namespace AsciiEngine