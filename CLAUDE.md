# CLAUDE.md — engine project context

## What this is
A learning-driven **game engine as a library**: D3D12-first, RHI-portable,
eventually C-bindable, proven by samples and tests rather than a shipped game.
Portfolio artifact + systems-learning vehicle. Author is a backend engineer
returning to C++ after a long hiatus.

## Working style (important)
- **Annotate modern C++ inline**: any C++17/20/23 feature in a snippet gets a
  one-line `// C++20: ...` comment, plus the trap if it has one.
- Deliver code with tests; run them where possible. Tests have caught real
  bugs (arena address-alignment, virtual default-args) — keep that culture.
- Push back on scope creep; cite DECISIONS below. The recurring trap is
  multi-backend work before the render graph stabilizes the RHI seams.
- Fatal `check()` for bugs; `std::expected` only for genuinely recoverable
  ops (shader hot reload). No exceptions.

## Architecture (the 8-axes decisions, AAA-shaped minus editor/reflection)
- **World**: retained mode, dual representation planned — scene components +
  render proxies, connected by a command queue (inline/single-threaded until
  the render-thread milestone).
- **Loop**: engine owns it. `class X : public eng::app::Application`,
  virtual `on_frame(FrameContext, dt)`; `run()` does pump→resize→dt→begin/end.
- **State**: hidden globals/singletons acceptable; documented thread affinity later.
- **Dialect**: OOP + hand-rolled type IDs (`ClassID`, `ENG_CLASS`, `is_a`/`cast`).
  NO reflection, NO codegen, NO RTTI (/GR- intended). Single inheritance from
  `Object` only — is_a walks one parent chain.
- **Layers**: core → rhi → rendergraph → renderer → scene → app. `rhi.h` has
  ZERO platform types; d3d12/metal/vulkan live in rhi/src/<backend>/ privately.
- **Iteration**: fast compiles + data hot reload + shader hot reload (m4).
  No DLL hot reload (conflicts with vtables/retained objects). Live++ optional later.
- **Memory**: NO GC. Lifetime categories, one mechanism each:
  frame-lifetime → `Arena` (bump, reset, trivially-destructible only) ·
  long-lived + externally referenced → `Pool<T>` + generational `Handle<T>`
  (24-bit index / 8-bit gen, gen 0 = null, wrap skips 0) ·
  shared assets → intrusive `RefCounted` + `Ref<T>` (ADOPT-on-construct
  convention, refs start at 1) · single-owner subsystems → plain/unique_ptr.
  Scene rule: pool slot's Ref is the ONE strong owner; everything else holds handles.
- **Errors**: `check` (fatal, ships), `checkSlow` (debug), `ensure`
  (non-fatal expression, fires debugger once per site).

## Graphics decisions
- D3D12 via **Agility SDK**: all includes `<directx/d3d12.h>` (NOT `<d3d12.h>` —
  that's the OS header; static_assert on D3D12_SDK_VERSION guards this).
  Agility exports compiled into EACH EXECUTABLE via `engine_agility_exports`
  INTERFACE target (static libs dead-strip unreferenced objects).
  DLLs post-build-copied to `$<exe_dir>/D3D12/`.
- **Enhanced barriers only** (sync/access/layout triple; rhi.h mirrors it).
  D3D12 rule encoded: ACCESS_NO_ACCESS pairs only with SYNC_NONE.
- **Bindless SM 6.6** (`ResourceDescriptorHeap`), binding tier 3 asserted at
  startup. Resources reach shaders as integer indices in root/push constants
  (128-byte budget). No descriptor-table plumbing.
- **DXC** runtime compilation (m3/m4); shader hot reload keeps old PSO on error.
- Frame pacing: fence per frame-slot; Execute → Present → Signal; wait in
  begin_frame on the slot's fence value. frame_index ≠ backbuffer_index.
- Deferred-delete queue (resource + fence value) for everything EXCEPT
  swap-chain backbuffers (ResizeBuffers needs refs released immediately after
  wait_idle).
- Debug arsenal before device creation: debug layer, GPU-based validation,
  DRED breadcrumbs+pagefaults. InfoQueue1 callback → logger; errors → check(false).
  Zero validation warnings policy. ReportLiveDeviceObjects at exit → zero leaks.
- Math: **DirectXMath** (vcpkg, cross-platform incl. NEON). Conventions pinned
  by tests/test_math.cpp: RIGHT-handed, row-vector (compose left-to-right:
  scale*rot*trans), depth [0,1], XMFLOAT* for storage / XMVECTOR-XMMATRIX for
  locals only, HLSL transpose-on-upload vs -Zpr decided at m3.
- /arch:AVX2 on eng_common (Windows), uniform across all TUs.

## Backends (contested history — hold this line)
- **D3D12 is the product.** Milestone 2 complete: rainbow clear, resize, clean
  shutdown, zero leaks.
- **User removed the Null backend** against advice (it was: test harness with
  op recording via std::variant, Mac dev loop, layering canary — code exists
  in history and chat outputs). If tests/Mac-dev pain appears, recommend
  restoring it; don't relitigate otherwise.
- **Metal**: ✅ at m2 parity (rainbow/resize/shutdown; ClearSample runs
  UNMODIFIED on both machines — the RHI's first existence proof). FROZEN at
  parity: m3+ device methods `check(false)` on Metal until a deliberate
  catch-up (per-milestone or at m8). Since barrier() is a no-op on Metal,
  Metal rendering correctly NEVER validates barriers. The D3D12 debug layer is no
  longer the ONLY barrier check, though: running a sample on Backend::Vulkan on the
  Mac puts the Khronos validation layers over the same RHI calls, which is the main
  reason Vulkan-on-macOS earns its keep. Still keep
  zero-validation-warnings discipline on Windows especially.
  metal-cpp vendored in third_party/metal-cpp (from Apple zip, VERSION.txt,
  updated with Xcode upgrades). *_PRIVATE_IMPLEMENTATION defines in exactly one TU.
  Known items: barrier() = no-op (auto hazard tracking), clear = render-pass
  load action (deferral is render-graph work, don't build now), dispatch_semaphore
  pacing, drawable autorelease-pool-per-frame (else 3-frame freeze),
  drawableSize × backingScaleFactor (else Retina blur), app_macos.mm is ObjC++
  (AppKit not covered by metal-cpp), pump() = manual event loop, never [NSApp run].
- **Vulkan**: ✅ at m2 parity on macOS (2026-08-19), and the backend now COMPILES
  on all three platforms (`if (WIN32 OR UNIX)` in rhi/CMakeLists.txt). Baseline is
  Vulkan 1.3 CORE: sync2 (the one backend where barrier() is real and maps 1:1 to
  the rhi triple), dynamic rendering, timeline semaphores. Reuses DXC via -spirv
  when shaders arrive.
  - **macOS via MoltenVK**, vendored in engine/third_party/MoltenVK (10 MB: the
    dynamic ICD only — dylib + MoltenVK_icd.json + VERSION.txt). vcpkg has NO
    molten-vk port; that is why it is vendored, same pattern as metal-cpp. The two
    files must stay in the same directory: the manifest's `library_path` is
    relative. `.gitignore` has a trailing `!` negation for the dylib — there are
    two blanket `*.dylib` rules, so it must stay at the END of the file.
  - Verified on Apple M1 Pro: device apiVersion **1.3.357** (clears the backend's
    >=1.3 gate), portability_subset advertised, dynamicRendering + synchronization2
    + timelineSemaphore all supported, Metal3 argument buffers used for descriptor
    sets (so m3 bindless has a path).
  - Three macOS-only requirements, all encoded: `VK_EXT_metal_surface` (takes the
    CAMetalLayer* that Window::native_handle() ALREADY returns — no new seam);
    `VK_KHR_portability_enumeration` + ENUMERATE_PORTABILITY_BIT on the instance
    (forget it and vkEnumeratePhysicalDevices returns ZERO devices with VK_SUCCESS);
    `VK_KHR_portability_subset` on the device, which the spec makes mandatory when
    advertised.
  - **Linux is still unwired** and the blocker is NOT Vulkan: there is no Window
    implementation at all, and native_window is one void* while xcb needs two
    values. SDL3 at m8, per Linux tiers below.
- **Loader discovery is engine-owned, not shell setup.** vcpkg does not register
  drivers or layers with the loader the way the LunarG installer does. CMake bakes
  the paths in (ENGINE_VK_ICD_FILE, ENGINE_VK_LAYER_PATH) and
  configure_loader_search_paths() applies them as **VK_ADD_DRIVER_FILES** (a FILE)
  and **VK_ADD_LAYER_PATH** (a DIRECTORY). Always the ADD_ variants: the plain ones
  REPLACE the loader's search and hide a developer's own SDK. Must run before ANY
  Vulkan call — the loader builds its driver list lazily and
  vkEnumerateInstanceExtensionProperties already needs it. On Windows set BOTH
  _putenv_s and SetEnvironmentVariableA: the loader is in vulkan-1.dll with its own
  CRT, so one alone is a silent no-op. STILL DEV-ONLY: the ICD path is an absolute
  source-tree path, so a shippable build needs the dylib staged next to the exe and
  found relative to it (wants an executable-dir primitive the engine lacks).
- **Linux tiers**: (1) SteamOS/Deck via Proton — the D3D12 backend runs under
  vkd3d-proton (SM6.6 bindless + enhanced barriers supported), zero work;
  (2) native = m8 Vulkan + an SDL3 window layer behind the existing Window
  interface (do NOT hand-roll X11/Wayland).
- **Out of scope, previous-gen APIs**: D3D11/OpenGL cannot implement this RHI
  (no explicit barriers/PSOs/fences/bindless — the abstractions have no
  meaning there; bgfx-style engines abstract higher to span them). WebGPU is
  same-generation and would slot in IF a browser target ever matters (caveat:
  no unbounded bindless — would need a binding fallback).
- **Mobile**: out of scope. One design residue only: render-graph passes (m5)
  carry explicit per-attachment load/store actions (TBDR platforms live or die
  by them; Metal already forces the model — the clear-as-load-action lesson).

## Build system
- CMake presets (`win-dev` VS gen / `mac-dev` Ninja) with `condition` per
  hostSystemName; vcpkg manifest mode, platform-conditional deps
  ("platform": "windows" for d3d12-memory-allocator, directx12-agility,
  directx-dxc); baseline pinned in vcpkg-configuration.json.
- Globs: plain GLOB per directory, NEVER GLOB_RECURSE across src/ (it swallows
  platform backend dirs — this bit us). Platform sources+headers glob inside
  their if(WIN32)/if(APPLE) blocks only.
- **Two-level project**: root `CMakeLists.txt` is a WORKSPACE (vcpkg wiring,
  artifact layout, which subprojects build). `engine/CMakeLists.txt` calls its
  own `cmake_minimum_required(3.25...4.3)` + `project(engine)` so the engine is
  self-contained: `add_subdirectory(<repo>/engine)` from a foreign project is
  sufficient, and our policies don't depend on the embedder's.
- **One consumable target: `engine::engine`.** Modules are OBJECT libraries
  (engine_core/rhi/rendergraph/renderer/scene/assets/app), public `include/` +
  private `src/` per module, linked PRIVATE into the `engine` facade — the one
  static/shared boundary. OBJECT-library objects are added ONLY to the target
  that names them directly, never transitively: that is why `sample` linking
  engine_app failed to link, and why the facade must name all seven.
- The facade's PUBLIC surface is declared explicitly in engine/CMakeLists.txt:
  cxx_std_23, ENGINE_PUBLIC_DEFINES, each module's `include/`, and the only two
  external deps that leak through public headers (Microsoft::DirectXMath for
  core/math.h, Boost::log for core/log.h). Everything else is PRIVATE.
- `engine_common` INTERFACE = std level + platform/config defines (consumer-safe,
  mirrored onto the facade). `engine_warnings` INTERFACE = -Wall/-Wextra/-Wshadow,
  /W4 /arch:AVX2, NOMINMAX — linked **PRIVATE** by engine targets so a consumer
  never inherits our warning or ISA flags. Both live in engine/, not the root.
- POSITION_INDEPENDENT_CODE ON per module: OBJECT libs do NOT inherit PIC from
  the shared lib they fold into. BUILD_SHARED_LIBS=ON builds but is NOT yet
  consumable — visibility is hidden and no symbol is annotated ENGINE_API
  (core/api.h) yet; configure emits a warning saying so. Static is supported.
- `ENGINE_BUILD_TESTS` defaults OFF in engine/ (embedders get no test suite);
  the workspace root opts in with a non-FORCE cache `set`, so a command-line
  `-DENGINE_BUILD_TESTS=OFF` still wins. `enable_testing()` must be called in
  the top-level dir. Tests link `engine::engine`, i.e. the consumer's target.
- Windows runtime staging (Agility exports per-exe, D3D12Core.dll into a D3D12/
  subfolder, dxil.dll, dxcompiler.dll) is engine-owned:
  `engine_stage_runtime_dependencies(<exe>)` from engine/cmake/EngineRuntime.cmake.
  A consumer calls it in one line and knows none of those details.
- vcpkg vulkan deps are `vulkan` (headers + loader) and `vulkan-validationlayers`,
  both UNGATED. They replaced `vulkan-sdk-components`, which also dragged in sdl2,
  glm, glslang, shaderc, spirv-cross, volk, jsoncpp, valijson, mimalloc and VMA —
  none used here — and duplicated the separate directx-dxc entry.
- No `install()`/`export()` yet — deliberate; adding it means deciding how the
  internal OBJECT libs relate to the export set.

## Milestones
1. ✅ core: assert/log/arena/pool/refcount/object/math + tests (all green)
2. ✅ D3D12 bring-up: device, debug arsenal, fence, swap chain, frames-in-flight,
   enhanced barriers, deferred delete, resize, clean shutdown. Application base
   + Win32 window + ClearSample.
   → 2b ✅ Metal at m2 parity, unmodified ClearSample as proof.
3. (CURRENT) Upload ring, buffer/texture creation (D3D12MA), bindless heap, DXC + PSO,
   **triangle** via vertex pulling (draw(3), SV_VertexID, no vertex buffers bound).
4. Shader hot reload + ImGui overlay (frame time).
5. Render graph: pass declaration, resource lifetime, barrier generation,
   transient aliasing. THE learning centerpiece; also when RHI seams get
   extracted for real.
6. Renderer: proxies, RenderScene, views, base pass, sorting (command queue inline).
7. Scene: World/Actor/Component, tick groups, transforms. Sample suite / stress
   scenes as the forcing function (no game).
8. Vulkan backend ✅ m2 parity on Win/macOS (validates the RHI barrier seam —
   sync2 is the only backend where barrier() is real). REMAINING: SDL3 window
   layer → Linux native,
   macOS via MoltenVK.
9. Audio: miniaudio for the DEVICE layer only; own mixer, voice management
   (stealing policy), Ref<SoundAsset>, distance/pan spatialization. The audio
   callback is a real-time thread: no locks, no allocation, no check() aborts —
   game→audio via lock-free command queue (preview of m10's discipline).
10. Render thread flip (game/render split, command queue goes real).
11. C ABI + one language binding (C# easiest) — design public structs to be
    flattenable to C now (handles are u32, descs are POD, no callbacks in API).
12. Networking, TIER 1 ONLY: UDP transport + reliability layer (ack/resend/
    ordering — Gaffer on Games is the curriculum), snapshot replication of
    transforms via the visit() seam, client interpolation. Needs scene layer +
    fixed timestep (m7) and realistically m10. Tier 2 (prediction, rollback,
    lag compensation, interest management) is explicitly OUT — a project, not
    a milestone.

## Repo layout
CMakeLists.txt (workspace root),
engine/ (the library, own project()): {core,rhi,rendergraph,renderer,scene,
assets,app}/{include,src}, tests/ (doctest, ctest), shaders/ (.vs/.ps/.cs.hlsl
→ DXC), third_party/{metal-cpp,MoltenVK}, cmake/{CompileShaders,EngineRuntime}.cmake.
sample/ (a consumer of engine::engine, sibling of engine/ — NOT inside it).

## Current task
Milestone 3, on D3D12, in dependency order: (1) upload ring buffer (leans on
begin_frame's fence guarantee), (2) buffer/texture creation via D3D12MA +
deferred delete in earnest, (3) shader-visible bindless CBV/SRV/UAV heap —
bindless_index() becomes real, (4) DXC runtime compilation + root signature
(root constants + heap-direct flags) + PSO, (5) triangle via vertex pulling:
draw(3), SV_VertexID, positions from a structured buffer via
ResourceDescriptorHeap — no vertex buffers bound. Also decide & test the HLSL
matrix convention (transpose-on-upload vs -Zpr) when the first cbuffer lands.
Metal stays frozen at parity — do not implement m3 there.
