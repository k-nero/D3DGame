//
// Created by Thanh Nguyen on 15/8/26.
//
// Pins the CONVENTIONS in math.h — not testing Microsoft's math, testing
// that a handedness/order violation fails ctest on the Mac instead of
// rendering inside-out on Windows months later.
#include <doctest/doctest.h>

#include <engine/core/math.h>

using namespace DirectX;
using namespace engine::math;

static float get_x(FXMVECTOR v) { return XMVectorGetX(v); }
static float get_z(FXMVECTOR v) { return XMVectorGetZ(v); }

TEST_CASE("convention 1+3: RH projection, depth [0,1], camera looks down -Z") {
    const XMMATRIX p = perspective(XM_PIDIV2, 16.f / 9.f, 0.1f, 100.f);

    // A point IN FRONT of an RH camera is at negative view-space Z.
    // Near plane -> depth 0:
    const XMVECTOR near_pt = XMVector3TransformCoord(XMVectorSet(0, 0, -0.1f, 1), p);
    CHECK(get_z(near_pt) == doctest::Approx(0.0f).epsilon(1e-4));

    // Far plane -> depth 1:
    const XMVECTOR far_pt = XMVector3TransformCoord(XMVectorSet(0, 0, -100.f, 1), p);
    CHECK(get_z(far_pt) == doctest::Approx(1.0f).epsilon(1e-4));
}

TEST_CASE("convention 2: row-vector, left-to-right composition") {
    // scale THEN translate: (1 * 2) + 10 = 12.
    // If this reads 22, someone composed right-to-left (column-vector habits).
    const XMMATRIX m = XMMatrixScaling(2, 2, 2) * XMMatrixTranslation(10, 0, 0);
    const XMVECTOR r = XMVector3TransformCoord(XMVectorSet(1, 0, 0, 1), m);
    CHECK(get_x(r) == doctest::Approx(12.0f));
}

TEST_CASE("look_at: RH view space has the target on -Z") {
    const XMMATRIX v = look_at({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    // World origin, seen from (0,0,5) looking at it, lands at view-space z = -5.
    const XMVECTOR origin_in_view = XMVector3TransformCoord(XMVectorZero(), v);
    CHECK(get_z(origin_in_view) == doctest::Approx(-5.0f));
}

TEST_CASE("Transform: identity by default") {
    constexpr Transform t{};
    XMVECTOR p = XMVector3TransformCoord(XMVectorSet(1, 2, 3, 1), to_matrix(t));
    CHECK(get_x(p) == doctest::Approx(1.0f));
    CHECK(XMVectorGetY(p) == doctest::Approx(2.0f));
    CHECK(get_z(p) == doctest::Approx(3.0f));
}

TEST_CASE("Transform: scale -> rotate -> translate order") {
    Transform t;
    t.scale    = {2.f, 2.f, 2.f};
    XMStoreFloat4(&t.rotation, XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), XM_PIDIV2));
    t.position = {10.f, 0.f, 0.f};

    // (1,0,0): scale -> (2,0,0); rotate +90° about Y (RH) -> (0,0,-2);
    // translate -> (10,0,-2). Any other result = wrong order or wrong handedness.
    XMVECTOR p = XMVector3TransformCoord(XMVectorSet(1, 0, 0, 1), to_matrix(t));
    CHECK(get_x(p) == doctest::Approx(10.0f).epsilon(1e-4));
    CHECK(XMVectorGetY(p) == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(get_z(p) == doctest::Approx(-2.0f).epsilon(1e-4));
}

TEST_CASE("to_float4x4: storage roundtrip preserves the matrix") {
    Transform t;
    t.position = {1.f, 2.f, 3.f};
    const float4x4 stored = to_float4x4(t);       // register -> storage
    const XMMATRIX back = XMLoadFloat4x4(&stored); // storage -> register

    XMVECTOR p = XMVector3TransformCoord(XMVectorZero(), back);
    CHECK(get_x(p) == doctest::Approx(1.0f));
    CHECK(XMVectorGetY(p) == doctest::Approx(2.0f));
    CHECK(get_z(p) == doctest::Approx(3.0f));
}

// Convention 4 is enforced by review, not by test: XMVECTOR/XMMATRIX never
// appear as struct members. grep for "XMMATRIX" outside function bodies if
// in doubt. Convention 5 (transpose-on-upload vs -Zpr) gets its test at
// milestone 3 when the first constant buffer exists.