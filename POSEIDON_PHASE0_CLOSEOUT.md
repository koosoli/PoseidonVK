# Poseidon Modernization Phase 0 Closeout

Date: July 2026

This note records what was completed for Phase 0 of the Poseidon modernization
plan, what was validated, and what should happen next before the project moves
into Phase 1.

## Scope

Phase 0 in the modernization report is the backend-neutral core and
observability pass. The goal was not visible rendering improvement. The goal
was to remove backend-coupling blockers, tighten boundaries, and make later
Vulkan work safer.

## Completed Work

The following Phase 0 tasks are complete in the repository:

- Removed the shared-engine `TextureGL33` dependency from FreeType text drawing.
- Added a backend-neutral texture validity hook via `Texture::HasValidGpuImage()`.
- Moved GL-only helper headers out of the shared Poseidon core and under GL33
  ownership.
- Added a source-audit test that prevents shared `engine/Poseidon` code from
  including `PoseidonGL33` headers.
- Replaced shared raw GL-shaped draw-record fields with backend-neutral mesh and
  texture resource IDs.
- Added GL33-local mesh and texture registries that resolve those resource IDs
  back to live backend objects at emit time.
- Extended render-frame statistics to track unique textures, unique vertex
  buffers, and unique index buffers separately.
- Added clearer startup logging for the requested backend and active renderer.
- Externalized the first shipped GL33 shader pair into `.glsl` files and added a
  configure-time shader embedding path for the main shader bank.
- Updated shader audit coverage so shipped GL33 shader sources are compiled by
  glslang from their current source-of-truth locations.

## Validation Completed

Phase 0 closeout validation completed with the current debug build:

- Backend boundary audit passed.
- GL33 shader audit passed.
- Render-frame statistics tests passed.
- Graphics backend factory and dummy-backend tests passed.
- Z-bias backend contract tests passed.
- GL33 shutdown and font-cache teardown audits passed.

Acceptance bundle result:

- 27 test cases passed.
- 1280 assertions passed.
- 0 failures.

The following observability points are also present in source:

- Startup log records the requested backend and chosen renderer.
- Render-frame logging records pass count, draw count, max draws in pass,
  texture count, vertex-buffer count, index-buffer count, and GL error count.

## What Phase 0 Changed in Practice

This phase mainly changes internal contracts and diagnostics.

Expected behavior:

- GL33 should continue to render the same content.
- The dummy backend should still work.
- Shared rendering code is no longer allowed to depend directly on GL33 types.
- Future backends can consume the shared frame contract without inheriting raw GL
  handle assumptions.

Non-goals of Phase 0:

- No intentional visual upgrade.
- No intentional framerate uplift.
- No Vulkan renderer yet.

If a local runtime smoke test looks the same as before, that is the expected
result.

## Recommended Next Steps Before Phase 1

The next work should stay narrow and reduce risk for the first Vulkan slice.

1. Perform one manual GL33 runtime smoke test from the built executable.
2. Capture one startup log and one render-frame log sample as a baseline for the
   Vulkan bring-up phase.
3. Review the resource-ID contract once more before introducing a second real
   backend, especially around lifetime, reuse, and teardown behavior.
4. Decide the Vulkan dependency path now: SDK-only, vcpkg, or dual-path.
5. Keep further shader externalization optional unless it directly supports the
   first Vulkan shader/tooling slice.

## Suggested Phase 1 Entry Slice

When Phase 1 starts, the smallest good first step is:

1. Add a `PoseidonVK` target.
2. Register a `vulkan` backend in the existing backend factory.
3. Make `--render vulkan` report availability clearly.
4. Open a window, clear to a known color, present, resize, and shut down cleanly
   under validation.

That is the right next stage. More GL33 refactoring is not required to close
Phase 0.