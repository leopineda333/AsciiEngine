/**
 * @file Types.hpp
 * @brief Tipos matematicos y graficos puros del motor (desacoplados de Raylib).
 *
 * @details Al definir Vec2, Vec3 y ColorRGB propios, cualquier proyecto consumidor
 * de AsciiEngine.lib compila sin necesidad de incluir raylib.h.
 */
#pragma once

#include <cstdint>

namespace AsciiEngine {

    // Vector 2D para coordenadas de pantalla, proyeccion isometrica y planos 2D.
    struct Vec2 {
        float x = 0.0f;
        float y = 0.0f;
    };

    // Vector 3D para coordenadas cartesianas del mundo (X, Y suelo; Z altura) y normales.
    struct Vec3 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    // Color compacto de 32 bits (4 bytes contiguos, amigable con cache L1/L2).
    struct ColorRGBA {
        uint8_t r = 255;
        uint8_t g = 255;
        uint8_t b = 255;
        uint8_t a = 255;
    };

    // Identificador numerico de material (2 bytes). Evita comparar std::string en el bucle principal.
    using MaterialId = uint16_t;

    // El ID 0 siempre apunta a un material de respaldo (magenta de diagnostico) para evitar crashes.
    constexpr MaterialId FALLBACK_MATERIAL_ID = 0;

    // Modo de distribucion espacial de los atomos al hornear geometria.
    enum class GridMode : uint8_t {
        REGULAR = 0, // Paso uniforme exacto (ideal para metales, madera tallada, muros).
        NOISY = 1  // Desplazamiento determinista y porosidad (ideal para follaje, piedra, cabello).
    };

} // namespace AsciiEngine