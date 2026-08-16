// eng/rhi/src/d3d12/d3d12_agility.cpp
//
// The Agility SDK contract: these two exports tell the D3D12 loader to use
// the redistributable runtime from .\D3D12\ instead of the OS one. Without
// them you are SILENTLY on the OS runtime — SM 6.6 / enhanced-barrier
// feature checks will fail confusingly on older Windows builds.
//
// D3D12_SDK_VERSION comes from the Agility headers, so this stays in sync
// with the vcpkg package automatically. The DLLs (D3D12Core.dll, and
// d3d12SDKLayers.dll for debug) must be copied to $<exe_dir>/D3D12/ —
// the post-build step in game/CMakeLists.txt does that.
#include <d3d12.h>
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = D3D12_SDK_VERSION;}

extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }