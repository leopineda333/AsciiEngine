// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file MathIso.hpp
 * @brief Nucleo matematico de proyeccion isometrica 2:1, algebra vectorial, ruido procedural y SDFs.
 *
 * @details Este modulo no depende de Raylib ni de ninguna libreria grafica externa.
 * Todas las funciones pequenas y llamadas millones de veces por segundo (Clamp, Lerp, Dot,
 * ComputeDepthKey, Fract) se declaran 'inline' en el header para que el compilador pueda
 * incrustarlas directamente en los bucles criticos sin overhead de llamada a funcion.
 */
#pragma once

#include <AsciiEngine/Types.hpp>
#include <algorithm> // std::min, std::max
#include <cmath>     // std::floor, std::sqrt, std::sin

namespace AsciiEngine::MathIso {

    /**
     * @brief Parametros de escala de la proyeccion isometrica 2:1 (dimetrica clasica).
     *
     * @details En isometria 2:1, cada unidad horizontal avanza el doble de pixeles que en vertical
     * (tileWidthHalf = 2 * tileHeightHalf), produciendo diagonales limpias de 26.565 grados.
     */
    struct IsoConfig {
        float tileWidthHalf = 16.0f; // Desplazamiento horizontal en pixeles por 1 unidad de mundo (X o Y).
        float tileHeightHalf = 8.0f;  // Desplazamiento vertical en pixeles por 1 unidad de mundo (relacion 2:1).
        float scaleZ = 16.0f; // Pixeles que sube un punto en pantalla por cada unidad de altura Z.
    };

    // =========================================================================
    // OPERACIONES ESCALARES Y DE COLOR
    // =========================================================================

    /**
     * @brief Acota un numero flotante al intervalo cerrado [minVal, maxVal].
     * @complexity O(1) — se compila tipicamente a dos instrucciones SIMD minss/maxss.
     */
    inline float Clamp(float value, float minVal, float maxVal) {
        return std::max(minVal, std::min(value, maxVal));
    }

    /**
     * @brief Interpolacion lineal (Linear Interpolation) entre 'a' y 'b' segun factor 't'.
     * @param t Cuando t = 0 devuelve 'a'; cuando t = 1 devuelve 'b'.
     * @complexity O(1).
     */
    inline float Lerp(float a, float b, float t) {
        return a + (b - a) * t;
    }

    /**
     * @brief Mezcla linealmente dos colores RGBA canal por canal, acotando t a [0.0, 1.0].
     * @complexity O(1).
     */
    ColorRGBA LerpColor(const ColorRGBA& a, const ColorRGBA& b, float t);

    // =========================================================================
    // ALGEBRA VECTORIAL 3D
    // =========================================================================

    /**
     * @brief Producto punto (Dot Product) en 3D: a.x*b.x + a.y*b.y + a.z*b.z.
     *
     * @details En iluminacion Lambertiana, si 'a' (normal) y 'b' (direccion de luz) son
     * unitarios, Dot(a, b) equivale al coseno del angulo entre ambos:
     *   1.0 = luz incide de frente | 0.0 = luz rasante (90 deg) | negativo = cara trasera.
     * @complexity O(1).
     */
    inline float Dot(const Vec3& a, const Vec3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    /**
     * @brief Devuelve un vector con la misma direccion que 'v' pero longitud exacta 1.0.
     * @details Si recibe un vector nulo (longitud ~0), devuelve (0, 0, 1) para evitar NaN.
     * @complexity O(1).
     */
    Vec3 Normalize(const Vec3& v);

    // =========================================================================
    // PROYECCION ISOMETRICA 2:1 (ESPACIO DE MUNDO <-> ESPACIO ISOMETRICO 2D)
    // =========================================================================

    /**
     * @brief Proyecta un punto 3D cartesiano del mundo (X, Y suelo; Z altura) a 2D isometrico.
     * @complexity O(1).
     */
    Vec2 WorldToIso(const Vec3& worldPos, const IsoConfig& cfg = {});

    /**
     * @brief Transformacion inversa exacta de WorldToIso asumiendo el plano del suelo (Z = 0).
     * @details Usada para: raycast del mouse en el Editor, calculo de chunks visibles en pantalla,
     * traduccion de teclas WASD a ejes del mundo y rellenado de celdas de terreno en el Z-Buffer.
     * @complexity O(1).
     */
    Vec2 IsoToWorldGround(const Vec2& isoPos, const IsoConfig& cfg = {});

    /**
     * @brief Calcula una clave escalar unica de profundidad para el AsciiZBuffer.
     *
     * @details En nuestra proyeccion isometrica, tanto +X como +Y avanzan hacia abajo en pantalla
     * (es decir, hacia el observador). Por eso la suma (X + Y) ordena los objetos de atras hacia
     * adelante. El termino (Z * 0.002f) desempata atomos que comparten la misma columna vertical
     * (misma base X, Y) para que lo mas alto gane sin alterar el orden entre filas distintas.
     * @complexity O(1).
     */
    inline float ComputeDepthKey(const Vec3& worldPos) {
        return (worldPos.x + worldPos.y) + (worldPos.z * 0.002f);
    }

    // =========================================================================
    // RUIDO DETERMINISTA PROCEDURAL (SIN TEXTURAS NI MEMORIA EXTRA)
    // =========================================================================

    /**
     * @brief Extrae la parte fraccionaria de un numero, siempre en el rango [0.0, 1.0).
     * @details Usar (v - floor(v)) garantiza positividad incluso cuando 'v' es negativo.
     * @complexity O(1).
     */
    inline float Fract(float v) {
        return v - std::floor(v);
    }

    /**
     * @brief Hash pseudoaleatorio determinista en [0.0, 1.0) para cualquier coordenada 3D y semilla.
     * @details Siempre devuelve el mismo valor para los mismos (x, y, z, seed), permitiendo que
     * grillas NOISY, terrenos y pasto sean 100% reproducibles sin guardar mapas de ruido en RAM.
     * @complexity O(1).
     */
    float Hash3D(float x, float y, float z, float seed);

    /**
     * @brief Ruido de valor 2D continuo en [0.0, 1.0] mediante interpolacion Hermite (smoothstep).
     * @details Usado para agrupar praderas, claros de tierra y cumulos organicos de follaje.
     * @complexity O(1) — evalua 4 esquinas enteras con Hash3D y 3 Lerps.
     */
    float ValueNoise2D(float x, float y, float seed);

    // =========================================================================
    // PRIMITIVAS SDF BASE (SIGNED DISTANCE FUNCTIONS ANALITICAS)
    // =========================================================================

    /**
     * @brief Evalua la ecuacion implicita de un elipsoide centrado en el origen con radios (rx, ry, rz).
     * @return Valor <= 1.0 si el punto esta dentro o en la superficie; > 1.0 si esta fuera.
     * @complexity O(1).
     */
    float EllipsoidMetric(const Vec3& localPos, const Vec3& radii);

    /**
     * @brief Calcula la normal superficial exacta en cualquier punto de un elipsoide via su gradiente.
     * @complexity O(1).
     */
    Vec3 EllipsoidNormal(const Vec3& localPos, const Vec3& radii);

} // namespace AsciiEngine::MathIso