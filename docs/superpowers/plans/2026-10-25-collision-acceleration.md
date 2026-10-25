# Collision Acceleration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce collision mesh loading, triangle work, and scene candidate checks while preserving current collision/query behavior.

**Architecture:** Cache local collision geometry and triangle BVHs. Maintain separate static/dynamic scene BVHs with query-boundary ECS synchronization, and retain deterministic collision response ordering.

**Tech Stack:** C++20, existing CMake/CTest/MSVC targets, no new dependencies.

**Spec:** docs/superpowers/specs/2026-10-25-collision-acceleration-design.md

## Global Constraints

- Preserve query signatures and current static-static/trigger/joint event behavior.
- Remain in the current checkout; preserve unrelated SDK/bridge edits; no commits or dependency changes.
- Direct component edits, mounting, file edits, and world switches must not leave stale collision data.
- Keep current narrow-phase approximations outside conservative bounds corrections.

## Review Focus

- Reflection/nonuniform/zero scale must not cause tree pruning to drop real hits.
- Failed mesh loads, file replacement, and mounted-only policy must invalidate cached results correctly.
- Deleted/reused entities and collider/body-type changes must update both scene trees.
- Candidate deduplication must preserve collision-event lifecycles and response order.
- Large objects and X-aligned scenes must retain correct results with reduced bounds/triangle work.

---

### Task 1: Geometry caching and triangle BVH

**Files:** Create src/physics/physics_acceleration.h and .cpp; tests/physics_acceleration_test.cpp. Modify physics_world.h/.cpp, platform.h/.cpp, CMakeLists.txt.

**Interfaces:** PlatformFile::getReadCacheKey(path) returns a source-metadata/resolution fingerprint. PhysicsWorld::GetStatistics() returns cumulative PhysicsStatistics by value. Internal PhysicsAcceleration owns cached CollisionMesh objects and generic BoundsTree traversal.

- [x] Add physics regression target and real fixture tests; first prove the rotated mesh sphere contact fails against current bounds/query behavior. Record baseline timings for repeated mesh queries and X-aligned scene stepping.
- [x] Implement metadata identities and mesh caches; add BVH triangle traversal for rays, bounds overlap and sphere contacts. Use conservative world bounds and reuse mesh-sphere contact output.
- [x] Add cache/load-count and triangle-work assertions, including edits, failed-load recovery, mounts, negative/zero scales and nearest hits.
- [x] Build tinyengine_physics_acceleration_tests and run CTest tinyengine_physics_acceleration. Expected: all cases pass and repeated mesh queries do not reload geometry.

### Task 2: Scene spatial trees and event lookup

**Files:** Modify src/physics/physics_acceleration.h/.cpp, physics_world.h/.cpp, tests/physics_acceleration_test.cpp.

**Interfaces:** PhysicsAcceleration owns static/dynamic BoundsTree snapshots; synchronize from current ECS bounds before public queries. BoundsTree supports filtered overlap/ray traversal and bounds refitting by payload.

- [x] Add deterministic broadphase work assertions for a same-X separated scene and large static box. Verify failure on the old candidate enumeration.
- [x] Integrate scene trees into simulation pair generation, raycast, sphereCast and Overlap. Reuse unchanged trees and refit moved entries; rebuild membership changes and excessive degradation.
- [x] Preserve old simulation ordering with the original collider traversal and X-minimum-only ranks; use sorted pair membership for lifecycle events and an entity set for debug state.
- [x] Add static edits, body-type switches, entity deletion/reuse, cross-world queries, static-static triggers, masks and joints. Compare randomized queries/events to independently calculated AABB references.
- [x] Run physics, core, extended, gameplay and platform CTests. Expected: all pass and static tree build count is stable in unchanged scenes.

### Task 3: Benchmarks, documentation and review

**Files:** Update physics test benchmarks, docs/reference/collision_acceleration.md and this ledger.

- [x] Run deterministic work-count benchmarks and optional timing output, including oversized geometry; decide whether a global-object list is warranted.
- [x] Build all configured targets and run the configured Debug CTest suite. Record any unrelated failures explicitly.
- [x] Run formatting/diff checks and Doxygen; update documentation with actual results and query synchronization limits.
- [x] Request an independent review of the whole diff, fix material findings and rerun affected tests before claiming completion.

## Execution ledger

- Pre-flight: Task 1 produces geometry/cache/statistics and BoundsTree interfaces consumed by Task 2; Task 2 produces counters and scene behavior measured by Task 3. No conflicting interfaces found.
- Ruling: Execute inline in the existing checkout under the user's approval of the proposed sequence and the session's autonomy instructions. Keep unrelated edits and do not create a worktree or commit on main.
- Task 1: complete. Observed the original rotated-mesh contact regression fail, then passed the mesh/cache/mount/scale/contact suite after implementation. The Debug pre-change benchmark was 12112.2 ms for 40 mesh rays and 3471.73 ms for five same-X scene steps.
- Task 2: complete. Observed the scene-tree regression fail before integration; the full physics acceleration executable then passed scene mutations, event filtering, randomized pair/overlap and nearest ray/cast reference checks, and oversized geometry.
- Ruling: Keep the exact original ColliderComponent traversal and minimum.x-only sort comparator for response ranks, including tied bounds. No entity-ID tie breaking is introduced.
- Review: independent read-only reviewer found near-parallel ray pruning, inward-rounded transformed bounds, and raw/normalized path identity mismatch. Reproduced both ray/bounds failures in executable regressions, fixed all three, and obtained a clean source re-review. Windows accepts the unresolved path alias used by the portable path test; that test also covers platforms with stricter path resolution.
- Ruling: Store the implementation reference under docs/reference, matching the repository's tracked documentation policy. Whitelist only this task's two superpowers documents, leaving historical local records ignored.
- Task 3 benchmark: final Debug timings 7.2473 ms for 40 mesh rays, 36.5428 ms for five same-X scene steps, and 38.1216 ms with one oversized static box. No oversized-object list is warranted by this fixture. Actual work: one mesh load, 41 triangle visits including warmup; 21504 scene bounds predicates per step, rising to 21528 with the large box.
- Task 3 verification: all configured Debug targets built successfully. Full CTest first run: 41/42 passed in 568.60 s; tinyengine_core hit the command's 180 s limit and logged one SceneSerializer::Save assertion. With no code changes, the isolated core test passed in 197.34 s under a 600 s limit. The save failure did not recur; its cause is not established. All 42 tests have a passing execution this run.
- Final checks: modified C++ formatting and git diff --check passed. Doxygen reported no physics/platform warnings; four groups of existing parameter-documentation warnings remain in unrelated headers. Independent source re-review confirmed the reported findings are resolved. Pre-existing SDK/bridge edits remain unchanged; no commits or Unreal modifications were made.
- Submission: the user subsequently requested a local commit. Commit only the collision acceleration implementation, its tests and documentation; exclude the three pre-existing project SDK/bridge modifications. This request supersedes the earlier no-commit implementation constraint.
