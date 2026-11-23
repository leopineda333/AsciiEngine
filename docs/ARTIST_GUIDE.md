# AsciiEngine — Guía de Arte Data-Driven (`v1.1.0`)

Esta guía explica cómo diseñar **materiales, objetos 3D, personajes articulados, biomas, superficies y partículas** usando exclusivamente archivos JSON en `assets/data/`, sin compilar ni modificar código C++.

---

## 1. Filosofía Visual y Sistema de Coordenadas

En **AsciiEngine** los caracteres ASCII no son iconos sobre una cuadrícula plana: son **átomos volumétricos** proyectados en isometría `2:1` y rasterizados sobre un Z-Buffer anclado al mundo.

### Ejes del Mundo
* **`+X`**: avanza hacia **abajo-derecha** en pantalla (frente por defecto de un personaje cuando `facingDeg = 0`).
* **`+Y`**: avanza hacia **abajo-izquierda** en pantalla (izquierda del personaje).
* **`+Z`**: apunta estrictamente **hacia arriba** (altura sobre el suelo `Z = 0`).

### Flujo de Trabajo Rápido (Hot-Reload)
1. Abre `Sandbox.exe` en modo juego o pulsa **`F1`** para entrar al **Editor Visual**.
2. Edita cualquier JSON en `assets/data/` con tu editor de texto favorito y guarda el archivo.
3. Vuelve a la ventana del motor y pulsa **`F5`** (*Hot-Reload*): las plantillas se hornean de nuevo al instante sin reiniciar la aplicación (y dentro del Editor `F1` conserva tus cambios en RAM).

---

## 2. Materiales (`assets/data/materials/materials.json`)

Un material define **cómo se lee la forma** (rampa de densidad ASCII), **qué paleta tiene** (3 paradas de color), **cómo se muestrea su superficie** (`REGULAR` vs `NOISY`) y **cómo reacciona a la luz** (`roughness` y `reflectivity`).

```json
{
  "id": "metal_polished",
  "asciiRamp": ".:-=+*#%@",
  "colors": {
    "dark":   "#1E2630",
    "normal": "#5E738A",
    "lit":    "#D2E4F5"
  },
  "gridMode": "REGULAR",
  "properties": {
    "roughness": 0.22,
    "reflectivity": 0.75,
    "occludesLight": true,
    "outline": true
  }
}
```

### Reglas de Diseño para Materiales
* **Orden de `asciiRamp`:** siempre de **menor a mayor cobertura de tinta** (ej. `".,:;+=x#%@"`). El motor elige el carácter izquierdo en sombra profunda y el derecho bajo luz directa o reflejo especular.
* **Paleta de 3 paradas (`dark` → `normal` → `lit`):** evita usar negro puro (`#000000`) en `dark`; usa sombras teñidas (ej. verde pino oscuro `#162B1E` o azul pizarra `#1E2630`) para que el volumen conserve identidad de noche y bajo lluvia.
* **`REGULAR` vs `NOISY`:**
  * `REGULAR`: rejilla exacta; ideal para arquitectura, madera tallada, acero y chasis sci-fi.
  * `NOISY`: aplica perturbación aleatoria determinista (`jitter` entre `0.0` y `0.5`) y porosidad (`density` entre `0.05` y `1.0`); ideal para copas de árboles, rocas erosionadas y pelaje.
* **`roughness` y `reflectivity` (especular Blinn-Phong en `v1.1.0`):**
  * Materiales mate (tela, tierra, corteza): `reflectivity: 0.0`, `roughness: 0.85–0.95`.
  * Metal, armaduras, cristales o superficies húmedas: `reflectivity: 0.45–0.85`, `roughness: 0.15–0.35`.
* **`occludesLight`:** ponlo en `false` para materiales emisivos (cristales brillantes, hologramas) o piezas de personajes que no deban proyectar sombra estática.
* **Diagnóstico rápido:** si escribes mal un `materialId` en cualquier objeto o personaje, el motor renderiza automáticamente el material **magenta brillante (`fallback_missing`)** para que detectes el error al instante.

---

## 3. Objetos Modulares (`assets/data/objects/*.json`)

Cada archivo `.json` en `assets/data/objects/` se registra y hornea automáticamente al arrancar. Un objeto se compone de una o varias piezas (`parts`) combinadas con primitivas SDF:

| `shape` | Dimensiones esperadas en `dimensions` | Uso recomendado |
| :--- | :--- | :--- |
| `ELLIPSOID` | `rx`, `ry`, `rz` (o `radius` uniforme) | Rocas, arbustos, copas frondosas, cúpulas, hongos. |
| `CYLINDER` | `radius`, `height` | Troncos, columnas, barriles, postes, torres. |
| `CONE` | `radius`, `height` | Pinos, techos cónicos, estalagmitas, carpas. |
| `BOX` | `width` (`x`), `depth` (`y`), `height` (`z`) | Muros, cajas, mesas, edificios, bloques sci-fi. |
| `ARCH` | `width`, `depth`, `height`, `openingWidth`, `openingHeight` | Pórticos, ruinas y puertas con pasillo central transitable. |

### Reglas de `step`, `windWeight` y Colisiones
* **Presupuesto de `step`:**
  * Objetos grandes (árboles, muros, arcos): `0.24` a `0.32`.
  * Props medianos (rocas, barriles, faroles): `0.18` a `0.24`.
  * **Nunca bajes `step` por debajo de `0.12`** en objetos grandes: multiplicará los átomos por 8× sin ganar nitidez en el Z-Buffer.
* **`windWeight`:** override por pieza entre `0.0` (tronco rígido) y `1.0` (copa flexible). El motor aplica flexión cuadrática (`altura²`), por lo que la base siempre queda anclada al suelo.
* **Colisionadores (`collider`):**
  * `NONE`: puramente decorativo.
  * `CIRCLE`: requiere `radius`.
  * `BOX`: requiere `width` y `depth` (soporta rotación libre en el mapa).
  * `ARCH_PILLARS`: requiere `width`, `depth` y `openingWidth` (genera automáticamente dos pilares sólidos dejando el centro libre).
* **Variación sin duplicar archivos:** puedes reutilizar un mismo prefab en el mapa cambiando `scale`, `rotDeg` y `overrideMaterialId` (por ejemplo, convertir una roca de granito en cristal helado o estatua metálica desde el Editor Visual `F1`).

---

## 4. Personajes Articulados (`assets/data/characters/*.json`)

El motor es **100% agnóstico a la anatomía**: no distingue humanos de lobos, arañas ni drones. Cada personaje es un árbol libre de `sections` enlazadas mediante `parentId`.

### El Concepto Más Importante: `localOffset` vs `pivotOffset`
* **`localOffset`:** dónde está ubicada la **articulación** (hombro, cadera, cuello, rotor) respecto a la articulación del padre.
* **`pivotOffset`:** hacia dónde se desplaza el **volumen geométrico** de esa pieza respecto a su propia articulación.

> **Ejemplo práctico (una pierna que cuelga de la cadera):**
> Si una pierna mide `size.z = 0.88`, pon `pivotOffset.z = -0.44` (la mitad de su altura hacia abajo). Así el pivote de rotación queda arriba en la cadera y la pierna oscila como un péndulo natural en lugar de girar por la rodilla.

### Pistas de Animación Procedural (`animTracks`)
Cada sección puede definir pistas para `IDLE`, `WALK`, `RUN` y `ATTACK` (si omites `RUN`, hereda `WALK` acelerado automáticamente; si omites `WALK`, cae a `IDLE`):

```json
"animTracks": {
  "WALK": {
    "rotAxis": "Y",
    "rotAmpDeg": 32.0,
    "frequency": 7.0,
    "phaseOffset": 3.14159
  }
}
```

* **Ejes de rotación local:**
  * `"Y"`: balanceo adelante-atrás (piernas, brazos, capas, inclinación al avanzar).
  * `"Z"`: giro horizontal (cabeza mirando a los lados, torsión de torso, colas, rotores).
  * `"X"`: balanceo lateral (alas, hombros, mecedora).
* **Extremidades opuestas:** usa `phaseOffset: 0.0` en la pierna izquierda y `phaseOffset: 3.14159` ($\pi$) en la derecha para que alternen en contrafase exacta.
* **Giro continuo:** agrega `"spinContinuous": true` para hélices, ruedas o anillos orbitales.
* **Criaturas voladoras:** define `"baseElevationZ": 1.6` en la raíz del personaje; automáticamente flotará sobre obstáculos bajos, sobrevolará lagos y no generará polvo de pisadas.

---

## 5. Biomas, Follaje y Superficies (`biomes/*.json` y `maps/*.json`)

* **Terreno del bioma (`ground`):** define `baseColor`, `altColor`, `glyphs` (ej. `".,'"`), `shadowGlyphs` (ej. `":;,"`) y `density` (`0.25–0.50` mantiene el estilo terminal limpio).
* **Distribución de praderas (`foliageDistribution`):**
  * `patchScale`: valores menores (`0.08–0.12`) crean praderas amplias; valores mayores (`0.25+`) crean islotes pequeños.
  * `patchSeparation`: umbral entre `0.0` y `0.9` que controla cuánto espacio vacío de tierra queda entre áreas de pasto.
  * `clumpStep`, `minBlades`, `maxBlades` y `clumpSpread`: controlan cuántas briznas nacen en cada mata y qué tan agrupadas están.
* **Superficies transitables (`surfaceZones` en el mapa):** usa `PLANKS_X` / `PLANKS_Y` para puentes y tarimas, `GRID` para patios de piedra o pisos metálicos, y `CHECKER` para salones. Activa `"walkableOverWater": true` en puentes para que tapen el agua y los reflejos debajo.

---

## 6. Partículas (`assets/data/particles/*.json`)

El mismo formato JSON sirve para efectos continuos (lluvia, ventisca, chispas de fogata) y para ráfagas efímeras (`SpawnBurst` en pisadas o explosiones):

* **`spawnHighVolume: true`**: nacen en lo alto sobre la cámara (lluvia, nieve, cenizas cayendo).
* **`gravityZ`**: negativo hace caer (`-8.5` impactos, `-3.2` polvo); positivo hace flotar (`+1.5` chispas, humo, luciérnagas).
* **`asciiOverLife` y `colorOverLife`**: listas de cualquier longitud que se interpolan desde el nacimiento (`0%`) hasta la muerte (`100%`) de cada partícula.
* **Pisadas automáticas (`v1.1.0`):** el motor dispara automáticamente `footstep_dust`, `footstep_snow` y `footstep_splash` según el terreno bajo los pies de cada criatura terrestre. Puedes reestilizar esos 3 JSON para adaptar las pisadas a tu bioma (ej. chispas eléctricas o esporas).

---

## 7. Limitaciones Técnicas Conocidas (Diseño Honesto)

Conocer los límites del renderizador ahorra horas de prueba y error:

1. **Sombras 2.5D por Heightfield Estático:** el mapa de sombras guarda una sola altura máxima $Z$ por columna del mundo estático. Por eso:
   * Un arco (`ARCH`) o estructura con techo proyecta sombra continua debajo del dintel.
   * Los personajes dinámicos en movimiento no escriben en el Heightfield (no proyectan sombra solar dinámica todavía).
   * Cuando modificas objetos en el Editor (`F1`), el Heightfield se reconstruye automáticamente.
2. **Sin Texturas UV ni Mallas `.obj` (hasta `v1.1.0`):** toda la geometría se construye combinando primitivas SDF (`ELLIPSOID`, `CYLINDER`, `CONE`, `BOX`, `ARCH`), modos `NOISY` y paletas de 3 paradas. El soporte para importar mallas *low-poly* `.obj` está contemplado para `v1.2.0`.
3. **Zero Overdraw = Sin Semitransparencias Apiladas:** cada celda del `AsciiZBuffer` conserva un único átomo ganador (el más cercano a la cámara). No es posible apilar múltiples capas translúcidas (como varios vidrios de colores uno detrás de otro); las partículas y reflejos usan mezcla directa con el fondo o submuestreo espacial.
4. **Resolución de Celda vs Detalle Microscópico:** con celdas por defecto de `5.5 × 9.5 px` (preset `HIGH`), detalles menores a `0.15` unidades de mundo suelen colapsar en la misma celda. Prioriza **siluetas claras y alto contraste tonal entre piezas** antes que micro-geometría densa.
5. **Presupuestos Fijos de Memoria (Garantía de 60 FPS):**
   * Pool de partículas SoA: tope estricto de **2,500 partículas simultáneas** (las nuevas se descartan si el pool se llena).
   * Luces puntuales temporales (`AddTemporaryLight`): tope de **32 luces efímeras simultáneas**.
   * Escala de personajes: mantener entre 6 y 16 secciones por criatura ofrece gran expresividad manteniendo el coste de cinemática directa por debajo de `0.05 ms`.