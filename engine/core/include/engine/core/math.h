//
// Created by Thanh Nguyen on 15/8/26.
//
#pragma once

#ifndef ENGINE_MATH_H
#define ENGINE_MATH_H
#include <DirectXMath.h>

namespace engine::math {

    // Aliases so call sites say eng::float3 — if the math library ever changes,
    // the blast radius is this file.
    using float2   = DirectX::XMFLOAT2;
    using float3   = DirectX::XMFLOAT3;
    using float4   = DirectX::XMFLOAT4;
    using float4x4 = DirectX::XMFLOAT4X4;

    // Storage-type transform (convention 4): plain, packable, serializable.
    struct Transform {
        float3 position{0.f, 0.f, 0.f};
        float4 rotation{0.f, 0.f, 0.f, 1.f};   // quaternion, identity by default
        float3 scale{1.f, 1.f, 1.f};
    };

    // load -> compute (register types) — callers XMStore* the result where needed.
    [[nodiscard]] inline DirectX::XMMATRIX to_matrix(const Transform& t) {
        using namespace DirectX;
        const XMVECTOR scale = XMLoadFloat3(&t.scale);
        const XMVECTOR rot   = XMLoadFloat4(&t.rotation);
        const XMVECTOR pos   = XMLoadFloat3(&t.position);
        // AffineTransformation composes scale -> rotate -> translate,
        // matching convention 2's left-to-right order.
        return XMMatrixAffineTransformation(scale, XMVectorZero(), rot, pos);
    }

    [[nodiscard]] inline float4x4 to_float4x4(const Transform& t) {
        float4x4 m;
        DirectX::XMStoreFloat4x4(&m, to_matrix(t));
        return m;
    }

    // Convention 1 wrappers: the ONLY projection/view helpers the engine uses.
    // Reaching past these to a *LH function is a convention violation.
    [[nodiscard]] inline DirectX::XMMATRIX perspective(float fov_y, float aspect,
                                                       float near_z, float far_z) {
        return DirectX::XMMatrixPerspectiveFovRH(fov_y, aspect, near_z, far_z);
    }

    [[nodiscard]] inline DirectX::XMMATRIX look_at(const float3& eye, const float3& target,
                                                   const float3& up) {
        using namespace DirectX;
        return XMMatrixLookAtRH(XMLoadFloat3(&eye), XMLoadFloat3(&target), XMLoadFloat3(&up));
    }
}

#endif //ENGINE_MATH_H
