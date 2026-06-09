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
    src/hydra_capture_engine.cpp
    src/image_output.cpp
    src/lidar_overlay.cpp
    src/lidar_point_cloud.cpp
    src/main.cpp
    src/options.cpp
    src/renderer_settings.cpp
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

if(WIN32)
    target_link_libraries(hydra_capture PRIVATE
        user32
        gdi32
    )
    target_compile_definitions(hydra_capture PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
