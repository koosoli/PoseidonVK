# Modernizing the Poseidon Engine

## A Community Feasibility and Implementation Plan for CWR-CE

Date: July 2026

Audience: CWR-CE contributors, engine programmers, rendering programmers,
technical artists, tooling maintainers, and community members familiar with the
Poseidon engine lineage.

---

## Abstract

This document proposes a practical path for modernizing the CWR-CE Poseidon
engine through a combination of careful manual engineering and AI-assisted
"vibe coding". The goal is not to rewrite the engine from scratch. The goal is
to preserve the existing game simulation, content compatibility, mission logic,
and community knowledge while incrementally upgrading the rendering backend,
asset scalability, tooling, and developer feedback loops.

The most promising modernization target is a Vulkan rendering backend. Vulkan
can improve long-term maintainability, GPU resource control, rendering
observability, multi-platform viability, and future visual features. However,
Vulkan is not a magic switch. It will not automatically allow huge meshes,
4K textures, ray tracing, or modern post-processing unless the engine's resource
model and content pipeline are modernized at the same time.

The recommended approach is staged:

1. First make the shared engine backend-neutral.
2. Then add a minimal Vulkan backend that only opens, clears, and presents.
3. Then bring Vulkan raster rendering to parity with the current GL33 backend.
4. Then improve texture streaming, high-poly support, shader quality, tooling,
   and optional advanced features.

This is feasible because CWR-CE already contains several foundations that many
legacy engines lack: a backend factory, a dummy renderer, a separated GL33
backend, frame observation and validation infrastructure, screenshot hooks,
benchmarking, terrain profiling, and render-frame statistics.

---

## Intentions

The purpose of this modernization plan is to give the community a shared,
realistic engineering direction.

It is meant to answer:

- Can Poseidon be modernized without throwing away the existing game?
- What work should happen first?
- Which changes offer the best return on investment?
- Which ideas are feasible but should wait?
- Where can AI-assisted coding help, and where is manual engine judgement still
  required?
- How can contributors work in parallel without destabilizing the project?

The intent is not to chase buzzwords. Vulkan, ray tracing, HDR, bindless
textures, AI coding, and modern physics are only useful if they serve the
engine, the game, and the modding community.

The guiding principles are:

- Preserve gameplay behavior.
- Preserve existing content compatibility where possible.
- Keep GL33 as the reference renderer until Vulkan is proven.
- Build observability before complexity.
- Modernize boundaries before features.
- Use AI-assisted coding to accelerate audits, scaffolding, tests, and repetitive
  migrations, but keep architecture decisions deliberate and reviewed.

---

## Expected Gains and Return on Investment

### 1. Better Long-Term Rendering Foundation

The current GL33 backend works, but OpenGL gives limited explicit control over
memory, synchronization, upload scheduling, and pipeline lifetime. Vulkan gives
the engine clearer ownership over these systems.

Expected gains:

- More predictable GPU resource management.
- Better debugging with Vulkan validation layers and RenderDoc markers.
- Cleaner future support for modern rendering features.
- More explicit performance tuning.

ROI: High, but only after a disciplined backend-neutralization phase.

### 2. Stronger Developer Tooling

The project already has useful render-frame validation, screenshot hooks,
benchmark logging, terrain profiling, and RenderDoc trigger support. Extending
these systems will make rendering work safer and faster.

Expected gains:

- Faster regression detection.
- Easier comparison between GL33 and Vulkan.
- Better performance investigations.
- More confidence when changing old rendering code.

ROI: Very high. Tooling work should start before the Vulkan renderer is complex.

### 3. Higher-Resolution Texture Support

Modern assets need better upload scheduling, residency tracking, and memory
budgets. Vulkan can support this well, but the engine needs explicit streaming
policy.

Expected gains:

- Less visible texture pop-in.
- Practical support for larger textures.
- Better control over loading-screen versus in-game upload budgets.
- Better diagnostics when VRAM pressure is high.

ROI: High for modding and visual upgrades, but dependent on resource telemetry.

### 4. Higher-Polygon Content Support

Higher-poly models require more than a Vulkan backend. Current paths still use
16-bit indices in important places, and some shape/section logic retains legacy
limits.

Expected gains:

- Larger and more detailed models.
- Cleaner modern asset pipeline.
- Better batching and section management.

ROI: Medium to high, but requires asset-format and mesh-pipeline work. This is
not an automatic Vulkan win.

### 5. Visual Modernization

Once Vulkan raster parity exists, shader-side improvements can produce visible
quality gains.

Good candidates:

- Per-fragment lighting.
- HDR framebuffer.
- Filmic tone mapping.
- Better shadow filtering or cascaded shadows.
- Normal mapping where content supports it.
- Bloom after HDR.

ROI: High after renderer parity. Low if attempted before the backend is stable.

### 6. Optional Advanced Features

Ray tracing and Jolt physics can be valuable, but they should not be early
milestones.

Ray tracing is best treated as hybrid ray-query shadows or ambient occlusion,
not as a full path-traced renderer.

Jolt physics is best treated as a visual-only secondary physics sandbox for
debris or ragdolls, not as a replacement for Poseidon's gameplay simulation.

ROI: Potentially good later. Poor if started before core modernization.

---

## AI-Assisted "Vibe Coding" and Manual Coding

AI-assisted coding can make this modernization more achievable, especially for a
community project. It can help contributors move faster through large,
repetitive, and unfamiliar areas of the codebase.

Good uses of AI-assisted coding:

- Source audits for GL dependencies, raw handles, unsafe casts, and fixed-size
  limits.
- Creating first-draft scaffolding for new backend classes.
- Generating unit tests and source-boundary tests.
- Mechanical migrations such as moving GL helper headers.
- Writing CMake target skeletons.
- Summarizing call graphs and data flow.
- Creating stress-test fixtures.
- Drafting documentation for new engine contracts.

Risky uses of AI-assisted coding:

- Designing the renderer architecture without human review.
- Rewriting gameplay simulation.
- Changing serialization or networking paths casually.
- Introducing clever abstractions that do not match the existing engine style.
- Large refactors without tests.
- Performance-sensitive Vulkan synchronization without validation and review.

The best model is mixed:

- Use AI to explore, scaffold, and accelerate.
- Use experienced maintainers to decide boundaries.
- Use tests, screenshots, validation layers, and profiling to verify.
- Keep changes small enough to review.

In practice, this means a contributor can ask an AI coding tool to perform a
targeted task such as "find every shared-engine include of PoseidonGL33" or
"create a source audit test for GL helper leakage", but the resulting patch
should still be reviewed like any other engine change.

---

## Current Feasibility Assessment

### Backend Selection Is Already Present

CWR-CE already has a graphics backend registry. The game registers dummy and
GL33 backends, and the runtime `--render` option selects the backend.

This means Vulkan can fit into an existing pattern rather than requiring a new
application architecture.

Needed additions:

- `RegisterVulkanGraphicsBackend()`.
- A `PoseidonVK` target.
- Vulkan availability checks.
- Updated `--render` help text.
- Runtime logging of the selected backend.

Feasibility: High.

### A Dummy Backend Already Exists

The dummy backend is useful because it shows the minimum engine surface a
backend must satisfy. It is not a Vulkan renderer template by itself, but it is a
good scaffold for lifecycle coverage and no-op method completeness.

Feasibility value: High.

### GL33 Is Already a Separate Backend

The GL33 implementation lives in `engine/PoseidonGL33`, which is exactly the
kind of separation a Vulkan backend needs.

However, the separation is not perfect. Some shared engine code still knows
about GL33.

Feasibility: High, with Phase 0 cleanup required.

### Shared Engine Still Contains GL Coupling

There is a confirmed hard blocker in shared rendering code:

```cpp
auto* gl = static_cast<class TextureGL33*>(tex);
return gl->GetHandle() != 0;
```

This lives in FreeType atlas texture handling. A Vulkan texture passed through
this code would be undefined behavior.

Required fix:

- Add a backend-neutral virtual method on `Texture`, for example
  `HasValidGpuImage()`.
- Override it in `TextureGL33`.
- Use the base method from shared code.
- Add a source-audit test preventing `engine/Poseidon` from including
  `PoseidonGL33`.

Feasibility: High. Priority: Critical.

### GL Helper Headers Are in the Wrong Layer

Several `Graphics/Core/GL*.hpp` files include `glad/gl.h` and call GL directly.
They are mostly inline helper wrappers used by GL33, but their location under
shared core is misleading and unsafe for future backend work.

Required fix:

- Move these helpers under GL33 ownership, or clearly isolate them as GL-only
  implementation detail.

Feasibility: High. Priority: High.

### Render-Frame Observation Is a Major Asset

The engine already records draw information and builds frame-level observations
for validation. This can become the bridge between legacy scene submission and
modern backend execution.

Current issue:

- The captured draw records still contain GL-style raw handles such as VAO IDs
  and texture IDs.

Required fix:

- Replace raw backend handles in shared draw records with backend-neutral
  resource IDs.
- Let GL33 and Vulkan resolve those IDs internally.

Feasibility: Medium-high. Priority: Critical for Vulkan and future ray tracing.

### Render Conventions Are Favorable

Tests show that the current renderer uses modern-friendly conventions such as
zero-to-one depth and clockwise front face expectations.

This reduces porting risk, but Vulkan must still be verified with:

- Explicit `VK_FRONT_FACE_CLOCKWISE`.
- Minimal triangle test.
- UI/origin screenshot test.
- Depth test smoke scene.

Feasibility: High.

### Shader Tooling Is Partly Present

The project already depends on `glslang`, and tests compile existing GL33
shaders. This is a strong base for externalizing shaders and adding SPIR-V
generation.

Required fix:

- Move inline shader strings into shader source files.
- Keep GL33 validation.
- Add Vulkan SPIR-V build outputs.

Feasibility: High.

### High-Resolution Texture Support Is Feasible but Needs Policy

The GL33 texture bank currently has very small per-frame upload throttles.
Modern texture support needs upload budgets, staging, and telemetry.

Required systems:

- Per-frame upload byte counters.
- Allocation counters.
- Loading-screen upload boost.
- Active-gameplay upload budget.
- Texture residency stats.
- Vulkan staging upload path.

Feasibility: Medium-high.

### High-Polygon Support Needs a Mesh Pipeline Audit

The current rendering path uses 16-bit indices in important places. This limits
large meshes and must be handled before promising modern high-poly content.

Required systems:

- 32-bit index support.
- Per-mesh index format metadata.
- Backend draw path support for 16-bit and 32-bit indices.
- Asset importer/exporter rules.
- Tests for synthetic >65k-vertex meshes.
- Audit of shape section and material limits.

Feasibility: Medium. Worth doing, but not part of the first Vulkan clear-frame.

### Full Physics Replacement Is Not Recommended

Poseidon physics is intertwined with gameplay, vehicles, AI, camera behavior,
network state, and scripting. Replacing it wholesale would risk breaking the
game.

Selective visual physics is realistic:

- Debris.
- Destroyed object fragments.
- Ragdolls after death state is final.
- Secondary vehicle parts.

Feasibility of full replacement: Low.

Feasibility of visual-only Jolt sandbox: Medium.

---

## Modernization Roadmap

### Phase 0: Backend-Neutral Core and Better Observability

Estimated effort: 3-6 weeks.

Goal: Make the shared engine safe for multiple graphics backends.

This is the most important phase. It reduces risk for every later phase.

Tasks:

| Task | Why it matters |
| --- | --- |
| Remove `TextureGL33` dependency from shared FreeType text drawing | Prevents Vulkan texture crashes |
| Add `Texture::HasValidGpuImage()` or equivalent | Backend-neutral hot-reload validity |
| Replace raw GL handles in shared draw records | Required for Vulkan and ray tracing |
| Move GL helper headers out of shared core | Restores backend boundaries |
| Add backend boundary source-audit tests | Prevents GL leakage from returning |
| Extend render-frame stats | Gives GL33/Vulkan comparison data |
| Log selected backend and GPU clearly | Makes test logs readable |
| Externalize first shader pair | Proves shader pipeline direction |

Acceptance criteria:

- Shared `engine/Poseidon` does not include `PoseidonGL33` headers.
- GL33 still renders.
- Dummy backend still works.
- FreeType text survives texture reload.
- Render-frame logs show backend, pass count, draw count, and useful resource
  counters.

AI-assisted coding fit: Excellent. Many tasks are audit-heavy and testable.

Manual coding fit: Required for API boundaries and resource ID design.

### Phase 1: Minimal Vulkan Backend

Estimated effort: 2-3 weeks.

Goal: `--render vulkan` opens a window, clears to a color, presents, handles
resize/minimize, and shuts down cleanly.

Tasks:

| Task | Notes |
| --- | --- |
| Add `PoseidonVK` CMake target | Parallel to `PoseidonGL33` |
| Add Vulkan dependency path | Vulkan SDK or vcpkg |
| Register Vulkan backend | Code name: `vulkan` |
| Update CLI help | Include `vulkan` in `--render` text |
| Create SDL3 Vulkan surface | Use SDL's Vulkan surface support |
| Create instance/device/swapchain | Keep feature requirements minimal |
| Add validation layers in debug builds | Non-negotiable for early Vulkan |
| Add frame sync | Fences/semaphores |
| Handle window resize | Avoid swapchain lifecycle crashes |
| Implement lifecycle stubs | `Pause`, `Restore`, `ResetForRemount`, shutdown |

Acceptance criteria:

- `PoseidonGame --render vulkan` presents a known clear color.
- Startup and shutdown are validation-clean.
- Invalid Vulkan availability is reported clearly.

AI-assisted coding fit: Good for scaffolding and CMake.

Manual coding fit: Required for synchronization, swapchain correctness, and
validation cleanup.

### Phase 2: Vulkan Raster Parity

Estimated effort: 8-14 weeks.

Goal: Vulkan renders the game with functional parity to GL33.

Recommended milestones:

1. Frame constants and camera matrices.
2. Static mesh buffers.
3. TL draw path.
4. Texture creation and mip use.
5. Sampler and material state.
6. Render-pass/pipeline-state cache.
7. Terrain segments.
8. 2D, HUD, and text.
9. Shadow depth pass.
10. Screenshot parity harness.

Implementation recommendations:

- Use Vulkan Memory Allocator or equivalent allocation layer.
- Use persistent staging/ring buffers for dynamic data.
- Build pipelines from render-frame descriptors.
- Prewarm common pipelines on load.
- Keep GL33 as the visual reference.
- Run Vulkan validation continuously during development.

Acceptance criteria:

- Main menu renders.
- A simple mission renders.
- Terrain and models render.
- HUD/text render.
- Shadow pass works or has a clearly tracked temporary fallback.
- Screenshot tests can compare GL33 and Vulkan.

AI-assisted coding fit: Moderate. Useful for repetitive boilerplate and tests.

Manual coding fit: High. Rendering parity requires careful debugging.

### Phase 3: Texture Streaming and Resource Scale

Estimated effort: 4-8 weeks.

Goal: Make larger textures and heavier scenes practical.

Tasks:

| Area | Work |
| --- | --- |
| Upload budgets | Separate loading-screen, gameplay, and boost budgets |
| Staging uploads | Use Vulkan staging buffers and queue submissions |
| Residency telemetry | Track allocated bytes, queued bytes, evictions |
| Texture bank | Add Vulkan texture bank equivalent |
| Compression policy | Prefer GPU-native compressed formats where possible |
| Stress scenes | Test 2K/4K textures and many unique materials |

Acceptance criteria:

- Texture upload behavior is measurable.
- High-res assets do not cause unexplained stalls.
- Logs show whether the bottleneck is upload, allocation, decode, or residency.

AI-assisted coding fit: Good for telemetry and tests.

Manual coding fit: Required for performance tuning.

### Phase 4: High-Polygon and Asset Pipeline Work

Estimated effort: 4-8 weeks.

Goal: Allow modern content without breaking legacy content.

Tasks:

| Area | Work |
| --- | --- |
| Indices | Add 32-bit index support |
| Mesh metadata | Track index format per mesh |
| Backend draw path | Support `uint16_t` and `uint32_t` draws |
| Shape limits | Audit 256-sized scratch arrays and section assumptions |
| Asset tools | Document safe budgets and split rules |
| Tests | Add synthetic high-poly mesh tests |

Acceptance criteria:

- Legacy 16-bit meshes still render.
- Large 32-bit-index meshes render in Vulkan.
- Asset tools warn when content exceeds compatibility limits.

AI-assisted coding fit: Good for audits and test generation.

Manual coding fit: Required for asset compatibility decisions.

### Phase 5: Visual Modernization

Estimated effort: 3-6 weeks.

Goal: Improve visual quality once Vulkan parity is stable.

Recommended order:

1. Per-fragment lighting.
2. HDR framebuffer.
3. Filmic tone mapping.
4. Better shadow filtering or cascaded shadows.
5. Normal mapping where content supports it.
6. Bloom after HDR.

Why this order:

- Per-fragment lighting gives immediate visible improvement.
- HDR and tone mapping improve the whole frame.
- Better shadows likely provide more value than early ray tracing.
- Normal mapping only pays off if content supports it.

Acceptance criteria:

- Visual changes are optional or configurable.
- GL33 reference remains available.
- Screenshots demonstrate before/after gains.

AI-assisted coding fit: Moderate.

Manual coding fit: High for shader quality and art direction.

### Phase 6: Vulkan Scalability Features

Estimated effort: 4-8 weeks.

Goal: Reduce CPU overhead and improve large-scene rendering.

Candidates:

- Descriptor arrays / bindless-style texture indexing.
- `VK_EXT_descriptor_indexing` where available.
- Indirect drawing for section batches.
- SSBO instance transforms instead of 256-matrix UBO cap.
- Pipeline cache persistence.
- GPU timing queries.

These are valuable, but they should follow parity and measurement. Do not add
them just because Vulkan supports them.

### Phase 7: Hybrid Ray Tracing

Estimated effort: 8-16 weeks after raster parity.

Goal: Optional ray-query effects, not a full ray-traced renderer.

Best first target:

- Ray-query sun/contact shadows for opaque static meshes.

Prerequisites:

- Stable Vulkan backend.
- Backend-neutral mesh and texture IDs.
- Draw manifest with transforms and material flags.
- BLAS build path for static meshes.
- Per-frame TLAS build/refit path.
- Hardware capability detection.
- Non-RT fallback.

Avoid early:

- Full path tracing.
- Reflections.
- Transparent ray tracing.
- Animated/skinned BLAS rebuilds.

### Phase 8: Visual-Only Jolt Physics Sandbox

Estimated effort: 4-8 weeks for first slice.

Goal: Add secondary visual physics without changing gameplay authority.

Good first slice:

1. Spawn debris rigid bodies after explosions.
2. Simulate locally in Jolt.
3. Render debris as non-gameplay visual objects.
4. Destroy debris after a timeout.
5. Do not replicate or feed results back into gameplay.

Later:

- Ragdolls.
- Building fragments.
- Vehicle secondary parts.

Do not replace:

- Vehicle handling.
- Soldier movement.
- Bullet simulation.
- AI navigation.
- Network authority.

---

## Suggested Community Workflow

### Workstream A: Backend Boundary Cleanup

Best for contributors comfortable with C++ architecture and tests.

Initial issues:

- Remove `TextureGL33` from shared FreeType drawing.
- Move GL helpers out of shared core.
- Add source-audit tests.
- Introduce backend-neutral resource IDs.

### Workstream B: Tooling and Observability

Best for contributors who like diagnostics, tests, and infrastructure.

Initial issues:

- Extend render-frame stats.
- Add texture upload counters.
- Add backend/GPU logging.
- Improve screenshot comparison scripts.
- Add validation-friendly debug names.

### Workstream C: Vulkan Skeleton

Best for contributors with Vulkan experience.

Initial issues:

- Create `PoseidonVK` target.
- Build SDL3 Vulkan window/surface path.
- Implement clear/present.
- Add debug messenger and validation layer setup.

### Workstream D: Shader Pipeline

Best for rendering programmers and tooling contributors.

Initial issues:

- Extract shader sources from inline strings.
- Preserve GL33 shader validation.
- Add SPIR-V generation path.
- Add shader cache/build integration.

### Workstream E: Asset Scale

Best for model/tooling contributors and technical artists.

Initial issues:

- Audit 16-bit index assumptions.
- Document current mesh/section limits.
- Create synthetic high-poly fixtures.
- Define modern asset budgets.

### Workstream F: Documentation

Best for contributors who know the engine and can explain it.

Initial issues:

- Document backend contract.
- Document texture bank lifecycle.
- Document render-frame records.
- Document safe AI-assisted coding workflows.

---

## First Practical Milestone

The first milestone should not be "Vulkan renders the game". That is too large.

The first milestone should be:

> The shared engine has no GL33 dependency, GL33 still works, render-frame stats
> are improved, and a Vulkan backend can be registered without unsafe shared-code
> assumptions.

Concrete checklist:

- `engine/Poseidon` does not include `PoseidonGL33`.
- FreeType atlas texture validity is backend-neutral.
- GL helper wrappers are owned by GL33.
- `DrawItem` no longer exposes raw GL handles as the shared contract.
- `--render` logs the selected backend.
- A source-audit test protects the boundary.
- GL33 screenshots still pass.

This milestone is ideal for mixed AI-assisted and manual coding. It is
reviewable, testable, and immediately useful even before Vulkan draws a triangle.

---

## Final Recommendation

The Poseidon engine can be modernized incrementally. The strongest route is not
a rewrite, and not a rush toward ray tracing. The strongest route is to turn the
existing renderer split, frame validation, benchmark hooks, and GL33 backend into
a disciplined multi-backend architecture.

Vulkan is feasible and worthwhile, but the first value comes from making the
engine cleaner, more observable, and easier to reason about. Once that foundation
exists, Vulkan raster parity becomes realistic. Once Vulkan parity exists,
high-resolution assets, high-poly content, modern lighting, better shadows,
optional ray-query effects, and visual-only physics all become much safer to
pursue.

AI-assisted coding can help the community move faster, especially on audits,
scaffolding, test generation, and mechanical migrations. But the modernization
will succeed only if the community keeps the old engine's behavior respected,
the review process serious, and the roadmap staged around measurable progress.

The best first step is Phase 0. It is small enough to start now, valuable even
without Vulkan, and it unlocks every larger modernization goal.
