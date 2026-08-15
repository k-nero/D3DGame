//
// Created by Thanh Nguyen on 15/8/26.
//

// eng/rhi/src/metal/metal_stub.cpp
//
// TOOLCHAIN PROOF, not a backend. Scope (enforced by review + DECISIONS.md):
//   1. metal-cpp compiles in this build
//   2. Foundation/Metal/QuartzCore link
//   3. A real MTLDevice can be created and queried
// Everything else delegates to the Null backend. If you find yourself adding
// a second Metal call to this file, you are writing the second-backend
// project — stop and go read DECISIONS.md.
//
// metal-cpp rule: these three defines must appear in EXACTLY ONE .cpp in the
// whole program (they instantiate the selector/class symbols). This is that
// file.
#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include <engine/core/log.h>
#include <engine/rhi/rhi.h>

namespace engine::rhi {

    // Provided by the null backend TU:
    IDevice* create_null_device();

    static IDevice* create_metal_stub_device() {
        // NS::Object follows Cocoa ownership: Create/alloc-init results are OWNED
        // by us — release when done. (Same convention family as COM's "out-params
        // arrive AddRef'd", spelled Apple-style.)
        MTL::Device* dev = MTL::CreateSystemDefaultDevice();
        if (!dev) {
            log::warn("metal-stub: no Metal device available");
            return create_null_device();
        }

        log::info("metal-stub: device '{}', unified memory: {}, max buffer: {} MB",
                 dev->name()->utf8String(),
                 dev->hasUnifiedMemory(),
                 dev->maxBufferLength() / (1024 * 1024));

        dev->release();   // proof complete; a real backend would keep this

        // The "backend" is Null wearing a name tag.
        return create_null_device();
    }

} // namespace eng::rhi