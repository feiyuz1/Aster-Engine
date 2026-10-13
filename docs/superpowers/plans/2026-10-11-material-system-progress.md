# SDD ledger — plan: docs/superpowers/plans/2026-10-11-material-system.md

Execution: native, authorized by the user's “开始” after receiving the written design and plan.
Workspace: C:/Users/feiyuz/.codex/worktrees/material-system/tinyengine
Branch: codex/material-system
Original checkout: D:/project/tinyengine (preserve; implementation occurs in the managed worktree).
Starting HEAD: 31e2c9f80a41b86fdef46b7c28be568900c4faef
Baseline: copied existing tracked working edits plus local assets/tests/docs; baseline verification pending.

## Pre-flight shared interfaces

- Tasks 1→2: MaterialAssetData and typed parameters → inheritance resolver. Consistent; v2 parsing and schema validation remain separate.
- Tasks 2→3: MaterialDefinitionData graph + parameter declarations → compiler. Consistent; retain v1 enum values and upgrade via explicit masks.
- Tasks 3→4: CompiledMaterial + MaterialTemplateSources + GeneratedShaderSources → GL cache. Consistent; new templates GLSL 430, legacy program remains compatible.
- Tasks 4→5: immutable MaterialRenderProxy + independent GL bindings → Renderer snapshots and batching. Consistent; binding identities must include actual values.
- Tasks 5→6→7: validator and draft commands → editor save requests → generation publication. Editor task uses injected requests until transactional apply is available in Task 7.
- Tasks 2/3/4/5→7: inheritance, compilation, programs, snapshots → atomic reload. Consistent; old values/code/layout are retained together.
- Tasks 3/4/7→8: template lookup, programs, generations → embedded fallback and package validation. Consistent; shared template includes enter cache and configure dependencies.

## Tasks

- Task 1: pending — typed contracts and legacy adapter.
- Task 2: pending — definitions, inheritance, dependency index.
- Task 3: pending — typed graph compiler.
- Task 4: pending — GL programs and shared templates.
- Task 5: pending — dynamic instances and consistent passes.
- Task 6: pending — material and graph editing.
- Task 7: pending — generation publication and reload rollback.
- Task 8: pending — packaging, examples and full verification.

## Evidence and decisions

- Ruling: Use a managed isolated worktree carrying the current working state — preserves uncommitted main-directory changes while implementing the authorized feature — cost if wrong: integration needs the recorded baseline snapshot, not a reset of the original checkout.
Baseline snapshot: bcf8424648860f16db21a45313afc365321891f4. Debug build succeeded; 8/9 CTest entries passed. Extended packaging test hardcodes build/Release but requires Debug build metadata: after building Release it fails expected Debug/got Release. This pre-existing fixture/configuration mismatch is recorded for final packaging verification.
Git baseline commit succeeded; automatic GC reports existing stray .meta refs in the shared original .git. Subsequent commits use per-command gc.auto=0; do not mutate shared refs.
Task 1 started: base bcf8424648860f16db21a45313afc365321891f4; initial tests use existing public behavior (transactional failure/unknown versions/duplicates).
Task 1 evidence: runtime red confirmed failed replacement/version/duplicates; typed API red confirmed missing GetMaterialData/Save. CPU and core green. Serialization helpers are in a dedicated MaterialTypes.cpp to keep public contracts light.
Task 1: complete (commits bcf8424..77d1330, tests: D:/CMake/bin/ctest.exe --test-dir build -C Debug -R '^(tinyengine_material_cpu|tinyengine_core)$' --output-on-failure → Total Test time (real) = 155.66 sec)
Task 2: Ruling: Config UTF-8 filesystem boundaries are included — material metadata in real Chinese directories failed at the existing narrow path conversion — cost if wrong: callers supplying locale-encoded paths would need UTF-8, matching asset path serialization. New behavior tested with physical Unicode directories, metadata and moves.
Task 2 evidence: missing MaterialSystem API red; mixed legacy/typed depth red then green; physical UTF-8 directory failure diagnosed at filesystem conversions. CPU/core/import final pre-commit run passed 3/3. Definitions, inherited values, root/context paths, reference collection and move rewriting are implemented.
Task 2: complete (commits 77d1330..3ca852a, tests: D:/CMake/bin/ctest.exe --test-dir build -C Debug -R '^(tinyengine_material_cpu|tinyengine_core|tinyengine_import_compatibility)$' --output-on-failure → Total Test time (real) = 175.79 sec)
Task 3 evidence: missing compiler API red; v1 arithmetic defaults and texture declarations red then green; numeric sampling and non-finite state red then green. CPU/core passed 2/2. MaterialGraph::Compile now returns shared-template surface/opacity functions and its existing core assertion uses that contract; EngineTests.cpp gets an exact Git whitelist. Builtin fixed PBR compilation preserves explicit per-map precedence over packed channels.
Task 3: complete (commits 3ca852a..5b92056, tests: D:/CMake/bin/ctest.exe --test-dir build -C Debug -R '^(tinyengine_material_cpu|tinyengine_core)$' --output-on-failure → Total Test time (real) = 156.89 sec)
Task 4: Ruling: Embed shared material shader sources during GL integration, ahead of Task 8 — legacy builtin-shader regression must keep working after extracting includes and later switching legacy materials to the common template — cost if wrong: Task 8 verifies this shared mechanism rather than introducing a second embedding path. Task 8 packaging/examples/cache checks remain outstanding.
Task 4 in progress: base 5b92056a795a9a1a124da76861eeec2e25488633. GPU test target and MaterialRenderTests added. Initial compile red on missing MaterialRenderProxy; actual OpenGL runtime red then confirmed value colors, UBO binding, texture semantics, fallback and complete units are missing. Readback hook reads scene HDR framebuffer; compile-count hook is TEMPORARY return 0 and must be replaced by cache counter before completion. Quad fixture uses existing OBJ-like .mesh syntax. GPU log has renderer/version (task-4-runtime-red.log).
MaterialTextureBinding/MaterialRenderProxy contracts and RenderItem.materialProxy added. Shared seven GLSL templates created. Legacy default.frag now includes shared Lighting.glsl; default.vert includes shared VertexTransform.glsl; math kept identical. CMake include expansion and embedded templates added via cmake/EmbedMaterialShaders.cmake. ShaderPipeline::LoadMaterialTemplates loads per-file external content or same-source embedded fallback, normalizes CR. GlMaterialProgramCache.hpp API declared; .cpp/actual GL binding and SdlGl integration NOT implemented yet. New CMake needs build verification after all changes.
Existing generated metadata assets/default.mat.meta was changed by registry scan during tests; do not stage it (restore from task baseline after tests as needed). Original D:/project/tinyengine still untouched.
Task 4 evidence: real GL values, padded UBO, sRGB/Linear, texture replacement, white/flat normal, once-only missing-texture warning and all 16 bindings pass. Existing render and embedded default shader regressions pass. Legacy reflection/locations cached until explicit shader reload; compressed texture decoding follows sampling semantic. Actual GL counter replaces temporary hook.
Task 4: complete (commits 5b92056..ba270db, tests: D:/CMake/bin/ctest.exe --test-dir build -C Debug -R '^(tinyengine_material_render|tinyengine_render_stability|tinyengine_render_builtin_shaders)$' --output-on-failure → Total Test time (real) =   3.76 sec)
Task 5 evidence: CPU setter/type/revision isolation and snapshot reuse pass; actual Renderer batch counts and entity destruction pass. GPU UV-hole Masked Base/Shadow, alternating cull states, current bones in both passes, compute-instancing rebinding, legacy/unified/node PBR <=1% HDR comparison pass on RTX4060. Full CPU/GPU/core/render passed 5/5 (167s). Real PBR GPU red exposed reserved GLSL packed identifier; renamed generated local. Batch hash accelerates lookup but full identities decide equality.
Task 5: complete (commits ba270db..e3aebf1, tests: D:/CMake/bin/ctest.exe --test-dir build -C Debug -R '^(tinyengine_material_cpu|tinyengine_material_render|tinyengine_render_stability|tinyengine_render_builtin_shaders)$' --output-on-failure → Total Test time (real) =   4.76 sec)
Task 6 evidence: missing editor module red; typed override/reset/invalid type, failed callback dirty retention, save/reopen, relative v2 child creation, explicit Vec4->RGB mask port validation, node/link deletion and real ImGui frame all pass. Editor executable built. Assets menu creates graph/instances; inspector opens typed module; legacy inspector retained. Dirty switching refuses to overwrite drafts; close and app exit offer Save/Discard/Cancel. Temporary Task6 save callbacks are replaced by transactional publication in Task7.
Task 6: complete (commits e3aebf1..a13985a, tests: D:/CMake/bin/ctest.exe --test-dir build -C Debug -R '^(tinyengine_editor_interaction|tinyengine_material_cpu)$' --output-on-failure → Total Test time (real) =   0.81 sec)
Task 7 evidence: missing Apply/Reload API red. CPU inherited publication/schema changes/deleted parameters/first-load rejection/invalid external edits/ResourceManager valid-data retention pass. Real GLSL rejection rolls back all snapshots and disk; repair/default changes publish without extra program compiles; same-path texture content gains an independent immutable version. Graph, v2 and legacy editor drafts now share atomic write/publication and GL validation. SourceReader allows legacy candidates to be validated from memory, including parent candidates. Latest CPU/GPU/editor 3/3 pass; prior complete core/render group 6/6 (176s) passed. Ctrl+R explicitly reloads selected materials.
Task 7: complete (commits a13985a..2b9c787, tests: D:/CMake/bin/ctest.exe --test-dir build -C Debug -R '^(tinyengine_material_cpu|tinyengine_material_render|tinyengine_editor_interaction)$' --output-on-failure → Total Test time (real) =   2.36 sec)
Task 8 in progress: missing WriteCache API red. Source/layout/dependency cache includes all six generated variants and full collision-check content; offline graph/material CPU validation added to compile and package staging. Embedded shader generation includes all transitive material directory sources and configure dependencies. Isolated no-template GPU test passes. Real ImGui workflow creates/saves parent, red/blue instances, assigns scene, updates defaults/dynamic color, rejects/repairs graph and packages scene; workflow passes. Extended suite exposed stale graph sampler contract assertion plus two compatibility defects: legacy project-relative references on asset moves and implicit alpha lost by adapter; corrected with regression checks. Packaging fixture now uses actual config target path. Final all-target verification remains outstanding.
Task 8 cache repair: full-suite diagnostics identified generated-source write failure; a real Windows reader holding content.key reproduced deterministic RED. WriteCache now compares all existing file content and only writes changes; locked identical cache reuse/timestamp test and CPU suite GREEN. Full suite rerun follows.
Task 8: complete (commits 2b9c787..c60e93f, tests: D:/CMake/bin/ctest.exe --test-dir build -C Debug --output-on-failure → Total Test time (real) = 221.54 sec)
Final review: one fresh gpt-6-astra reviewer, range bcf8424..3efc47e. Eight Important findings accepted after code verification: legacy resolved rendering, persisted schema inactivity, negative dependency recovery, portable inherited texture overrides, custom-root aliases, obsolete GPU/CPU cache lifetime, reload/document coherence, mixed legacy-v2 inheritance. Minor deferred: declaration range editor controls.
Final: Ruling: Existing IBL physical accuracy — preserve the specified baseline while sharing lighting code; cost if wrong: existing lighting inaccuracies remain a separate task.
Final: Ruling: Arbitrary legacy fragment execution — preserve the fixed-program/custom-uniform compatibility contract; cost if wrong: legacy shader files do not become independent programs.
Final: Ruling: Exact Alpha shadows and previous-frame bone velocity — retain the explicitly scoped approximations; cost if wrong: those effects remain approximate.
Final: Ruling: Preexisting whole-cache budget eviction — keep that mechanism, fix new generation-retention pressure; cost if wrong: unrelated budget-edge behavior still requires separate validation.
Final: minor (deferred): Parameter range authoring remains file-based; declaration panel lacks optional min/max controls.
Final fix 5: custom-root assets alias RED→GREEN, including mounted child/parent/texture; full suite pending after fix pass.
Final fix 3: parent-only recovery after failed first child load RED→GREEN; retain attempted dependency paths, refresh failed proxies without blocking healthy parent publication.
Final fix 8: legacy child of typed instance/graph RED→GREEN, merge effective parent representation then apply present legacy fields subject to explicit false flags.
Final fix 2: restart and offline compiler after schema invalidation RED→GREEN. Persist validated_override type provenance in v2 children with original values retained, and roll back grouped file writes on failure; genuine unknown first-load overrides still fail.
Final fix 4: actual ImGui inherited texture Override checkbox then save/package with source directory unavailable RED→GREEN; serialize child-relative texture paths.
Final fix 7: actual Ctrl+R key workflow then Save for clean instance/graph RED→GREEN; dirty reload preserves draft, updates discard baseline and blocks stale overwrite with explicit conflict.
Final fix 1: real HDR values/textures/instancing, actual reflected uniform readback, and cast-before/clip-after alpha shadow in static/instanced paths RED→GREEN. Legacy Base consumes effective snapshots; Shadow uses shared compiled opacity/geometry.
Final fix 6: obsolete CPU description weak lifetime and real glIsProgram/glIsTexture repeated 24-version stress RED→GREEN; preserve held snapshots, retire unused objects at frame boundaries, weak CPU dedup and bounded inactive-program reuse.
Final fix 2 expanded coverage: registered descendants not loaded in the scene RED→GREEN via dependency closure; locked dependent provenance write also verifies rollback of parent/children and live generation.
Final fix 5 nested-prefix extension: explicit configured logical asset root API compilation RED→GREEN; mounted Data/Content child/parent/texture pass. Engine supplies project.assetDirectory alongside physical asset root, preserving multi-level archive keys.
Final: fixed 1 legacy shader snapshot rendering — TestLegacyResolvedPasses HDR/instancing/texture/uniform/shadow RED→GREEN, suite 12/12.
Final: fixed 2 persisted schema invalidation — TestReloadPublication fresh system/offline compilation/registered unloaded descendant RED→GREEN and grouped-file rollback check, suite 12/12.
Final: fixed 3 failed-first-load descendants — TestFailedParentRepair RED→GREEN, suite 12/12.
Final: fixed 4 inherited texture portability — actual ImGui Override then save/package/source removal RED→GREEN, suite 12/12.
Final: fixed 5 configured asset-root aliases — custom and nested logical root child/parent/texture RED→GREEN, suite 12/12.
Final: fixed 6 obsolete generation resources — TestCpuGenerationLifetime and TestGpuGenerationLifetime weak ownership / glIsProgram / glIsTexture RED→GREEN, suite 12/12.
Final: fixed 7 stale Ctrl+R drafts — MaterialExternalReload actual key workflow / clean save / dirty conflict / discard baseline RED→GREEN, suite 12/12.
Final: fixed 8 mixed inheritance — TestLegacyChildOfTypedParent typed and graph parent RED→GREEN, suite 12/12.
Final verification: all Debug targets built; serial full CTest 12/12 passed (205.67 sec) at functional HEAD 036de0e. Latest workflow package launched from MaterialNoShaders for 3 frames, draw calls 2, exit 0, embedded shader fallback confirmed. Original main-directory edits preserved; named managed worktree retained for integration choice.
