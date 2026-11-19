<div align="center">

```text
    ___   _____ ______________   _______   _____________   ________
   /   | / ___// ____/  _/  _/  / ____/ | / / ____/  _/ | / / ____/
  / /| | \__ \/ /    / / / /   / __/ /  |/ / / __ / //  |/ / __/   
 / ___ |___/ / /____/ /_/ /   / /___/ /|  / /_/ // // /|  / /___   
/_/  |_/____/\____/___/___/  /_____/_/ |_/\____/___/_/ |_/_____/   
```

**2.5D Isometric Data-Driven Volumetric ASCII Rendering Engine**  
*Built from scratch in C++20 on top of Raylib*

[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![Raylib](https://img.shields.io/badge/Raylib-6.2-F5F5F5?style=for-the-badge&logo=raylib&logoColor=black)](https://www.raylib.com/)
[![CMake](https://img.shields.io/badge/CMake-3.20+-064F8C?style=for-the-badge&logo=cmake&logoColor=white)](https://cmake.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x64-0078D6?style=for-the-badge&logo=windows&logoColor=white)]()
[![Status](https://img.shields.io/badge/Status-Phase%200%20Complete-2EA043?style=for-the-badge)]()

**[English](#english)** • **[Español](#español)**

</div>

---

<a name="english"></a>
## English

### Overview

**AsciiEngine** is an encapsulated, 100% data-driven 2.5D isometric rendering pseudo-engine written in **C++20** as a static library (`.lib`) over **Raylib**.

Unlike traditional tile-based roguelikes where characters act as static symbolic icons, **AsciiEngine treats ASCII glyphs as volumetric pixels (atoms)** projected in 2:1 isometric space. Every object, character, material, particle system, biome, and weather state is defined via external JSON templates—achieving dynamic lighting, 2.5D heightfield shadows, water reflections, wind-driven foliage, and procedural animations without sprites or traditional textures.

The codebase is also designed with a **didactic approach**: modules are heavily documented line-by-line to explain the underlying math, cache-friendly memory layouts, and architectural decisions.

---

### Engineering Priorities

Every design decision in this engine strictly follows this hierarchy:

| Priority | Pillar | Core Standard |
| :---: | :--- | :--- |
| **1** | **Optimization** | Zero heap allocations in the main loop, Data-Oriented Design (SoA), 3-layer spatial/frustum culling, and an $O(N)$ **Screen-Space ASCII Z-Buffer** for zero GPU overdraw at $\ge 60$ FPS. |
| **2** | **Scalability** | 100% Data-Driven architecture. Add materials, objects, biomes, particles, or articulated creatures purely through JSON without recompiling C++. |
| **3** | **Aesthetics** | Smoothed monospaced typography, neutral dark-gray base for Day/Night/Snow modulation, 3-stop material colorization (`dark`/`normal`/`lit`), depth-based outlines, shadows, and reflections. |
| **4** | **Functionality & DX** | Clean Facade API for third-party integration, built-in **Raygui Map Editor** with Hot-Reloading (`F5`), and real-time **Telemetry & Debug HUD** (`F3`/`F4`). |

---

### Core Features

* **Data-Driven Materials:** Custom ASCII density ramps, 3-stop color ramps, and `REGULAR` (solid) vs `NOISY` (organic jitter) spatial grids assignable per section.
* **Modular Objects & Point-Cloud Baking:** Unlimited multi-part JSON templates baked once into contiguous memory (Flyweight pattern) with integrated collision volumes.
* **Agnostic Modular Characters:** Hierarchical JSON bone/section trees (`lower_body`, `upper_body`, `head`, limbs, wings, thrusters) with procedural animation tracks—supporting humanoids, quadrupeds, or robots alike.
* **Dynamic Environment:** Astronomical Day/Night cycle, hemispherical Global Illumination (GI), point lights, 2.5D heightfield shadow casting, wind vector fields, and rippled water reflections.
* **Particles & Biome Weather:** Fixed-size SoA particle pools (fire, rain, snow, insects) driven by biome-specific Markov weather transitions.
* **Integrated Editor & Telemetry:** Visual map placement, live environment tweaking, JSON serialization, memory/glyph profiling (`F3`), and visual debug overlays (`F4`).

---

### Quick Integration (Facade Pattern)

Consumer projects link `AsciiEngine` without ever including `raylib.h` or `json.hpp`:

```cpp
#include <AsciiEngine/Engine.hpp>

int main() {
    AsciiEngine::Context engine;
    if (!engine.Initialize(1600, 900, "Sandbox Demo - AsciiEngine")) {
        return -1;
    }

    engine.LoadRegistry("assets/data");
    engine.LoadMap("assets/data/maps/forest_village.json");

    while (engine.IsRunning()) {
        float dt = engine.GetFrameTime();
        engine.Update(dt);
        engine.Render();
    }

    engine.Shutdown();
    return 0;
}
```

---

### Repository Structure

```text
AsciiEngine/
├── CMakeLists.txt           # Builds AsciiEngine (.lib) + Sandbox (.exe)
├── include/AsciiEngine/     # Public Facade & subsystem headers
├── src/                     # Engine implementation (core, math, render, data, sim, physics, editor, debug)
├── assets/
│   ├── data/                # JSON registries (materials, objects, characters, particles, biomes, weather, maps)
│   └── fonts/               # High-res monospaced .ttf fonts
├── sandbox/                 # Executable demo consuming the engine library
├── third_party/             # Raylib 5.5, nlohmann/json, Raygui
└── docs/                    # Architecture notes & JSON schemas
```

---

### Building (Visual Studio + CMake)

1. Clone the repository:
   ```bash
   git clone [https://github.com/YOUR_USERNAME/AsciiEngine.git](https://github.com/YOUR_USERNAME/AsciiEngine.git)
   ```
2. Open **Visual Studio 2022** → **File → Open → Folder** and select the `AsciiEngine` root directory.
3. Let Visual Studio auto-configure CMake (`x64-Debug` or `x64-Release`).
4. Select `Sandbox.exe` as the startup item and press **`F5`**.

---

### Development Roadmap

- [x] **Phase 0:** Repository setup, CMake static library (`.lib`) + Sandbox (`.exe`), and Facade `Context`.
- [ ] **Phase 1:** Isometric 2:1 math core (`MathIso`), typography LUT, and JSON `MaterialSystem`.
- [ ] **Phase 2:** 3-Layer Culling (`SpatialHashGrid` + Frustum), Screen-Space ASCII Z-Buffer, and Debug HUD v1 (`F3`).
- [ ] **Phase 3:** Modular JSON object templates, Point-Cloud Baking (Flyweight), and simple collision physics.
- [ ] **Phase 4:** Dynamic Day/Night cycle, hemispherical GI, point lights, and 2.5D heightfield shadows.
- [ ] **Phase 5:** Wind fields, water reflections, SoA JSON particles, and biome-driven weather system.
- [ ] **Phase 6:** Agnostic modular JSON character system and procedural animation tracks.
- [ ] **Phase 7:** Embedded Raygui Map Editor, Undo/Redo, JSON export, and Hot-Reloading (`F5`).
- [ ] **Phase 8:** Final profiling, didactic documentation audit, and `v1.0.0` release.

---

<a name="español"></a>
## Español

### Descripción General

**AsciiEngine** es un pseudo-motor de renderizado ASCII 2.5D isométrico, encapsulado como biblioteca estática (`.lib`) en **C++20** sobre **Raylib** y diseñado con una arquitectura **100% Data-Driven**.

A diferencia de los roguelikes tradicionales donde cada letra representa un icono plano sobre una casilla, **AsciiEngine utiliza caracteres ASCII como píxeles volumétricos (átomos)** proyectados en perspectiva isométrica 2:1. Cada objeto, personaje, material, sistema de partículas, bioma y clima se define mediante plantillas JSON externas, logrando iluminación dinámica, sombras 2.5D, reflejos en agua, viento en vegetación y animaciones procedurales sin sprites ni texturas convencionales.

Además, el código fuente está pensado con un enfoque **didáctico**: comentado línea por línea para explicar las matemáticas, el diseño orientado a caché y la arquitectura detrás de cada subsistema.

---

### Jerarquía de Prioridades

Toda decisión de ingeniería en este proyecto sigue estrictamente este orden:

| Prioridad | Pilar | Estándar Técnico |
| :---: | :--- | :--- |
| **1** | **Optimización** | Cero asignaciones dinámicas en el bucle principal, Data-Oriented Design (SoA), Culling en 3 capas y **Screen-Space ASCII Z-Buffer** en $O(N)$ con overdraw cero a $\ge 60$ FPS. |
| **2** | **Escalabilidad** | Arquitectura 100% Data-Driven. Crear nuevos materiales, objetos, biomas, partículas o criaturas articuladas requiere únicamente archivos JSON, sin recompilar C++. |
| **3** | **Estética** | Tipografía monoespaciada suavizada, fondo gris oscuro neutro para modular Día/Noche/Nieve, colorización de 3 paradas (`dark`/`normal`/`lit`), siluetas por profundidad, sombras y reflejos. |
| **4** | **Funcionalidad y DX** | API encapsulada (patrón Facade) fácil de integrar en otros proyectos, **Editor de Mapas integrado con Raygui** y Hot-Reloading (`F5`), y **Vista de Debug/Telemetría** (`F3`/`F4`). |

---

### Características Principales

* **Sistema de Materiales Data-Driven:** Rampas ASCII personalizadas, colorización de 3 paradas y grillas ajustables `REGULAR` (sólidos) o `NOISY` (hojas, cabello, roca erosionada) aplicables por sección.
* **Templates de Objetos y Point-Cloud Baking:** Plantillas JSON multiparte ilimitadas, horneadas una sola vez en memoria contigua (patrón Flyweight) con volúmenes de colisión integrados.
* **Personajes Modulares Agnósticos:** Árboles jerárquicos de secciones en JSON (`lower_body`, `upper_body`, `head`, extremidades, alas, propulsores) con pistas de animación procedural para humanoides, animales o robots.
* **Entorno Dinámico:** Ciclo Día-Noche astronómico, iluminación global hemisférica (GI), luces puntuales dinámicas, sombras 2.5D por mapa de alturas, campo vectorial de viento y reflejos en agua.
* **Partículas y Clima por Biomas:** Pools de partículas SoA de tamaño fijo (fuego, lluvia, nieve, insectos) controlados por transiciones climáticas aleatorias orientadas a biomas.
* **Editor Integrado y Telemetría:** Colocación visual de plantillas, configuración global del mundo, exportación a JSON, monitoreo de RAM/glifos en pantalla (`F3`) y overlays visuales (`F4`).

---

### Integración Rápida (Patrón Facade)

Cualquier proyecto externo puede consumir `AsciiEngine` sin incluir `raylib.h` ni `json.hpp`:

```cpp
#include <AsciiEngine/Engine.hpp>

int main() {
    AsciiEngine::Context engine;
    if (!engine.Initialize(1600, 900, "Sandbox Demo - AsciiEngine")) {
        return -1;
    }

    engine.LoadRegistry("assets/data");
    engine.LoadMap("assets/data/maps/forest_village.json");

    while (engine.IsRunning()) {
        float dt = engine.GetFrameTime();
        engine.Update(dt);
        engine.Render();
    }

    engine.Shutdown();
    return 0;
}
```

---

### Compilación (Visual Studio + CMake)

1. Clona el repositorio:
   ```bash
   git clone [https://github.com/TU_USUARIO/AsciiEngine.git](https://github.com/TU_USUARIO/AsciiEngine.git)
   ```
2. Abre **Visual Studio 2022** → **Archivo → Abrir → Carpeta** y selecciona la raíz de `AsciiEngine`.
3. Espera a que Visual Studio genere la caché de CMake automáticamente (`x64-Debug` o `x64-Release`).
4. Selecciona `Sandbox.exe` como elemento de inicio y presiona **`F5`**.

---

### Hoja de Ruta (Roadmap)

- [x] **Fase 0:** Estructura del repositorio, CMake librería estática (`.lib`) + Sandbox (`.exe`) y Fachada `Context`.
- [ ] **Fase 1:** Núcleo matemático isométrico 2:1 (`MathIso`), tabla tipográfica LUT y `MaterialSystem` en JSON.
- [ ] **Fase 2:** Culling en 3 capas (`SpatialHashGrid` + Frustum), Screen-Space ASCII Z-Buffer y Debug HUD v1 (`F3`).
- [ ] **Fase 3:** Plantillas de objetos modulares en JSON, Point-Cloud Baking (Flyweight) y física de colisiones.
- [ ] **Fase 4:** Ciclo Día-Noche dinámico, iluminación hemisférica GI, luces puntuales y sombras 2.5D.
- [ ] **Fase 5:** Campo de viento, reflejos en agua, partículas SoA en JSON y clima aleatorio por biomas.
- [ ] **Fase 6:** Sistema de personajes modulares agnósticos en JSON y pistas de animación procedural.
- [ ] **Fase 7:** Editor visual de mapas con Raygui, Undo/Redo, guardado en JSON y Hot-Reloading (`F5`).
- [ ] **Fase 8:** Perfilado final de rendimiento, auditoría didáctica de comentarios y lanzamiento `v1.0.0`.

---

<div align="center">

**Author / Autor:** Leonardo Lopez Pineda (*Eryan*)  
*Built with C++20 & Raylib*

</div>
