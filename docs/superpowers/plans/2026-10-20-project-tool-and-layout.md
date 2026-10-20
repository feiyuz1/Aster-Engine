# Project Tool and Repository Layout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Commit the independent project tool before reorganizing the engine repository.

**Architecture:** A small Windows program reuses SolutionGenerator and a filesystem-only SDK locator. Programs contains executable entry points, while existing engine services stay in src/include. Source and local generated files are migrated separately.

**Tech Stack:** C++17, Win32, CMake 3.21+, PowerShell, Visual Studio 2022.

**Spec:** docs/superpowers/specs/2026-10-20-project-tool-and-repository-layout-design.md

## Global Constraints

- Only reorganize the engine repository. Keep game project build/ and Source/ conventions.
- ProjectTool does not link the engine, SDL, OpenGL or ImGui.
- Keep existing tool target names, command arguments and engine service locations.
- User-level menu registration must preserve default applications and protect a newer installation from an older uninstaller.
- Build and test must not register real user shell menus.
- Do not rename active CMake caches into supposedly usable build directories. Preserve old files and rollback records.
- Existing features are already committed through 9b9934b. Keep the new tool and organization in separate commits.

## Review Focus

- Existing CMake cache points at another still-valid SDK: regenerate against the selected SDK (Task 1).
- Unicode/space paths, missing toolchains and invalid project files: visible diagnostics and nonzero exits (Task 1).
- Registration partially fails or an old installation unregisters a newer one: preserve the previous menu (Task 1).
- Moved documents contain relative links; local-only reports stay local (Task 2).
- Old build trees still provide dependencies to another worktree; migration must refuse unsafe moves (Task 3).

### Task 1: Standalone ProjectTool

**Files:** Programs/ProjectTool/{CMakeLists.txt,Main.cpp,ShellMenu.hpp,ShellMenu.cpp}; include/MiniEngine/Project/SDKPaths.hpp; src/Project/SDKPaths.cpp; tests/{ProjectToolTests.cmake,ProjectShellMenuTests.cpp}; existing SolutionGenerator, Scaffolder and root CMake.

**Interfaces:** `FindProjectSDK(std::filesystem::path)` returns SDK root or empty. `RegisterProjectMenu(HKEY, path, key)` / `UnregisterProjectMenu(HKEY, path, key)` return an operation result. CLI supports --generate-project, --register, --unregister, --quiet, --help; optional --sdk-root and --cmake allow explicit diagnostics and automation.

- [ ] Write functional tests for generation, A→B cache rebinding, Unicode/space paths, missing project/CMake and log creation; registry tests use a temporary HKCU subtree.
- [ ] Run tests against absent tool. Expected: FAIL because EXE does not exist.
- [ ] Extract SDK lookup, force TinyEngine_DIR as well as prefix, add standalone Win32 tool and reversible owned menu registration. No-argument invocation offers register/remove/cancel.
- [ ] Configure Programs/ProjectTool with tests and disconnected FetchContent; build Release and run CTest. Expected: all tests pass, no external libraries linked.
- [ ] Integrate SDK installation and editor development staging; rerun project creation and SDK/runtime checks. Expected: passing.
- [ ] Commit `feat: add standalone project solution generator and shell menu` before Task 2.

### Task 2: Tracked source and documentation organization

**Files:** Programs/CMakeLists.txt and four executable directories; Scripts/Build/ReleaseSmoke.ps1; docs/{README.md,Guides,Reference,Architecture,Reports}; .gitignore, README, CMake, Doxyfile and CI.

**Interfaces:** Existing CMake target names unchanged; root output paths for older targets remain compatible. Documentation links resolve after relocation.

- [ ] Move only the four executable entry sources, release smoke script and tracked current documents. Update file headers, CMake grouping, script root resolution and links. Leave historical plans/specs in place.
- [ ] Replace the document index with links to tracked current documents; preserve references to local history in a separate local archive index.
- [ ] Build all tools and editor; run commandlet validation and package smoke. Expected: unchanged CLI behavior.
- [ ] Commit `refactor: organize engine programs scripts and documentation`.

### Task 3: Build entry points and safe local cleanup

**Files:** CMakePresets.json; Scripts/Maintenance/OrganizeRepository.ps1; Scripts/Tests/RepositoryLayout.ps1; build docs, CI and gitignore. Track the three existing required regression sources and Ogg test fixture currently omitted from Git.

**Interfaces:** Presets put new builds in Intermediate/Build and downloads in Intermediate/Dependencies. A root-checked maintenance script defaults to preview, records moves, supports rollback, and refuses active or externally referenced caches.

- [ ] Add presets for editor and core CI, user override documentation, and no developer-specific dependency defaults.
- [ ] Add migration guard tests (wrong root, collision, foreign CMake HOME, dependency references) and observe failure before implementing the script.
- [ ] Build from the new paths with explicit local dependency overrides. Run all Debug tests and relevant Release external SDK/tool tests. Expected: all pass with no dependency on legacy build directories.
- [ ] Review all changes, fix important findings, commit separately. Keep only verified inputs in Git.
- [ ] Fast-forward the clean daily checkout to the committed result, configure its new build paths and verify tool/runtime smoke before archiving old caches.
- [ ] Preview then apply safe local moves with a manifest; regenerate the delivered SDK/editor and register the stable ProjectTool path. Verify registry command and actual generation through that command. Record any caches kept because another checkout still depends on them.

## Execution decisions

The user explicitly instructed proceeding: commit existing features before organization. Use inline execution and the existing managed worktree. No remote push. A final review is required; actual Explorer visual inspection is reported separately if native UI tools are unavailable.
