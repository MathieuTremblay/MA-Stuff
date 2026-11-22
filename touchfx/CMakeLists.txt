cmake_minimum_required(VERSION 3.20)
project(TouchFX VERSION 0.5.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  # Static runtime: the .exe runs without installing the VC++ redistributable.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_options(/utf-8 /W3)
endif()

include(FetchContent)

option(TOUCHFX_SYSTEM_SDL2 "Use the system SDL2 instead of downloading it (Linux dev builds)" OFF)

if(TOUCHFX_SYSTEM_SDL2)
  find_package(SDL2 REQUIRED)
  set(TOUCHFX_SDL_LIBS SDL2::SDL2)
else()
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL2
    URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz)
  FetchContent_MakeAvailable(SDL2)
  set(TOUCHFX_SDL_LIBS SDL2::SDL2main SDL2::SDL2-static)
endif()

FetchContent_Declare(imgui
  URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.5.tar.gz)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_MakeAvailable(json)

# Embed the UI font into the executable so the app is a single portable .exe
set(TOUCHFX_FONT_SRC ${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf)
set(TOUCHFX_FONT_CPP ${CMAKE_CURRENT_BINARY_DIR}/font_data.cpp)
add_custom_command(
  OUTPUT ${TOUCHFX_FONT_CPP}
  COMMAND ${CMAKE_COMMAND} -DIN=${TOUCHFX_FONT_SRC} -DOUT=${TOUCHFX_FONT_CPP}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
  DEPENDS ${TOUCHFX_FONT_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake)

add_executable(TouchFX WIN32
  src/main.cpp
  src/ui.cpp
  src/state.cpp
  src/glutil.cpp
  src/effects.cpp
  src/fx_fluid.cpp
  src/fx_particles.cpp
  src/fx_kaleido.cpp
  src/fx_lasers.cpp
  src/fx_strobe.cpp
  src/fx_sparkles.cpp
  src/fx_ripples.cpp
  src/fx_ribbons.cpp
  src/fx_geometry.cpp
  src/fx_tunnel.cpp
  src/fx_lightning.cpp
  src/fx_plexus.cpp
  src/fx_fireworks.cpp
  src/fx_starfield.cpp
  src/fx_spectrum.cpp
  src/fx_grid.cpp
  src/post.cpp
  src/audio.cpp
  src/dmx.cpp
  src/dmx_apply.cpp
  src/sysstats.cpp
  src/ndi.cpp
  src/platform.cpp
  third_party/glad/src/gl.c
  ${TOUCHFX_FONT_CPP}
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)

target_include_directories(TouchFX PRIVATE
  src
  third_party/glad/include
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends)

target_compile_definitions(TouchFX PRIVATE
  TOUCHFX_VERSION="${PROJECT_VERSION}"
  IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

target_link_libraries(TouchFX PRIVATE ${TOUCHFX_SDL_LIBS} nlohmann_json::nlohmann_json)

if(WIN32)
  target_sources(TouchFX PRIVATE res/touchfx.rc)
  target_link_libraries(TouchFX PRIVATE ole32 shell32 ws2_32)
  target_compile_definitions(TouchFX PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
  if(MINGW)
    target_link_options(TouchFX PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
else()
  target_link_libraries(TouchFX PRIVATE ${CMAKE_DL_LIBS} pthread)
endif()
