# PoseidonVK Integration Plan

## Project Overview

**PoseidonVK** (`C:\Users\mail\OneDrive\Documents\GitHub\CWR-CE\`) is a C++20 Vulkan 1.1 graphics backend for the CWR-CE engine (Arma Cold War Assault Remastered — Community Edition, based on Bohemia Interactive's official GPL source release).

**Current state:** Phase 2 of a multi-phase roadmap. The Vulkan backend has swapchain, scene rendering (6 shader families), cascade shadow maps, 2D/HUD rendering, texture/mesh management, and frame constant upload working. It renders through a backend-neutral `Frame`/`FramePlan`/`RenderPassDescriptor` pipeline shared with the GL33 backend. ~85-90% through the initial "raster parity" phase.

**Key architectural commitments:**
- Pure C++ Vulkan (no Rust FFI)
- Backend-neutral Frame pipeline — all backends consume `SubmitFramePlan(const Frame&)`
- Typed `RenderPassDescriptor` with strongly-typed enums replacing legacy bitmasks
- Opaque resource handles (MeshHandle, TextureHandle) with backend-local resolution
- Both GL33 (priority 100, default) and Vulkan (priority 50) registered in the same factory
- SDL3 windowing, CMake + vcpkg + Clang build system
- Unit-tested (9 Vulkan test files)
- Compatible with `D:\SteamLibrary\steamapps\common\Arma Cold War Assault Demo\`

**Roadmap phases remaining:** Phase 3 (modern assets), Phase 4 (asset scale/visual modernization), Phase 5 (Forward+ clustered lighting), Phase 6 (compute shader weather particles), Phase 9 (async audio).

---

## Reference Projects

### Reference A: FP_269_vk_WIP (clean-sheet Vulkan renderer + improved game engine)
**Path:** `C:\Users\mail\OneDrive\Documents\GitHub\CWR-CE\referencesandinspirations\FP_269_vk_WIP\`

A fully permissively-licensed, clean-sheet Vulkan renderer with a complete set of modern rendering features, paired with an improved game engine layer (`Poseidon2/`). The renderer (`fp\simple_renderer_vk\`) is decoupled and reusable. The game engine contains many fixes and improvements over the original CWR-CE.

**Rendering features available:**
| Feature | Key Files |
|---|---|
| **Cluster / Forward+ lighting** | `fp\simple_renderer_vk\Clusters.cpp`, `Clusters.hpp`, `ShadersCluster.glsl` |
| **Post-processing + FSR** | `fp\simple_renderer_vk\PostProcess.cpp`, `PostProcess.hpp`, `ShadersPostProcess.glsl`, `FsrGlslIncludes.glsl` |
| **Procedural atmospheric sky** (Rayleigh + Mie scattering, cubemap-based, HDR Rgba16f, Earth curvature model, compute-shader stars with GPU culling, mesh shader star rendering) | `fp\simple_renderer_vk\Sky.cpp`, `Sky.hpp`, `ShadersSky.glsl`, `ShadersWorldTypes.glsl`, `StarInfo.inc` |
| **Clouds** | Likely in `Sky.cpp` or separate files — investigate |
| **Rain** | Likely in `ParticleManager.cpp`, `ShadersParticle.glsl`, or `Landscape.cpp` — investigate |
| **Sea / ocean** (tide simulation, wave animation, normal map) | `fp\simple_renderer_vk\Landscape.cpp`, `Landscape.hpp` |
| **GPU particles** | `fp\simple_renderer_vk\ParticleManager.cpp`, `ParticleManager.hpp`, `ShadersParticle.glsl` |
| **GPU terrain** | `fp\simple_renderer_vk\Landscape.cpp`, `Landscape.hpp` |
| **Cascaded shadow maps** (smooth quality transitions) | `fp\simple_renderer_vk\CascadedShadow.cpp`, `CascadedShadow.hpp` |
| **Night vision** | Check rendering pipeline or post-processing files |
| **Mesh shader / meshlets** | `fp\simple_renderer_vk\ShadersMeshlet.glsl`, `meshoptimizer\` (vendored) |
| **GLTF model loading** | `fp\simple_renderer_vk\ModelDataGltf.cpp`, `ModelDataGltf.hpp`, `SkinnedModelDataGltf.cpp` |
| **COLLADA (.dae) model loading** | Likely in `fp\simple_renderer_vk\ModelData.cpp` or separate — investigate |
| **World shaders** | `fp\simple_renderer_vk\ShadersWorld.glsl`, `ShadersWorldTypes.glsl` |
| **Vulkan GPU abstraction** | `fp\simple_renderer_vk\gpu\GpuBuffer.hpp`, `gpu\Image.hpp`, `gpu\Pipeline.hpp`, `gpu\Shader.hpp`, `gpu\DescriptorSet.hpp`, `gpu\CommandBuffer.hpp` |
| **VR (OpenXR)** — tested on Quest 3, Valve Index | `fp\simple_renderer_vk\vr\EyeMasks.cpp`, `EyeMasks.hpp`, `Common.hpp` |
| **Multiscreen support** | Likely in `GpuManager.cpp` or config system |
| **Joystick support** | Likely in `Poseidon2\keyInput.cpp` or core input system |
| **SPIR-V compiler tool** | `fp\spirv_compiler\main.cpp` (custom GLSL→SPIR-V with include resolution) |

**Standalone asset pipeline tools:**
| Tool | Path | Purpose |
|---|---|---|
| **BC7 texture compression** | `texCompression\bc7\`, `texCompression\detex\` | Full BC7 compressor + decompressor |
| **spec→metal conversion** | `tools\spec_to_metal\` | Specular to metalness material conversion |
| **ODOL→MLOD converter** | `tools\odol_to_mlod\` | Arma model format conversion |
| **PAA tool** | `tools\paa\` | PAA texture format tool |
| **GLTF convert** | `tools\gltfConvert\` | General glTF importer/exporter |
| **DDS→C array** | `tools\ddsToArray\` | DDS to embedded array |
| **X-Ray→GLTF** | `tools\xray_to_gltf\` | S.T.A.L.K.E.R. X-Ray model converter |
| **MView→GLTF** | `tools\mview_to_gltf\` | MView model converter |
| **Lip sync tool** | `tools\lip_sync\` | Lip synchronization generation (has its own readme) |

**Build notes:** Uses CMake + MinGW cross-compilation (toolchain files: `gcc10_tool`, `gcc11_tool`, `clang_tool`, `llvm_mingw_tool`). Targets Windows from Linux. Links: vulkan-1, glfw, OpenAL, PhysX, glslang, OpenXR. The `fpRendererVk` library has its own shader compilation pipeline via the custom `spirvCompiler` tool.

---

### Reference B: Wgpu Fork (new-renderer-infrastructure)
**Path:** `C:\Users\mail\OneDrive\Documents\GitHub\CWR-CE\referencesandinspirations\AnotherforkCWR-CE-new-renderer-infrastructure\CWR-CE-new-renderer-infrastructure\`

A fork of CWR-CE by paavohuhtala that adds a Rust/wgpu graphics backend alongside the existing OpenGL 3.3 backend. The wgpu backend communicates with C++ via a C ABI FFI (`engine\WgpuRenderer\include\wgpu_renderer.hpp` ↔ `engine\WgpuRenderer\rust\src\ffi.rs`).

**Key features available:**
| Feature | Location |
|---|---|
| **HDR pipeline plan** | `engine\WgpuRenderer\docs\hdr-pipeline-plan.md` |
| **Procedural sky plan** | `engine\WgpuRenderer\docs\procedural-sky-plan.md` |
| **GPU object rendering plan** | `engine\WgpuRenderer\docs\gpu-object-rendering-plan.md` |
| **GPU culling + depth prepass plan** | `engine\WgpuRenderer\docs\gpu-culling-and-depth-plan.md` |
| **Forward+ clustered lighting plan** | `engine\WgpuRenderer\docs\forward-plus-plan.md` |
| **Cascaded shadow map plan** | `engine\WgpuRenderer\docs\cascaded-shadow-map-plan.md` |
| **Water rendering plan** | `engine\WgpuRenderer\docs\water-rendering-plan.md`, `water-cdlod-geometry-plan.md` |
| **Terrain rendering** | `engine\WgpuRenderer\docs\terrain-fractal-detail-plan.md`, `terrain-conform-vegetation-roads-plan.md` |
| **Bindless textures plan** | `engine\WgpuRenderer\docs\bindless-textures-plan.md` |
| **SSAO plan** | `engine\WgpuRenderer\docs\screen-space-ao-plan.md` |
| **Master roadmap (dependencies)** | `engine\WgpuRenderer\docs\implementation-roadmap.md` |
| **WGSL shaders (reference)** | `engine\WgpuRenderer\rust\src\shaders\shading.wgsl`, `lighting.wgsl`, `shadow.wgsl`, `skin.wgsl`, `gbuffer.wgsl` |
| **3D shaders (reference)** | `engine\WgpuRenderer\rust\src\gfx3d\shader3d.wgsl`, `cull.wgsl`, `hiz.wgsl`, `gpu_driven.wgsl` |
| **Terrain shaders (reference)** | `engine\WgpuRenderer\rust\src\terrain\terrain.wgsl`, `terrain_shadow.wgsl` |
| **Water shaders (reference)** | `engine\WgpuRenderer\rust\src\water\water.wgsl` |
| **Sky shaders (reference)** | `engine\WgpuRenderer\rust\src\sky\sky.wgsl`, `sky_sh.wgsl` |
| **Post-processing shaders** | `engine\WgpuRenderer\rust\src\bloom.wgsl`, `exposure.wgsl`, `tonemap.wgsl` |

**Key architectural note:** The wgpu backend bypasses `SubmitFramePlan` — it captures scene state directly in `NextFrame()` and builds a flat `WgrFrame` struct shipped to Rust. This means the clean Frame/FramePlan/RenderPassDescriptor abstraction is present in this fork's Poseidon library but **not consumed by the wgpu backend**.

**Build:** CMake + vcpkg + Clang (same as upstream). The wgpu backend is gated by `POSEIDON_ENABLE_WGPU` (default ON) and requires Rust + corrosion CMake module. GL33 remains the default.

---

### Reference C: Upstream CWR-CE
**Path:** `C:\Users\mail\OneDrive\Documents\GitHub\CWR-CE\referencesandinspirations\Upstream_CWR-CE-onwhichourforkisbased\CWR-CE-main\`

The original upstream CWR-CE. OpenGL 3.3 only, no Vulkan. The base from which both PoseidonVK and the wgpu fork diverged.

---

## Recommendations

### What to take from FP_269_vk_WIP (Rendering)

These are C++ Vulkan implementations that can be ported directly into `engine\PoseidonVK\`. Each suggestion includes the source path and the target context.

**1. Cluster/Tiled Lighting (Forward+)**
- Source: `fp\simple_renderer_vk\Clusters.cpp`, `Clusters.hpp`, `ShadersCluster.glsl`
- Target phase: Phase 5 (Forward+ clustered lighting)
- This is a complete implementation. The `Clusters` class builds a 2D tile grid, assigns lights per tile via compute, and produces a light index list. The shader (`ShadersCluster.glsl`) consumes the cluster data for per-pixel light evaluation.
- Integration: Port cluster building logic into PoseidonVK's frame setup. The shader GLSL needs adapting to PoseidonVK's shader compilation pipeline (currently `cmake/GenerateShaderHeader.cmake` compiles GLSL→SPIR-V at configure time via glslang).

**2. HDR Post-Processing with FSR**
- Source: `fp\simple_renderer_vk\PostProcess.cpp`, `PostProcess.hpp`, `ShadersPostProcess.glsl`, `FsrGlslIncludes.glsl`
- Target: Standalone feature, no phase dependency
- Complete post-processing chain: tone mapping → FSR upscaling. The `FsrGlslIncludes.glsl` contains AMD's FSR 1.0 GLSL implementation (EASU + RCAS).
- Integration: Add as a post-processing pass at the end of the Vulkan render graph. Requires HDR intermediate target (Rgba16Float).

**3. Procedural Atmospheric Sky**
- Source: `fp\simple_renderer_vk\Sky.cpp`, `Sky.hpp`, `ShadersSky.glsl`, `ShadersWorldTypes.glsl`, `StarInfo.inc`
- Target: Standalone feature
- Full GPU-based atmospheric scattering sky:
  - **Rayleigh scattering** (simplified sun rays calculation, wavelength-dependent scattering)
  - **Mie scattering** (haze/glow around the sun, phase eccentricity g=0.995)
  - **Earth curvature model** (r_earth = 6371000m, h_sky = 10000m atmosphere)
  - **Sun occlusion** (checks if sun is behind the earth's horizon)
  - **HDR cubemap** (Rgba16f, 512x512 per face, generated at runtime via procedural shader)
  - **Stars** (real star data from StarInfo.inc, uploaded via compute shader, GPU-culled per frame using frustum culling, rendered via mesh shaders with billboard quads)
  - **Star twinkle** (compute shader random brightness simulation with per-star timing)
  - **VR support** (dual-eye star rendering)
- The sky was originally generated every frame; optimized to render into a cubemap that is sampled per frame.
- Integration: New draw pass in the frame pipeline. The sky cubemap generation can run at lower frequency. Stars require mesh shader support.

**4. Clouds**
- Source: Investigate `fp\simple_renderer_vk\Sky.cpp` or separate files
- Target: Standalone feature
- Confirmed working in v0.120. Likely layered cloud rendering in the sky system.
- Integration: After procedural sky is ported.

**5. Sea / Ocean with Waves**
- Source: `fp\simple_renderer_vk\Landscape.cpp`, `Landscape.hpp`
- Target: Standalone feature or Phase 3
- Full tide simulation (sun + moon gravity), dynamic wave height (sinusoidal, maxWave=0.25m), animated sea texture, water surface normal map fix (was reversed, causing dark rendering).
- Also in `Poseidon2\landscape.cpp` lines 424-445 for the tide/wave physics.
- Integration: Port the landscape water rendering into PoseidonVK's terrain draw path.

**6. Rain**
- Source: Investigate `ParticleManager.cpp`, `ShadersParticle.glsl`, or `Landscape.cpp`
- Target: Standalone or Phase 6
- Confirmed working in v0.120. Likely uses the particle system or a screen-space rain effect.
- Integration: After GPU particles are ported, or as a standalone screen-space effect.

**7. Night Vision**
- Source: Check post-processing pipeline or `GpuManager.cpp`
- Target: Standalone feature
- Confirmed working in v0.120. Likely a post-processing effect (color remapping, gain boost).
- Integration: Post-processing pass.

**8. GPU Particles**
- Source: `fp\simple_renderer_vk\ParticleManager.cpp`, `ParticleManager.hpp`, `ShadersParticle.glsl`
- Target phase: Phase 6 (compute shader weather particles)
- Compute-shader-based particle system with GPU simulation and rendering.
- Integration: Requires compute pipeline support in PoseidonVK.

**9. GPU Terrain**
- Source: `fp\simple_renderer_vk\Landscape.cpp`, `Landscape.hpp`
- Target: Improvement over current terrain rendering
- Heightmap-based terrain with LOD, texturing, and shadowing.
- Integration: Replace or augment the current terrain draw path in PoseidonVK.

**10. Cascaded Shadow Maps (smooth transitions)**
- Source: `fp\simple_renderer_vk\CascadedShadow.cpp`, `CascadedShadow.hpp`
- Target: Compare with PoseidonVK's existing shadow implementation
- 4-cascade shadow maps with PCF. v0.120 mentions "shadow quality transitions are more smooth" — investigate the cascade blending approach.
- PoseidonVK already has cascade shadows (`EngineVK_Shadow.cpp`). Compare approaches and adopt improvements.

**11. GLTF Model Import**
- Source: `fp\simple_renderer_vk\ModelDataGltf.cpp`, `ModelDataGltf.hpp`, `SkinnedModelDataGltf.cpp`, `SkinnedModelDataGltf.hpp`
- Target: Phase 3 (modern assets) or Phase 4 (asset scale)
- Full GLTF 2.0 loader with skeletal animation support. This enables using modern 3D assets alongside legacy P3D/ODOL models.
- Integration: Add as an alternative model format in the asset pipeline alongside the existing P3D loader. This does NOT require removing or modifying the existing P3D/ODOL support — it's additive.

**12. COLLADA (.dae) Model Import**
- Source: Investigate `fp\simple_renderer_vk\ModelData.cpp` or separate files
- Target: Phase 3 (modern assets)
- COLLADA is a universal model format supported by Blender, 3ds Max, Maya, and many others. Alternative/complement to GLTF.
- Integration: Add as another model format alongside P3D and GLTF.

**13. Lip Sync Tool**
- Source: `tools\lip_sync\` (has its own readme)
- Target: Asset pipeline tool
- Generates lip synchronization data from audio. Useful for character animation improvements.

**14. GPU Abstraction Layer**
- Source: `fp\simple_renderer_vk\gpu\GpuBuffer.hpp`, `gpu\Image.hpp`, `gpu\Pipeline.hpp`, `gpu\Shader.hpp`, `gpu\DescriptorSet.hpp`, `gpu\CommandBuffer.hpp`, `gpu\volk.h/.c`
- Target: Architecture improvement
- Clean C++ RAII wrappers around Vulkan objects. Currently PoseidonVK makes raw Vulkan calls directly in `EngineVK.cpp`.
- Consider adopting a similar wrapper pattern to reduce code duplication and improve safety.

**Standalone asset tools (can be used as-is):**
- `texCompression\bc7\` + `texCompression\detex\` — BC7 compressor/decompressor. Add BC7 as a supported texture format alongside DXT1-5 in the texture loading pipeline.
- `tools\spec_to_metal\` — Specular→metalness conversion tool. Useful for PBR material migration.
- `tools\odol_to_mlod\` — ODOL→MLOD converter. Useful for asset pipeline debugging.
- `tools\paa\` — PAA texture format tool. Useful for inspecting/modifying CWR textures.
- `tools\gltfConvert\` — GLTF import/export. Pairs with the GLTF model loading.

---

### What to take from FP_269_vk_WIP (Gameplay / Engine Fixes)

These are improvements found in `Poseidon2\` that should be investigated and ported to PoseidonVK's engine layer at `C:\Users\mail\OneDrive\Documents\GitHub\CWR-CE\engine\Poseidon\`.

#### Verified Improvements (confirmed exist in FP_269_vk_WIP, not in base CWR-CE)

**1. Multiple / Commander Turret**
- Location: `Poseidon2\tank.cpp`, `tank.hpp`, `vehicleTypes.cpp`, `vehicleAI.cpp`
- Evidence: Commander turret with independent elevation (`tank.cpp:110-111`), separate fire control (`tank.cpp:2513`), dedicated aiming section (`tank.cpp:2545`). Muzzle array distinguishes `_muzzleOnMainTurret` vs `_muzzleOnComTurret` (`tank.hpp:42-44`). Multiple config weapons define turret mounts.
- Base CWR-CE state: Already has `_mainTurret` + `_comTurret` (`Tank.hpp:28-29`) — so this may already be present. Compare implementations.

**2. Sea / Ocean Waves with Tide**
- Location: `Poseidon2\landscape.cpp` lines 424-445 and `landscape.hpp` lines 737-738
- Evidence: Sun + moon tide simulation (`sunTide`, `moonTide`), sinusoidal wave height (`_seaWaveSpeed`), dynamic `_seaLevelWave`, animated sea texture (`m_seaTextureIndex += deltaT * 10.0f`). Historical fix: "Water surface normals were reversed" and "Lighting direction was wrong on terrain" (lines 837-842).
- Base CWR-CE state: Ocean exists but with simpler static sea level. Port the tide/wave simulation and the normal map fix.

**3. Multiple Barrel System (Shilka)**
- Location: `Poseidon2\weapons.cpp` lines 800-813, `weapons.hpp` lines 828-840
- Evidence: Configurable `barrelCount` (Shilka=4, up to 38 for Gatling types). Barrel rotation vector alternates fire between barrels. Per-barrel muzzle flash and cartridge ejection positions. Config `animationIgnoreReloadTime=1` for autocannons.
- Base CWR-CE state: Standard single-barrel weapon system. Port the barrel rotation, multi-barrel config parsing, and per-barrel effects.

**4. Floating / Amphibious Vehicles**
- Location: `Poseidon2\tankOrCar.hpp` line 44, `tankOrCar.cpp` line 24, `tank.cpp` lines 1840-1866, `car.cpp` lines 1320-1340
- Evidence: `_canFloat` config flag. Full buoyancy simulation with stabilizing torque and water friction. Floating vehicles move at 0.5 m/s in water. Non-floating vehicles get 0.1-0.3x reduced buoyancy (sink slower). Config values like `canFloat=1` for BRDM.
- Base CWR-CE state: Simple drowning mechanic for infantry. Vehicle amphibious physics are basic. Port the buoyancy simulation.

**5. Reload While Running (Vehicle Autocannons)**
- Location: `Poseidon2\weapons.cpp` lines 1303-1305, `weapons.hpp` line 387
- Evidence: `m_animationIgnoreReloadTime` flag. When true, weapon fires without being blocked by reload animation. Used for Shilka, ZSU, 2A14 autocannons. Config-driven per muzzle.
- Base CWR-CE state: Reload animation blocks firing. Port the flag system. Note: infantry reload-while-running not found — this is vehicle-only.

#### Claimed Fixes (reported to work in FP_269_vk_WIP, exact location unknown)

**6. Roof Movement Fix**
- Reported: Movement on building roofs does not work in original CWR-CE (characters slide off or cannot stand on roofs), but works in FP_269_vk_WIP.
- Investigate: `Poseidon2\collisions.cpp`, `house.cpp`, `person.cpp`, `soldierOld.cpp`, `object.cpp`

**7. Vehicle Surface Riding**
- Reported: Riding on roof/external surfaces of vehicles (standing on tank, jeep hood) does not work in original but works in FP_269_vk_WIP.
- Investigate: `Poseidon2\tank.cpp`, `car.cpp`, `helicopter.cpp`, `transport.cpp`, `collisions.cpp`

**8. Camera Improvements**
- Reported (Facebook): Camera follows player underwater/underground, third-person camera has Bullet physics collisions.
- Investigate: `Poseidon2\camera.cpp`, `camera.hpp`, `cameraHold.cpp`

**9. Animated Loading Screens**
- Reported (Facebook): Animated images play during loading screens.
- Investigate: `Poseidon2\progress.cpp`, `progress.hpp` or config system.

#### Same as Original (verified — not improved)

Items where FP_269_vk_WIP has the same behavior as base CWR-CE (no fix found):
- **Unit/group limits** — still 12 units/group, 63 groups (9 letters x 7 colors)
- **Projectile lifetime** — config-driven `timeToLive`, simulation capped at 10s. No hardcoded 5000m despawn. Already flexible.
- **Parachute speed** — same helicopter-based simulation, config-driven `landingspeed`
- **Radio chat overlap** — same unbounded queue with priority ordering. No overlap prevention but messages queue sequentially.
- **Swimming** — same drowning-only mechanic (no swimming animation). Both have `TODO - underwater` comment.

#### Not Present in Either Codebase

Items not found in FP_269_vk_WIP or base CWR-CE (would need to be built from scratch if desired):
- Shotgun simulation (pellet spread, buckshot)
- Underground basements / cellar interiors
- Wall/fence jumping / vaulting mechanic
- Backpack / secondary inventory system
- Knife / melee combat
- Rain with 3D droplet particles (FP has rain working but mechanism unknown)
- Cannon on wheeled vehicles (already supported via shared TankOrCar system — no fix needed)

---

### What to take from the Wgpu Fork

**Design documents:** The 20+ markdown plans in `engine\WgpuRenderer\docs\` are the primary value. They solve the same problems PoseidonVK faces — for the same engine (CWR-CE), with the same constraints:
- `implementation-roadmap.md` — Cross-phase dependency graph. Directly applicable to PoseidonVK's phase planning.
- `hdr-pipeline-plan.md` — Format choice (Rgba16Float), tonemapping (Hable), exposure calculation. Already partially implemented in FP_269_vk_WIP's PostProcess.
- `procedural-sky-plan.md` — Atmospheric scattering model, sky cubemap generation. Reference for Sky.cpp implementation.
- `forward-plus-plan.md` — Tile size, light assignment, MSAA compatibility. Reference for Clusters.cpp.
- `gpu-culling-and-depth-plan.md` — Depth prepass, Hi-Z map, GPU occlusion culling.
- `gpu-object-rendering-plan.md` — GPU-driven draw call generation, indirect rendering.
- `cascaded-shadow-map-plan.md` — Split distribution, cascade selection, PCF filtering.
- `bindless-textures-plan.md` — Descriptor indexing architecture, texture atlas vs heap.
- `water-rendering-plan.md` + `water-cdlod-geometry-plan.md` — GPU CDLOD water with Gerstner waves.
- `terrain-fractal-detail-plan.md` + `terrain-conform-vegetation-roads-plan.md` — GPU terrain detail.
- `sky-visibility-ambient-plan.md`, `screen-space-ao-plan.md` — Ambient occlusion approaches.
- `rendering-performance-plan.md` — Draw call overhead, upload optimization.
- `foliage-translucency-plan.md`, `depth-prepass-plan.md`, `compute-skin-bake-plan.md` — Specialized rendering features.

**WGSL shaders (algorithmic reference):**
- `rust\src\shaders\shading.wgsl` — Lighting model, specular, fog. Directly translatable to GLSL.
- `rust\src\gfx3d\shader3d.wgsl` — 3D object rendering with material, skinning.
- `rust\src\gpu_driven\gpu_driven.wgsl` — GPU-driven culling and indirect draw.
- `rust\src\terrain\terrain.wgsl` — Terrain rendering with heightmap sampling.
- `rust\src\water\water.wgsl` — Water rendering with Gerstner waves and Fresnel.
- `rust\src\sky\sky.wgsl` — Procedural atmospheric sky.
- `rust\src\bloom.wgsl`, `exposure.wgsl`, `tonemap.wgsl` — Post-processing.

**What to AVOID from the wgpu fork:**
- The Rust `cdylib` + C ABI FFI architecture. PoseidonVK is pure C++ — the FFI layer adds build complexity (corrosion, Rust MSRV 1.87, cdylib deployment) and debugging difficulty without benefit.
- The `NextFrame()` bypass pattern — it duplicates frame-building logic outside the centralized Frame pipeline. PoseidonVK should keep `ConsumesRenderFramePlan() = true` and pass all rendering data through `SubmitFramePlan`.

---

### What from the Upstream CWR-CE

The upstream is essentially PoseidonVK's current codebase minus the Vulkan backend. No additional recommendations beyond what's already in PoseidonVK.

---

## Compatibility Requirements

**Critical:** The Vulkan backend must remain compatible with the Arma Cold War Assault Demo (`D:\SteamLibrary\steamapps\common\Arma Cold War Assault Demo\`).

This means:
- PBO archive reading from the Demo's `DTA/`, `AddOns/`, `Campaigns/` directories (already in `engine\Poseidon\IO\PackFiles\`)
- PAA texture format (DXT1/3/5 compression) — already supported via `TextureVK`
- P3D/ODOL model format — already supported via `MeshRegistryVK`
- Config.cpp parsing — already in `engine\Poseidon\IO\ParamFile\`
- SQF scripting engine — already in `engine\Poseidon\Scripting\`
- GL33 backend must remain functional and default (priority 100) — already the case
- Vulkan backend must achieve visual parity with GL33 on Demo scenes before adding modern features

All modern asset additions (GLTF, BC7, spec→metal, HDR) are **additive** — they extend the engine's capabilities without breaking legacy format support.

---

## Suggested Sequencing

The following is a suggested order. Each agent should assess dependencies and propose their own sequencing.

**Wave 1 (Standalone, no infra dependency):**
- HDR post-processing + FSR (`PostProcess.cpp` + `FsrGlslIncludes.glsl`)
- Procedural atmospheric sky (`Sky.cpp` + `ShadersSky.glsl`)
- Cascaded shadow improvements (smooth transitions)
- Night vision post-processing effect

**Wave 2 (Requires compute / depth prepass):**
- Cluster/Forward+ lighting (`Clusters.cpp`)
- GPU particles (`ParticleManager.cpp`)

**Wave 3 (Large terrain/water features):**
- GPU terrain (`Landscape.cpp` + wgpu terrain docs + WGSL reference)
- Sea/ocean with tide and waves (`Landscape.cpp` water rendering)
- GPU water (wgpu water docs + WGSL reference)

**Wave 4 (Weather & atmosphere):**
- Clouds
- Rain
- Weather particle system

**Wave 5 (Advanced rendering):**
- GPU-driven culling (wgpu culling docs + WGSL reference)
- Bindless textures (wgpu bindless docs)
- Mesh shaders (`ShadersMeshlet.glsl` + `meshoptimizer/`)

**Parallel track (asset pipeline, no rendering dependency):**
- GLTF model import (`ModelDataGltf.cpp`)
- COLLADA model import
- BC7 texture support (`texCompression/`)
- spec→metal conversion tool (`tools/spec_to_metal/`)
- Lip sync tool (`tools/lip_sync/`)
- GPU abstraction layer refactor (`gpu/*.hpp`)

**Gameplay fixes (engine layer, independent of rendering):**
- Multiple barrel rotation system
- Amphibious vehicle physics
- Multi-turret improvements
- Roof movement fix (investigate)
- Vehicle surface riding fix (investigate)
- Camera improvements (underwater/underground, third-person collision)
- Animated loading screens

**Input / platform (independent):**
- Joystick support
- Multiscreen support
- VR improvements (already partially present)

---

## Notes for AI Agents

This document is a starting point for discussion, not a prescriptive plan. Each agent should:
1. Read the relevant source files from the referenced paths
2. Form their own opinion on what should be ported, adapted, or left as-is
3. Consider dependencies between features (the wgpu fork's `implementation-roadmap.md` is the best reference for this)
4. Assess whether code can be used directly or needs architectural adaptation for PoseidonVK's Frame/FramePlan pipeline
5. Maintain the separation between Vulkan backend code and the shared engine library — no Vulkan handles in `engine\Poseidon\`
6. Keep GL33 backend functional as the default and verification baseline

### Independent Audit

The recommendations above are not exhaustive. Each agent should independently audit all three reference projects for anything not listed here that might be valuable to port. Look for:

- **Rendering features** not yet covered (decals, weather effects, water, specific shadow techniques, etc.)
- **Shader techniques** (procedural generation, lighting models, blending approaches)
- **Asset pipeline improvements** (format converters, compression tools, material workflows)
- **Engine systems** (audio, physics, input, file system, networking — anything that could benefit PoseidonVK or the core engine)
- **Build system patterns** (CMake modules, dependency management, cross-compilation)
- **Testing approaches** (test infrastructure, benchmarks, validation tools)
- **Documentation** (design docs, architecture notes, roadmaps)

If something is missing or the sequencing can be improved, propose a revision. This plan is a living document meant to be challenged and improved.

The FP_269_vk_WIP code uses C++20 with some GCC/Clang extensions. PoseidonVK uses C++20 with Clang (MSVC frontend on Windows). Porting should account for potential `#pragma` and intrinsic differences.
