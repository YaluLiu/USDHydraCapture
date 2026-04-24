cmake_minimum_required(VERSION 3.16)
project(hydra_capture LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT pxr_DIR AND DEFINED ENV{PXR_DIR})
    set(pxr_DIR "$ENV{PXR_DIR}")
endif()

find_package(OpenGL REQUIRED)
find_package(pxr REQUIRED CONFIG)

add_executable(hydra_capture
    src/aov_output.cpp
    src/hydra_capture.cpp
    src/options.cpp
    src/renderer_config.cpp
)

target_include_directories(hydra_capture PRIVATE
    ${PXR_INCLUDE_DIRS}
)

target_link_libraries(hydra_capture PRIVATE
    arch
    cameraUtil
    gf
    glf
    hd
    hio
    js
    plug
    sdf
    tf
    usd
    usdGeom
    usdImagingGL
    vt
    OpenGL::GL
    ${CMAKE_DL_LIBS}
)

add_executable(hydra_capture_unit_tests
    src/aov_output.cpp
    src/options.cpp
    src/renderer_config.cpp
    tests/test_cli_config_paths.cpp
)

target_include_directories(hydra_capture_unit_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${PXR_INCLUDE_DIRS}
)

target_link_libraries(hydra_capture_unit_tests PRIVATE
    js
    tf
    vt
)

if(WIN32)
    target_link_libraries(hydra_capture PRIVATE
        user32
        gdi32
    )
    target_compile_definitions(hydra_capture PRIVATE NOMINMAX)
endif()
