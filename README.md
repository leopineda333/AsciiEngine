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

[![Version](https://img.shields.io/badge/Version-1.0.0-2EA043?style=for-the-badge)]()
[![License](https://img.shields.io/badge/License-zlib-blue?style=for-the-badge)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![Raylib](https://img.shields.io/badge/Raylib-6.2-F5F5F5?style=for-the-badge&logo=raylib&logoColor=black)](https://www.raylib.com/)
[![CMake](https://img.shields.io/badge/CMake-3.20+-064F8C?style=for-the-badge&logo=cmake&logoColor=white)](https://cmake.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x64-0078D6?style=for-the-badge&logo=windows&logoColor=white)]()

<img width="740" height="480" alt="image" src="https://github.com/user-attachments/assets/c6e0d4df-e4c3-4c93-9897-4dd926567706" /><img width="740" height="480" alt="image" src="https://github.com/user-attachments/assets/3998e189-d0a1-48fc-80e4-78400ed381af" />



**[English](#english)** • **[Español](#español)**

</div>

---

<a name="english"></a>
## English

### Overview

**AsciiEngine** is an open-source (zlib), encapsulated, 100% data-driven 2.5D isometric rendering engine written in **C++20** as a static library (`.lib`) over **Raylib**.

Unlike traditional tile-based roguelikes where characters act as flat symbolic icons, **AsciiEngine treats ASCII glyphs as volumetric pixels (atoms)** projected in 2:1 isometric space. Every object, character, material, particle system, walkable surface, biome, and weather state is defined via external JSON templates—achieving dynamic lighting, 2.5D heightfield shadows, rippled water reflections, wind-driven interactive foliage, and procedural animations without sprites or traditional textures.

The codebase is also engineered with a **didactic approach**: all 23 header and source files are audited and documented line-by-line to explain the underlying math, cache-aligned memory layouts, Big-O complexity, and architectural decisions.

---

### Engineering Priorities

Every design decision in this engine strictly follows this hierarchy:

| Priority | Pillar | Core Standard |
| :---: | :--- | :--- |
| **1** | **Optimization** | Zero heap allocations in the main loop, Data-Oriented Design (SoA), 3-layer spatial/frustum culling, adaptive `EffectLodState`, and an $O(N)$ **World-Locked ASCII Z-Buffer** (16B cache-aligned cells) for zero GPU overdraw at $\ge 60$ FPS. |
| **2** | **Scalability** | 100% Data-Driven & genre-agnostic architecture. Add fantasy, sci-fi, or urban materials, objects, walkable surfaces, biomes, particles, or articulated creatures purely through JSON without recompiling C++. |
| **3** | **Aesthetics** | Smoothed monospaced typography with $O(1)$ UV LUT, Sub-Pixel Offset to eliminate rigid grid stiffness, volumetric cell backing (Pass 0), terminal-dark continuous Day/Night/Snow modulation, 3-stop material colorization (`dark`/`normal`/`lit`), depth-based outlines, shadows, and water reflections. |
| **4** | **Functionality & DX** | Strictly encapsulated Facade + PIMPL API (`Engine.hpp` exposes zero third-party headers), embedded **Raygui Map Editor** (`F1`) with Undo/Redo & Hot-Reloading (`F5`), and real-time **Telemetry & Debug Overlays** (`F3`/`F4`). |

---

### Core Features

- **World-Locked Sub-Pixel ASCII Z-Buffer:** 3-pass $O(W \times H)$ renderer (Volumetric Backing → Depth Silhouettes/Outlines → Zero-Overdraw Glyphs) with adaptive splatting up to `2.50x` zoom and zero character swimming.
- **Data-Driven Materials:** Custom ASCII density ramps, 3-stop color palettes, and `REGULAR` (architectural) vs `NOISY` (organic jitter/porosity) sampling assignable per section or overridden per map instance.
- **Modular Objects & Point-Cloud Baking:** Multi-part JSON SDF templates (`ELLIPSOID`, `CYLINDER`, `CONE`, `BOX`, `ARCH`) baked once as hollow shells into contiguous memory (Flyweight pattern) with integrated 2D collision volumes (`CIRCLE`, `BOX`, `ARCH_PILLARS`).
- **Agnostic Modular Characters:** Arbitrary hierarchical JSON section trees topologically sorted at load time for non-recursive $O(S)$ Forward Kinematics, supporting humanoids, quadrupeds, or hovering drones with procedural animation tracks (`IDLE`, `WALK`, `RUN`, `ATTACK`).
- **Dynamic Environment & Interactive Foliage:** $C^1$-continuous astronomical Day/Night cycle, hemispherical Global Illumination (GI), flickering point lights, 2.5D heightfield raymarched shadows, phase-integrated wind fields, rippled water reflections, walkable `SurfaceZone` structures (bridges/floors), and clumped interactive grass/flowers backed by an $O(1)$ 2D `GroundMask`.
- **Particles & Biome Weather:** Fixed-size 2,500-particle SoA pool (~83 KB, $O(1)$ swap-with-last removal) driven by biome-specific Markov weather transitions and dynamic snow accumulation/melting.
- **Embedded Map Editor & Telemetry:** Visual placement with isometric ghost preview, live world/selection inspectors, 100-step Undo/Redo command stack (`Ctrl+Z`/`Ctrl+Y`), full JSON export (`Ctrl+S`), Hot-Reloading (`F5`), and real-time profiling (`F3`/`F4`).

---

### Quick Integration (Facade Pattern)

Consumer projects link `AsciiEngine.lib` without ever including `raylib.h`, `raygui.h`, `json.hpp`, or `windows.h`:

```cpp
#include <AsciiEngine/Engine.hpp>

int main() {
    AsciiEngine::Context engine;
    if (!engine.Initialize(1600, 900, "AsciiEngine v1.0.0 - Sandbox Demo")) {
        return -1;
    }

    engine.LoadRegistry("assets/data");
    engine.LoadMap("assets/data/maps/forest_demo.json");

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

### Controls (Sandbox & Editor)

| Input | Play Mode | Map Editor Mode (`F1`) |
| :--- | :--- | :--- |
| **`WASD` + `Shift`** | Move / Sprint | Free camera pan (faster with `Shift`) |
| **Mouse Wheel** | Camera zoom (`0.95x` – `2.50x`) | Camera zoom / `Shift + Wheel` scales brush |
| **LMB / RMB / MMB** | — | Place or select / Delete nearest / Drag pan |
| **`Q` / `E`** | `E`: Trigger `ATTACK` animation | Rotate brush `-15°` / `+15°` |
| **`TAB`** | Cycle controlled character | — |
| **`1` / `2` / `3` / `M` / `R`** | Clear / Rain / Blizzard / Cycle Biome / Markov toggle | Controlled via Right Inspector |
| **`Left` / `Right` / `Space`** | Scrub / Pause time of day | Controlled via Right Inspector |
| **`Ctrl+Z` / `Ctrl+Y` / `Ctrl+S`** | — | Undo / Redo / Save Map JSON |
| **`O` / `B` / `G` / `V`** | Toggle Outlines / ASCII Border / Sub-Pixel / Backing | Same |
| **`H` / `T` / `K`** | Toggle 2.5D Shadows / Torch | Same (`K`: Toggle Grid Snap) |
| **`F1` / `F3` / `F4` / `F5`** | Editor / Telemetry HUD / Debug Overlays / Hot-Reload | Same (`F5` reloads templates keeping RAM edits) |

---

### Reference Benchmarks (`x64-Release`, 1600×900)

Profiled under heavy stress (Blizzard, 100% snow coverage, 582 map instances, 38 simultaneously active animated characters, 12+ point lights, reflections enabled):

| Scenario | FPS (`Avg 60f`) | Frame Work (`Sim + Cull + ZBuf + GPU`) | Active Subsystem RAM |
| :--- | :---: | :---: | :---: |
| **Wide Stress (`0.95x` zoom, 136 visible objects, 141k atoms)** | **60** (`59.3`) | `13.53 ms` (`0.02 + 0.01 + 9.20 + 4.30 ms`) | `~1.79 MB` fixed |
| **Close-Up (`2.15x` zoom, 100% LOD, 1,378 particles, 361 blades)** | **60** (`60.0`) | `7.42 ms` (`0.03 + 0.00 + 3.91 + 3.48 ms`) | `~1.79 MB` fixed |

*Total pre-allocated subsystem footprint (`ZBuf 450 KB + Heightfield 841 KB + GroundMask 302 KB + Flyweight Clouds 144 KB + SoA Pool 83 KB + SpatialGrid 9 KB`) remains completely flat with zero memory leaks across extended sessions.*

---

### Repository Structure

```text
AsciiEngine/
├── CMakeLists.txt           # Builds AsciiEngine (.lib) + Sandbox (.exe)
├── LICENSE                  # zlib Open Source License
├── include/AsciiEngine/     # Public Facade & encapsulated subsystem headers
├── src/                     # Engine implementation (core, math, render, data, sim, physics, editor, debug, platform)
├── assets/
│   ├── data/                # JSON registries (materials, objects, characters, particles, biomes, weather, maps)
│   └── fonts/               # High-res monospaced .ttf fonts
├── sandbox/                 # Executable demo consuming the engine library
├── third_party/             # Raylib 6.2, nlohmann/json, Raygui
└── docs/                    # Architecture notes & JSON schemas
```

---

### Building (Visual Studio + CMake)

1. Clone the repository:
   ```bash
   git clone [https://github.com/leopineda333/AsciiEngine.git](https://github.com/leopineda333/AsciiEngine.git)
   ```
2. Open **Visual Studio 2022** → **File → Open → Folder** and select the `AsciiEngine` root directory (or run `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release`).
3. Select **`x64-Release`** (recommended for full 60 FPS performance) or `x64-Debug`.
4. Select `Sandbox.exe` as the startup item and press **`F5`**.

---

### Development Roadmap (`v1.0.0` Complete)

- [x] **Phase 0:** Repository setup, CMake static library (`.lib`) + Sandbox (`.exe`), and Facade `Context`.
- [x] **Phase 1:** Isometric 2:1 math core (`MathIso`), typography LUT, and JSON `MaterialSystem`.
- [x] **Phase 2:** 3-Layer Culling (`SpatialHashGrid` + Frustum), World-Locked ASCII Z-Buffer, and Debug HUD (`F3`).
- [x] **Phase 3:** Modular JSON object templates, Point-Cloud Baking (Flyweight), and simple collision physics.
- [x] **Phase 4:** Dynamic Day/Night cycle, hemispherical GI, point lights, and 2.5D heightfield shadows.
- [x] **Phase 5:** Wind fields, water reflections, SoA JSON particles, biome weather, LOD scaling, Sub-Pixel Offset, interactive foliage, and `SurfaceZone` floors.
- [x] **Phase 6:** Agnostic modular JSON character system and procedural animation tracks.
- [x] **Phase 7:** Embedded Raygui Map Editor, Undo/Redo, JSON export, and Hot-Reloading (`F5`).
- [x] **Phase 8:** Final profiling, line-by-line didactic documentation audit, zlib licensing, and `v1.0.0` release.

### License

**AsciiEngine** is licensed under the **zlib License**—the same permissive open-source license used by Raylib. You are free to use, modify, and distribute it in both personal and commercial projects. See [`LICENSE`](LICENSE) for full details.

---

<a name="español"></a>
## Español

### Descripción General

**AsciiEngine** es un motor de renderizado ASCII 2.5D isométrico, open source (licencia **zlib**), encapsulado como biblioteca estática (`.lib`) en **C++20** sobre **Raylib** y diseñado con una arquitectura **100% Data-Driven**.

A diferencia de los roguelikes tradicionales donde cada carácter representa un icono plano sobre una casilla, **AsciiEngine trata los glifos ASCII como píxeles volumétricos (átomos)** proyectados en perspectiva isométrica 2:1. Cada objeto, personaje, material, sistema de partículas, superficie transitable, bioma y clima se define mediante plantillas JSON externas, logrando iluminación dinámica, sombras 2.5D por mapa de alturas, reflejos en agua, vegetación interactiva con viento y animaciones procedurales sin sprites ni texturas convencionales.

Además, todo el código fuente (los 23 archivos header y fuente) está auditado con un enfoque **didáctico**: comentado línea por línea para explicar las matemáticas, el diseño orientado a caché, la complejidad algorítmica y las decisiones de arquitectura detrás de cada subsistema.

---

### Jerarquía de Prioridades

Toda decisión de ingeniería en este proyecto sigue estrictamente este orden:

| Prioridad | Pilar | Estándar Técnico |
| :---: | :--- | :--- |
| **1** | **Optimización** | Cero asignaciones dinámicas en el bucle principal, Data-Oriented Design (SoA), Culling en 3 capas, `EffectLodState` adaptativo y **World-Locked ASCII Z-Buffer** en $O(N)$ (celdas de 16B alineadas a caché) con overdraw cero a $\ge 60$ FPS. |
| **2** | **Escalabilidad** | Arquitectura 100% Data-Driven y agnóstica al género. Crear nuevos materiales, objetos, pisos transitables, biomas, partículas o criaturas articuladas para fantasía, sci-fi o urbano requiere únicamente archivos JSON, sin recompilar C++. |
| **3** | **Estética** | Tipografía monoespaciada suavizada con LUT UV $O(1)$, Sub-Pixel Offset para romper la rigidez de cuadrícula, respaldo volumétrico (Pase 0), fondo oscuro estilo terminal con modulación Día/Noche/Nieve continua, colorización de 3 paradas (`dark`/`normal`/`lit`), siluetas por profundidad, sombras y reflejos. |
| **4** | **Funcionalidad y DX** | API encapsulada (patrón Facade + PIMPL, cero headers externos filtrados), **Editor de Mapas integrado con Raygui** (`F1`), Undo/Redo, Hot-Reloading (`F5`) y **Vista de Debug/Telemetría** (`F3`/`F4`). |

---

### Características Principales

- **ASCII Z-Buffer World-Locked con Sub-Pixel Offset:** Renderizador de 3 pases en $O(W \times H)$ (Respaldo volumétrico → Siluetas/Outlines por profundidad → Glifos a color con Zero Overdraw), splatting adaptativo hasta zoom `2.50x` y cero *character swimming*.
- **Sistema de Materiales Data-Driven:** Rampas ASCII personalizadas, paletas de 3 paradas y modos de muestreo `REGULAR` (arquitectura/metal) o `NOISY` (hojas, roca, pelaje) aplicables por sección o sobrescribibles por instancia en el mapa.
- **Templates de Objetos y Point-Cloud Baking:** Plantillas JSON SDF multiparte (`ELLIPSOID`, `CYLINDER`, `CONE`, `BOX`, `ARCH`) horneadas una sola vez como cáscara hueca en memoria contigua (patrón Flyweight) con volúmenes de colisión integrados (`CIRCLE`, `BOX`, `ARCH_PILLARS`).
- **Personajes Modulares Agnósticos:** Árboles jerárquicos arbitrarios en JSON ordenados topológicamente al cargar para cinemática directa no recursiva en $O(S)$, soportando humanoides, cuadrúpedos o drones con pistas procedurales (`IDLE`, `WALK`, `RUN`, `ATTACK`).
- **Entorno Dinámico y Follaje Interactivo:** Ciclo Día-Noche astronómico continuo $C^1$, iluminación global hemisférica (GI), luces puntuales con parpadeo orgánico, sombras 2.5D por Heightfield, campo de viento con fase integrada continua, agua animada con reflejos, superficies transitables (`SurfaceZone` para puentes/pisos) y matas de pasto/flores interactivas respaldadas por una `GroundMask` 2D $O(1)$.
- **Partículas y Clima por Biomas:** Pool SoA fijo de 2,500 partículas (~83 KB, borrado $O(1)$ *swap-with-last*) dirigido por transiciones climáticas de Markov por bioma y acumulación/derretimiento progresivo de nieve.
- **Editor Integrado y Telemetría:** Colocación visual con *ghost preview* isométrico, inspectores de mundo y selección, pila Undo/Redo de 100 pasos (`Ctrl+Z`/`Ctrl+Y`), guardado completo a JSON (`Ctrl+S`), Hot-Reloading (`F5`) y perfilado en vivo (`F3`/`F4`).

---

### Integración Rápida (Patrón Facade)

Cualquier proyecto externo puede enlazar `AsciiEngine.lib` sin incluir `raylib.h`, `raygui.h`, `json.hpp` ni `windows.h`:

```cpp
#include <AsciiEngine/Engine.hpp>

int main() {
    AsciiEngine::Context engine;
    if (!engine.Initialize(1600, 900, "AsciiEngine v1.0.0 - Sandbox Demo")) {
        return -1;
    }

    engine.LoadRegistry("assets/data");
    engine.LoadMap("assets/data/maps/forest_demo.json");

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

### Controles (Sandbox y Editor)

| Entrada | Modo Juego | Modo Editor (`F1`) |
| :--- | :--- | :--- |
| **`WASD` + `Shift`** | Mover / Correr | Cámara libre (más rápido con `Shift`) |
| **Rueda del Mouse** | Zoom (`0.95x` – `2.50x`) | Zoom / `Shift + Rueda` escala el pincel |
| **Clic Izq / Der / Medio** | — | Colocar o seleccionar / Borrar / Arrastrar cámara |
| **`Q` / `E`** | `E`: Disparar animación `ATTACK` | Rotar pincel `-15°` / `+15°` |
| **`TAB`** | Alternar personaje controlado | — |
| **`1` / `2` / `3` / `M` / `R`** | Sol / Tormenta / Ventisca / Bioma / Auto-clima | Ajustable desde el Inspector Derecho |
| **`Izq` / `Der` / `Espacio`** | Retroceder / Avanzar / Pausar hora del día | Ajustable desde el Inspector Derecho |
| **`Ctrl+Z` / `Ctrl+Y` / `Ctrl+S`** | — | Undo / Redo / Guardar mapa JSON |
| **`O` / `B` / `G` / `V`** | Outlines / Borde ASCII / Sub-Pixel / Backing | Igual |
| **`H` / `T` / `K`** | Sombras 2.5D / Antorcha del foco | Igual (`K`: Grid Snap) |
| **`F1` / `F3` / `F4` / `F5`** | Editor / Telemetría / Overlays / Hot-Reload | Igual (`F5` recarga plantillas sin borrar cambios) |

---

### Rendimiento de Referencia (`x64-Release`, 1600×900)

Perfilado bajo escenario de estrés (Ventisca, 100% nieve, 582 instancias en mapa, 38 personajes animados simultáneos, 12+ luces puntuales, reflejos activos):

| Escenario | FPS (`Avg 60f`) | Tiempo por Frame (`Sim + Cull + ZBuf + GPU`) | RAM Subsistemas |
| :--- | :---: | :---: | :---: |
| **Plano Abierto (`0.95x`, 136 objetos visibles, 141k átomos)** | **60** (`59.3`) | `13.53 ms` (`0.02 + 0.01 + 9.20 + 4.30 ms`) | `~1.79 MB` fijos |
| **Plano Cercano (`2.15x`, LOD 100%, 1,378 partículas, 361 pasto)** | **60** (`60.0`) | `7.42 ms` (`0.03 + 0.00 + 3.91 + 3.48 ms`) | `~1.79 MB` fijos |

*La huella pre-alojada de subsistemas (`ZBuf 450 KB + Heightfield 841 KB + GroundMask 302 KB + Clouds Flyweight 144 KB + Pool SoA 83 KB + SpatialGrid 9 KB`) se mantiene plana con cero fugas de memoria.*

---

### Compilación (Visual Studio + CMake)

1. Clona el repositorio:
   ```bash
   git clone [https://github.com/leopineda333/AsciiEngine.git](https://github.com/leopineda333/AsciiEngine.git)
   ```
2. Abre **Visual Studio 2022** → **Archivo → Abrir → Carpeta** y selecciona la raíz de `AsciiEngine` (o ejecuta `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release`).
3. Selecciona **`x64-Release`** (recomendado para los 60 FPS completos) o `x64-Debug`.
4. Selecciona `Sandbox.exe` como elemento de inicio y presiona **`F5`**.

---

### Hoja de Ruta (`v1.0.0` Completada)

- [x] **Fase 0:** Estructura del repositorio, CMake librería estática (`.lib`) + Sandbox (`.exe`) y Fachada `Context`.
- [x] **Fase 1:** Núcleo matemático isométrico 2:1 (`MathIso`), tabla tipográfica LUT y `MaterialSystem` en JSON.
- [x] **Fase 2:** Culling en 3 capas (`SpatialHashGrid` + Frustum), World-Locked ASCII Z-Buffer y Debug HUD (`F3`).
- [x] **Fase 3:** Plantillas de objetos modulares en JSON, Point-Cloud Baking (Flyweight) y física de colisiones.
- [x] **Fase 4:** Ciclo Día-Noche dinámico, iluminación hemisférica GI, luces puntuales y sombras 2.5D.
- [x] **Fase 5:** Campo de viento, reflejos en agua, partículas SoA en JSON, clima por biomas, LOD adaptativo, Sub-Pixel Offset, follaje interactivo y superficies `SurfaceZone`.
- [x] **Fase 6:** Sistema de personajes modulares agnósticos en JSON y pistas de animación procedural.
- [x] **Fase 7:** Editor visual de mapas con Raygui, Undo/Redo, guardado en JSON y Hot-Reloading (`F5`).
- [x] **Fase 8:** Perfilado final de rendimiento, auditoría didáctica línea por línea, licencia zlib y lanzamiento `v1.0.0`.

### Licencia

**AsciiEngine** se distribuye bajo la licencia **zlib**, la misma licencia open source permisiva utilizada por Raylib. Eres libre de usarlo, modificarlo y distribuirlo en proyectos personales o comerciales. Consulta [`LICENSE`](LICENSE) para más detalles.

---

<div align="center">

**Author / Autor:** Leonardo Lopez Pineda (*Eryan*)  
*Built with C++20 & Raylib • Licensed under zlib*

</div>
