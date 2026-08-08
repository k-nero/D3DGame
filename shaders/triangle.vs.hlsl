// Placeholder so the offline-compile target has something to chew on.
float4 main(uint vid : SV_VertexID) : SV_Position
{
    float2 p = float2((vid << 1) & 2, vid & 2);
    return float4(p * 2.0 - 1.0, 0.0, 1.0);
}
