// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file CharacterSystem.cpp
 * @brief Implementacion comentada linea por linea del horneado de personajes, ordenamiento topologico,
 *        cinematica directa 3x3 y animacion procedural.
 */
#include <AsciiEngine/CharacterSystem.hpp>
#include <AsciiEngine/MaterialSystem.hpp>
#include <AsciiEngine/LightingSystem.hpp>
#include <AsciiEngine/AsciiZBuffer.hpp>
#include <AsciiEngine/CullingSystem.hpp>
#include "json.hpp" // Dependencia privada: nunca se expone en headers publicos.
#include "raylib.h" // Solo para TraceLog() en diagnosticos de carga.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace AsciiEngine {

    using json = nlohmann::json;
    namespace fs = std::filesystem;

    namespace {

        constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;

        /**
         * @brief Lee un Vec3 flexible desde JSON (acepta arreglo [x, y, z] u objeto {"x", "y", "z"}).
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
         * @brief Traduce el texto de eje en JSON ("X", "Y", "Z") a AnimAxis.
         */
        AnimAxis ParseAxis(std::string_view s) {
            if (s == "X" || s == "x") return AnimAxis::X;
            if (s == "Y" || s == "y") return AnimAxis::Y;
            if (s == "Z" || s == "z") return AnimAxis::Z;
            return AnimAxis::NONE;
        }

        const char* AnimStateToStr(AnimState st) {
            switch (st) {
            case AnimState::IDLE:   return "IDLE";
            case AnimState::WALK:   return "WALK";
            case AnimState::RUN:    return "RUN";
            case AnimState::ATTACK: return "ATTACK";
            default:                return "IDLE";
            }
        }

        // =========================================================================
        // ALGEBRA DE MATRICES DE ROTACION 3x3 (ROW-MAJOR) PARA JERARQUIAS ARTICULADAS
        // =========================================================================
        // Layout en memoria:
        //   [ m[0] m[1] m[2] ]   <- Fila 0 (proyeccion sobre eje X)
        //   [ m[3] m[4] m[5] ]   <- Fila 1 (proyeccion sobre eje Y)
        //   [ m[6] m[7] m[8] ]   <- Fila 2 (proyeccion sobre eje Z)

        void SetIdentity(float m[9]) {
            m[0] = 1.0f; m[1] = 0.0f; m[2] = 0.0f;
            m[3] = 0.0f; m[4] = 1.0f; m[5] = 0.0f;
            m[6] = 0.0f; m[7] = 0.0f; m[8] = 1.0f;
        }

        /**
         * @brief Construye una matriz de rotacion elemental alrededor del eje X, Y o Z (regla de mano derecha).
         */
        void MakeAxisRotation(AnimAxis axis, float rad, float out[9]) {
            SetIdentity(out);
            if (axis == AnimAxis::NONE || std::abs(rad) < 1e-5f) return;

            float c = std::cos(rad);
            float s = std::sin(rad);
            if (axis == AnimAxis::X) {
                // Rotacion en el plano YZ (X permanece fijo).
                out[4] = c; out[5] = -s;
                out[7] = s; out[8] = c;
            }
            else if (axis == AnimAxis::Y) {
                // Rotacion en el plano XZ (Y permanece fijo -> balanceo de piernas/brazos adelante-atras).
                out[0] = c; out[2] = s;
                out[6] = -s; out[8] = c;
            }
            else if (axis == AnimAxis::Z) {
                // Rotacion en el plano XY (Z permanece fijo -> giro yaw del personaje o helices).
                out[0] = c; out[1] = -s;
                out[3] = s; out[4] = c;
            }
        }

        /**
         * @brief Compone dos rotaciones: out = a * b (aplica primero 'b' local y luego 'a' del padre).
         */
        void MulMat3(const float a[9], const float b[9], float out[9]) {
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    out[r * 3 + c] = a[r * 3 + 0] * b[0 * 3 + c]
                        + a[r * 3 + 1] * b[1 * 3 + c]
                        + a[r * 3 + 2] * b[2 * 3 + c];
                }
            }
        }

        /**
         * @brief Transforma un vector 3D (posicion local o normal) por una matriz de rotacion 3x3.
         * @details Como toda matriz de rotacion es ortogonal (det = +1), sirve directamente para rotar
         *          normales sin necesidad de calcular matriz inversa-transpuesta.
         */
        inline Vec3 MulMat3Vec(const float m[9], Vec3 v) {
            return {
                m[0] * v.x + m[1] * v.y + m[2] * v.z,
                m[3] * v.x + m[4] * v.y + m[5] * v.z,
                m[6] * v.x + m[7] * v.y + m[8] * v.z
            };
        }

        /**
         * @brief Aplica porosidad y perturbacion organica si el material de la seccion es GridMode::NOISY (ej. pelaje).
         */
        bool ApplySectionNoise(Vec3& p, float step, const Material& mat, float seed) {
            if (mat.gridMode != GridMode::NOISY) return true;
            if (MathIso::Hash3D(p.x, p.y, p.z, seed + 11.7f) > mat.density) return false;

            float jAmp = mat.jitter * step * 1.5f;
            p.x += (MathIso::Hash3D(p.x, p.y, p.z, seed + 1.3f) - 0.5f) * jAmp;
            p.y += (MathIso::Hash3D(p.x, p.y, p.z, seed + 2.7f) - 0.5f) * jAmp;
            p.z += (MathIso::Hash3D(p.x, p.y, p.z, seed + 5.1f) - 0.5f) * jAmp;
            return true;
        }

        /**
         * @brief Hornea la cascara exterior SDF de una seccion del personaje desplazada por su pivotOffset.
         *
         * @details Diferencia crucial entre localOffset y pivotOffset:
         *   - localOffset: donde se ubica la ARTICULACION (ej. cadera u hombro) respecto al padre.
         *   - pivotOffset: donde se ubica el CENTRO GEOMETRICO de la pieza respecto a esa articulacion.
         *     Ejemplo: una pierna de altura 0.88u con pivotOffset.z = -0.44u cuelga hacia abajo
         *     desde la cadera (0,0,0), logrando que rote como un pendulo desde arriba en vez de por la mitad.
         */
        void BakeSectionGeometry(BakedCharacterSection& sec, const json& secJson,
            const MaterialSystem& materials, float seed) {
            const std::string shape = secJson.value("shape", "BOX");
            const Vec3 size = ReadVec3(secJson, "size", { 0.5f, 0.5f, 0.5f });
            const Vec3 pivotOff = ReadVec3(secJson, "pivotOffset", { 0.0f, 0.0f, 0.0f });
            const float step = MathIso::Clamp(secJson.value("cellScale", 0.14f), 0.08f, 0.45f);
            const Material& mat = materials.Get(sec.materialId);

            auto pushAtom = [&](Vec3 p, Vec3 n, float ao) {
                BakedAtom a;
                // Aplicamos pivotOffset al hornear: ahorra una suma por atomo en cada frame de Render().
                a.localPos = { p.x + pivotOff.x, p.y + pivotOff.y, p.z + pivotOff.z };
                a.localNormal = MathIso::Normalize(n);
                a.ao = MathIso::Clamp(ao, 0.30f, 1.0f);
                a.materialId = sec.materialId;
                sec.atoms.push_back(a);
                };

            // Semi-dimensiones acotadas al menos a medio paso para que piezas delgadas nunca salgan vacias.
            const float hx = std::max(size.x * 0.5f, step * 0.5f);
            const float hy = std::max(size.y * 0.5f, step * 0.5f);
            const float hz = std::max(size.z * 0.5f, step * 0.5f);

            if (shape == "ELLIPSOID") {
                const Vec3 radii = { hx, hy, hz };
                for (float z = -hz; z <= hz; z += step) {
                    for (float y = -hy; y <= hy; y += step) {
                        for (float x = -hx; x <= hx; x += step) {
                            Vec3 p = { x, y, z };
                            if (!ApplySectionNoise(p, step, mat, seed)) continue;
                            float m = MathIso::EllipsoidMetric(p, radii);
                            // Cascara hueca [0.42 .. 1.0]: descarta atomos internos invisibles.
                            if (m > 1.0f || m < 0.42f) continue;

                            Vec3 norm = MathIso::EllipsoidNormal(p, radii);
                            float ao = 0.50f + 0.50f * norm.z;
                            pushAtom(p, norm, ao);
                        }
                    }
                }
            }
            else if (shape == "CYLINDER") {
                const float rx = hx;
                const float ry = hy;
                for (float z = -hz; z <= hz; z += step) {
                    const bool isCap = (std::abs(z) >= hz - step * 0.9f);
                    for (float y = -ry; y <= ry; y += step) {
                        for (float x = -rx; x <= rx; x += step) {
                            Vec3 p = { x, y, z };
                            if (!ApplySectionNoise(p, step, mat, seed)) continue;
                            float nx = p.x / rx;
                            float ny = p.y / ry;
                            float rSq = nx * nx + ny * ny;
                            if (rSq > 1.0f) continue;
                            if (!isCap && rSq < 0.35f) continue;

                            Vec3 norm = isCap ? Vec3{ 0.0f, 0.0f, (z >= 0.0f) ? 1.0f : -1.0f }
                            : Vec3{ p.x / (rx * rx), p.y / (ry * ry), 0.12f };
                            float ao = 0.52f + 0.48f * ((z + hz) / std::max(size.z, 0.01f));
                            pushAtom(p, norm, ao);
                        }
                    }
                }
            }
            else if (shape == "CONE") {
                for (float z = -hz; z <= hz; z += step) {
                    float t = (z + hz) / std::max(size.z, 0.01f); // 0 en la base -> 1 en el vertice.
                    float curRx = std::max(0.03f, hx * (1.0f - t));
                    float curRy = std::max(0.03f, hy * (1.0f - t));
                    for (float y = -curRy; y <= curRy; y += step) {
                        for (float x = -curRx; x <= curRx; x += step) {
                            Vec3 p = { x, y, z };
                            if (!ApplySectionNoise(p, step, mat, seed)) continue;
                            float rSq = (p.x / curRx) * (p.x / curRx) + (p.y / curRy) * (p.y / curRy);
                            if (rSq > 1.0f) continue;
                            if (t > 0.12f && t < 0.92f && rSq < 0.35f) continue;

                            Vec3 norm = { p.x / curRx, p.y / curRy, 0.55f };
                            pushAtom(p, norm, 0.50f + 0.50f * t);
                        }
                    }
                }
            }
            else { // BOX (por defecto)
                for (float z = -hz; z <= hz; z += step) {
                    const bool isZFace = (std::abs(z) >= hz - step * 0.95f);
                    for (float y = -hy; y <= hy; y += step) {
                        const bool isYFace = (std::abs(y) >= hy - step * 0.95f);
                        for (float x = -hx; x <= hx; x += step) {
                            const bool isXFace = (std::abs(x) >= hx - step * 0.95f);
                            // Solo conservamos las 6 caras externas de la caja.
                            if (!isXFace && !isYFace && !isZFace) continue;

                            Vec3 p = { x, y, z };
                            if (!ApplySectionNoise(p, step, mat, seed)) continue;

                            Vec3 norm = { 0.0f, 0.0f, 0.0f };
                            if (isXFace) norm.x = (x >= 0.0f) ? 1.0f : -1.0f;
                            if (isYFace) norm.y = (y >= 0.0f) ? 1.0f : -1.0f;
                            if (isZFace) norm.z = (z >= 0.0f) ? 1.0f : -1.0f;

                            float ao = 0.52f + 0.48f * ((z + hz) / std::max(size.z, 0.01f));
                            pushAtom(p, norm, ao);
                        }
                    }
                }
            }

            // Salvaguarda para micro-piezas (ej. ojos, sensores): garantiza al menos 1 atomo visible.
            if (sec.atoms.empty()) {
                pushAtom({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, 1.0f);
            }
            sec.atoms.shrink_to_fit();
        }

        /**
         * @brief Parsea una pista de animacion procedural desde el bloque "animTracks" de una seccion.
         */
        AnimTrack ParseAnimTrack(const json& tJson) {
            AnimTrack tr;
            tr.active = true;
            // Soportamos tanto notacion corta ("axis"/"amplitude") como traslacion y rotacion combinadas.
            tr.posAxis = ParseAxis(tJson.value("posAxis", tJson.value("axis", "")));
            tr.posAmplitude = tJson.value("posAmplitude", tJson.value("amplitude", 0.0f));
            tr.rotAxis = ParseAxis(tJson.value("rotAxis", ""));
            tr.rotAmpRad = tJson.value("rotAmpDeg", 0.0f) * DEG_TO_RAD;
            tr.frequency = tJson.value("frequency", 4.0f);
            tr.phaseOffset = tJson.value("phaseOffset", 0.0f);
            tr.spinContinuous = tJson.value("spinContinuous", false);
            return tr;
        }

    } // namespace anonimo

    CharacterSystem::CharacterSystem() {
        // Pre-reservamos 64 ranuras en el scratch de poses: suficiente para criaturas complejas
        // con cero llamadas a malloc/new durante el bucle de renderizado.
        m_poseScratch.reserve(64);
    }

    bool CharacterSystem::LoadTemplatesDirectory(const std::string& charactersDir, const MaterialSystem& materials) {
        m_templates.clear();
        m_templateMap.clear();

        if (!fs::exists(charactersDir) || !fs::is_directory(charactersDir)) {
            TraceLog(LOG_WARNING, "CHARACTERS: No existe directorio: %s", charactersDir.c_str());
            return false;
        }

        for (const auto& entry : fs::directory_iterator(charactersDir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;

            std::ifstream file(entry.path());
            if (!file.is_open()) continue;

            try {
                json root = json::parse(file, nullptr, true, true);
                CharacterTemplate tpl;
                tpl.id = root.value("id", entry.path().stem().string());
                tpl.name = root.value("name", tpl.id);
                tpl.collisionRadius = root.value("collisionRadius", 0.65f);
                tpl.walkSpeed = root.value("walkSpeed", 13.0f);
                tpl.runSpeed = root.value("runSpeed", 22.0f);
                tpl.baseElevationZ = root.value("baseElevationZ", 0.0f);

                if (!root.contains("sections") || !root["sections"].is_array()) continue;

                // 1. Leemos y horneamos todas las secciones en un arreglo temporal, conservando parentId.
                struct RawSection {
                    BakedCharacterSection baked;
                    std::string           parentId;
                    bool                  placed = false;
                };
                std::vector<RawSection> rawList;
                rawList.reserve(root["sections"].size());

                float seedCounter = 1.0f;
                for (const auto& secJson : root["sections"]) {
                    RawSection rs;
                    rs.baked.id = secJson.value("id", "part");
                    rs.parentId = secJson.value("parentId", "");
                    rs.baked.materialId = materials.GetId(secJson.value("materialId", ""));
                    rs.baked.localOffset = ReadVec3(secJson, "localOffset");

                    if (secJson.contains("animTracks") && secJson["animTracks"].is_object()) {
                        const auto& tr = secJson["animTracks"];
                        if (tr.contains("IDLE"))   rs.baked.tracks[static_cast<size_t>(AnimState::IDLE)] = ParseAnimTrack(tr["IDLE"]);
                        if (tr.contains("WALK"))   rs.baked.tracks[static_cast<size_t>(AnimState::WALK)] = ParseAnimTrack(tr["WALK"]);
                        if (tr.contains("RUN"))    rs.baked.tracks[static_cast<size_t>(AnimState::RUN)] = ParseAnimTrack(tr["RUN"]);
                        if (tr.contains("ATTACK")) rs.baked.tracks[static_cast<size_t>(AnimState::ATTACK)] = ParseAnimTrack(tr["ATTACK"]);
                    }

                    BakeSectionGeometry(rs.baked, secJson, materials, seedCounter * 4.13f);
                    seedCounter += 1.0f;
                    rawList.push_back(std::move(rs));
                }

                // 2. Ordenamiento topologico (variante simple de Kahn):
                //    Colocamos primero las raices (parentId vacio) y en pasadas sucesivas los hijos
                //    cuyo padre ya quedo ubicado en placedMap. Esto permite escribir las secciones en
                //    CUALQUIER orden en el JSON y garantiza que al recorrer tpl.sections[0..N-1],
                //    el padre siempre haya sido evaluado antes que cualquiera de sus hijos.
                std::unordered_map<std::string, int16_t> placedMap;
                bool progress = true;
                while (tpl.sections.size() < rawList.size() && progress) {
                    progress = false;
                    for (RawSection& rs : rawList) {
                        if (rs.placed) continue;

                        int16_t pIdx = -1;
                        if (!rs.parentId.empty()) {
                            auto it = placedMap.find(rs.parentId);
                            if (it == placedMap.end()) continue; // El padre aun no entra; esperar siguiente vuelta.
                            pIdx = it->second;
                        }

                        rs.baked.parentIdx = pIdx;
                        int16_t ownIdx = static_cast<int16_t>(tpl.sections.size());
                        placedMap[rs.baked.id] = ownIdx;
                        tpl.sections.push_back(std::move(rs.baked));
                        rs.placed = true;
                        progress = true;
                    }
                }

                // Respaldo defensivo: si un JSON tiene un parentId inexistente o ciclo, adjuntamos a raiz sin crashear.
                for (RawSection& rs : rawList) {
                    if (!rs.placed) {
                        TraceLog(LOG_WARNING, "CHARACTERS: Seccion '%s' en '%s' tiene parentId invalido '%s'.",
                            rs.baked.id.c_str(), tpl.id.c_str(), rs.parentId.c_str());
                        rs.baked.parentIdx = -1;
                        tpl.sections.push_back(std::move(rs.baked));
                    }
                }

                // 3. Calculamos radios envolventes (XY y altura Z) en pose de reposo para Frustum Culling 2D.
                std::vector<Vec3> restPos(tpl.sections.size(), { 0.0f, 0.0f, 0.0f });
                for (size_t i = 0; i < tpl.sections.size(); ++i) {
                    const auto& s = tpl.sections[i];
                    restPos[i] = (s.parentIdx >= 0)
                        ? Vec3{ restPos[s.parentIdx].x + s.localOffset.x,
                                restPos[s.parentIdx].y + s.localOffset.y,
                                restPos[s.parentIdx].z + s.localOffset.z }
                    : s.localOffset;

                    for (const BakedAtom& a : s.atoms) {
                        float ax = restPos[i].x + a.localPos.x;
                        float ay = restPos[i].y + a.localPos.y;
                        float az = restPos[i].z + a.localPos.z + tpl.baseElevationZ;
                        tpl.boundRadiusXY = std::max(tpl.boundRadiusXY, std::sqrt(ax * ax + ay * ay) + 0.45f);
                        tpl.boundHeightZ = std::max(tpl.boundHeightZ, az + 0.45f);
                    }
                }

                CharacterTemplateId newId = static_cast<CharacterTemplateId>(m_templates.size());
                m_templateMap[tpl.id] = newId;
                m_templates.push_back(std::move(tpl));
            }
            catch (const json::parse_error& e) {
                TraceLog(LOG_ERROR, "CHARACTERS: Error en %s -> %s", entry.path().string().c_str(), e.what());
            }
        }

        TraceLog(LOG_INFO, "CHARACTERS: %zu plantillas de personajes horneadas (%.1f KB en RAM).",
            m_templates.size(), static_cast<float>(GetBakedMemoryBytes()) / 1024.0f);
        return !m_templates.empty();
    }

    void CharacterSystem::ClearInstances() {
        m_instances.clear();
        m_controlledIdx = 0;
    }

    CharacterTemplateId CharacterSystem::FindTemplateId(std::string_view id) const {
        auto it = m_templateMap.find(std::string(id));
        return (it != m_templateMap.end()) ? it->second : INVALID_CHARACTER_ID;
    }

    bool CharacterSystem::SpawnCharacter(std::string_view templateId, Vec2 worldPos, float facingDeg,
        bool isPlayer, float wanderRadius, float scale) {
        CharacterTemplateId tid = FindTemplateId(templateId);
        if (tid == INVALID_CHARACTER_ID) {
            TraceLog(LOG_WARNING, "CHARACTERS: Plantilla desconocida '%.*s'.",
                static_cast<int>(templateId.size()), templateId.data());
            return false;
        }

        const CharacterTemplate& tpl = m_templates[tid];
        CharacterInstance inst;
        inst.templateId = tid;
        inst.actor.position = worldPos;
        inst.actor.collisionRadius = tpl.collisionRadius * scale;
        inst.actor.moveSpeed = tpl.walkSpeed;
        inst.actor.facingAngleRad = facingDeg * DEG_TO_RAD;
        inst.scale = scale;
        inst.isPlayer = isPlayer;
        inst.homePos = worldPos;
        inst.wanderTarget = worldPos;
        inst.wanderRadius = wanderRadius;
        inst.aiWaitTimer = 0.8f;

        // Solo un personaje puede tener isPlayer == true a la vez.
        if (isPlayer) {
            for (auto& existing : m_instances) existing.isPlayer = false;
            m_controlledIdx = m_instances.size();
        }

        m_instances.push_back(inst);
        return true;
    }

    void CharacterSystem::TriggerPlayerAttack() {
        if (m_controlledIdx < m_instances.size()) {
            // Reiniciamos animTime = 0 para que la curva de golpe comience siempre desde el inicio.
            m_instances[m_controlledIdx].attackTimer = 0.48f;
            m_instances[m_controlledIdx].animTime = 0.0f;
        }
    }

    void CharacterSystem::CycleControlledCharacter() {
        if (m_instances.size() <= 1) return;

        // El personaje anterior pasa a NPC patrullando alrededor del lugar donde lo dejamos.
        m_instances[m_controlledIdx].isPlayer = false;
        m_instances[m_controlledIdx].homePos = m_instances[m_controlledIdx].actor.position;

        m_controlledIdx = (m_controlledIdx + 1) % m_instances.size();
        m_instances[m_controlledIdx].isPlayer = true;
    }

    void CharacterSystem::Update(float dt, Vec2 playerDesiredWorldDir, bool playerSprint, float groundFriction,
        const World& world, SpatialHashGrid& spatialGrid,
        std::vector<uint32_t>& scratchIds, std::vector<ChunkCoord>& scratchChunks) {
        const float safeDt = std::min(dt, 0.05f);

        for (size_t idx = 0; idx < m_instances.size(); ++idx) {
            CharacterInstance& inst = m_instances[idx];
            const CharacterTemplate& tpl = m_templates[inst.templateId];

            Vec2 desiredDir = { 0.0f, 0.0f };
            bool isRunning = false;

            if (inst.isPlayer) {
                // --- PERSONAJE CONTROLADO POR EL JUGADOR ---
                desiredDir = playerDesiredWorldDir;
                isRunning = playerSprint;
                inst.actor.moveSpeed = isRunning ? tpl.runSpeed : tpl.walkSpeed;
            }
            else if (inst.wanderRadius > 0.5f) {
                // --- IA SIMPLE DE DEAMBULACION PARA NPCs ---
                float dx = inst.wanderTarget.x - inst.actor.position.x;
                float dy = inst.wanderTarget.y - inst.actor.position.y;
                float distSq = dx * dx + dy * dy;

                if (distSq < 0.8f) {
                    // Llego a su destino: espera un tiempo aleatorio antes de elegir un nuevo punto.
                    inst.aiWaitTimer -= safeDt;
                    if (inst.aiWaitTimer <= 0.0f) {
                        std::uniform_real_distribution<float> angleDist(0.0f, 6.2831853f);
                        std::uniform_real_distribution<float> radDist(2.0f, inst.wanderRadius);
                        std::uniform_real_distribution<float> waitDist(1.4f, 3.8f);

                        float a = angleDist(m_rng);
                        float r = radDist(m_rng);
                        float candX = inst.homePos.x + std::cos(a) * r;
                        float candY = inst.homePos.y + std::sin(a) * r;

                        // Criaturas voladoras (baseElevationZ > 0.3) pueden sobrevolar agua; las terrestres la evitan.
                        if (tpl.baseElevationZ > 0.3f || !world.FindWaterZoneAt(candX, candY)) {
                            inst.wanderTarget = { candX, candY };
                        }
                        inst.aiWaitTimer = waitDist(m_rng);
                    }
                }
                else {
                    // Camina pausadamente (55% de walkSpeed) hacia su objetivo.
                    desiredDir = { dx, dy };
                    inst.actor.moveSpeed = tpl.walkSpeed * 0.55f;
                }
            }

            // Las entidades voladoras conservan friccion aerea constante; las terrestres heredan friccion de nieve/suelo.
            inst.actor.friction = (tpl.baseElevationZ > 0.3f) ? 6.0f : groundFriction;

            // 1. Integracion fisica semi-implicita y resolucion de colisiones contra el escenario.
            CollisionSolver::UpdateActorPhysics(
                inst.actor, desiredDir, safeDt, world, spatialGrid, scratchIds, scratchChunks
            );

            // 2. Maquina de estados de animacion con prioridad: ATTACK > RUN > WALK > IDLE.
            float speedSq = inst.actor.velocity.x * inst.actor.velocity.x + inst.actor.velocity.y * inst.actor.velocity.y;
            if (inst.attackTimer > 0.0f) {
                inst.attackTimer -= safeDt;
                inst.animState = AnimState::ATTACK;
            }
            else if (speedSq > (tpl.walkSpeed * tpl.walkSpeed * 1.15f) || (isRunning && speedSq > 1.0f)) {
                inst.animState = AnimState::RUN;
            }
            else if (speedSq > 0.35f) {
                inst.animState = AnimState::WALK;
            }
            else {
                inst.animState = AnimState::IDLE;
            }

            // 3. Reloj de fase de animacion (acelerado 1.35x al correr para sincronizar cadencia de zancadas).
            float animSpeedMul = (inst.animState == AnimState::RUN) ? 1.35f : 1.0f;
            inst.animTime += safeDt * animSpeedMul;
            if (inst.animTime > 628.3185f) inst.animTime -= 628.3185f;
        }
    }

    void CharacterSystem::SubmitToZBuffer(AsciiZBuffer& zBuffer,
        const LightingSystem& lighting,
        const MaterialSystem& materials,
        const World& world,
        const MathIso::IsoConfig& isoConfig,
        const Rect2D& viewportRect,
        float zoom, float camTopLeftX, float camTopLeftY,
        float screenW, float screenH,
        int splatRadius, bool allowReflections, float windTime,
        uint32_t& atomsEvaluated, uint32_t& atomsCulled) const {
        for (const CharacterInstance& inst : m_instances) {
            if (inst.templateId >= m_templates.size()) continue;
            const CharacterTemplate& tpl = m_templates[inst.templateId];

            // 1. Frustum Culling 2D (Capa 2): descartamos personajes fuera de pantalla antes de evaluar huesos.
            Vec3  rootWorldPos = { inst.actor.position.x, inst.actor.position.y, tpl.baseElevationZ };
            Vec2  baseIso = MathIso::WorldToIso(rootWorldPos, isoConfig);
            float baseScrX = baseIso.x * zoom - camTopLeftX;
            float baseScrY = baseIso.y * zoom - camTopLeftY;

            float  radX = tpl.boundRadiusXY * inst.scale * isoConfig.tileWidthHalf * zoom;
            float  hgt = tpl.boundHeightZ * inst.scale * isoConfig.scaleZ * zoom;
            Rect2D charRect = { baseScrX - radX, baseScrY - hgt, radX * 2.0f, hgt + radX };
            if (!viewportRect.Intersects(charRect)) continue;

            // Matriz raiz del personaje orientada segun su angulo de marcha en el plano XY (eje Z).
            float rootRot[9];
            MakeAxisRotation(AnimAxis::Z, inst.actor.facingAngleRad, rootRot);

            // =========================================================================
            // 2. CINEMATICA DIRECTA (FORWARD KINEMATICS) LINEAL EN O(S) SIN RECURSION
            // =========================================================================
            // Gracias al ordenamiento topologico hecho al cargar, m_poseScratch[sec.parentIdx]
            // ya contiene la posicion y rotacion mundial exacta del padre cuando evaluamos al hijo.
            const size_t secCount = tpl.sections.size();
            m_poseScratch.resize(secCount);

            for (size_t s = 0; s < secCount; ++s) {
                const BakedCharacterSection& sec = tpl.sections[s];

                // Cadena de respaldo de pistas: estado actual -> WALK (si estamos en RUN) -> IDLE.
                const AnimTrack* tr = &sec.tracks[static_cast<size_t>(inst.animState)];
                if (!tr->active && inst.animState == AnimState::RUN) {
                    tr = &sec.tracks[static_cast<size_t>(AnimState::WALK)];
                }
                if (!tr->active) {
                    tr = &sec.tracks[static_cast<size_t>(AnimState::IDLE)];
                }

                // Desplazamiento y rotacion local respecto al pivote del padre.
                Vec3 localPos = {
                    sec.localOffset.x * inst.scale,
                    sec.localOffset.y * inst.scale,
                    sec.localOffset.z * inst.scale
                };
                float localRot[9];
                SetIdentity(localRot);

                if (tr->active) {
                    float phase = inst.animTime * tr->frequency + tr->phaseOffset;
                    float wave = std::sin(phase);

                    // Traslacion armonica local (ej. respiracion del torso o levitacion del dron).
                    if (tr->posAxis != AnimAxis::NONE && std::abs(tr->posAmplitude) > 1e-5f) {
                        float delta = wave * tr->posAmplitude * inst.scale;
                        if (tr->posAxis == AnimAxis::X) localPos.x += delta;
                        else if (tr->posAxis == AnimAxis::Y) localPos.y += delta;
                        else if (tr->posAxis == AnimAxis::Z) localPos.z += delta;
                    }

                    // Rotacion armonica pendular o giro continuo (ej. piernas, cola, helices).
                    if (tr->rotAxis != AnimAxis::NONE) {
                        float angle = tr->spinContinuous ? phase : (wave * tr->rotAmpRad);
                        MakeAxisRotation(tr->rotAxis, angle, localRot);
                    }
                }

                // Componemos la transformacion jerarquica:
                //   WorldPos_hijo = WorldPos_padre + (WorldRot_padre * localPos_hijo)
                //   WorldRot_hijo = WorldRot_padre * localRot_hijo
                SectionWorldPose& pose = m_poseScratch[s];
                if (sec.parentIdx >= 0) {
                    const SectionWorldPose& parent = m_poseScratch[static_cast<size_t>(sec.parentIdx)];
                    Vec3 rotatedOff = MulMat3Vec(parent.rot, localPos);
                    pose.position = {
                        parent.position.x + rotatedOff.x,
                        parent.position.y + rotatedOff.y,
                        parent.position.z + rotatedOff.z
                    };
                    MulMat3(parent.rot, localRot, pose.rot);
                }
                else {
                    Vec3 rotatedOff = MulMat3Vec(rootRot, localPos);
                    pose.position = {
                        rootWorldPos.x + rotatedOff.x,
                        rootWorldPos.y + rotatedOff.y,
                        rootWorldPos.z + rotatedOff.z
                    };
                    MulMat3(rootRot, localRot, pose.rot);
                }
            }

            // =========================================================================
            // 3. PROYECCION, SOMBREADO Y REFLEJO EN AGUA DE CADA SECCION
            // =========================================================================
            const bool nearWater = allowReflections &&
                world.IsNearAnyWater(inst.actor.position.x, inst.actor.position.y, tpl.boundHeightZ * 2.0f + 2.0f);
            size_t refCounter = 0;

            for (size_t s = 0; s < secCount; ++s) {
                const BakedCharacterSection& sec = tpl.sections[s];
                const SectionWorldPose& pose = m_poseScratch[s];
                const Material& mat = materials.Get(sec.materialId);

                const BakedAtom* atoms = sec.atoms.data();
                const size_t     atomCount = sec.atoms.size();

                for (size_t i = 0; i < atomCount; ++i) {
                    const BakedAtom& a = atoms[i];
                    ++atomsEvaluated;

                    // Transformamos el atomo desde el espacio de la seccion al espacio mundial 3D.
                    Vec3 scaledLocal = { a.localPos.x * inst.scale, a.localPos.y * inst.scale, a.localPos.z * inst.scale };
                    Vec3 rotOffset = MulMat3Vec(pose.rot, scaledLocal);
                    Vec3 worldPt = {
                        pose.position.x + rotOffset.x,
                        pose.position.y + rotOffset.y,
                        pose.position.z + rotOffset.z
                    };

                    Vec2  isoPt = MathIso::WorldToIso(worldPt, isoConfig);
                    float zoomedX = isoPt.x * zoom;
                    float zoomedY = isoPt.y * zoom;

                    // Pre-check rapido de Capa 3 (16 px de holgura para no recortar contornos en bordes).
                    float scrX = zoomedX - camTopLeftX;
                    float scrY = zoomedY - camTopLeftY;
                    if (scrX < -16.0f || scrX >= screenW + 16.0f || scrY < -16.0f || scrY >= screenH + 16.0f) {
                        ++atomsCulled;
                        continue;
                    }

                    // Rotamos la normal por pose.rot para que cada extremidad reaccione dinamicamente al Sol/antorcha.
                    Vec3       worldNorm = MathIso::Normalize(MulMat3Vec(pose.rot, a.localNormal));
                    ShadedAtom shaded = lighting.ShadeAtom(worldPt, worldNorm, a.ao, materials, sec.materialId, true);

                    float depth = MathIso::ComputeDepthKey(worldPt);
                    if (!zBuffer.SubmitAtom(zoomedX, zoomedY, depth, shaded.glyph, shaded.color,
                        sec.materialId, mat.outline, splatRadius, false)) {
                        ++atomsCulled;
                    }

                    // Reflejo espejado en agua (submuestreo 50%: uno de cada dos atomos).
                    if (nearWater && worldPt.z > 0.12f && ((refCounter++ & 1) == 0)) {
                        // Invertimos Z (* -0.80f) para proyectar la silueta de cabeza dentro del lago y aplicamos onda.
                        Vec2 refIso = MathIso::WorldToIso({ worldPt.x, worldPt.y, -worldPt.z * 0.80f }, isoConfig);
                        refIso.x += std::sin(refIso.y * 0.22f + windTime * 3.8f + worldPt.x * 0.6f) * 2.4f;

                        Vec2 refGround = MathIso::IsoToWorldGround(refIso, isoConfig);
                        // Si hay un puente transitable sobre el agua en ese punto, el puente oculta el reflejo.
                        const SurfaceZone* sz = world.FindSurfaceZoneAt(refGround.x, refGround.y);
                        if (!sz || !sz->walkableOverWater) {
                            if (const WaterZone* wz = world.FindWaterZoneAt(refGround.x, refGround.y)) {
                                ColorRGBA refCol = MathIso::LerpColor(shaded.color, wz->deepColor, 0.58f);
                                refCol.r = static_cast<uint8_t>(refCol.r * 0.82f);
                                refCol.g = static_cast<uint8_t>(refCol.g * 0.85f);
                                refCol.b = static_cast<uint8_t>(refCol.b * 0.92f);

                                // Depth -0.10f: gana sobre la superficie del agua (-0.15f) pero queda detras de objetos (>=0).
                                float depthRef = MathIso::ComputeDepthKey({ refGround.x, refGround.y, 0.0f }) - 0.10f;
                                zBuffer.SubmitAtom(refIso.x * zoom, refIso.y * zoom, depthRef,
                                    shaded.glyph, refCol, FALLBACK_MATERIAL_ID, false, 0, true);
                            }
                        }
                    }
                }
            }
        }
    }

    const CharacterInstance* CharacterSystem::GetControlledCharacter() const {
        if (m_controlledIdx < m_instances.size()) {
            return &m_instances[m_controlledIdx];
        }
        return nullptr;
    }

    std::string CharacterSystem::GetControlledName() const {
        const CharacterInstance* c = GetControlledCharacter();
        if (!c || c->templateId >= m_templates.size()) return "None";
        return m_templates[c->templateId].name;
    }

    const char* CharacterSystem::GetControlledAnimStateName() const {
        const CharacterInstance* c = GetControlledCharacter();
        return c ? AnimStateToStr(c->animState) : "IDLE";
    }

    size_t CharacterSystem::GetBakedMemoryBytes() const {
        size_t total = 0;
        for (const CharacterTemplate& tpl : m_templates) {
            for (const BakedCharacterSection& sec : tpl.sections) {
                total += sec.atoms.capacity() * sizeof(BakedAtom);
            }
        }
        return total;
    }

    bool CharacterSystem::RemoveCharacter(size_t index) {
        if (index >= m_instances.size()) return false;
        m_instances.erase(m_instances.begin() + static_cast<ptrdiff_t>(index));

        // Mantenemos siempre un indice de control valido si aun quedan personajes en el mapa.
        if (m_instances.empty()) {
            m_controlledIdx = 0;
        }
        else {
            if (m_controlledIdx >= m_instances.size()) {
                m_controlledIdx = m_instances.size() - 1;
            }
            m_instances[m_controlledIdx].isPlayer = true;
        }
        return true;
    }

    std::vector<EntitySpawnDef> CharacterSystem::ExportEntitySpawns() const {
        // Convierte las instancias vivas actuales al formato serializable del bloque "entities" en JSON.
        std::vector<EntitySpawnDef> out;
        out.reserve(m_instances.size());

        for (const CharacterInstance& inst : m_instances) {
            if (inst.templateId >= m_templates.size()) continue;
            EntitySpawnDef def;
            def.characterId = m_templates[inst.templateId].id;
            def.position = inst.actor.position;
            def.facingDeg = inst.actor.facingAngleRad / DEG_TO_RAD;
            def.scale = inst.scale;
            def.wanderRadius = inst.wanderRadius;
            def.isPlayer = inst.isPlayer;
            out.push_back(std::move(def));
        }
        return out;
    }

} // namespace AsciiEngine