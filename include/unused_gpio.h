// unused_gpio.h
#pragma once
#include <Arduino.h>
#include <array>

// C++17: constexpr std::array with aggregate initialization
// Excludes: Used pins, Strapping (0,3,45,46), USB (19,20), 
// Internal Flash/PSRAM (26-37), and default UART0 (43,44),
// and RGB LED (48)
constexpr std::array<int, 27> kUnusedGpios = {{
    1, 7, 8, 9, 15, 16, 17, 18, 22, 23, 24, 25,
    38, 39, 40, 41, 42, 49, 50, 51, 52, 53, 54, 55, 56, 57
}};

// C++17: inline + noexcept for zero-overhead runtime init
inline void ConfigureUnusedGpios() noexcept {
    for (const auto& pin : kUnusedGpios) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }
}