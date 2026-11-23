cmake_minimum_required(VERSION 3.20)
project(AsciiEngine VERSION 1.1.0 LANGUAGES C CXX)

# Estandar C++20 obligatorio en toda la solucion
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Detecta si AsciiEngine es el proyecto raiz o se consume via add_subdirectory / FetchContent
set(ASCIIENGINE_IS_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(ASCIIENGINE_IS_TOP_LEVEL ON)
endif()

option(ASCIIENGINE_BUILD_SANDBOX "Compilar el ejecutable demo Sandbox" ${ASCIIENGINE_IS_TOP_LEVEL})

# =========================================================================
# DEPENDENCIA PRIVADA: RAYLIB 6.2 (Precompilado local en Windows / FetchContent en Linux y CI)
# =========================================================================
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)

set(ASCIIENGINE_RAYLIB_INCLUDE_DIR "")

if(WIN32
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/raylib/CMakeLists.txt"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/raylib/lib/raylib.lib")
    message(STATUS "AsciiEngine: Usando Raylib precompilado local (Windows).")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/third_party/raylib")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/raylib/include")
        set(ASCIIENGINE_RAYLIB_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/raylib/include")
    endif()
else()
    find_package(raylib QUIET)
    if(NOT raylib_FOUND)
        include(FetchContent)
        message(STATUS "AsciiEngine: Obteniendo Raylib 6.2 desde GitHub via FetchContent...")
        FetchContent_Declare(
            raylib
            GIT_REPOSITORY https://github.com/raysan5/raylib.git
            GIT_TAG        6.2
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(raylib)
    endif()
endif()

# =========================================================================
# LIBRERIA ESTATICA DEL MOTOR (AsciiEngine.lib / libAsciiEngine.a)
# =========================================================================
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)

add_library(AsciiEngine STATIC ${ENGINE_SOURCES})

# Unicamente include/ es PUBLIC: garantiza el patron Facade + PIMPL.
target_include_directories(AsciiEngine
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# SYSTEM PRIVATE silencia warnings internos de raygui y nlohmann/json bajo -Wall -Wextra.
target_include_directories(AsciiEngine SYSTEM PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/raygui"
)

if(ASCIIENGINE_RAYLIB_INCLUDE_DIR)
    target_include_directories(AsciiEngine SYSTEM PRIVATE "${ASCIIENGINE_RAYLIB_INCLUDE_DIR}")
endif()

# En bibliotecas estaticas, PRIVATE oculta headers pero propaga el enlace binario al ejecutable final.
target_link_libraries(AsciiEngine PRIVATE raylib)

if(WIN32)
    target_link_libraries(AsciiEngine PRIVATE psapi)
endif()

if(MSVC)
    target_compile_options(AsciiEngine PRIVATE /W4 /utf-8)
else()
    target_compile_options(AsciiEngine PRIVATE -Wall -Wextra)
endif()

# =========================================================================
# EJECUTABLE SANDBOX (Consumidor puro via AsciiEngine/Engine.hpp)
# =========================================================================
if(ASCIIENGINE_BUILD_SANDBOX)
    add_executable(Sandbox "${CMAKE_CURRENT_SOURCE_DIR}/sandbox/main.cpp")
    target_link_libraries(Sandbox PRIVATE AsciiEngine)

    if(MSVC)
        target_compile_options(Sandbox PRIVATE /W4 /utf-8)
    else()
        target_compile_options(Sandbox PRIVATE -Wall -Wextra)
    endif()

    # Copia portable de assets/ junto al binario generado (Windows y Linux)
    add_custom_command(TARGET Sandbox POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:Sandbox>/assets"
    )

    # Fija el Working Directory de Visual Studio en la raiz del repo al pulsar F5
    set_target_properties(Sandbox PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()