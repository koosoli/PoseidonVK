# PoseidonVK

Arma: Cold War Assault - Remastered - Community Edition modernization fork.

[![Sponsor on GitHub](https://img.shields.io/badge/Sponsor-GitHub%20Sponsors-ea4aaa?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/koosoli)
[![Buy Me a Coffee](https://img.shields.io/badge/Buy%20me%20a%20coffee-support-ffdd00?logo=buymeacoffee&logoColor=black)](https://buymeacoffee.com/koosoli)

`koosoli/PoseidonVK` is a modernization-focused fork of the community
[`ofpisnotdead-com/CWR-CE`](https://github.com/ofpisnotdead-com/CWR-CE)
codebase, which continues Bohemia Interactive's official source release for the
classic Poseidon engine behind *Arma: Cold War Assault* / *Operation Flashpoint:
Cold War Crisis*.

PoseidonVK tries to stay compatible with upstream CWR-CE for as long as
practical while deliberately diverging where modernization requires a different
architecture. The goal is not to replace the upstream project. The goal is to
explore a more explicit, backend-neutral, modern engine direction while keeping
the original game and data formats alive.

This project is not an official Bohemia Interactive product.

## Current Preview

![PoseidonVK Vulkan smoke-test screenshot showing the experimental procedural sky and volumetric clouds](Screenshots/3.png)

Current Vulkan 1.3 smoke-test screenshot using separate demo game data. The
Vulkan backend is in active raster-parity work and can render the full game
scene with known gaps (see Phase 2 below).

### Experimental Vulkan Presentation

The Vulkan backend has an opt-in world-composition path for local smoke tests:

- Cached HDR sky map driven by the simulation's sun, moon phase, star rotation,
  fog, and weather state. It includes a portable deterministic night-star field
  rather than importing an unlicensed reference catalogue.
- Persistent absolute-world 3D cloud density, distance, and double-buffered
  light volumes with simulation-time wind displacement, half-resolution
  raymarching, temporal reconstruction, and terrain cloud shadows. Enable it with
  `POSEIDON_VK_VOLUMETRIC_CLOUDS=1`; it retains cockpit, binder, HUD, and other
  legacy display-referred content after world composition.
- Isolated FP16 world target, ACES tone mapping, bloom, and stable angular eye
  exposure. GPU-only temporal adaptation is available only with
  `POSEIDON_VK_TEMPORAL_EXPOSURE=1`; it remains under tuning. Legacy briefing
  pages, cockpit, HUD, and other display-referred UI are composed after tone
  mapping.
- Exposure uses stable camera-to-sun angular metering. Foreground-object sun
  occlusion remains future work because it needs a validated projected
  visibility mask rather than a centre-depth approximation.

This path remains experimental. It does not yet provide a dedicated
low-resolution bloom chain, lens flare, or dynamic resolution.
GPU timing shows the compositor costs about 0.06 ms in the Demo while world
rasterization and clouds dominate, so the current frame rate is not expected to
match the renderer maturity of the reference GL33 path or related renderer forks.

## Modernization Goals

The long-term goal is to turn the classic Poseidon codebase into a clear,
observable, backend-neutral rendering and asset platform that can support both
legacy Cold War Assault content and modern asset workflows.

Current priorities include:

- **GL33 reference renderer:** Keep the existing OpenGL 3.3 renderer working as
  the visual baseline while other backends are developed.
- **Backend-neutral core:** Remove OpenGL-specific assumptions from shared
  engine code so future backends do not inherit GL-era coupling.
- **Vulkan path:** Prepare for a first-class Vulkan backend with explicit
  resource ownership, validation, frame synchronization, and modern GPU
  features.
- **Render observability:** Improve render-frame validation, logging,
  diagnostics, and telemetry so renderer changes can be tested instead of
  guessed.
- **Modern asset pipeline:** Add native loader paths for modern formats beside
  legacy formats, with backend-neutral descriptors that Vulkan can use fully and
  GL33 can fall back from cleanly.
- **Upstream compatibility where practical:** Keep syncing with upstream CWR-CE
  possible when it does not block this fork's modernization goals.

## Roadmap

This roadmap is intentionally staged. GL33 remains the reference renderer until
new backends prove parity, and each phase should keep the game buildable and
smoke-testable.

### Phase 0 - Backend-Neutral Core And Observability

- [x] Remove direct shared-engine dependency on GL33 texture types.
- [x] Add backend-neutral texture validity checks.
- [x] Move GL-only helper code under GL33 ownership.
- [x] Add source-audit tests that keep shared `engine/Poseidon` code free of
  GL33 implementation headers.
- [x] Replace raw GL-shaped draw-record fields with backend-owned resource IDs.
- [x] Add GL33-local mesh and texture resource resolution at emit time.
- [x] Extend render-frame statistics for textures, vertex buffers, and index
  buffers.
- [x] Externalize the first GL33 shader sources and audit them with glslang.
- [x] Fix GL33 texture replay/fallback handling found during demo smoke testing.
- [ ] Keep running manual GL33 demo smoke tests as review feedback lands.

### Phase 1 - Minimal Vulkan Backend

- [x] Add a `PoseidonVK` target.
- [x] Register a `vulkan` backend in the existing backend factory.
- [x] Make `--render vulkan` parse cleanly and report loader/device availability
  while `auto` continues to prefer GL33.
- [x] Create a Vulkan bootstrap renderer with an SDL Vulkan window, instance,
  surface, physical-device selection, and logical device.
- [x] Add a minimal Vulkan swapchain.
- [x] Clear to a known color, present one frame, and shut down cleanly.
- [x] Add a Vulkan debug messenger and validation-layer diagnostics.
- [x] Harden basic resize and swapchain recreation under validation layers.

### Phase 2 - Vulkan Raster Parity

- [x] Add validation/RenderDoc-friendly names and labels to Vulkan bootstrap
  objects and the clear-present command path.
- [x] Add a tested Vulkan frame-constants bridge for camera, viewport, clip
  range, world rect, and fog values.
- [x] Create swapchain image views, render pass, and framebuffers, then clear
  through the render-pass path.
- [x] Add the first empty Vulkan pipeline layout for bootstrap graphics
  pipeline bring-up.
- [x] Add the first Vulkan bootstrap triangle shader sources and compile-check
  them under Vulkan GLSL rules.
- [x] Draw a validation-clean bootstrap triangle through the Vulkan render-pass
  path.
- [x] Lock the Vulkan bootstrap push-constant byte layout to the shader
  contract with tests.
- [x] Route the backend-neutral frame plan into Vulkan frame constants and use
  the viewport path for the bootstrap primitive.
- [x] Upload Vulkan frame constants into a backend-owned uniform buffer and
  descriptor set.
- [x] Upload bootstrap vertex and index buffers through the Vulkan buffer
  helper and draw the primitive with `vkCmdDrawIndexed`.
- [x] Upload frame lighting state and per-draw constants into Vulkan-owned
  buffers and descriptors.
- [x] Add tested render-pass descriptor to Vulkan raster, depth, and blend
  state translation helpers.
- [x] Add the first Vulkan scene shaders and a scene pipeline that consume
  camera, projection, and per-draw world constants through a tested push
  constant, and draw a lit mesh through the descriptor-set path.
- [x] Drive fog from the uploaded frame constants in the scene fragment shader
  (vertex-computed fog factor mixed toward frame.fogColor).
- [x] Drive directional sun lighting from the uploaded frame constants in the
  scene shaders.
- [x] Add first frame-global active local light extraction and scene-shader
  consumption for Vulkan point/spot lighting bring-up.
- [ ] Refine Vulkan local lighting toward per-object light lists,
  material-scaled colors, and GL33 visual parity.
- [x] Drive the Vulkan scene draw loop from the backend-neutral frame plan,
  issuing one indexed draw per recorded command with per-draw SSBO world
  matrices (bring-up buffers shared until real mesh upload lands).
- [x] Upload static and dynamic mesh buffers through backend-owned resources.
- [x] Implement texture creation, sampler state, mip use, and fallback behavior.
- [x] Build complete Vulkan scene pipeline state from the existing render-pass
  descriptors.
- [x] Render main menu overlays, HUD, lines, polygons, and text via the 2D screen pipeline.
- [x] Add multitexturing support to the scene pipeline: detail, grass, water-bump, and
  specular textures loaded from `CfgDetailTextures`, bound to descriptor set 2, and
  consumed in the fragment shader via per-draw `ShaderFamily` dispatch.
- [x] Implement all six ShaderFamily branches in `scene.frag.glsl`: Normal (single-texture
  lit), Shadow (black silhouette), Water (bump-normal specular), Detail (AT2X blend),
  Grass (rgb modulate + alpha coefficient), and Flat (single-texture lit, no normal).
- [x] Propagate `SetGrassParams` from engine to `FrameConstantsVK` grass uniform and
  implement `TextBankVK::InitDetailTextures` / `StartFrame` parity with GL33.
- [/] Render terrain, models, sky, water, cockpit, and shadow passes in the 3D scene
  pipeline. (Multitexturing, shader-family dispatch, detail/grass/water textures, and
  viewport/winding fixes have landed; UV scale (`texMat`) and per-object material alpha
  in shadow pass are known remaining gaps.)
- [ ] Fix legacy software-T&L sky, cloud, and clipped-horizon ordering: retain legacy
  render state, render fallback background geometry before opaque foreground geometry,
  and keep only true UI/HUD draws in the late depth-disabled screen path.
- [ ] Add screenshot or frame-observation checks to compare Vulkan against GL33.
- [ ] Add validation-clean smoke scenes, resize/device-loss checks, and CPU/GPU timing
  baselines for representative Demo scenes.

### Phase 3 - Renderer And Asset Foundations

- [ ] Make `FramePassKind` execute real pass boundaries, attachment clear/load behavior,
  offscreen image lifecycle, and explicit image-layout/barrier tracking.
- [ ] Add transient render-target allocation, staged upload rings, compute/storage-buffer
  infrastructure, pipeline diagnostics, GPU timing, and device capability reporting.
- [ ] Define backend-neutral texture, mesh, material, compression, and residency
  descriptors, including explicit color-space and sampler semantics.
- [ ] Add 32-bit index metadata and backend draw support before importing modern meshes.
- [ ] Add upload budgets, staging paths, residency telemetry, diagnostics, and
  backend-specific fallback/transcode behavior.
- [ ] Keep GL33 insulated with fallback, transcode, emulation, or explicit
  diagnostics when a modern asset feature cannot map directly.
- [ ] Add GLTF 2.0 import with skeletal animation, fixtures, malformed-input handling,
  and backend-neutral mesh/material output.
- [ ] Add COLLADA (.dae) import only if it remains needed after GLTF workflow support.
- [ ] Add BC7 asset-pipeline support only with runtime format checks and fallback/transcode.
- [ ] Add independently licensed offline specular-to-metalness and lip-sync tools with
  generated-asset provenance metadata.
- [ ] Vulkan Camera Matrix Floating-Point Precision: Refactor the camera projection uniform math to utilize large-world relative-coordinate anchoring to eliminate vertex jitter at extreme map distances (5000m+).
- [ ] Graceful Asset Loader Fallbacks: Lock down `TextBankVK` format mappings to route all legacy unhandled color formats (like paletted P8 or uncompressed AI88) straight to `kFallbackResourceId = 1` cleanly without outputting console clutter.

### Phase 4 - HDR And Presentation

- [x] Add an opt-in `R16G16B16A16_SFLOAT` world target with an isolated
  world/cloud composition pass.
- [/] Add opt-in exposure, bloom, ACES tone mapping, presentation conversion,
  and an explicit UI-after-tonemap composition policy. Current exposure uses
  smooth camera-to-sun metering plus an HDR-core visibility heuristic; temporal
  adaptation, exact depth occlusion, lens flare, and a low-resolution bloom
  chain remain outstanding.
- [ ] Add night-vision post-processing.
- [ ] Define internal-resolution, dynamic-resolution, and upscaling policy.
- [ ] Add AMD FidelityFX Super Resolution (FSR) only after the HDR pass chain and
  resolution policy exist.

### Phase 5 - Modern Visual Base

- [ ] Improve cascaded-shadow split distribution, transition blending, filtering, and bias.
- [x] Implement a cached HDR sky map for the opt-in Vulkan path, including a
  high-radiance sun, atmosphere/horizon treatment, moon phase and halo, and a
  portable star-field fallback driven by `LightSun` celestial state. Physical
  atmosphere LUTs, aerial perspective, and environment-lighting integration
  remain outstanding.
- [ ] Add an MSAA-ready depth-and-normal prepass with matching alpha-cutout behavior.
- [ ] Add Hi-Z, SSAO/GTAO, and other sampleable-depth/normal consumers.
- [ ] Add Gerstner-wave water, soft shore/coast treatment, refraction, and sky/environment
  reflection in dependency order.
- [ ] Add Forward+ clustered lighting after HDR; do not block water on the full lighting
  implementation.
- [x] Add opt-in persistent Vulkan volumetric clouds: simulation-driven 3D
  density and distance fields, double-buffered light volumes, half-resolution
  raymarch/reconstruction/composite passes, simulation-time wind, history
  invalidation on volume movement, and terrain shadows. Cloud-aware rain,
  gameplay weather queries, external authored noise assets, and particle
  infrastructure remain outstanding.

### Phase 6 - GPU Scale And Optional Tiers

- [ ] Add descriptor-indexed/bindless material binding as an enhanced tier with a
  conventional descriptor fallback.
- [ ] Add retained static-scene data, GPU frustum/distance culling, LOD selection,
  indirect draws, and Hi-Z occlusion.
- [ ] Keep arbitrary camera views first-class for shadows and reflections.
- [ ] Move terrain/water CDLOD selection to the GPU only when profiling proves the CPU
  path is a multiview bottleneck.
- [ ] Add GPU particles for smoke, fire, explosions, rain, and environmental effects.
- [ ] Add volumetric clouds, planar reflections, compute skinning, meshlets, mesh shaders,
  sparse resources, and other high-end effects only behind capability checks.

### Phase 7 - Gameplay, Simulation, And Parity

- [ ] Compare upstream behavior before changing shared gameplay systems; preserve all
  original-game behavior unless an intentional, tested improvement is documented.
- [ ] Implement configurable multi-barrel weapon rotation and vehicle autocannon reload
  behavior where differential testing shows it is missing.
- [ ] Audit existing amphibious physics, tide/wave sea level, and commander-turret behavior
  before changing or duplicating them.
- [ ] Fix roof movement and vehicle-surface riding with reproducible collision scenarios.
- [ ] Improve third-person camera collision and underwater/underground following behavior.
- [ ] Add swimming: water-entry state, buoyancy/drag/thrust, actions/input, animation/config
  strategy, AI water traversal, breath/drowning rules, restrictions, and exit behavior.
- [ ] Add proper 6DoF support: manual camera roll, full spatial orientation commands, and
  safe entity-orientation rules that preserve ground-vehicle behavior.
- [ ] Fix water surface normals and terrain lighting direction.
- [ ] Add animated loading screens with audio synchronization.
- [ ] Keep Box3D as a separately profiled experimental presentation-physics track, not a
  renderer or simulation-parity prerequisite.

### Phase 8 - Input, Platform, And VR

- [ ] Add SDL3 joystick/gamepad support and configurable mappings.
- [ ] Add multiscreen UI size, offset, and position configuration.
- [ ] Add OpenXR only after renderer capability negotiation, stereo frame timing, and
  hardware-specific validation are stable.

### Phase 9 - Async Audio Pipeline Modernization

- [ ] Refactor blocking command-radio playback into asynchronous multi-channel sample
  streaming without changing original-game queue behavior unless explicitly intended.

## AI-Assisted Development And Funding

Much of this fork is developed with AI-assisted exploration, code auditing,
test generation, and iterative refactoring. That is useful for a large legacy
3D simulation codebase, but it also consumes a lot of API tokens. In practice,
modernizing this engine this way is expensive.

If you want to support this work:

- GitHub Sponsors: <https://github.com/sponsors/koosoli>
- Buy Me a Coffee: <https://buymeacoffee.com/koosoli>

Any money received with a note that it is for this project will be used entirely
to pay for API calls and direct development costs for this fork.

## Source, Brand, And Game Data

Three things must stay separate:

**Source code**

The engine and game executable source in this repository is licensed under
GPL-3.0-or-later with additional terms under Section 7. You may study, modify,
and redistribute it under those terms.

**Name and branding**

`ARMA`, `Operation Flashpoint`, their logos, and related marks are not granted by
this repository. Those trademarks remain with their respective owners. This fork
must not present itself as an official Bohemia Interactive product.

**Game data**

Models, textures, sounds, missions, voices, and other game assets are not part of
this repository and are not covered by the GPL. They ship separately under the
Arma Public License Share Alike (APL-SA). A free demo is available on Steam for
testing locally built binaries.

In short: the code is free software, the names are not, and the game data comes
separately.

## Quick Start

### Build Requirements

- [Clang](https://clang.llvm.org/)
- [CMake](https://cmake.org/)
- [Ninja](https://ninja-build.org/)
- [vcpkg](https://vcpkg.io/)

On Windows, the core tools can be installed with:

```powershell
winget install Kitware.CMake LLVM.LLVM Ninja-build.Ninja
```

Then follow Microsoft's vcpkg setup guide:
<https://learn.microsoft.com/en-us/vcpkg/get_started/get-started?pivots=shell-powershell>

### Compiling

Windows x64:

```sh
cmake --preset win-x64-clang-rwdi
cmake --build build/win-x64-clang-rwdi
```

GNU/Linux x64:

```sh
cmake --preset linux-x64-clang-rwdi
cmake --build build/linux-x64-clang-rwdi
```

For local smoke testing in this fork, the demo executable is usually the safest
target:

```sh
cmake --build build/win-x64-clang-rwdi --target PoseidonGameDemo
```

### Running The Vulkan Backend

After building, copy `PoseidonGameDemo.exe` into the Steam demo installation
folder and launch with the Vulkan renderer:

```sh
cd "D:\SteamLibrary\steamapps\common\Arma Cold War Assault Demo"
PoseidonGameDemo.exe --render vulkan --window
```

The `--render` flag selects the backend (`auto`, `gl33`, or `vulkan`).
The `--window` flag starts in windowed mode.

Enable the experimental sky, cloud, and HDR composition path with:

```powershell
$env:POSEIDON_VK_PROCEDURAL_SKY='1'; $env:POSEIDON_VK_VOLUMETRIC_CLOUDS='1'; $env:POSEIDON_VK_HDR='1'; .\PoseidonGameDemo.exe --render vulkan --window
```

### Smoke Testing

Copy the locally built demo executable into the Steam demo installation folder
and launch it there so it can find the separate game data.

Steam demo page:
<https://store.steampowered.com/app/4819000/Arma_Cold_War_Assault_Remastered_Demo/>

> [!WARNING]
> Locally compiled binaries are not guaranteed to be drop-in compatible with
> every original retail game folder. The Steam demo data is the preferred local
> smoke-test target for this fork.

### Upstream Builds

The upstream project publishes CI builds at:
<https://ofpisnotdead-com.github.io/CWR-CE-builds/>

Those builds are useful for comparison, but they may not include changes from
this fork.

## Repository Layout

- [Apps](apps/README.md) - executable targets and application entry points
- [Engine](engine/README.md) - engine libraries and Rust Trident tooling
- [Master server tools](mserver/README.md) - Rust service and CLI crates
- [Tests](tests/README.md) - unit and regression test source trees
- `cmake/` - presets, toolchains, vcpkg triplets, and overlay ports
- `docker/` - container support for services and runtime environments
- `packages/` - ignored local game-data staging area
- `resources/` - application icon resources
- `thirdparty/` - vendored third-party headers and sources

## Project Notes

- [Contributing](CONTRIBUTING.md)
- [Credits](CREDITS.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)
- [Vendored dependencies](thirdparty/README.md)

## License

The source in this repository is licensed under the **GNU General Public License
v3.0 or later**, with additional terms under **Section 7** of the GPL. See
[`LICENSE`](LICENSE) for the full text.

This license does not grant any right to use `ARMA`, `Operation Flashpoint`, or
any other Bohemia Interactive or Electronic Arts trademark.

The [`thirdparty/`](thirdparty) directory is excluded from this repository's GPL
license. It contains vendored third-party code, such as glad and the RenderDoc
API header, under their own respective licenses. See
[`thirdparty/README.md`](thirdparty/README.md). Dependencies pulled in via
[`vcpkg.json`](vcpkg.json) likewise remain under their own licenses.

`ARMA` is a registered trademark of BOHEMIA INTERACTIVE a.s. `OPERATION
FLASHPOINT` is a registered trademark of Electronic Arts Inc. Use of those names
here is informational and does not constitute any grant, waiver, endorsement, or
official affiliation.

### Game Data And Assets

Game data and assets are not part of this repository and are not covered by the
GPL. They are released separately by Bohemia Interactive under the Arma Public
License Share Alike (APL-SA):

- APL-SA license text:
  <https://www.bohemia.net/community/licenses/arma-public-license-share-alike>

The compiled binaries need game data to run. You can obtain free demo game data
from Steam:

- *Arma: Cold War Assault Remastered* Demo:
  <https://store.steampowered.com/app/4819000>

Whatever you do with assets is governed by the APL-SA linked above. Whatever you
do with this source is governed by the GPL with additional terms per Section 7 in
[`LICENSE`](LICENSE).

## Contributing

This fork welcomes bug reports, experiments, ports, tooling improvements,
documentation updates, tests, and modernization work that fit the goals above.

Please use issues for bugs and proposals, and open pull requests for source,
build, test, or documentation changes. See [`CONTRIBUTING.md`](CONTRIBUTING.md)
for contribution guidelines.
