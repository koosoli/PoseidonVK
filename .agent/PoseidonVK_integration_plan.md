# PoseidonVK Integration Plan

## Purpose

This is the implementation plan for modernizing CWR-CE while preserving every
feature of the original game. It prioritizes verified parity, measurable
performance, maintainable direct Vulkan code, and additive modern features.

This document supersedes earlier feature inventories that described reference
code as directly portable. Reference projects are valuable design inputs, but
their code, licensing, maturity, and hardware assumptions vary substantially.

## Non-Negotiable Rules

1. Preserve original-game feature parity. Rendering, gameplay, AI, audio,
   scripting, networking, controls, and asset compatibility must not regress.
2. Keep GL33 as the default renderer and the visual reference until Vulkan
   parity is proven by representative automated and manual comparisons.
3. Keep shared `engine/Poseidon/` backend-neutral. Backend code owns Vulkan
   handles, synchronization, resources, and Vulkan-only optimizations.
4. Retain the GPL-3.0-or-later licensing model. Do not copy AGPL reference
   code into this project without an explicit licensing decision and review.
5. Treat hardware capabilities as tiers. Advanced hardware must enhance the
   renderer, never gate legacy-game rendering or gameplay.
6. Mark roadmap work complete only after its build, unit/contract tests, and
   appropriate capture, smoke, or behavior checks have passed.

## Current Baseline

- `PoseidonVK` is a pure C++ Vulkan backend which consumes the shared
  `Frame`/`FramePlan` pipeline alongside GL33.
- The current Vulkan requirement is Vulkan 1.3. The instance and physical
  device checks use `VK_API_VERSION_1_3`.
- Vulkan has real scene, texture, HUD, shadow, and six shader-family paths,
  but raster parity is not yet proven. The README still tracks missing visual
  captures and known material/fallback gaps.
- A legacy software-T&L fallback path currently has a sky/cloud/horizon order
  regression. Fixing that regression is the next renderer milestone.

## Reference Policy

### Upstream CWR-CE

**Role:** Original-game feature and GL33 behavior oracle.

- Compare gameplay, UI, scripting, asset behavior, and GL33 rendering against
  upstream before changing shared systems.
- Do not interpret matching directory trees or successful builds as proof of
  feature parity. Use behavior tests and representative Demo scenes.

### Wgpu Renderer Fork

**Role:** Primary modern-renderer design and dependency reference.

Adopt its proven concepts where they fit the FramePlan architecture:

- HDR scene target, exposure, bloom, tone mapping, and explicit UI composition
- depth/normal prepass, depth resolves, Hi-Z, and arbitrary-view culling
- retained static-scene data, GPU culling/LOD, and indirect draws
- water dependency order: waves first, prepass-dependent coast/refraction next,
  planar reflections only after arbitrary-view culling
- MSAA-aware forward rendering, alpha-to-coverage foliage, GPU timing, and
  feature-specific diagnostics

Do not copy its architecture into PoseidonVK:

- Do not add the Rust `cdylib`, C ABI renderer boundary, or Corrosion build
  dependency to the Vulkan backend.
- Do not bypass `SubmitFramePlan` with a second renderer-specific scene capture
  path. Extend shared inputs only when a semantic renderer requirement applies
  to more than one backend.

Some wgpu-fork documents remain future plans, not implemented source. In
particular, do not assume its Forward+, SSAO/GTAO, volumetric clouds, FSR,
mesh-shader stars, GPU terrain selection, or planar reflections are ready to
port without verifying the exact source and commit.

### FP_269_vk_WIP

**Role:** Behavioral and algorithmic reference only.

- Much of the renderer and tools carry `AGPL-3.0-or-later` SPDX headers and
  depend on FP-specific GPU abstractions, assets, formats, mesh shaders, sparse
  resources, and global state.
- Reimplement techniques from independent specifications, public papers, and
  vendor samples. Do not treat FP source as drop-in code.
- FSR 1 can be integrated from AMD's own appropriately licensed source, with
  its required notices, after the HDR/post-process foundation exists.
- Audit every third-party utility, shader include, generated star catalog, and
  asset separately before use.

### Binary Swimming Addon

**Role:** Gameplay behavior reference only.

The addon is a compiled PBO, not source code. Swimming must be designed and
implemented natively in the shared engine.

## Vulkan Capability Tiers

### Baseline: Vulkan 1.3

Required for current PoseidonVK. Supports original-game-compatible rendering,
legacy assets, HDR, normal graphics pipelines, and conventional descriptor
binding.

### Enhanced: Optional Feature Checks

Use runtime checks and fallbacks for descriptor indexing, non-uniform indexing,
buffer device address, indirect count draws, BC7 sampling, and advanced depth
resolve modes. These unlock bindless materials and scalable GPU-driven work but
must not replace conventional per-material draw submission.

### High-End: Optional Hardware

Mesh shaders, sparse residency, fragment barycentrics, specialized subgroup
features, and very high-cost reflections are optional. Provide conventional
vertex/index, billboard, or disabled-quality fallbacks.

## Phase 1 - Parity and Measurement Gate

This phase blocks broad renderer modernization.

1. Fix the legacy software-T&L fallback path.
   - Retain render descriptor data with fallback batches instead of flattening
     all geometry into one late depth-disabled screen list.
   - Schedule legacy sky, clouds, and clipped horizon before opaque foreground
     geometry as required by their descriptor semantics.
   - Keep true UI, HUD, and screen-space overlays late and depth-disabled.
   - Honor depth and blend behavior where the legacy descriptor requires it.
2. Close known raster gaps.
   - `texMat` UV scale
   - material alpha in shadow rendering
   - local-light/material parity
   - legacy texture fallback behavior
3. Add verification.
   - Deterministic GL33 versus Vulkan capture scenes for sky/clouds, terrain,
     horizon, water, cockpit, cutouts, overlays, HUD, and cascaded shadows.
   - Image tolerances plus frame observation records where exact matching is
     unrealistic.
   - Validation-clean smoke tests, RenderDoc labels, resize/device-loss checks,
     and CPU/GPU timing baselines.
4. Compare CSM behavior with GL33 before replacing its algorithm.

## Phase 2 - Renderer Foundation

Build the infrastructure that modern passes require before adding effects.

- Make `FramePassKind` execute real pass boundaries, attachment loads/clears,
  and resource transitions instead of flattening all draws into one swapchain
  render pass.
- Add offscreen image lifecycle, transient target allocation, image-layout and
  barrier tracking, and a small explicit pass scheduler.
- Add staged upload rings, residency/accounting telemetry, pipeline-cache
  diagnostics, and per-pass GPU timing.
- Add compute pipeline, storage-buffer, and synchronization infrastructure.
- Publish a device capability report that records format, MSAA, sampled-depth,
  descriptor-indexing, indirect-draw, and compression support.
- Add focused RAII helpers only as new images, buffers, descriptors, and pass
  resources need clear ownership. Do not perform a broad wrapper rewrite.

## Phase 3 - Asset and Material Foundation

This track can run in parallel with Phase 2.

- Add UInt16/UInt32 index metadata and draw support before importing modern
  models.
- Define backend-neutral mesh, texture, material, color-space, compression,
  sampler, and residency descriptors.
- Add upload budgets, staging paths, asynchronous loading where safe, and
  explicit fallback/transcode diagnostics for GL33 and Vulkan.
- Define PBR material semantics and color-space behavior before adding modern
  content; do not use specular-to-metal conversion as a runtime substitute.
- Add glTF import with fixtures, malformed-input handling, skeletal animation,
  and clear compatibility limits.
- Defer COLLADA until glTF covers the intended authoring workflow or a concrete
  user need remains.
- Add BC7 only after per-device format support and a fallback/transcode path
  exist. Audit compressor and decompressor licenses separately.
- Treat lip-sync generation and material conversion as independently licensed
  offline tools with generated-asset manifests.

## Phase 4 - HDR and Presentation

- Render the scene to a linear `R16G16B16A16_SFLOAT` intermediate target.
- Add exposure controls, bloom, tone mapping, presentation conversion, and an
  explicit UI-after-tonemap composition policy.
- Add night vision as a post-process effect in this pipeline.
- Define internal resolution, dynamic-resolution, and upscaling policy.
- Add FSR only after the policy and pass chain exist; preserve AMD licensing
  notices and compose UI at the correct resolution.

## Phase 5 - Modern Visual Base

- Improve CSM split distribution, transition blending, filtering, and bias.
- Add procedural clear sky, sun, atmosphere, aerial perspective, environment
  lighting, and ordinary instanced/billboard stars as a baseline.
- Add an MSAA-ready depth+normal prepass with identical transforms and
  alpha-cutout coverage to color rendering.
- Add sampleable depth/normal consumers: Hi-Z, SSAO/GTAO, contact effects, and
  depth-aware water coast rendering.
- Add Gerstner-wave water, soft coast/wet bands, refraction, and sky/environment
  reflections in that order. Do not make FFT ocean a baseline requirement.
- Add clustered Forward+ lighting after HDR. It may proceed alongside prepass
  work, but water must not wait for all Forward+ work.
- Design clouds after the sky interface is stable; add rain after particle and
  weather infrastructure is proven.

## Phase 6 - GPU Scale and Advanced Visuals

- Add descriptor-indexed/bindless material binding as an enhanced tier with
  conventional descriptor fallback.
- Add retained static-scene data, GPU frustum/distance culling, LOD selection,
  indirect draw generation, and Hi-Z occlusion.
- Keep arbitrary camera views first-class for shadows and eventual reflections.
- Move terrain or water CDLOD selection to the GPU only if profiling shows CPU
  selection is a real multiview bottleneck.
- Add GPU particles for smoke, fire, explosions, rain, and environmental FX
  after compute timing and synchronization are stable.
- Add volumetric clouds, full planar reflections, compute skinning, meshlet
  paths, and mesh shaders only as optional capability tiers.

## Phase 7 - Gameplay and Simulation

Renderer work must not block independent engine improvements, but each change
needs upstream behavior comparison first.

- Implement configurable multi-barrel weapons and vehicle autocannon reload
  rules where differential tests show they are absent.
- Audit existing amphibious physics, tide/wave sea level, commander turrets,
  and terrain/water lighting before claiming they require a port.
- Investigate and fix roof movement, vehicle-surface riding, and third-person
  camera collision with reproducible scenarios.
- Add swimming as new engine work: water-entry state, buoyancy/drag/thrust,
  actions and input, animation/config strategy, AI traversal, breath/drowning,
  weapon/action restrictions, entry/exit, and regression scenarios.
- Add proper 6DoF as separate work: manual camera roll, full spatial orientation
  commands, safe entity orientation rules, and controls that do not break
  ground-vehicle behavior.
- Keep animated loading screens as a UI/audio feature, not a renderer dependency.
- Keep Box3D in a separately profiled experimental presentation-physics track;
  it is not a prerequisite for renderer or gameplay parity.

## Phase 8 - Input, Platform, and VR

- Add SDL3 joystick/gamepad support and configurable mappings first.
- Add multi-monitor UI size, offset, and placement configuration.
- Add OpenXR only after stable pass timing, renderer capability negotiation,
  and stereo/multiview validation. Do not claim headset compatibility without
  device testing.

## Phase 9 - Audio

- Add asynchronous multi-channel radio/sample streaming with frame-pacing and
  overlap regression tests.
- Add loading-screen audio synchronization.
- Preserve existing OpenAL/VON behavior and original-game audio features.

## Completion Gate for Every Feature

Before marking a README item complete:

1. Build the relevant target and run applicable unit/contract tests.
2. Run validation and capture diagnostics for renderer changes.
3. Compare GL33, Vulkan, and upstream behavior when relevant.
4. Document hardware capability/fallback behavior for optional GPU features.
5. Update `README.md` to mark shipped work complete or add the missing shipped
   roadmap item.

## Notes for Agents

- Read source and record exact commits before relying on a reference project.
- Separate implemented behavior from design documents and release claims.
- Do not introduce Vulkan handles into shared engine records.
- Prefer the smallest correct implementation and profile before moving work to
  the GPU.
- Preserve original-game assets, data licenses, branding restrictions, and
  third-party attribution requirements.
