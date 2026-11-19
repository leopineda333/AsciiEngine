/**
 * @file Engine.cpp
 * @brief Implementacion de la Fachada, carga de fuente con LUT O(1) y prueba visual Fase 1.
 */
#include <AsciiEngine/Engine.hpp>
#include <AsciiEngine/MathIso.hpp>
#include <AsciiEngine/MaterialSystem.hpp>
#include "raylib.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <vector>

namespace AsciiEngine {

    // Estructura privada que contiene todos los recursos de Raylib y subsistemas del motor.
    struct Context::Impl {
        bool initialized = false;
        int screenWidth = 1600;
        int screenHeight = 900;

        // Tipografia de alta resolucion con cache O(1) de coordenadas UV.
        Font renderFont = {};
        bool customFontLoaded = false;
        float glyphAspectRatio = 0.58f;
        std::array<Rectangle, 128> glyphLUT = {};

        // Subsistemas de Fase 1.
        std::string dataRootPath = "assets/data";
        MaterialSystem materialSystem;
        MathIso::IsoConfig isoConfig;

        // Estado interactivo del banco de pruebas de Fase 1 (luz orbital + intensidad).
        float lightAngleRad = 0.8f;
        float lightElevation = 0.65f;
        float lightIntensity = 1.0f;
        bool  autoOrbitLight = true;

        // IDs cacheados de los 5 materiales de demostracion.
        std::vector<MaterialId> showcaseMaterials;

        void LoadTypography() {
            // Prioridad de fuentes: primero fuente del proyecto, luego Consolas de Windows, luego default.
            const char* candidates[] = {
                "assets/fonts/SourceCodePro-Bold.ttf",
                "C:/Windows/Fonts/consolab.ttf",
                "C:/Windows/Fonts/consola.ttf"
            };

            customFontLoaded = false;
            for (const char* path : candidates) {
                if (FileExists(path)) {
                    // Horneamos a 36px con filtro bilinear para curvas suaves al escalar.
                    renderFont = LoadFontEx(path, 36, nullptr, 95);
                    SetTextureFilter(renderFont.texture, TEXTURE_FILTER_BILINEAR);
                    customFontLoaded = true;
                    TraceLog(LOG_INFO, "TYPOGRAPHY: Fuente cargada desde %s", path);
                    break;
                }
            }

            if (!customFontLoaded) {
                renderFont = GetFontDefault();
                TraceLog(LOG_WARNING, "TYPOGRAPHY: Usando fuente por defecto de Raylib.");
            }

            // Construimos la Lookup Table O(1) para los caracteres ASCII imprimibles (32..126).
            for (int c = 32; c < 127; ++c) {
                int idx = GetGlyphIndex(renderFont, c);
                glyphLUT[c] = renderFont.recs[idx];
            }
        }

        // Dibuja un glifo individual en O(1) respetando la proporcion real de la fuente.
        void DrawGlyph(char c, Vec2 screenPos, float heightPx, ColorRGBA col) const {
            unsigned char uc = static_cast<unsigned char>(c);
            if (uc <= 32 || uc >= 127) return;

            const Rectangle& src = glyphLUT[uc];
            float scale = heightPx / static_cast<float>(renderFont.baseSize);
            float destW = (src.width > 0.0f) ? (src.width * scale) : (heightPx * glyphAspectRatio);
            float destH = (src.height > 0.0f) ? (src.height * scale) : heightPx;

            Rectangle dest = { screenPos.x, screenPos.y, destW, destH };
            Vector2 origin = { destW * 0.5f, destH * 0.5f };
            DrawTexturePro(renderFont.texture, src, dest, origin, 0.0f, { col.r, col.g, col.b, col.a });
        }

        // Renderiza una esfera SDF de prueba para validar MathIso + MaterialSystem (REGULAR vs NOISY).
        void RenderMaterialTestSphere(Vec3 centerWorld, float radius, MaterialId matId, Vec3 lightDir) const {
            const Material& mat = materialSystem.Get(matId);
            const Vec3 radii = { radius, radius, radius };
            const float step = 0.24f;
            const float glyphHeight = 9.5f;
            const Vec2 screenOrigin = { screenWidth * 0.5f, screenHeight * 0.52f };

            // Recorremos Z, Y, X en orden ascendente: para una esfera convexa esto dibuja
            // de atras hacia adelante de forma natural (en Fase 2 esto pasara al AsciiZBuffer O(N)).
            for (float z = -radius; z <= radius; z += step) {
                for (float y = -radius; y <= radius; y += step) {
                    for (float x = -radius; x <= radius; x += step) {
                        Vec3 samplePos = { x, y, z };

                        // Si el material es NOISY, perturbamos la muestra y aplicamos densidad.
                        if (mat.gridMode == GridMode::NOISY) {
                            float hDensity = MathIso::Hash3D(x, y, z, 11.7f);
                            if (hDensity > mat.density) continue;

                            float jAmp = mat.jitter * step * 2.0f;
                            samplePos.x += (MathIso::Hash3D(x, y, z, 1.3f) - 0.5f) * jAmp;
                            samplePos.y += (MathIso::Hash3D(x, y, z, 2.7f) - 0.5f) * jAmp;
                            samplePos.z += (MathIso::Hash3D(x, y, z, 5.1f) - 0.5f) * jAmp;
                        }

                        // Evaluamos la SDF: solo dibujamos la cascara exterior visible (metric en [0.68, 1.0]).
                        float metric = MathIso::EllipsoidMetric(samplePos, radii);
                        if (metric > 1.0f || metric < 0.68f) continue;

                        // Normal analitica de la esfera y oclusion ambiental simple segun altura normalizada.
                        Vec3 normal = MathIso::EllipsoidNormal(samplePos, radii);
                        float ao = MathIso::Clamp(0.45f + 0.55f * normal.z, 0.25f, 1.0f);

                        // Iluminacion directa Lambert + minimo ambiental para ver siempre el color oscuro.
                        float diffuse = std::max(0.0f, MathIso::Dot(normal, lightDir)) * lightIntensity;
                        float luma = MathIso::Clamp((diffuse + 0.14f) * ao, 0.0f, 1.0f);

                        // Muestreamos glifo y color de 3 paradas desde el MaterialSystem.
                        char glyph = mat.asciiRamp.empty() ? '#' : materialSystem.SampleGlyph(matId, luma);
                        ColorRGBA col = materialSystem.SampleColor(matId, luma);

                        // Proyeccion 3D -> 2D isometrica 2:1.
                        Vec3 worldPt = { centerWorld.x + samplePos.x, centerWorld.y + samplePos.y, centerWorld.z + samplePos.z };
                        Vec2 isoPt = MathIso::WorldToIso(worldPt, isoConfig);
                        Vec2 screenPt = { screenOrigin.x + isoPt.x, screenOrigin.y + isoPt.y };

                        DrawGlyph(glyph, screenPt, glyphHeight, col);
                    }
                }
            }

            // Etiqueta del material debajo de cada esfera usando la fuente suavizada.
            Vec2 labelIso = MathIso::WorldToIso({ centerWorld.x, centerWorld.y, -radius - 1.6f }, isoConfig);
            Vec2 labelPos = { screenOrigin.x + labelIso.x, screenOrigin.y + labelIso.y };
            const char* modeTag = (mat.gridMode == GridMode::NOISY) ? "[NOISY]" : "[REGULAR]";
            std::string caption = mat.name + "\n" + modeTag;

            Vector2 textSize = MeasureTextEx(renderFont, mat.name.c_str(), 15.0f, 1.0f);
            DrawTextEx(renderFont, caption.c_str(), { labelPos.x - textSize.x * 0.5f, labelPos.y }, 15.0f, 1.0f, LIGHTGRAY);
        }
    };

    Context::Context() : m_impl(std::make_unique<Impl>()) {}
    Context::~Context() { Shutdown(); }

    bool Context::Initialize(int width, int height, const char* title) {
        m_impl->screenWidth = width;
        m_impl->screenHeight = height;

        SetConfigFlags(FLAG_WINDOW_RESIZABLE);
        InitWindow(width, height, title);
        SetTargetFPS(60);

        m_impl->initialized = IsWindowReady();
        if (m_impl->initialized) {
            m_impl->LoadTypography();
        }
        return m_impl->initialized;
    }

    bool Context::LoadRegistry(const std::string& dataRoot) {
        m_impl->dataRootPath = dataRoot;
        std::filesystem::path matPath = std::filesystem::path(dataRoot) / "materials" / "materials.json";

        bool ok = m_impl->materialSystem.LoadFromFile(matPath.string());

        // Registramos 5 materiales para el banco de pruebas de Fase 1.
        m_impl->showcaseMaterials = {
            m_impl->materialSystem.GetId("metal_iron"),
            m_impl->materialSystem.GetId("wood_bark"),
            m_impl->materialSystem.GetId("foliage_pine"),
            m_impl->materialSystem.GetId("stone_granite"),
            m_impl->materialSystem.GetId("snow_fresh")
        };
        return ok;
    }

    bool Context::IsRunning() const {
        return m_impl->initialized && !WindowShouldClose();
    }

    float Context::GetFrameTime() const {
        // Delegamos el delta time de alta precision a Raylib.
        return ::GetFrameTime();
    }

    void Context::Update(float dt) {
        // Sincronizamos tamano si el usuario redimensiona la ventana.
        m_impl->screenWidth = GetScreenWidth();
        m_impl->screenHeight = GetScreenHeight();

        // Controles del laboratorio Fase 1:
        // SPACE: pausar/reanudar orbita solar | IZQ/DER: girar luz | ARR/ABJ: intensidad | F5: Hot-Reload JSON.
        if (IsKeyPressed(KEY_SPACE)) m_impl->autoOrbitLight = !m_impl->autoOrbitLight;
        if (m_impl->autoOrbitLight)  m_impl->lightAngleRad += dt * 0.9f;
        if (IsKeyDown(KEY_LEFT)) { m_impl->autoOrbitLight = false; m_impl->lightAngleRad -= dt * 2.0f; }
        if (IsKeyDown(KEY_RIGHT)) { m_impl->autoOrbitLight = false; m_impl->lightAngleRad += dt * 2.0f; }
        if (IsKeyDown(KEY_UP))       m_impl->lightIntensity = MathIso::Clamp(m_impl->lightIntensity + dt * 0.8f, 0.1f, 1.5f);
        if (IsKeyDown(KEY_DOWN))     m_impl->lightIntensity = MathIso::Clamp(m_impl->lightIntensity - dt * 0.8f, 0.1f, 1.5f);
        if (IsKeyPressed(KEY_F5))    LoadRegistry(m_impl->dataRootPath);
    }

    void Context::Render() const {
        BeginDrawing();

        // Fondo gris oscuro neutro (RGB 30, 32, 36) base del motor.
        ClearBackground({ 30, 32, 36, 255 });

        // Calculamos la direccion unitaria de la luz de prueba.
        Vec3 lightDir = MathIso::Normalize({
            std::cos(m_impl->lightAngleRad),
            std::sin(m_impl->lightAngleRad),
            m_impl->lightElevation
            });

        // Dibujamos las 5 esferas alineadas horizontalmente en isometria (worldX = +offset, worldY = -offset).
        const int count = static_cast<int>(m_impl->showcaseMaterials.size());
        const float halfSpan = (count - 1) * 0.5f;
        for (int i = 0; i < count; ++i) {
            float offset = (static_cast<float>(i) - halfSpan) * 7.8f;
            Vec3 center = { offset, -offset, 0.0f };
            m_impl->RenderMaterialTestSphere(center, 2.3f, m_impl->showcaseMaterials[i], lightDir);
        }

        // HUD minimo de verificacion de Fase 1.
        DrawFPS(15, 15);
        DrawTextEx(m_impl->renderFont,
            TextFormat("Fase 1: MathIso + MaterialSystem (%zu materiales) | Luz: %.2f | [SPACE] Orbita | [Flechas] Luz | [F5] Hot-Reload",
                m_impl->materialSystem.Count() - 1, m_impl->lightIntensity),
            { 15.0f, 45.0f }, 16.0f, 1.0f, RAYWHITE);

        EndDrawing();
    }

    void Context::Shutdown() {
        if (m_impl && m_impl->initialized) {
            if (m_impl->customFontLoaded) {
                UnloadFont(m_impl->renderFont);
                m_impl->customFontLoaded = false;
            }
            CloseWindow();
            m_impl->initialized = false;
        }
    }

} // namespace AsciiEngine