cmake_minimum_required(VERSION 3.20)
project(AsciiEngineWorkspace LANGUAGES CXX)

# Estándar C++20 obligatorio en toda la solución
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- 1. LIBRERÍA ESTÁTICA: AsciiEngine ---
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/include/*.hpp"
)

add_library(AsciiEngine STATIC ${ENGINE_SOURCES})

# PUBLIC: lo que ve Sandbox (solo nuestra fachada)
target_include_directories(AsciiEngine PUBLIC
    "${CMAKE_SOURCE_DIR}/include"
)

# PRIVATE: Raylib, JSON y Raygui quedan encapsulados dentro del motor
target_include_directories(AsciiEngine PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/raylib/include"
    "${CMAKE_SOURCE_DIR}/third_party/nlohmann"
    "${CMAKE_SOURCE_DIR}/third_party/raygui"
)

# --- 2. EJECUTABLE CONSUMIDOR: Sandbox ---
add_executable(Sandbox "${CMAKE_SOURCE_DIR}/sandbox/main.cpp")

target_link_directories(Sandbox PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/raylib/lib"
)

target_link_libraries(Sandbox PRIVATE
    AsciiEngine
    raylib.lib
    winmm.lib
)

# Fija el Working Directory de Visual Studio en la raíz del repo al pulsar F5
set_target_properties(Sandbox PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)