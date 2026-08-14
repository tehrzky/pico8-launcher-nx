cmake_minimum_required(VERSION 3.13)
project(pico8_launcher CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED ENV{DEVKITPRO})
    set(ENV{DEVKITPRO} /opt/devkitpro)
endif()
set(DEVKITPRO $ENV{DEVKITPRO})

list(APPEND CMAKE_MODULE_PATH "${DEVKITPRO}/cmake")
include(SwitchTools REQUIRED)

file(GLOB SOURCES "source/*.cpp")

add_executable(pico8_launcher ${SOURCES})
set_target_properties(pico8_launcher PROPERTIES SUFFIX ".elf")

target_include_directories(pico8_launcher PRIVATE
    include
    ${DEVKITPRO}/portlibs/switch/include
    ${DEVKITPRO}/libnx/include
)

target_link_directories(pico8_launcher PRIVATE
    ${DEVKITPRO}/portlibs/switch/lib
    ${DEVKITPRO}/libnx/lib
)

target_link_libraries(pico8_launcher PRIVATE
    SDL2_ttf
    SDL2_image
    SDL2
    EGL
    GLESv2
    glapi
    drm_nouveau
    freetype
    png
    jpeg
    webp
    curl
    mbedtls
    mbedcrypto
    mbedx509
    z
    nx
)

nx_create_nro(pico8_launcher
    ROMFS ${CMAKE_CURRENT_SOURCE_DIR}/romfs
)
