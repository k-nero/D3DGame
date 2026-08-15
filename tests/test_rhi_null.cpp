//
// Created by Thanh Nguyen on 15/8/26.
//
#include <doctest/doctest.h>

#include <engine/rhi/rhi.h>
#include <engine/rhi/rhi_null.h>

using namespace engine::rhi;

namespace {
struct DeviceFixture {
    IDevice* dev;
    DeviceFixture()  { dev = create_device(Backend::Null, {.width = 640, .height = 360}); }
    ~DeviceFixture() { destroy_device(dev); }
};
} // namespace

TEST_CASE("frame loop: index cycles, backbuffer valid, recording is per-frame") {
    DeviceFixture f;

    auto [cmd, backbuffer, frame_index] = f.dev->begin_frame();
    CHECK(frame_index == 0);
    CHECK(!backbuffer.is_null());
    CHECK(cmd != nullptr);
    f.dev->end_frame();

    auto fr1 = f.dev->begin_frame();
    CHECK(fr1.frame_index == 1);              // frames_in_flight defaults to 2
    CHECK(null::recording(f.dev).empty());    // begin_frame cleared frame 0's records
    f.dev->end_frame();

    auto fr2 = f.dev->begin_frame();
    CHECK(fr2.frame_index == 0);              // ...and wraps
    f.dev->end_frame();
}

TEST_CASE("recording preserves order and payloads — the graph-test mechanism") {
    DeviceFixture f;
    auto fr = f.dev->begin_frame();

    // milestone-2's frame, spelled through the interface:
    const TextureBarrier to_rt{
        .texture       = fr.backbuffer,
        .sync_after    = Sync::RenderTarget,
        .access_after  = Access::RenderTarget,
        .layout_before = Layout::Present,
        .layout_after  = Layout::RenderTarget,
    };
    fr.cmd->barrier({{to_rt}});
    fr.cmd->clear_render_target(fr.backbuffer, {0.1f, 0.2f, 0.3f, 1.0f});
    fr.cmd->draw(3);

    const TextureBarrier to_present{
        .texture       = fr.backbuffer,
        .sync_before   = Sync::RenderTarget,
        .access_before = Access::RenderTarget,
        .layout_before = Layout::RenderTarget,
        .layout_after  = Layout::Present,
    };
    fr.cmd->barrier({{to_present}});
    f.dev->end_frame();

    auto rec = null::recording(f.dev);
    REQUIRE(rec.size() == 5);                 // barrier, clear, draw, barrier, present

    // C++17 std::get_if: null if the record isn't that alternative
    auto* b0 = std::get_if<null::BarrierRec>(&rec[0]);
    REQUIRE(b0);
    CHECK(b0->texture.layout_after == Layout::RenderTarget);

    auto* cl = std::get_if<null::ClearRec>(&rec[1]);
    REQUIRE(cl);
    CHECK(cl->target == fr.backbuffer);
    CHECK(cl->color[2] == doctest::Approx(0.3f));

    auto* dr = std::get_if<null::DrawRec>(&rec[2]);
    REQUIRE(dr);
    CHECK(dr->vertex_count == 3);
    CHECK(dr->instance_count == 1);           // convenience overload's default

    auto* b1 = std::get_if<null::BarrierRec>(&rec[3]);
    REQUIRE(b1);
    CHECK(b1->texture.layout_after == Layout::Present);

    CHECK(std::holds_alternative<null::PresentRec>(rec[4]));
}

TEST_CASE("upload buffer: initial_data lands in mappable storage") {
    DeviceFixture f;
    constexpr uint32_t payload[4] = {1, 2, 3, 4};

    auto h = f.dev->create_buffer({
        .size         = sizeof(payload),
        .memory       = Memory::Upload,
        .usage        = BufferUsage::Storage,
        .initial_data = std::as_bytes(std::span{payload}),
    });

    void* p = f.dev->map(h);
    REQUIRE(p != nullptr);
    CHECK(std::memcmp(p, payload, sizeof(payload)) == 0);

    static_cast<uint32_t*>(p)[0] = 99;        // CPU-writable, like a real upload heap
    f.dev->unmap(h);
    CHECK(static_cast<uint32_t*>(f.dev->map(h))[0] == 99);
    f.dev->destroy(h);
}

TEST_CASE("bindless_index is stable across other allocations") {
    const DeviceFixture f;
    const auto a = f.dev->create_texture({.width = 4, .height = 4});
    const auto b = f.dev->create_texture({.width = 8, .height = 8});

    const uint32_t ia = f.dev->bindless_index(a);
    CHECK(f.dev->bindless_index(a) == ia);    // stable
    CHECK(f.dev->bindless_index(b) != ia);    // distinct

    f.dev->destroy(a);
    f.dev->destroy(b);
}

TEST_CASE("resize invalidates the old backbuffer handle") {
    const DeviceFixture f;
    auto before = f.dev->begin_frame();
    f.dev->end_frame();

    f.dev->resize(1920, 1080);

    const auto after = f.dev->begin_frame();
    CHECK(after.backbuffer != before.backbuffer);   // stale handles must die —
    f.dev->end_frame();                             // same lesson ResizeBuffers teaches
}