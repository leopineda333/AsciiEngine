/**
 * @file MathIso.cpp
 * @brief Implementacion del nucleo matematico isometrico y utilidades SDF.
 */
#include <AsciiEngine/MathIso.hpp>

namespace AsciiEngine::MathIso {

    ColorRGBA LerpColor(const ColorRGBA& a, const ColorRGBA& b, float t) {
        // Restringimos t a [0, 1] para evitar desbordamientos en uint8_t.
        float clampedT = Clamp(t, 0.0f, 1.0f);
        return {
            static_cast<uint8_t>(Lerp(static_cast<float>(a.r), static_cast<float>(b.r), clampedT)),
            static_cast<uint8_t>(Lerp(static_cast<float>(a.g), static_cast<float>(b.g), clampedT)),
            static_cast<uint8_t>(Lerp(static_cast<float>(a.b), static_cast<float>(b.b), clampedT)),
            static_cast<uint8_t>(Lerp(static_cast<float>(a.a), static_cast<float>(b.a), clampedT))
        };
    }

    Vec3 Normalize(const Vec3& v) {
        float lenSq = Dot(v, v);
        // Evitamos division por cero si recibimos un vector nulo.
        if (lenSq < 1e-8f) {
            return { 0.0f, 0.0f, 1.0f };
        }
        float invLen = 1.0f / std::sqrt(lenSq);
        return { v.x * invLen, v.y * invLen, v.z * invLen };
    }

    Vec2 WorldToIso(const Vec3& worldPos, const IsoConfig& cfg) {
        // Eje X del mundo avanza hacia abajo-derecha; eje Y hacia abajo-izquierda.
        // Eje Z eleva el punto restando pixeles en pantalla (Y positivo apunta hacia abajo).
        return {
            (worldPos.x - worldPos.y) * cfg.tileWidthHalf,
            (worldPos.x + worldPos.y) * cfg.tileHeightHalf - (worldPos.z * cfg.scaleZ)
        };
    }

    Vec2 IsoToWorldGround(const Vec2& isoPos, const IsoConfig& cfg) {
        // Despeje algebraico exacto de WorldToIso asumiendo worldPos.z = 0.
        float normX = isoPos.x / cfg.tileWidthHalf;
        float normY = isoPos.y / cfg.tileHeightHalf;
        return {
            (normX + normY) * 0.5f,
            (normY - normX) * 0.5f
        };
    }

    float Hash3D(float x, float y, float z, float seed) {
        // Producto punto contra primos irracionales + seno de alta frecuencia.
        // Produce distribucion uniforme sin necesidad de tablas de ruido en memoria.
        float p = x * 12.9898f + y * 78.233f + z * 37.719f + seed;
        return Fract(std::sin(p) * 43758.5453f);
    }

    float ValueNoise2D(float x, float y, float seed) {
        float ix = std::floor(x);
        float iy = std::floor(y);
        float fx = x - ix;
        float fy = y - iy;

        // Curva Hermite (smoothstep) para evitar esquinas visibles entre celdas de ruido.
        float ux = fx * fx * (3.0f - 2.0f * fx);
        float uy = fy * fy * (3.0f - 2.0f * fy);

        float v00 = Hash3D(ix, iy, 0.0f, seed);
        float v10 = Hash3D(ix + 1.0f, iy, 0.0f, seed);
        float v01 = Hash3D(ix, iy + 1.0f, 0.0f, seed);
        float v11 = Hash3D(ix + 1.0f, iy + 1.0f, 0.0f, seed);

        float nx0 = Lerp(v00, v10, ux);
        float nx1 = Lerp(v01, v11, ux);
        return Lerp(nx0, nx1, uy);
    }

    float EllipsoidMetric(const Vec3& localPos, const Vec3& radii) {
        // Ecuacion implicita: (x/rx)^2 + (y/ry)^2 + (z/rz)^2 <= 1
        float nx = localPos.x / radii.x;
        float ny = localPos.y / radii.y;
        float nz = localPos.z / radii.z;
        return (nx * nx) + (ny * ny) + (nz * nz);
    }

    Vec3 EllipsoidNormal(const Vec3& localPos, const Vec3& radii) {
        // Derivada parcial del elipsoide: (2x/rx^2, 2y/ry^2, 2z/rz^2).
        // Al normalizar, el factor 2 se cancela automaticamente.
        Vec3 grad = {
            localPos.x / (radii.x * radii.x),
            localPos.y / (radii.y * radii.y),
            localPos.z / (radii.z * radii.z)
        };
        return Normalize(grad);
    }

} // namespace AsciiEngine::MathIso