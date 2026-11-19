/**
 * @file MathIso.hpp
 * @brief Proyeccion isometrica 2:1, funciones matematicas, hash determinista y SDFs base.
 *
 * @details Centraliza todas las conversiones entre espacio Cartesiano 3D y pantalla 2D,
 * ademas de operaciones inline de bajo costo para iluminacion y ruido procedural.
 */
#pragma once

#include <AsciiEngine/Types.hpp>
#include <algorithm>
#include <cmath>

namespace AsciiEngine::MathIso {

    // Configuracion de escala de la proyeccion isometrica 2:1 (ancho = 2 * alto).
    struct IsoConfig {
        float tileWidthHalf = 16.0f; // Pixeles horizontales por unidad de mundo.
        float tileHeightHalf = 8.0f;  // Pixeles verticales por unidad de mundo (relacion 2:1).
        float scaleZ = 16.0f; // Pixeles de elevacion por unidad en el eje Z.
    };

    // --- Operaciones Escales y de Color (inline para cero overhead de llamada) ---

    inline float Clamp(float value, float minVal, float maxVal) {
        return std::max(minVal, std::min(value, maxVal));
    }

    inline float Lerp(float a, float b, float t) {
        return a + (b - a) * t;
    }

    // Interpola linealmente dos colores RGBA en el rango t = [0.0, 1.0].
    ColorRGBA LerpColor(const ColorRGBA& a, const ColorRGBA& b, float t);

    // --- Algebra Vectorial 3D ---

    inline float Dot(const Vec3& a, const Vec3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    Vec3 Normalize(const Vec3& v);

    // --- Proyeccion Isometrica 2:1 ---

    // Convierte un punto 3D del mundo (X, Y, Z) a coordenadas 2D isometricas en pixeles.
    Vec2 WorldToIso(const Vec3& worldPos, const IsoConfig& cfg = {});

    // Conversion inversa: traduce coordenadas isometricas 2D al plano del suelo (Z = 0).
    // Fundamental para seleccion con mouse y colocacion en el Editor de Mapas.
    Vec2 IsoToWorldGround(const Vec2& isoPos, const IsoConfig& cfg = {});

    // Clave escalar de profundidad para resolver oclusion en isometria sin ambiguedad.
    // A mayor (X + Y), mas cerca de la camara; Z desempata puntos en la misma columna vertical.
    inline float ComputeDepthKey(const Vec3& worldPos) {
        return (worldPos.x + worldPos.y) + (worldPos.z * 0.002f);
    }

    // --- Ruido Espacial Determinista (para grillas NOISY sin texturas) ---

    // Parte fraccionaria siempre en el rango [0.0, 1.0).
    inline float Fract(float v) {
        return v - std::floor(v);
    }

    // Hash pseudoaleatorio determinista en [0.0, 1.0) a partir de una coordenada 3D y semilla.
    float Hash3D(float x, float y, float z, float seed);

    // --- Primitivas SDF Base (Signed Distance Functions) ---

    // Evalua un elipsoide centrado en el origen con radios (rx, ry, rz).
    // Retorna <= 1.0 si el punto esta dentro o en la superficie.
    float EllipsoidMetric(const Vec3& localPos, const Vec3& radii);

    // Calcula el vector normal unitario en la superficie de un elipsoide (gradiente analitico).
    Vec3 EllipsoidNormal(const Vec3& localPos, const Vec3& radii);

} // namespace AsciiEngine::MathIso