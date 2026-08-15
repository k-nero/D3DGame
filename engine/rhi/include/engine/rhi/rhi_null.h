//
// Created by Thanh Nguyen on 15/8/26.
//

#pragma once
#ifndef ENGINE_RHI_NULL_H
#define ENGINE_RHI_NULL_H
// eng/rhi/rhi_null.h — test-inspection API for the Null backend.
//
// The Null backend doesn't just no-op: it RECORDS every command-list call
// into a per-frame list. This is the render graph's future test harness —
// "assert the shadow-pass barrier came before the base-pass read" runs on
// the Mac in milliseconds against these records.
//
// Recording semantics: cleared at begin_frame, appended during the frame,
// stable between end_frame and the next begin_frame — inspect there.
//
// recording() is valid ONLY for devices created with Backend::Null.

#include <array>
#include <cstddef>
#include <span>
#include <variant>
#include <vector>

#include "rhi.h"

namespace engine::rhi::null {

    struct BarrierRec        { TextureBarrier texture; };   // one record per texture barrier
    struct BufferBarrierRec  { BufferBarrier buffer; };     // one per buffer barrier
    struct ClearRec          { TextureHandle target; std::array<float, 4> color{}; };
    struct SetTargetsRec     { std::vector<TextureHandle> colors; TextureHandle depth; };
    struct ViewportRec       { uint32_t width, height; };
    struct SetPSORec         { PSOHandle pso; };
    struct SetIndexBufferRec { BufferHandle buffer; Format format; };
    struct PushConstantsRec  { std::vector<std::byte> data; };
    struct DrawRec           { uint32_t vertex_count, instance_count; };
    struct DrawIndexedRec    { uint32_t index_count, instance_count; };
    struct DispatchRec       { uint32_t x, y, z; };
    struct PresentRec        { uint64_t frame_counter; };

    // C++17: std::variant — a type-safe tagged union. Tests use
    // std::holds_alternative<T>(rec) / std::get_if<T>(&rec) to match records.
    using Record = std::variant<
        BarrierRec, BufferBarrierRec, ClearRec, SetTargetsRec, ViewportRec,
        SetPSORec, SetIndexBufferRec, PushConstantsRec,
        DrawRec, DrawIndexedRec, DispatchRec, PresentRec>;

    [[nodiscard]] std::span<const Record> recording(const IDevice*);

} // namespace eng::rhi::null
#endif //ENGINE_RHI_NULL_H
