# Collision acceleration design

## Intent and approved approach

The user approved the proposed sequence after reviewing the local UE 5.3.2 Chaos implementation: cache collision meshes and build triangle BVHs, then introduce separate static/dynamic scene trees. Oversized-object lists are conditional on measurements. This is an independent implementation; no Unreal source is copied.

## Goals and constraints

- Preserve public query signatures, layer/trigger/joint filtering, collision response ordering, and Enter/Stay/Exit behavior, including existing static-static events.
- Share cached local-space geometry between colliders using the same path. Detect loose-file edits, mounted-file edits, mount/policy changes, and working-directory changes without reading unchanged mesh contents.
- Use triangle BVHs for mesh raycasts, mesh-versus-bounds, and mesh-sphere contacts. Keep current narrow-phase approximations for other shape pairs.
- Use separate scene AABB trees for static and non-static bodies. Reuse unchanged trees and refit changed bounds; rebuild on membership changes or severe tree degradation.
- Direct ECS component edits remain supported. Synchronize scene bounds at query boundaries; this still requires an O(n) component scan because ECS component pointers have no mutation revisions.
- Bounds must conservatively enclose rotated, translated, reflected, nonuniformly scaled and degenerate shapes. Missing transforms must not create phantom colliders.
- Expose cumulative profiling counters for real load attempts, bounds checks, triangle tests, and tree builds. clear() releases caches and resets counters.
- Remain in the current checkout and preserve the pre-existing project SDK/bridge edits. No dependency changes, commits, or Unreal modifications.

## Alternatives

1. Improve the existing X-axis sweep: small change but retains its poor behavior for objects aligned along X and does not accelerate mesh triangles.
2. Cached geometry plus triangle and scene BVHs: selected, addresses both the expensive geometry work and spatial distribution without a third-party backend.
3. Port Chaos or enable another physics backend: substantially different scope and behavior; not selected.

## Architecture

An internal physics acceleration module owns a generic median-split AABB tree, cached mesh vertices/triangles, and scene snapshots. Scene and triangle trees share traversal and refitting code. Mesh-node bounds are transformed conservatively into world space during traversal, so zero and negative scales do not require an inverse transform.

A small PlatformFile cache identity API fingerprints resolution context and source metadata, including mounted archives/directories. Mesh loading continues to go through PlatformFile and MeshResource.

PhysicsWorld owns acceleration data lazily with copy-on-write state, retaining its existing value semantics. Public queries synchronize current ECS bounds before traversing a tree. Simulation collects candidate pairs from a single bounds snapshot, orders them using the exact original collider traversal and X-minimum-only sort, and runs existing filtering/response. Mesh-sphere contact data is reused for response; event lookup uses sorted sets instead of repeated linear searches.

## Verification and limits

Add an isolated physics regression executable with generated fixtures: nearest-hit/reference comparisons, mesh cache edits/mount switches, rotated/scaled meshes, trigger lifecycle/filtering, scene mutation/entity removal, dense X alignment, and large colliders. Assert work counters rather than wall-clock thresholds; print optional benchmark timings for the current machine.

Run the existing core, extended, gameplay and platform tests, then the configured CTest suite. Retain full logs under ignored build/artifact directories. Run formatting and Doxygen checks when available.

BVHs do not eliminate genuinely overlapping pairs or a single giant triangle. No sleep system, precise sphere/capsule shape pairs, or backend integration is added. Add a global oversized-object list only if the large-object benchmark demonstrates a need.
