// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file Input.hpp
 * @brief Codigos de teclado, raton y gamepad desacoplados de la capa grafica subyacente.
 *
 * @details Permite a los juegos clientes consultar entrada a traves de AsciiEngine::Context
 * sin incluir raylib.h ni acoplarse a codigos de teclas o mandos especificos de un backend.
 */
#pragma once

#include <cstdint>

namespace AsciiEngine {

    /**
     * @brief Teclas comunes expuestas por la Fachada del motor.
     */
    enum class Key : uint16_t {
        Unknown = 0,
        W, A, S, D,
        Q, E, R, T, M,
        O, B, G, V, H, K,
        Num1, Num2, Num3, Num4,
        Space, Tab, Escape, Enter,
        LeftShift, RightShift,
        LeftControl, RightControl,
        Left, Right, Up, Down,
        F1, F2, F3, F4, F5
    };

    /**
     * @brief Botones del raton expuestos por la Fachada del motor.
     */
    enum class MouseButton : uint8_t {
        Left = 0,
        Right = 1,
        Middle = 2
    };

    /**
     * @brief Botones estandar de gamepad (layout universal Xbox / PlayStation / Nintendo).
     */
    enum class GamepadButton : uint8_t {
        Unknown = 0,
        DPadUp, DPadRight, DPadDown, DPadLeft,
        FaceUp,    // Y / Triangulo
        FaceRight, // B / Circulo
        FaceDown,  // A / Equis
        FaceLeft,  // X / Cuadrado
        LeftBumper,  // LB / L1
        RightBumper, // RB / R1
        Select,      // Back / Share / Minus
        Start,       // Start / Options / Plus
        LeftStick,   // L3
        RightStick   // R3
    };

    /**
     * @brief Ejes analogicos de gamepad.
     * @details Los sticks devuelven [-1.0 .. 1.0]; los gatillos se normalizan a [0.0 .. 1.0].
     */
    enum class GamepadAxis : uint8_t {
        LeftX = 0,
        LeftY,
        RightX,
        RightY,
        LeftTrigger,
        RightTrigger
    };

} // namespace AsciiEngine