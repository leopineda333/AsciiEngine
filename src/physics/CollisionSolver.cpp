// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file CollisionSolver.cpp
 * @brief Implementacion comentada linea por linea de la fisica semi-implicita y colisiones 2D.
 *
 * @details Caracteristicas clave:
 *   - Integracion de Euler semi-implicita (actualiza velocidad primero y luego posicion con la nueva
 *     velocidad): mas estable energeticamente que Euler explicito ante variaciones de dt.
 *   - Deslizamiento tangencial suave: al chocar en diagonal contra un muro o tronco, proyectamos
 *     la velocidad sobre la tangente para que el actor resbale fluidamente sin pegarse.
 *   - Consulta O(1) al SpatialHashGrid con buffers pre-alojados: cero asignaciones en el heap.
 */
#include <AsciiEngine/World.hpp>
#include <AsciiEngine/MathIso.hpp>
#include <AsciiEngine/CullingSystem.hpp>

#include <algorithm>
#include <cmath>

namespace AsciiEngine::CollisionSolver {

    namespace {

        /**
         * @brief Cancela la componente de velocidad que apunta hacia dentro del obstaculo.
         *
         * @details Matematica de proyeccion vectorial:
         *   Sea 'n' la normal unitaria que apunta HACIA AFUERA del obstaculo.
         *   El producto punto vn = Dot(v, n) mide cuanto se mueve el actor en direccion a 'n'.
         *   - Si vn >= 0: el actor ya se esta alejando del obstaculo -> no tocamos la velocidad.
         *   - Si vn <  0: el actor intenta penetrar el obstaculo -> restamos (n * vn), dejando
         *     intacta la componente tangencial para que se deslice paralelo a la pared.
         */
        void RemoveInwardVelocity(Vec2& velocity, Vec2 pushNormal) {
            float vn = velocity.x * pushNormal.x + velocity.y * pushNormal.y;
            if (vn < 0.0f) {
                velocity.x -= pushNormal.x * vn;
                velocity.y -= pushNormal.y * vn;
            }
        }

        /**
         * @brief Resolucion exacta Circulo (actor) vs Circulo (tronco, roca, columna).
         * @complexity O(1).
         */
        void ResolveCircleVsCircle(SimpleActor& actor, Vec2 center, float obsRadius) {
            // Vector desde el centro del obstaculo hacia el centro del actor.
            float dx = actor.position.x - center.x;
            float dy = actor.position.y - center.y;
            float minDist = actor.collisionRadius + obsRadius;
            float distSq = dx * dx + dy * dy;

            // Descarte rapido sin sqrt si no hay superposicion.
            if (distSq < minDist * minDist) {
                float dist = std::sqrt(distSq);
                // Si ambos centros coinciden exactamente (dist ~ 0), empujamos en +X por seguridad.
                Vec2 normal = (dist > 1e-5f) ? Vec2{ dx / dist, dy / dist } : Vec2{ 1.0f, 0.0f };
                float overlap = minDist - dist;

                // Separacion posicional exacta + proyeccion tangencial de velocidad.
                actor.position.x += normal.x * overlap;
                actor.position.y += normal.y * overlap;
                RemoveInwardVelocity(actor.velocity, normal);
            }
        }

        /**
         * @brief Resolucion exacta Circulo (actor) vs Caja Rotada / OBB (muros, pilares de arcos).
         *
         * @details En vez de resolver geometria rotada directamente en espacio del mundo, aplicamos
         * un cambio de base al espacio local de la caja (donde es un AABB alineado a ejes):
         *   1. Trasladamos por -boxCenter y rotamos por -theta usando cosRot/sinRot precalculados.
         *   2. Encontramos el punto mas cercano dentro de [-hx..hx, -hy..hy] con dos Clamp().
         *   3. Si hay colision, rotamos la normal de empuje de vuelta por +theta al espacio del mundo.
         * @complexity O(1) — cero llamadas a sin() o cos().
         */
        void ResolveCircleVsOBB(SimpleActor& actor, Vec2 boxCenter, Vec2 halfSize, float cosRot, float sinRot) {
            // 1. Transformacion Mundo -> Espacio Local del OBB (rotacion inversa: -theta).
            float rx = actor.position.x - boxCenter.x;
            float ry = actor.position.y - boxCenter.y;
            float localX = rx * cosRot + ry * sinRot;
            float localY = -rx * sinRot + ry * cosRot;

            // 2. Punto mas cercano sobre/dentro del rectangulo local al centro del circulo.
            float closestX = MathIso::Clamp(localX, -halfSize.x, halfSize.x);
            float closestY = MathIso::Clamp(localY, -halfSize.y, halfSize.y);

            float diffX = localX - closestX;
            float diffY = localY - closestY;
            float distSq = diffX * diffX + diffY * diffY;

            if (distSq < actor.collisionRadius * actor.collisionRadius) {
                float dist = std::sqrt(distSq);
                Vec2  localNormal = { 0.0f, 1.0f };
                float overlap = 0.0f;

                if (dist > 1e-5f) {
                    // Caso comun: el centro del actor sigue fuera del OBB, solo toca con su radio.
                    localNormal = { diffX / dist, diffY / dist };
                    overlap = actor.collisionRadius - dist;
                }
                else {
                    // Caso extremo: el centro del actor penetro dentro del OBB (ej. spawn encima).
                    // Lo expulsamos por el eje que requiera el desplazamiento minimo (MTV).
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

                // 3. Transformacion Espacio Local -> Mundo (rotacion directa: +theta).
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
        // Limitamos dt a 50 ms (20 FPS minimos simulados): evita "tunneling" (atravesar muros)
        // si el sistema operativo pausa el hilo al arrastrar o redimensionar la ventana.
        float safeDt = std::min(dt, 0.05f);

        // 1. Integracion de Euler semi-implicito con soporte de acelerador analogico (throttle en (0..1]).
        float inputLenSq = desiredWorldDir.x * desiredWorldDir.x + desiredWorldDir.y * desiredWorldDir.y;
        float maxSpeed = actor.moveSpeed;

        if (inputLenSq > 1e-5f) {
            float inputLen = std::sqrt(inputLenSq);
            float invLen = 1.0f / inputLen;
            float dirX = desiredWorldDir.x * invLen;
            float dirY = desiredWorldDir.y * invLen;

            // Si la magnitud viene de un stick inclinado parcialmente (< 1.0), escalamos la velocidad tope.
            float throttle = std::min(1.0f, inputLen);
            maxSpeed = actor.moveSpeed * throttle;

            actor.velocity.x += dirX * actor.acceleration * throttle * safeDt;
            actor.velocity.y += dirY * actor.acceleration * throttle * safeDt;

            // Interpolacion angular por el camino mas corto [-PI, +PI] hacia la direccion de marcha.
            float targetAngle = std::atan2(dirY, dirX);
            float diff = targetAngle - actor.facingAngleRad;
            while (diff < -3.14159265f) diff += 6.2831853f;
            while (diff > 3.14159265f) diff -= 6.2831853f;
            actor.facingAngleRad += diff * MathIso::Clamp(12.0f * safeDt, 0.0f, 1.0f);
        }
        else {
            // Sin entrada direccional: aplicamos amortiguamiento (friccion del suelo; menor en nieve).
            float damping = std::max(0.0f, 1.0f - actor.friction * safeDt);
            actor.velocity.x *= damping;
            actor.velocity.y *= damping;
        }

        // Acotamos la magnitud de velocidad al maximo proporcional actual.
        float speedSq = actor.velocity.x * actor.velocity.x + actor.velocity.y * actor.velocity.y;
        if (speedSq > maxSpeed * maxSpeed) {
            float invSpeed = maxSpeed / std::sqrt(speedSq);
            actor.velocity.x *= invSpeed;
            actor.velocity.y *= invSpeed;
        }

        // Avanzamos la posicion utilizando la velocidad recien integrada.
        actor.position.x += actor.velocity.x * safeDt;
        actor.position.y += actor.velocity.y * safeDt;

        // 2. Consulta espacial O(1): solo inspeccionamos los chunks inmediatamente cercanos al actor.
        float queryMargin = actor.collisionRadius + 1.5f;
        float invChunk = 1.0f / spatialGrid.GetChunkSize();
        ChunkRange range = {
            static_cast<int32_t>(std::floor((actor.position.x - queryMargin) * invChunk)),
            static_cast<int32_t>(std::floor((actor.position.x + queryMargin) * invChunk)),
            static_cast<int32_t>(std::floor((actor.position.y - queryMargin) * invChunk)),
            static_cast<int32_t>(std::floor((actor.position.y + queryMargin) * invChunk))
        };

        spatialGrid.QueryChunks(range, scratchIds, scratchChunks);

        // 3. Resolucion iterativa en 2 pasadas:
        //    Cuando el actor choca contra una esquina formada por dos objetos, la pasada 1 puede
        //    empujarlo ligeramente dentro del segundo objeto; la pasada 2 estabiliza la posicion final.
        const auto& instances = world.GetInstances();
        const float limit = world.GetWorldHalfSize() - actor.collisionRadius;

        for (int iter = 0; iter < 2; ++iter) {
            // Mantenemos al actor dentro del perimetro del mundo.
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
                    // Descomponemos el portico en 2 pilares OBB laterales separados por openingWidth:
                    //   |<-- pillarW -->|<--- openingWidth --->|<-- pillarW -->|
                    // El eje X local rotado del arco viene dado por el vector unitario (cosRot, sinRot).
                    float totalW = (col.halfSize.x * 2.0f) * inst.scale.x;
                    float openW = col.openingWidth * inst.scale.x;
                    float pillarW = std::max(0.2f, (totalW - openW) * 0.5f);
                    float offsetDist = (openW * 0.5f) + (pillarW * 0.5f);
                    Vec2  pillarHalf = { pillarW * 0.5f, col.halfSize.y * inst.scale.y };

                    Vec2 leftCenter = { center2D.x - inst.cosRot * offsetDist, center2D.y - inst.sinRot * offsetDist };
                    Vec2 rightCenter = { center2D.x + inst.cosRot * offsetDist, center2D.y + inst.sinRot * offsetDist };

                    ResolveCircleVsOBB(actor, leftCenter, pillarHalf, inst.cosRot, inst.sinRot);
                    ResolveCircleVsOBB(actor, rightCenter, pillarHalf, inst.cosRot, inst.sinRot);
                }
            }
        }
    }

} // namespace AsciiEngine::CollisionSolver