# Poseidon Engine Modernization - Living Agent Instructions

## Meta-Guidance: This Is a Living Document

This document captures the default strategy, architectural preferences, and
workflow for this workspace. It is guidance, not a cage. If a future agent sees
a cleaner, safer, or more maintainable route, it should explain the tradeoff and
propose an update to this file.

## Core Project Goals

1. **Modernize for Vulkan:** Progressively move the CWR-CE Poseidon engine
   toward a clean, explicit Vulkan rendering backend.
2. **High Maintainability for Humans and AI:** Keep code boundaries explicit,
   modular, and easy to parse. Avoid dense or clever abstractions that make the
   old engine harder to reason about.
3. **Upstream Compatibility:** Preserve compatibility with the original
   Arma: Cold War Assault data layer and frame behavior where practical, so the
   fork can keep rebasing simulation, gameplay, and asset fixes from upstream.
4. **Long-Term Asset Evolution:** Build foundations that can eventually support
   modern Arma Reforger/Enfusion-style asset and modding workflows.

## Workflow Guidelines

### Planning and Tracking

For major changes, start with a short actionable plan and keep a local TODO list
if the work spans multiple turns. Break structural changes into micro-steps and
validate at natural checkpoints, especially after CMake changes, header moves,
or renderer-boundary edits.

### Model Routing

At the end of a work cycle, briefly advise whether the next subtask is suitable
for a cheaper mechanical model or should use a stronger reasoning model.

- **Stronger models:** Architecture, backend boundaries, resource lifetime
  contracts, synchronization, and review of previous commits for hidden drift.
- **Cheaper models:** Repetitive migrations, boilerplate, simple tests, and
  mechanical updates that already have a clear plan.

## Compilation and Smoke-Test Gates

Do not let large amounts of uncompiled code pile up. Suggest or run a build
after changing CMake targets, moving headers, externalizing shaders, changing
shared render contracts, or touching backend lifecycle code.

For local manual smoke tests in this workspace, always compile `PoseidonGameDemo.exe` at the directory `C:\Users\mail\OneDrive\Documents\GitHub\CWR-CE\build\codex-vk-dbg\apps\cwr\GameDemo` so that the user can manually smoke test. Do not build the full `PoseidonGame` executable unless the user explicitly asks for it.

When a functional milestone is reached, ask for or perform a manual smoke test
with the real executable and original game assets only when it adds useful
coverage beyond local `--check` runs. Manual smoke is most valuable after
runtime-renderer behavior changes, GL33 gameplay-risk changes, swapchain/window
lifetime changes, asset loading changes, crash fixes, or before a manual commit
intended for sharing. Routine source-only work, tests, docs, or internal
plumbing can usually rely on local build/unit/`--check` validation.

When handing a newly compiled `PoseidonGameDemo.exe` to the user for manual
smoke testing, always tell them what to test for specifically. Name the backend,
the expected visible behavior, and the most likely regression signs for the
recent change. Avoid vague requests like "please test it"; give a compact
checklist tied to the code that changed.

For Vulkan bootstrap smoke tests, be explicit about the current renderer stage.
Until Vulkan raster parity is implemented, `--render vulkan` should show only
the current bootstrap primitive or clear-color stage while game simulation or
audio continues. Do not ask the user to expect normal scene rendering from
Vulkan until the mesh, texture, material, and draw paths have actually landed.

## Regression-Proof Engineering Guidelines

1. **Test-backed iteration:** Extend tests when changing seams, backend-neutral
   contracts, shader source ownership, render-frame stats, or lifecycle logic.
2. **Boundary audits:** Keep shared `engine/Poseidon` free of backend-specific
   GL33/Vulkan headers and direct GPU handles.
3. **Observability:** Prefer explicit logging and frame-validation telemetry
   when a resource or frame invariant can fail at runtime.
4. **GL33 visual parity:** Treat GL33 as the reference renderer until Vulkan is
   proven. Backend-neutral work must not degrade GL33 correctness.

## Architectural Boundaries

- Shared core engine code under `engine/Poseidon/` should remain backend-neutral.
- Do not leak raw OpenGL handles (`GLuint`) or Vulkan handles (`VkBuffer`,
  `VkImage`) into shared frame structures.
- Prefer backend-owned resource IDs for shared frame records. Backends resolve
  those IDs to concrete GPU state at emit time.
- If a backend binds a fallback GPU resource, the captured shared resource ID
  must also describe that fallback. Do not capture an ID that resolves to a
  missing or stale GPU handle.
- Frame statistics should remain backend-neutral and should count resources from
  the immutable frame values, not by walking live backend objects.

## Modern Asset Integration Strategy

The long-term asset goal is to make this engine a credible open-source path for
modern Enfusion-style workflows while preserving legacy CWA/OFP compatibility.
Modern asset support should be implemented natively beside existing legacy
formats, not as ad hoc conversion steps scattered through renderer code.

- Prefer abstract loader interfaces such as `TextureBank` selecting between
  `PaaLoader`, `EddsLoader`, or future loaders based on source format.
- Keep parsers and asset metadata backend-neutral. They should produce explicit
  texture, mesh, material, and compression descriptors that any backend can
  inspect.
- Align modern formats with Vulkan capabilities where useful, such as BC7 block
  compression, descriptor arrays, bindless-style indexing, explicit residency,
  and staged upload paths.
- Keep GL33 insulated from Vulkan-only assumptions. When a modern asset feature
  cannot map directly to GL33, use a clear fallback, transcode path, emulation,
  or explicit diagnostic rather than leaking Vulkan requirements into legacy
  rendering.
- Treat new format support as an asset-pipeline contract first and a renderer
  optimization second. The parser should not depend on a specific backend, even
  when Vulkan is the primary target for full fidelity.

## Current Phase Notes

Phase 0 is closed out in `.agent/POSEIDON_PHASE0_CLOSEOUT.md`. Phase 1 now has
a working `PoseidonVK` bootstrap path: the backend registers, `--render vulkan`
opens an SDL Vulkan window, creates an instance/surface/device/swapchain,
clears/presents, draws a validation-friendly bootstrap triangle, reports
availability, and handles the first resize/swapchain lifecycle cases under
validation.

The recommended Phase 2 entry slice is deliberately small:

1. Keep GL33 as the reference renderer and continue manual GL33 smoke tests.
2. Add Vulkan diagnostics that make RenderDoc/validation captures readable.
3. Introduce backend-neutral frame/camera constants without leaking Vulkan
   handles into shared `engine/Poseidon` contracts.
4. Bind the uploaded frame constants from Vulkan shaders.
5. Only then start uploading real terrain/model/HUD resources.
