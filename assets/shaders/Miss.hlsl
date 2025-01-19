#include "Common.hlsl"

[shader("miss")]
void Miss(inout HitInfo payload : SV_RayPayload)
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    float2 dims = DispatchRaysDimensions().xy;
    
    float ramp = launchIndex.y / dims.y;
    payload.colorAndDistance = float4(0.2f, 0.2f, 0.4f - 0.1f * ramp, -1.f);
}