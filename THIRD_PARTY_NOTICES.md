# Third-party notices

TinyEngine uses the following third-party projects. Their source code is either supplied through a local source tree, found as a system package, or populated by CMake FetchContent; it is not copied into this repository.

- SDL 2.32.x — zlib license — https://www.libsdl.org/
- Dear ImGui — MIT license — https://github.com/ocornut/imgui
- Assimp — BSD 3-Clause license — https://github.com/assimp/assimp
- stb / stb_vorbis — MIT license or public domain — https://github.com/nothings/stb
- FreeType — FreeType License or GPLv2 — https://freetype.org/ (required on non-Windows platforms)
- Jolt Physics — MIT license — https://github.com/jrouwe/JoltPhysics (optional)

Release builds must retain the corresponding upstream license texts for every enabled dependency. Assimp and Dear ImGui are editor/import-tool dependencies. SDL and stb_vorbis are used by the runtime; FreeType is used by non-Windows runtime builds; Jolt is included only when `TINYENGINE_ENABLE_JOLT=ON`.
