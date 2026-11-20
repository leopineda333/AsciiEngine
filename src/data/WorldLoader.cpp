/**
 * @file WorldLoader.cpp
 * @brief Cargador data-driven de objetos JSON, Point-Cloud Baker (Flyweight) y cargador de mapas.
 */
#include <AsciiEngine/World.hpp>
#include <AsciiEngine/MathIso.hpp>
#include <AsciiEngine/MaterialSystem.hpp>
#include <AsciiEngine/CullingSystem.hpp>
#include "json.hpp"
#include "raylib.h" // Solo para TraceLog en diagnosticos.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace AsciiEngine {

    using json = nlohmann::json;
    namespace fs = std::filesystem;

    namespace {

        constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;

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

        uint8_t HexNibble(char c) {
            if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(10 + (c - 'a'));
            if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(10 + (c - 'A'));
            return 0;
        }

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

        // Aplica jitter y densidad si el material usa GridMode::NOISY.
        // Retorna false si el punto es descartado por porosidad.
        bool ApplyMaterialGridNoise(Vec3& p, float step, const Material& mat, float seed) {
            if (mat.gridMode != GridMode::NOISY) {
                return true;
            }
            if (MathIso::Hash3D(p.x, p.y, p.z, seed + 11.7f) > mat.density) {
                return false;
            }
            float jAmp = mat.jitter * step * 2.0f;
            p.x += (MathIso::Hash3D(p.x, p.y, p.z, seed + 1.3f) - 0.5f) * jAmp;
            p.y += (MathIso::Hash3D(p.x, p.y, p.z, seed + 2.7f) - 0.5f) * jAmp;
            p.z += (MathIso::Hash3D(p.x, p.y, p.z, seed + 5.1f) - 0.5f) * jAmp;
            return true;
        }

        // Agrega un atomo horneado y actualiza los radios envolventes del prefab.
        void PushBakedAtom(BakedPrefab& prefab, Vec3 localPos, Vec3 localNormal, float ao,
            MaterialId matId, float windWeight, char overrideChar = '\0') {
            BakedAtom atom;
            atom.localPos = localPos;
            atom.localNormal = MathIso::Normalize(localNormal);
            atom.ao = MathIso::Clamp(ao, 0.2f, 1.0f);
            atom.materialId = matId;
            atom.windWeightByte = static_cast<uint8_t>(MathIso::Clamp(windWeight, 0.0f, 1.0f) * 255.0f);
            atom.structuralOverride = overrideChar;

            float xyDist = std::sqrt(localPos.x * localPos.x + localPos.y * localPos.y);
            prefab.boundRadiusXY = std::max(prefab.boundRadiusXY, xyDist + 0.35f);
            prefab.boundHeightZ = std::max(prefab.boundHeightZ, localPos.z + 0.35f);

            prefab.atoms.push_back(atom);
        }

        // Hornea una parte geometrica individual del JSON dentro de la nube contigua del prefab.
        void BakePrefabPart(BakedPrefab& prefab, const json& part, const MaterialSystem& materials, float partSeed) {
            const std::string shape = part.value("shape", "BOX");
            const std::string matName = part.value("materialId", "");
            const MaterialId  matId = materials.GetId(matName);
            const Material& mat = materials.Get(matId);

            const Vec3  offset = ReadVec3(part, "offset");
            const float step = MathIso::Clamp(part.value("step", 0.25f), 0.10f, 2.0f);
            const float windW = part.value("windWeight", mat.windWeight);

            const json dims = part.contains("dimensions") ? part["dimensions"] : json::object();

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
                            // Conservamos solo la cascara exterior para evitar atomos internos invisibles.
                            if (m > 1.0f || m < 0.62f) continue;

                            Vec3 norm = MathIso::EllipsoidNormal(p, radii);
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
                            if (rSq > rSqMax) continue;
                            if (!isTop && rSq < rSqMin) continue;

                            Vec3 norm = isTop ? Vec3{ 0.0f, 0.0f, 1.0f } : Vec3{ p.x, p.y, 0.15f };
                            float ao = 0.45f + 0.55f * (z / std::max(h, 0.01f));
                            PushBakedAtom(prefab, { offset.x + p.x, offset.y + p.y, offset.z + p.z },
                                norm, ao, matId, windW * (z / std::max(h, 0.01f)));
                        }
                    }
                }
            }
            else if (shape == "CONE") {
                const float baseR = dims.value("radius", 1.8f);
                const float h = dims.value("height", 4.0f);
                const float slopeZ = baseR / std::max(h, 0.01f);

                for (float z = 0.0f; z <= h; z += step) {
                    const float t = z / std::max(h, 0.01f);
                    const float curR = baseR * (1.0f - t);
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

                            // Las ramas altas reciben mas luz ambiental y mas movimiento de viento.
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

                const bool  isArch = (shape == "ARCH");
                const float openHalfW = dims.value("openingWidth", sx * 0.42f) * 0.5f;
                const float openHeight = dims.value("openingHeight", sz * 0.68f);

                for (float z = 0.0f; z <= sz; z += step) {
                    const bool isTop = (z >= sz - step * 0.9f);
                    for (float y = -hy; y <= hy; y += step) {
                        const bool isFaceYPos = (y >= hy - step * 0.9f);
                        const bool isFaceYNeg = (y <= -hy + step * 0.9f);
                        for (float x = -hx; x <= hx; x += step) {
                            // Recortamos el hueco transitable central si la primitiva es un arco.
                            if (isArch && std::abs(x) < openHalfW && z < openHeight) {
                                continue;
                            }

                            const bool isFaceXPos = (x >= hx - step * 0.9f);
                            const bool isFaceXNeg = (x <= -hx + step * 0.9f);

                            // Detectamos caras interiores del marco de la puerta para que el arco tenga grosor real.
                            const bool isArchInnerSide = isArch && (z < openHeight) &&
                                (std::abs(std::abs(x) - openHalfW) <= step * 1.05f);
                            const bool isArchLintel = isArch && (std::abs(x) < openHalfW) &&
                                (z >= openHeight && z <= openHeight + step * 1.05f);

                            if (!isTop && !isFaceXPos && !isFaceXNeg && !isFaceYPos && !isFaceYNeg &&
                                !isArchInnerSide && !isArchLintel) {
                                continue; // Interior solido invisible: se descarta en el horneado.
                            }

                            Vec3 p = { x, y, z };
                            if (!ApplyMaterialGridNoise(p, step, mat, partSeed)) continue;

                            Vec3 norm = { 0.0f, 0.0f, 1.0f };
                            char structural = '\0';

                            if (isTop) {
                                norm = { 0.0f, 0.0f, 1.0f };
                                structural = '=';
                            }
                            else if ((isFaceXPos || isFaceXNeg) && (isFaceYPos || isFaceYNeg)) {
                                norm = { isFaceXPos ? 1.0f : -1.0f, isFaceYPos ? 1.0f : -1.0f, 0.1f };
                                structural = '|'; // Aristas verticales nitidas.
                            }
                            else if (isArchInnerSide) {
                                norm = { (x > 0.0f) ? -1.0f : 1.0f, 0.0f, 0.0f };
                            }
                            else if (isArchLintel) {
                                norm = { 0.0f, 0.0f, -1.0f };
                            }
                            else if (isFaceXPos) norm = { 1.0f,  0.0f, 0.0f };
                            else if (isFaceXNeg) norm = { -1.0f,  0.0f, 0.0f };
                            else if (isFaceYPos) norm = { 0.0f,  1.0f, 0.0f };
                            else                 norm = { 0.0f, -1.0f, 0.0f };

                            float ao = 0.50f + 0.50f * (z / std::max(sz, 0.01f));
                            PushBakedAtom(prefab, { offset.x + p.x, offset.y + p.y, offset.z + p.z },
                                norm, ao, matId, windW, structural);
                        }
                    }
                }
            }
        }

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

        // Registramos la instancia en todos los chunks que toca su radio maximo XY.
        float maxScaleXY = std::max(scale.x, scale.y);
        spatialGrid.InsertInstance(id, pos.x, pos.y, prefab->boundRadiusXY * maxScaleXY);
        return id;
    }

    bool World::LoadMapFromFile(const std::string& mapPath, SpatialHashGrid& spatialGrid) {

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

        m_instances.clear();
        m_mapLights.clear();
        m_dayNightConfig = DayNightConfig{};
        m_giConfig = GlobalIlluminationConfig{};

        if (root.contains("worldConfig")) {
            const auto& wc = root["worldConfig"];
            m_worldHalfSize = wc.value("worldHalfSize", 140.0f);
            if (wc.contains("culling")) {
                m_chunkSize = wc["culling"].value("chunkSize", 24.0f);
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
        }

        // Lectura de fuentes de luz estaticas / parpadeantes del mapa.
        if (root.contains("lights") && root["lights"].is_array()) {
            float phaseCounter = 0.0f;
            for (const auto& l : root["lights"]) {
                PointLight pl;
                pl.position = ReadVec3(l, "pos", { 0.0f, 0.0f, 2.0f });
                pl.color = ReadColor(l, "color", { 255, 160, 75, 255 });
                pl.intensity = l.value("intensity", 1.2f);
                pl.radius = l.value("radius", 10.0f);
                pl.flicker = l.value("flicker", false);
                pl.flickerPhase = phaseCounter * 2.13f;
                phaseCounter += 1.0f;
                m_mapLights.push_back(pl);
            }
        }

        // Estimamos cantidad total para pre-reservar el SpatialHashGrid sin realojamientos.
        size_t estimatedCount = root.contains("instances") ? root["instances"].size() : 0;
        if (root.contains("scatterGroups")) {
            for (const auto& sg : root["scatterGroups"]) {
                estimatedCount += sg.value("count", 0u);
            }
        }
        m_instances.reserve(estimatedCount);
        spatialGrid.Reset(m_chunkSize, estimatedCount);

        // 1. Instancias colocadas explicitamente en el JSON.
        if (root.contains("instances") && root["instances"].is_array()) {
            for (const auto& item : root["instances"]) {
                PrefabId pid = GetPrefabId(item.value("prefabId", ""));
                if (pid == INVALID_PREFAB_ID) continue;

                Vec3 pos = ReadVec3(item, "pos");
                float rot = item.value("rotDeg", 0.0f);
                Vec3 scale = { 1.0f, 1.0f, 1.0f };
                if (item.contains("scale")) {
                    if (item["scale"].is_number()) {
                        float s = item["scale"].get<float>();
                        scale = { s, s, s };
                    }
                    else {
                        scale = ReadVec3(item, "scale", { 1.0f, 1.0f, 1.0f });
                    }
                }
                SpawnInstance(pid, pos, rot, scale, spatialGrid);
            }
        }

        // 2. Grupos de dispersion procedural determinista (ej. 500 arboles en 1 bloque JSON).
        if (root.contains("scatterGroups") && root["scatterGroups"].is_array()) {
            for (const auto& sg : root["scatterGroups"]) {
                PrefabId pid = GetPrefabId(sg.value("prefabId", ""));
                if (pid == INVALID_PREFAB_ID) continue;

                const uint32_t count = sg.value("count", 0u);
                const float minX = sg.value("minX", -110.0f);
                const float maxX = sg.value("maxX", 110.0f);
                const float minY = sg.value("minY", -110.0f);
                const float maxY = sg.value("maxY", 110.0f);
                const float clearRadius = sg.value("clearCenterRadius", 12.0f);
                const float scaleMin = sg.value("scaleMin", 0.85f);
                const float scaleMax = sg.value("scaleMax", 1.25f);
                const float seed = sg.value("seed", 1.0f);

                for (uint32_t i = 0; i < count; ++i) {
                    float fi = static_cast<float>(i);
                    float hx = MathIso::Hash3D(fi, seed, 1.1f, seed * 7.3f);
                    float hy = MathIso::Hash3D(fi, seed, 2.3f, seed * 13.7f);
                    float hr = MathIso::Hash3D(fi, seed, 3.7f, seed * 5.9f);
                    float hs = MathIso::Hash3D(fi, seed, 5.1f, seed * 11.1f);

                    float wx = MathIso::Lerp(minX, maxX, hx);
                    float wy = MathIso::Lerp(minY, maxY, hy);

                    // Dejamos libre el claro central alrededor del spawn y los arcos de prueba.
                    float dx = wx - m_playerSpawn.x;
                    float dy = wy - m_playerSpawn.y;
                    if ((dx * dx + dy * dy) < (clearRadius * clearRadius)) {
                        continue;
                    }

                    float s = MathIso::Lerp(scaleMin, scaleMax, hs);
                    SpawnInstance(pid, { wx, wy, 0.0f }, hr * 360.0f, { s, s, s }, spatialGrid);
                }
            }
        }

        TraceLog(LOG_INFO, "WORLD: Mapa '%s' cargado con %zu instancias en %u chunks.",
            mapPath.c_str(), m_instances.size(), spatialGrid.GetTotalChunks());
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

} // namespace AsciiEngine