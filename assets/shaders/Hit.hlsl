#include "Common.hlsl"

StructuredBuffer<STriVertex> bTriVertex : register(t0);

[shader("closesthit")]
void ClosestHit(inout HitInfo payload, Attributes attrib)
{
    float3 barycentrics = float3(1.f - attrib.barycentric.x - attrib.barycentric.y,
                                 attrib.barycentric.x,
                                 attrib.barycentric.y);
    
    uint vertID = 3 * PrimitiveIndex();
    
    float3 hitColor = bTriVertex[vertID].color * barycentrics.x 
                    + bTriVertex[vertID + 1].color * barycentrics.y 
                    + bTriVertex[vertID + 2].color * barycentrics.z;
    
    payload.colorAndDistance = float4(hitColor, RayTCurrent());
}