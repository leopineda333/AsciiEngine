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

[![Version](https://img.shields.io/badge/Version-1.1.0-2EA043?style=for-the-badge)]()
[![CI](https://img.shields.io/github/actions/workflow/status/leopineda333/AsciiEngine/ci.yml?style=for-the-badge&label=CI%20Windows%20%26%20Linux)](https://github.com/leopineda333/AsciiEngine/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/License-zlib-blue?style=for-the-badge)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![Raylib](https://img.shields.io/badge/Raylib-6.2-F5F5F5?style=for-the-badge&logo=raylib&logoColor=black)](https://www.raylib.com/)
[![CMake](https://img.shields.io/badge/CMake-3.20+-064F8C?style=for-the-badge&logo=cmake&logoColor=white)](https://cmake.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20x64-0078D6?style=for-the-badge)]()

**[English](#english)** • **[Español](#español)** • **[Artist Guide (JSON)](docs/ARTIST_GUIDE.md)**

<img width="740" height="480" alt="image" src="https://github.com/user-attachments/assets/c6e0d4df-e4c3-4c93-9897-4dd926567706" /><img width="740" height="480" alt="image" src="https://github.com/user-attachments/assets/3998e189-d0a1-48fc-80e4-78400ed381af" />


</div>

---

<a name="english"></a>
## English

### Overview

**AsciiEngine** is an open-source (**zlib**), cross-platform (Windows & Linux), 100% data-driven 2.5D isometric rendering engine written in **C++20** as a static library (`.lib` / `.a`) over **Raylib**.

Unlike traditional tile-based roguelikes where characters act as flat symbolic icons, **AsciiEngine treats ASCII glyphs as volumetric pixels (atoms)** projected in 2:1 isometric space. Every object, character, material, particle system, walkable surface, biome, and weather state is defined via external JSON templates—achieving dynamic lighting, Blinn-Phong specular highlights, 2.5D heightfield shadows, rippled water reflections, wind-driven interactive foliage, and procedural animations without sprites or traditional textures.

The codebase is engineered with a **didactic approach**: all header and source files are audited and documented line-by-line to explain the underlying math, cache-aligned memory layouts, Big-O complexity, and architectural decisions.

---

### What's New in `v1.1.0`

- **Exposed Engine Settings & Quality Presets (`Settings.hpp`):** Full POD `EngineSettings` API (`GetSettings`, `ApplySettings`, `ApplyQualityPreset` for `LOW` / `MEDIUM` / `HIGH` / `ULTRA`, and `LoadSettings` / `SaveSettings` to `user_settings.json`) so games can expose custom graphics menus (outlines, volumetric cell backing, atmospheric influence, shadow raymarch steps, GI strength, reflections, wind, and foliage/particle density).
- **Ephemeral FX & Material Specular:** Instant `SpawnParticleBurst`, self-destructing timed emitters (`AddTimedParticleEmitter`), automatic terrain-aware footsteps (dust, snow, water splash), temporary point lights with smooth fade-out (`AddTemporaryLight`), and fast Blinn-Phong specular highlights driven by material `roughness` and `reflectivity`.
- **Decoupled Gameplay & Native Gamepad API (`Input.hpp`):** Game controls live in client code rather than engine internals. `Context` exposes encapsulated keyboard, mouse, ground cursor raycasting (`GetCursorWorldPosition`), and native multi-controller gamepad polling with radial deadzones, normalized triggers, and proportional analog movement.
- **Artist Guide, Linux Parity & CI:** Complete JSON authoring guide with honest technical limitations ([`docs/ARTIST_GUIDE.md`](docs/ARTIST_GUIDE.md)), native Linux private RAM telemetry (`/proc/self/statm`), portable `.ttf` font discovery, hybrid CMake (`FetchContent` automatic Raylib 6.2 fallback), and automated Windows & Linux CI via GitHub Actions.

---

### Engineering Priorities

| Priority | Pillar | Core Standard |
| :---: | :--- | :--- |
| **1** | **Optimization** | Zero heap allocations in the main loop, Data-Oriented Design (SoA), 3-layer spatial/frustum culling, adaptive `EffectLodState`, and an $O(N)$ **World-Locked ASCII Z-Buffer** (16B cache-aligned cells) for zero GPU overdraw at $\ge 60$ FPS. |
| **2** | **Scalability** | 100% Data-Driven & genre-agnostic architecture. Add fantasy, sci-fi, or urban materials, objects, walkable surfaces, biomes, particles, or articulated creatures purely through JSON without recompiling C++. |
| **3** | **Aesthetics** | Smoothed monospaced typography with $O(1)$ UV LUT, Sub-Pixel Offset, volumetric cell backing (Pass 0), continuous Day/Night/Snow modulation, 3-stop colorization (`dark`/`normal`/`lit`), Blinn-Phong specular highlights, depth outlines, shadows, and reflections. |
| **4** | **Functionality & DX** | Strictly encapsulated Facade + PIMPL API (verified by CI), exposed `EngineSettings`, native gamepad support, embedded **Raygui Map Editor** (`F1`) with Undo/Redo & Hot-Reloading (`F5`), and real-time **Telemetry & Debug Overlays** (`F3`/`F4`). |

---

### Core Features

- **World-Locked Sub-Pixel ASCII Z-Buffer:** 3-pass $O(W \times H)$ renderer (Volumetric Backing → Depth Silhouettes/Outlines → Zero-Overdraw Glyphs) with adaptive splatting up to `2.50x` zoom and zero character swimming.
- **Data-Driven Materials:** Custom ASCII density ramps, 3-stop color palettes, `REGULAR` vs `NOISY` sampling, and physical `roughness`/`reflectivity` properties assignable per section or overridden per map instance.
- **Modular Objects & Point-Cloud Baking:** Multi-part JSON SDF templates (`ELLIPSOID`, `CYLINDER`, `CONE`, `BOX`, `ARCH`) baked once as hollow shells into contiguous memory (Flyweight pattern) with integrated 2D collision volumes (`CIRCLE`, `BOX`, `ARCH_PILLARS`).
- **Agnostic Modular Characters:** Arbitrary hierarchical JSON section trees topologically sorted at load time for non-recursive $O(S)$ Forward Kinematics, supporting humanoids, quadrupeds, or hovering drones with procedural animation tracks (`IDLE`, `WALK`, `RUN`, `ATTACK`) and terrain-aware footsteps.
- **Dynamic Environment & Interactive Foliage:** $C^1$-continuous astronomical Day/Night cycle, hemispherical GI, flickering & temporary point lights, configurable 2.5D heightfield raymarched shadows, phase-integrated wind fields, rippled water reflections, walkable `SurfaceZone` bridges/floors, and clumped interactive grass/flowers backed by an $O(1)$ 2D `GroundMask`.
- **Particles & Biome Weather:** Fixed-size 2,500-particle SoA pool (~83 KB, $O(1)$ swap-with-last removal) supporting continuous emitters, timed `ttl` emitters, and `SpawnBurst` effects driven by biome-specific Markov weather transitions.
- **Embedded Map Editor & Telemetry:** Visual placement with isometric ghost preview, live world/selection inspectors, 100-step Undo/Redo stack (`Ctrl+Z`/`Ctrl+Y`), full JSON export (`Ctrl+S`), Hot-Reloading (`F5`), and real-time profiling (`F3`/`F4`).

---

### Quick Integration (Facade + Gameplay API)

Consumer projects link `AsciiEngine` without ever including `raylib.h`, `raygui.h`, `json.hpp`, or `windows.h`:

```cpp
#include <AsciiEngine/Engine.hpp>

int main() {
    using namespace AsciiEngine;

    Context engine;
    if (!engine.Initialize(1600, 900, "My ASCII Game")) {
        return -1;
    }

    // Load user graphics settings or create default HIGH preset
    if (!engine.LoadSettings("user_settings.json")) {
        engine.ApplyQualityPreset(QualityPreset::HIGH);
        engine.SaveSettings("user_settings.json");
    }

    engine.LoadRegistry("assets/data");
    engine.LoadMap("assets/data/maps/forest_demo.json");

    while (engine.IsRunning()) {
        float dt = engine.GetFrameTime();

        // Encapsulated keyboard + analog gamepad movement
        Vec2 move = engine.GetGamepadLeftStick(0);
        if (engine.IsKeyDown(Key::W)) move.y -= 1.0f;
        if (engine.IsKeyDown(Key::S)) move.y += 1.0f;
        if (engine.IsKeyDown(Key::A)) move.x -= 1.0f;
        if (engine.IsKeyDown(Key::D)) move.x += 1.0f;
        engine.SetControlledMoveInput(move, engine.IsKeyDown(Key::LeftShift));

        // Cursor raycast FX (burst + temporary light)
        if (engine.IsMouseButtonPressed(MouseButton::Left)) {
            Vec2 aim = engine.GetCursorWorldPosition();
            engine.SpawnParticleBurst("impact_burst", { aim.x, aim.y, 0.45f }, 20);
            engine.AddTemporaryLight({ aim.x, aim.y, 1.1f }, { 255, 170, 80, 255 }, 2.4f, 10.5f, 1.5f);
        }

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
| **`WASD` / Left Stick** | Move (analog speed on gamepad) | Free camera pan |
| **`Shift` / `RT` / `L3`** | Sprint | Faster camera pan |
| **Mouse Wheel / Right Stick Y** | Camera zoom (`0.95x` – `2.50x`) | Camera zoom / `Shift + Wheel` scales brush |
| **LMB / RMB / MMB** | LMB: Cursor impact burst + timed sparks | Place or select / Delete nearest / Drag pan |
| **`Q` / `E` / Gamepad `A` or `X`** | `E` / `A` / `X`: Attack + burst + flash light | `Q`/`E`: Rotate brush `-15°` / `+15°` |
| **`TAB` / Gamepad `RB`** | Cycle controlled character | — |
| **`1` / `2` / `3` / `M` / `R`** | Clear / Rain / Blizzard / Biome (`D-Pad Up`) / Auto-weather | Controlled via Right Inspector |
| **`Left` / `Right` / `Space`** | Scrub (`D-Pad L/R`) / Pause (`Start`) time of day | Controlled via Right Inspector |
| **`Ctrl+Z` / `Ctrl+Y` / `Ctrl+S`** | — | Undo / Redo / Save Map JSON |
| **`O` / `B` / `G` / `V`** | Toggle Outlines / ASCII Border / Sub-Pixel / Backing | Same |
| **`H` / `T` (`Y`) / `K`** | Toggle 2.5D Shadows / Torch | Same (`K`: Toggle Grid Snap) |
| **`F1` / `F3` / `F4` / `F5`** | Editor / Telemetry HUD / Debug Overlays / Hot-Reload | Same (`F5` reloads templates keeping RAM edits) |

---

### Reference Benchmarks (`x64-Release`, 1600×900)

Profiled under heavy stress (Blizzard, 100% snow coverage, 582 map instances, 38 simultaneously active animated characters, 12+ point lights, reflections enabled):

| Scenario | FPS (`Avg 60f`) | Frame Work (`Sim + Cull + ZBuf + GPU`) | Active Subsystem RAM |
| :--- | :---: | :---: | :---: |
| **Wide Stress (`0.95x` zoom, 136 visible objects, 141k atoms)** | **60** (`59.3`) | `13.53 ms` (`0.02 + 0.01 + 9.20 + 4.30 ms`) | `~1.79 MB` fixed |
| **Close-Up (`2.15x` zoom, 100% LOD, 1,378 particles, 361 blades)** | **60** (`60.0`) | `7.42 ms` (`0.03 + 0.00 + 3.91 + 3.48 ms`) | `~1.79 MB` fixed |

*Total pre-allocated subsystem footprint (`ZBuf 450 KB + Heightfield 841 KB + GroundMask 302 KB + Flyweight Clouds 144 KB + SoA Pool 83 KB + SpatialGrid 9 KB`) remains flat with zero memory leaks across extended sessions.*

---

### Repository Structure

```text
AsciiEngine/
├── .github/workflows/ci.yml # Automated Facade/License audit + Windows & Linux builds
├── CMakeLists.txt           # Cross-platform CMake (local Raylib or automatic FetchContent 6.2)
├── LICENSE                  # zlib Open Source License
├── include/AsciiEngine/     # Public Facade, Settings.hpp, Input.hpp & subsystem headers
├── src/                     # Engine implementation (core, math, render, data, sim, physics, editor, debug, platform)
├── assets/
│   ├── data/                # JSON registries (materials, objects, characters, particles, biomes, weather, maps)
│   └── fonts/               # Optional bundled monospaced .ttf fonts
├── sandbox/                 # Executable demo consuming AsciiEngine via its public Gameplay API
├── third_party/             # nlohmann/json, Raygui, optional local Raylib
└── docs/
    └── ARTIST_GUIDE.md      # Complete JSON art creation guide & technical limitations
```

---

### Building (Windows & Linux)

CMake automatically uses a local Windows `third_party/raylib` build if present, or fetches and compiles **Raylib 6.2** cleanly via `FetchContent` on Linux and fresh clones.

#### Windows (Visual Studio 2022+ or CLI)
```bash
git clone [https://github.com/leopineda333/AsciiEngine.git](https://github.com/leopineda333/AsciiEngine.git)
cd AsciiEngine
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```
*(In Visual Studio: **File → Open → Folder**, select **`x64-Release`**, pick `Sandbox.exe`, and press **`F5`**.)*

#### Linux (Ubuntu / Debian)
```bash
sudo apt-get update && sudo apt-get install -y \
  build-essential cmake libasound2-dev libx11-dev libxrandr-dev libxi-dev \
  libgl1-mesa-dev libglu1-mesa-dev libxcursor-dev libxinerama-dev \
  libwayland-dev libxkbcommon-dev

git clone [https://github.com/leopineda333/AsciiEngine.git](https://github.com/leopineda333/AsciiEngine.git)
cd AsciiEngine
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/Sandbox
```

---

### Development Roadmap

- [x] **Phases 0–8 (`v1.0.0`):** Core 2:1 isometric math, JSON registries, 3-layer culling, World-Locked Sub-Pixel ASCII Z-Buffer, Flyweight baking, 2D collisions, Day/Night + GI + 2.5D heightfield shadows, biome Markov weather, SoA particles, modular procedural characters, Raygui Map Editor + Undo/Redo, full didactic audit, and zlib release.
- [x] **`v1.1.0` (Current):** Exposed `EngineSettings` + Quality Presets + JSON persistence, `SpawnBurst` & timed `ttl` emitters, automatic terrain-aware footsteps, temporary lights, Blinn-Phong specular (`roughness`), decoupled Gameplay API + native analog Gamepad support, `docs/ARTIST_GUIDE.md`, Linux parity, and automated Windows/Linux CI.
- [ ] **`v1.2.0` (Planned):** `.obj` low-poly mesh baker into `BakedPrefab`, lightweight dynamic character shadows, and optional spatial audio hooks.

### License

**AsciiEngine** is licensed under the **zlib License**—the same permissive open-source license used by Raylib. You are free to use, modify, and distribute it in both personal and commercial projects. See [`LICENSE`](LICENSE) for full details.

---

<a name="español"></a>
## Español

### Descripción General

**AsciiEngine** es un motor de renderizado ASCII 2.5D isométrico, open source (licencia **zlib**), multiplataforma (**Windows y Linux**), encapsulado como biblioteca estática (`.lib` / `.a`) en **C++20** sobre **Raylib** y diseñado con una arquitectura **100% Data-Driven**.

A diferencia de los roguelikes tradicionales donde cada carácter representa un icono plano sobre una casilla, **AsciiEngine trata los glifos ASCII como píxeles volumétricos (átomos)** proyectados en perspectiva isométrica 2:1. Cada objeto, personaje, material, sistema de partículas, superficie transitable, bioma y clima se define mediante plantillas JSON externas, logrando iluminación dinámica, brillo especular Blinn-Phong, sombras 2.5D por mapa de alturas, reflejos en agua, vegetación interactiva con viento y animaciones procedurales sin sprites ni texturas convencionales.

Todo el código fuente está auditado con un enfoque **didáctico**: comentado línea por línea para explicar las matemáticas, el diseño orientado a caché, la complejidad algorítmica y las decisiones de arquitectura detrás de cada subsistema.

---

### Novedades de `v1.1.0`

- **Configuración del Motor Expuesta y Presets (`Settings.hpp`):** API POD `EngineSettings` (`GetSettings`, `ApplySettings`, `ApplyQualityPreset` para `LOW` / `MEDIUM` / `HIGH` / `ULTRA`, y `LoadSettings` / `SaveSettings` con `user_settings.json`) para que cada juego exponga en su menú de opciones exactamente los ajustes que desee (bordes, respaldo volumétrico, influencia atmosférica, pasos de sombra, intensidad GI, reflejos, viento y densidad de follaje/partículas).
- **FX Efímeros y Brillo Especular:** Ráfagas `SpawnParticleBurst`, emisores temporales con `ttl` (`AddTimedParticleEmitter`), pisadas automáticas según terreno (polvo, nieve y salpicadura en agua), luces puntuales temporales con *fade-out* (`AddTemporaryLight`) y reflejos especulares Blinn-Phong rápidos gobernados por `roughness` y `reflectivity` de cada material.
- **API de Gameplay Desacoplada y Gamepad Nativo (`Input.hpp`):** Los controles de juego viven en el cliente (`sandbox/main.cpp`) y no dentro del motor. `Context` expone teclado, ratón, raycast del cursor al suelo (`GetCursorWorldPosition`) y soporte nativo multi-mando con *deadzone* radial, gatillos normalizados y velocidad analógica proporcional.
- **Guía para Artistas, Paridad Linux y CI:** Guía completa de arte JSON y limitaciones técnicas honestas ([`docs/ARTIST_GUIDE.md`](docs/ARTIST_GUIDE.md)), telemetría nativa de RAM privada en Linux (`/proc/self/statm`), búsqueda portable de fuentes `.ttf`, CMake híbrido (`FetchContent` automático de Raylib 6.2) e integración continua en Windows y Linux mediante GitHub Actions.

---

### Jerarquía de Prioridades

| Prioridad | Pilar | Estándar Técnico |
| :---: | :--- | :--- |
| **1** | **Optimización** | Cero asignaciones dinámicas en el bucle principal, Data-Oriented Design (SoA), Culling en 3 capas, `EffectLodState` adaptativo y **World-Locked ASCII Z-Buffer** en $O(N)$ (celdas de 16B alineadas a caché) con overdraw cero a $\ge 60$ FPS. |
| **2** | **Escalabilidad** | Arquitectura 100% Data-Driven y agnóstica al género. Crear nuevos materiales, objetos, pisos transitables, biomas, partículas o criaturas articuladas para fantasía, sci-fi o urbano requiere únicamente archivos JSON, sin recompilar C++. |
| **3** | **Estética** | Tipografía monoespaciada suavizada con LUT UV $O(1)$, Sub-Pixel Offset, respaldo volumétrico (Pase 0), fondo oscuro estilo terminal con modulación Día/Noche/Nieve continua, colorización de 3 paradas (`dark`/`normal`/`lit`), especular Blinn-Phong, siluetas por profundidad, sombras y reflejos. |
| **4** | **Funcionalidad y DX** | API encapsulada (patrón Facade + PIMPL verificado por CI), `EngineSettings` expuesto, gamepad nativo, **Editor de Mapas integrado con Raygui** (`F1`), Undo/Redo, Hot-Reloading (`F5`) y **Vista de Debug/Telemetría** (`F3`/`F4`). |

---

### Integración Rápida (Fachada + Gameplay API)

Cualquier proyecto externo enlaza `AsciiEngine` sin incluir jamás `raylib.h`, `raygui.h`, `json.hpp` ni `windows.h`:

```cpp
#include <AsciiEngine/Engine.hpp>

int main() {
    using namespace AsciiEngine;

    Context engine;
    if (!engine.Initialize(1600, 900, "Mi Juego ASCII")) {
        return -1;
    }

    // Carga ajustes de usuario o crea el perfil HIGH por defecto
    if (!engine.LoadSettings("user_settings.json")) {
        engine.ApplyQualityPreset(QualityPreset::HIGH);
        engine.SaveSettings("user_settings.json");
    }

    engine.LoadRegistry("assets/data");
    engine.LoadMap("assets/data/maps/forest_demo.json");

    while (engine.IsRunning()) {
        float dt = engine.GetFrameTime();

        // Movimiento encapsulado con teclado y stick analogico del gamepad
        Vec2 move = engine.GetGamepadLeftStick(0);
        if (engine.IsKeyDown(Key::W)) move.y -= 1.0f;
        if (engine.IsKeyDown(Key::S)) move.y += 1.0f;
        if (engine.IsKeyDown(Key::A)) move.x -= 1.0f;
        if (engine.IsKeyDown(Key::D)) move.x += 1.0f;
        engine.SetControlledMoveInput(move, engine.IsKeyDown(Key::LeftShift));

        // FX por raycast del cursor (rafaga + luz temporal)
        if (engine.IsMouseButtonPressed(MouseButton::Left)) {
            Vec2 aim = engine.GetCursorWorldPosition();
            engine.SpawnParticleBurst("impact_burst", { aim.x, aim.y, 0.45f }, 20);
            engine.AddTemporaryLight({ aim.x, aim.y, 1.1f }, { 255, 170, 80, 255 }, 2.4f, 10.5f, 1.5f);
        }

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
| **`WASD` / Stick Izquierdo** | Mover (velocidad analógica en mando) | Cámara libre |
| **`Shift` / `RT` / `L3`** | Correr | Cámara libre más rápida |
| **Rueda Mouse / Stick Der. Y** | Zoom (`0.95x` – `2.50x`) | Zoom / `Shift + Rueda` escala el pincel |
| **Clic Izq / Der / Medio** | Izq: Estallido + emisor temporal en cursor | Colocar o seleccionar / Borrar / Arrastrar cámara |
| **`Q` / `E` / Mando `A` o `X`** | `E` / `A` / `X`: Ataque + ráfaga + luz flash | `Q`/`E`: Rotar pincel `-15°` / `+15°` |
| **`TAB` / Mando `RB`** | Alternar personaje controlado | — |
| **`1` / `2` / `3` / `M` / `R`** | Sol / Tormenta / Ventisca / Bioma (`D-Pad Arriba`) / Auto-clima | Ajustable desde el Inspector Derecho |
| **`Izq` / `Der` / `Espacio`** | Hora (`D-Pad Izq/Der`) / Pausar (`Start`) ciclo | Ajustable desde el Inspector Derecho |
| **`Ctrl+Z` / `Ctrl+Y` / `Ctrl+S`** | — | Undo / Redo / Guardar mapa JSON |
| **`O` / `B` / `G` / `V`** | Outlines / Borde ASCII / Sub-Pixel / Backing | Igual |
| **`H` / `T` (`Y`) / `K`** | Sombras 2.5D / Antorcha del foco | Igual (`K`: Grid Snap) |
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

### Compilación (Windows y Linux)

CMake detecta automáticamente si existe un `third_party/raylib` precompilado local en Windows; en Linux o clones limpios descarga y compila **Raylib 6.2** automáticamente vía `FetchContent`.

#### Windows (Visual Studio 2022+ o CLI)
```bash
git clone [https://github.com/leopineda333/AsciiEngine.git](https://github.com/leopineda333/AsciiEngine.git)
cd AsciiEngine
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```
*(En Visual Studio: **Archivo → Abrir → Carpeta**, selecciona **`x64-Release`**, elige `Sandbox.exe` y pulsa **`F5`**.)*

#### Linux (Ubuntu / Debian)
```bash
sudo apt-get update && sudo apt-get install -y \
  build-essential cmake libasound2-dev libx11-dev libxrandr-dev libxi-dev \
  libgl1-mesa-dev libglu1-mesa-dev libxcursor-dev libxinerama-dev \
  libwayland-dev libxkbcommon-dev

git clone [https://github.com/leopineda333/AsciiEngine.git](https://github.com/leopineda333/AsciiEngine.git)
cd AsciiEngine
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/Sandbox
```

---

### Hoja de Ruta (Roadmap)

- [x] **Fases 0–8 (`v1.0.0`):** Núcleo isométrico 2:1, registros JSON, culling en 3 capas, World-Locked Sub-Pixel ASCII Z-Buffer, horneado Flyweight, colisiones 2D, ciclo Día-Noche + GI + sombras 2.5D por Heightfield, clima Markov por biomas, partículas SoA, personajes modulares procedurales, Editor Visual Raygui con Undo/Redo, auditoría didáctica línea por línea y lanzamiento bajo licencia zlib.
- [x] **`v1.1.0` (Actual):** `EngineSettings` expuesto + Presets de calidad + persistencia JSON, `SpawnBurst` y emisores con `ttl`, pisadas automáticas por terreno, luces temporales, especular Blinn-Phong (`roughness`), Gameplay API desacoplada + soporte nativo de Gamepad analógico, `docs/ARTIST_GUIDE.md`, paridad Linux y CI automático en Windows/Linux.
- [ ] **`v1.2.0` (Planeada):** Horneador de mallas `.obj` *low-poly* hacia `BakedPrefab`, sombras dinámicas ligeras para personajes y hooks opcionales de audio espacial.

### Licencia

**AsciiEngine** se distribuye bajo la licencia **zlib**, la misma licencia open source permisiva utilizada por Raylib. Eres libre de usarlo, modificarlo y distribuirlo en proyectos personales o comerciales. Consulta [`LICENSE`](LICENSE) para más detalles.

---

<div align="center">

**Author / Autor:** Leonardo Lopez Pineda (*Eryan*)  
*Built with C++20 & Raylib • Licensed under zlib*

</div>
