// Copyright (c) 2026 Leonardo Lopez Pineda
// SPDX-License-Identifier: Zlib
/**
 * @file PlatformMemory.cpp
 * @brief Consulta aislada de memoria RAM privada del proceso dependiente del sistema operativo.
 *
 * @details Se aisla en su propia unidad de traduccion porque windows.h define macros globales
 * (como Rectangle, CloseWindow, ShowCursor o DrawText) que chocan directamente con los simbolos
 * de raylib.h. En Linux lee /proc/self/statm usando solo POSIX y la biblioteca estandar.
 */
#include <AsciiEngine/DebugHUD.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <fstream>
#include <unistd.h> // sysconf(_SC_PAGESIZE)
#endif

namespace AsciiEngine {

    float QueryProcessPrivateMemoryMB() {
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS_EX pmc = {};
        pmc.cb = sizeof(pmc);

        if (GetProcessMemoryInfo(GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
            sizeof(pmc))) {
            return static_cast<float>(pmc.PrivateUsage) / (1024.0f * 1024.0f);
        }
        return 0.0f;
#elif defined(__linux__)
        // /proc/self/statm expone 7 contadores en paginas:
        //   size resident shared text lib data dt
        // (resident - shared) aproxima las paginas privadas activas en RAM del proceso.
        std::ifstream statm("/proc/self/statm");
        if (!statm.is_open()) {
            return 0.0f;
        }

        unsigned long totalPages = 0;
        unsigned long residentPages = 0;
        unsigned long sharedPages = 0;
        if (!(statm >> totalPages >> residentPages >> sharedPages)) {
            return 0.0f;
        }

        long pageSizeBytes = sysconf(_SC_PAGESIZE);
        if (pageSizeBytes <= 0) {
            return 0.0f;
        }

        unsigned long privatePages = (residentPages > sharedPages)
            ? (residentPages - sharedPages)
            : residentPages;
        double privateBytes = static_cast<double>(privatePages) * static_cast<double>(pageSizeBytes);
        return static_cast<float>(privateBytes / (1024.0 * 1024.0));
#else
        return 0.0f;
#endif
    }

} // namespace AsciiEngine