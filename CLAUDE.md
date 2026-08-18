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
- **Metal**: currently being brought to m2 parity ONLY (rainbow/resize/shutdown).
  metal-cpp vendored in third_party/metal-cpp (from Apple zip, VERSION.txt,
  updated with Xcode upgrades). *_PRIVATE_IMPLEMENTATION defines in exactly one TU.
  Known items: barrier() = no-op (auto hazard tracking), clear = render-pass
  load action (deferral is render-graph work, don't build now), dispatch_semaphore
  pacing, drawable autorelease-pool-per-frame (else 3-frame freeze),
  drawableSize × backingScaleFactor (else Retina blur), app_macos.mm is ObjC++
  (AppKit not covered by metal-cpp), pump() = manual event loop, never [NSApp run].
- **Vulkan**: designated after render graph (m5+): Linux native + Mac via
  MoltenVK, reuses DXC via -spirv. Placeholder folder only today.

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
- No `install()`/`export()` yet — deliberate; adding it means deciding how the
  internal OBJECT libs relate to the export set.

## Milestones
1. ✅ core: assert/log/arena/pool/refcount/object/math + tests (all green)
2. ✅ D3D12 bring-up: device, debug arsenal, fence, swap chain, frames-in-flight,
   enhanced barriers, deferred delete, resize, clean shutdown. Application base
   + Win32 window + ClearSample.
   → 2b (CURRENT): Metal to m2 parity, unmodified ClearSample as proof.
3. Upload ring, buffer/texture creation (D3D12MA), bindless heap, DXC + PSO,
   **triangle** via vertex pulling (draw(3), SV_VertexID, no vertex buffers bound).
4. Shader hot reload + ImGui overlay (frame time).
5. Render graph: pass declaration, resource lifetime, barrier generation,
   transient aliasing. THE learning centerpiece; also when RHI seams get
   extracted for real.
6. Renderer: proxies, RenderScene, views, base pass, sorting (command queue inline).
7. Scene: World/Actor/Component, tick groups, transforms. Sample suite / stress
   scenes as the forcing function (no game).
8. Vulkan backend (validates RHI).
9. C ABI + one language binding (C# easiest) — design public structs to be
   flattenable to C now (handles are u32, descs are POD, no callbacks in API).
10. Render thread flip (game/render split, command queue goes real).

## Repo layout
CMakeLists.txt (workspace root),
engine/ (the library, own project()): {core,rhi,rendergraph,renderer,scene,
assets,app}/{include,src}, tests/ (doctest, ctest), shaders/ (.vs/.ps/.cs.hlsl
→ DXC), third_party/metal-cpp, cmake/{CompileShaders,EngineRuntime}.cmake.
sample/ (a consumer of engine::engine, sibling of engine/ — NOT inside it).

## Current task
Metal m2 parity (see Backends). Then m3 on D3D12. Do not let Metal drift past
parity into feature work.
