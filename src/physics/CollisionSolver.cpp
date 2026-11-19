/**
 * @file CollisionSolver.cpp
 * @brief Integracion fisica semi-implicita y resolucion de colisiones 2D (Circulo, OBB y Arcos).
 *
 * @complexity O(K) donde K es el numero de instancias en los chunks adyacentes al actor.
 * Cero asignaciones dinamicas en memoria por frame.
 */
#include <AsciiEngine/World.hpp>
#include <AsciiEngine/MathIso.hpp>
#include <AsciiEngine/CullingSystem.hpp>

#include <algorithm>
#include <cmath>

namespace AsciiEngine::CollisionSolver {

    namespace {

        // Elimina la componente de velocidad que apunta hacia dentro del obstaculo
        // para lograr deslizamiento tangencial suave sin rebotes artificiales.
        void RemoveInwardVelocity(Vec2& velocity, Vec2 pushNormal) {
            float vn = velocity.x * pushNormal.x + velocity.y * pushNormal.y;
            if (vn < 0.0f) {
                velocity.x -= pushNormal.x * vn;
                velocity.y -= pushNormal.y * vn;
            }
        }

        // Resolucion exacta Circulo vs Circulo (troncos, rocas).
        void ResolveCircleVsCircle(SimpleActor& actor, Vec2 center, float obsRadius) {
            float dx = actor.position.x - center.x;
            float dy = actor.position.y - center.y;
            float minDist = actor.collisionRadius + obsRadius;
            float distSq = dx * dx + dy * dy;

            if (distSq < minDist * minDist) {
                float dist = std::sqrt(distSq);
                Vec2 normal = (dist > 1e-5f) ? Vec2{ dx / dist, dy / dist } : Vec2{ 1.0f, 0.0f };
                float overlap = minDist - dist;

                actor.position.x += normal.x * overlap;
                actor.position.y += normal.y * overlap;
                RemoveInwardVelocity(actor.velocity, normal);
            }
        }

        // Resolucion exacta Circulo vs Caja Rotada (OBB) usando cos/sin precalculados de la instancia.
        void ResolveCircleVsOBB(SimpleActor& actor, Vec2 boxCenter, Vec2 halfSize, float cosRot, float sinRot) {
            // 1. Trasladamos y rotamos al espacio local de la caja (rotacion inversa: -theta).
            float rx = actor.position.x - boxCenter.x;
            float ry = actor.position.y - boxCenter.y;
            float localX = rx * cosRot + ry * sinRot;
            float localY = -rx * sinRot + ry * cosRot;

            // 2. Buscamos el punto mas cercano dentro del rectangulo local [-hx..hx, -hy..hy].
            float closestX = MathIso::Clamp(localX, -halfSize.x, halfSize.x);
            float closestY = MathIso::Clamp(localY, -halfSize.y, halfSize.y);

            float diffX = localX - closestX;
            float diffY = localY - closestY;
            float distSq = diffX * diffX + diffY * diffY;

            if (distSq < actor.collisionRadius * actor.collisionRadius) {
                float dist = std::sqrt(distSq);
                Vec2 localNormal = { 0.0f, 1.0f };
                float overlap = 0.0f;

                if (dist > 1e-5f) {
                    localNormal = { diffX / dist, diffY / dist };
                    overlap = actor.collisionRadius - dist;
                }
                else {
                    // Si el centro del actor penetro el interior del OBB, expulsamos por la cara mas cercana.
                    float penX = halfSize.x - std::abs(localX);
                    float penY = halfSize.y - std::abs(localY);
                    if (penX < penY) {
                        localNormal = { (localX >= 0.0f) ? 1.0f : -1.0f, 0.0f };
                        overlap = penX + actor.collisionRadius;
                    }
                    else {
                        localNormal = { 0.0f, (localY >= 0.0f) ? 1.0f : -1.0f };
                        overlap = penY + actor.collisionRadius;
                    }
                }

                // 3. Devolvemos la normal de empuje al espacio del mundo (rotacion directa: +theta).
                Vec2 worldNormal = {
                    localNormal.x * cosRot - localNormal.y * sinRot,
                    localNormal.x * sinRot + localNormal.y * cosRot
                };

                actor.position.x += worldNormal.x * overlap;
                actor.position.y += worldNormal.y * overlap;
                RemoveInwardVelocity(actor.velocity, worldNormal);
            }
        }

    } // namespace anonimo

    void UpdateActorPhysics(SimpleActor& actor,
        Vec2 desiredWorldDir,
        float dt,
        const World& world,
        SpatialHashGrid& spatialGrid,
        std::vector<uint32_t>& scratchIds,
        std::vector<ChunkCoord>& scratchChunks) {
        // Limitamos dt por seguridad si el usuario arrastra la ventana en Windows.
        float safeDt = std::min(dt, 0.05f);

        // 1. Integracion de Euler semi-implicito (aceleracion + friccion exponencial suave).
        float inputLenSq = desiredWorldDir.x * desiredWorldDir.x + desiredWorldDir.y * desiredWorldDir.y;
        if (inputLenSq > 1e-5f) {
            float invLen = 1.0f / std::sqrt(inputLenSq);
            float dirX = desiredWorldDir.x * invLen;
            float dirY = desiredWorldDir.y * invLen;

            actor.velocity.x += dirX * actor.acceleration * safeDt;
            actor.velocity.y += dirY * actor.acceleration * safeDt;

            // Orientacion suave hacia la direccion de movimiento.
            float targetAngle = std::atan2(dirY, dirX);
            float diff = targetAngle - actor.facingAngleRad;
            while (diff < -3.14159265f) diff += 6.2831853f;
            while (diff > 3.14159265f) diff -= 6.2831853f;
            actor.facingAngleRad += diff * MathIso::Clamp(12.0f * safeDt, 0.0f, 1.0f);
        }

        // Aplicamos amortiguamiento (friccion de suelo) y limite de velocidad maxima.
        float damping = std::max(0.0f, 1.0f - actor.friction * safeDt);
        if (inputLenSq <= 1e-5f) {
            actor.velocity.x *= damping;
            actor.velocity.y *= damping;
        }

        float speedSq = actor.velocity.x * actor.velocity.x + actor.velocity.y * actor.velocity.y;
        if (speedSq > actor.moveSpeed * actor.moveSpeed) {
            float invSpeed = actor.moveSpeed / std::sqrt(speedSq);
            actor.velocity.x *= invSpeed;
            actor.velocity.y *= invSpeed;
        }

        // Avanzamos posicion con la nueva velocidad.
        actor.position.x += actor.velocity.x * safeDt;
        actor.position.y += actor.velocity.y * safeDt;

        // 2. Consulta O(1) al SpatialHashGrid: solo evaluamos chunks que toca el actor.
        float queryMargin = actor.collisionRadius + 1.5f;
        float invChunk = 1.0f / spatialGrid.GetChunkSize();
        ChunkRange range = {
            static_cast<int32_t>(std::floor((actor.position.x - queryMargin) * invChunk)),
            static_cast<int32_t>(std::floor((actor.position.x + queryMargin) * invChunk)),
            static_cast<int32_t>(std::floor((actor.position.y - queryMargin) * invChunk)),
            static_cast<int32_t>(std::floor((actor.position.y + queryMargin) * invChunk))
        };

        spatialGrid.QueryChunks(range, scratchIds, scratchChunks);

        // 3. Resolucion iterativa (2 pasadas evitan atascos en esquinas formadas por dos objetos).
        const auto& instances = world.GetInstances();
        const float limit = world.GetWorldHalfSize() - actor.collisionRadius;

        for (int iter = 0; iter < 2; ++iter) {
            // Limites perimetrales del mundo.
            actor.position.x = MathIso::Clamp(actor.position.x, -limit, limit);
            actor.position.y = MathIso::Clamp(actor.position.y, -limit, limit);

            for (uint32_t id : scratchIds) {
                const MapInstance& inst = instances[id];
                const BakedPrefab* prefab = world.GetPrefab(inst.prefabId);
                if (!prefab || prefab->collider.type == ColliderType::NONE) continue;

                const ColliderDef& col = prefab->collider;
                const Vec2 center2D = { inst.position.x, inst.position.y };

                if (col.type == ColliderType::CIRCLE) {
                    float scaleAvg = (inst.scale.x + inst.scale.y) * 0.5f;
                    ResolveCircleVsCircle(actor, center2D, col.radius * scaleAvg);
                }
                else if (col.type == ColliderType::BOX) {
                    Vec2 scaledHalf = { col.halfSize.x * inst.scale.x, col.halfSize.y * inst.scale.y };
                    ResolveCircleVsOBB(actor, center2D, scaledHalf, inst.cosRot, inst.sinRot);
                }
                else if (col.type == ColliderType::ARCH_PILLARS) {
                    // Descomponemos el arco en 2 pilares OBB laterales, dejando el paso central libre.
                    float totalW = (col.halfSize.x * 2.0f) * inst.scale.x;
                    float openW = col.openingWidth * inst.scale.x;
                    float pillarW = std::max(0.2f, (totalW - openW) * 0.5f);
                    float offsetDist = (openW * 0.5f) + (pillarW * 0.5f);
                    Vec2  pillarHalf = { pillarW * 0.5f, col.halfSize.y * inst.scale.y };

                    // Eje X local rotado del arco.
                    Vec2 leftCenter = { center2D.x - inst.cosRot * offsetDist, center2D.y - inst.sinRot * offsetDist };
                    Vec2 rightCenter = { center2D.x + inst.cosRot * offsetDist, center2D.y + inst.sinRot * offsetDist };

                    ResolveCircleVsOBB(actor, leftCenter, pillarHalf, inst.cosRot, inst.sinRot);
                    ResolveCircleVsOBB(actor, rightCenter, pillarHalf, inst.cosRot, inst.sinRot);
                }
            }
        }
    }

} // namespace AsciiEngine::CollisionSolver