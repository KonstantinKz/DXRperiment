#include "Common.hlsl"

StructuredBuffer<STriVertex> bTriVertex : register(t0);

struct TriColor
{
    float4 a;
    float4 b;
    float4 c;
};

cbuffer cColors : register(b0)
{
    TriColor triC[3];
}

[shader("closesthit")]
void ClosestHit(inout HitInfo payload, Attributes attrib)
{
    float3 barycentrics = float3(1.f - attrib.barycentric.x - attrib.barycentric.y,
                                 attrib.barycentric.x,
                                 attrib.barycentric.y);
    
    uint vertID = 3 * PrimitiveIndex();
    
    
    float3 hitColor = float3(0.6, 0.7, 0.6);
    
    if (InstanceID() < 3)
    {
        int instId = InstanceID();
        hitColor = triC[instId].a * barycentrics.x + triC[instId].b * barycentrics.y +
             triC[instId].c * barycentrics.z;
    }
    
    //hitColor = bTriVertex[vertID].color * barycentrics.x 
    //                + bTriVertex[vertID + 1].color * barycentrics.y 
    //                + bTriVertex[vertID + 2].color * barycentrics.z;
    
    payload.colorAndDistance = float4(hitColor, RayTCurrent());
}