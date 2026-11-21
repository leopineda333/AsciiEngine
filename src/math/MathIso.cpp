// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file MathIso.cpp
 * @brief Implementacion comentada linea por linea del nucleo matematico isometrico y SDFs.
 */
#include <AsciiEngine/MathIso.hpp>

namespace AsciiEngine::MathIso {

    ColorRGBA LerpColor(const ColorRGBA& a, const ColorRGBA& b, float t) {
        // 1. Acotamos 't' a [0, 1]: si t saliera de rango, el cast a uint8_t daria wrap-around (artefactos de color).
        float clampedT = Clamp(t, 0.0f, 1.0f);

        // 2. Convertimos cada canal de entero [0..255] a float, interpolamos y reconvertimos a uint8_t.
        return {
            static_cast<uint8_t>(Lerp(static_cast<float>(a.r), static_cast<float>(b.r), clampedT)),
            static_cast<uint8_t>(Lerp(static_cast<float>(a.g), static_cast<float>(b.g), clampedT)),
            static_cast<uint8_t>(Lerp(static_cast<float>(a.b), static_cast<float>(b.b), clampedT)),
            static_cast<uint8_t>(Lerp(static_cast<float>(a.a), static_cast<float>(b.a), clampedT))
        };
    }

    Vec3 Normalize(const Vec3& v) {
        // 1. Calculamos la longitud al cuadrado (x^2 + y^2 + z^2) usando Dot contra si mismo.
        float lenSq = Dot(v, v);

        // 2. Proteccion contra division por cero o vectores casi nulos (umbral 1e-8).
        //    Devolver +Z (hacia arriba) garantiza una normal valida en caso degenerado.
        if (lenSq < 1e-8f) {
            return { 0.0f, 0.0f, 1.0f };
        }

        // 3. Calculamos el inverso de la longitud una sola vez y multiplicamos 3 veces
        //    (en CPU moderna, 1 sqrt + 1 div + 3 mul es mas rapido que 3 divisiones).
        float invLen = 1.0f / std::sqrt(lenSq);
        return { v.x * invLen, v.y * invLen, v.z * invLen };
    }

    Vec2 WorldToIso(const Vec3& worldPos, const IsoConfig& cfg) {
        // Transformacion matricial isometrica 2:1:
        //   isoX = (X - Y) * tileWidthHalf  -> +X va abajo-derecha, +Y va abajo-izquierda.
        //   isoY = (X + Y) * tileHeightHalf - (Z * scaleZ)
        //          Nota: restamos Z porque en pantalla 2D el eje Y crece hacia abajo;
        //          elevar un objeto en +Z debe moverlo hacia arriba en la ventana.
        return {
            (worldPos.x - worldPos.y) * cfg.tileWidthHalf,
            (worldPos.x + worldPos.y) * cfg.tileHeightHalf - (worldPos.z * cfg.scaleZ)
        };
    }

    Vec2 IsoToWorldGround(const Vec2& isoPos, const IsoConfig& cfg) {
        // Despeje algebraico exacto de WorldToIso cuando Z = 0:
        //   Dado: normX = isoX / tileWidthHalf  = X - Y
        //         normY = isoY / tileHeightHalf = X + Y
        //   Sumando ambas ecuaciones:    normX + normY = 2X  =>  X = (normX + normY) * 0.5
        //   Restando la primera a la 2a: normY - normX = 2Y  =>  Y = (normY - normX) * 0.5
        float normX = isoPos.x / cfg.tileWidthHalf;
        float normY = isoPos.y / cfg.tileHeightHalf;
        return {
            (normX + normY) * 0.5f,
            (normY - normX) * 0.5f
        };
    }

    float Hash3D(float x, float y, float z, float seed) {
        // 1. Proyectamos (x, y, z) sobre 3 constantes decimales grandes e inconmensurables
        //    para evitar patrones repetitivos sobre ejes o diagonales de la grilla.
        float p = x * 12.9898f + y * 78.233f + z * 37.719f + seed;

        // 2. Multiplicamos el seno por un numero grande (43758.5453) y nos quedamos solo con
        //    los decimales: pequenas variaciones en p producen saltos pseudoaleatorios en [0, 1).
        return Fract(std::sin(p) * 43758.5453f);
    }

    float ValueNoise2D(float x, float y, float seed) {
        // 1. Identificamos la celda entera (ix, iy) que contiene al punto (x, y)
        //    y su coordenada fraccionaria local (fx, fy) dentro de [0, 1).
        float ix = std::floor(x);
        float iy = std::floor(y);
        float fx = x - ix;
        float fy = y - iy;

        // 2. Aplicamos la curva smoothstep f(t) = 3t^2 - 2t^3 (derivada 0 en t=0 y t=1).
        //    Sin este suavizado, la interpolacion lineal dejaria aristas cuadradas visibles.
        float ux = fx * fx * (3.0f - 2.0f * fx);
        float uy = fy * fy * (3.0f - 2.0f * fy);

        // 3. Evaluamos un valor pseudoaleatorio fijo en cada una de las 4 esquinas de la celda.
        float v00 = Hash3D(ix, iy, 0.0f, seed); // Esquina inferior-izquierda
        float v10 = Hash3D(ix + 1.0f, iy, 0.0f, seed); // Esquina inferior-derecha
        float v01 = Hash3D(ix, iy + 1.0f, 0.0f, seed); // Esquina superior-izquierda
        float v11 = Hash3D(ix + 1.0f, iy + 1.0f, 0.0f, seed); // Esquina superior-derecha

        // 4. Interpolacion bilineal: primero en X (fila inferior y fila superior), luego en Y.
        float nx0 = Lerp(v00, v10, ux);
        float nx1 = Lerp(v01, v11, ux);
        return Lerp(nx0, nx1, uy);
    }

    float EllipsoidMetric(const Vec3& localPos, const Vec3& radii) {
        // Ecuacion canonica del elipsoide: (x/rx)^2 + (y/ry)^2 + (z/rz)^2 <= 1.
        // Al dividir por los radios, normalizamos el elipsoide a una esfera unitaria de radio 1.
        float nx = localPos.x / radii.x;
        float ny = localPos.y / radii.y;
        float nz = localPos.z / radii.z;
        return (nx * nx) + (ny * ny) + (nz * nz);
    }

    Vec3 EllipsoidNormal(const Vec3& localPos, const Vec3& radii) {
        // El vector perpendicular (normal) a una superficie implicita F(x,y,z) = 1 es su gradiente:
        //   dF/dx = 2x / rx^2,  dF/dy = 2y / ry^2,  dF/dz = 2z / rz^2.
        // Como Normalize() reescala el vector a longitud 1, omitimos el factor constante 2.
        Vec3 grad = {
            localPos.x / (radii.x * radii.x),
            localPos.y / (radii.y * radii.y),
            localPos.z / (radii.z * radii.z)
        };
        return Normalize(grad);
    }

} // namespace AsciiEngine::MathIso