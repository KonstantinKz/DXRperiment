#include "Common.hlsl"

//StructuredBuffer<STriVertex> bTriVertex : register(t0);

// For globalconst buffer
//struct TriColor
//{
//    float4 a;
//    float4 b;
//    float4 c;
//};

//cbuffer cColors : register(b0)
//{
//    TriColor triC[3];
//}

cbuffer Colors : register(b0)
{
    float4 A;
    float4 B;
    float4 C;
    float4 D;
}

[shader("closesthit")]
void ClosestHit(inout HitInfo payload, Attributes attrib)
{
    float3 barycentrics = float3(1.f - attrib.barycentric.x - attrib.barycentric.y,
                                 attrib.barycentric.x,
                                 attrib.barycentric.y);
    
    float3 hitColor = float3(0.6, 0.7, 0.6);
    
    
        int instId = InstanceID();
        hitColor = A * barycentrics.x + B * barycentrics.y +
             C * barycentrics.z;
    
    //uint vertID = 3 * PrimitiveIndex();
    //hitColor = bTriVertex[vertID].color * barycentrics.x 
    //                + bTriVertex[vertID + 1].color * barycentrics.y 
    //                + bTriVertex[vertID + 2].color * barycentrics.z;
    
    payload.colorAndDistance = float4(hitColor, RayTCurrent());
}

[shader("closesthit")]
void PlaneClosestHit(inout HitInfo payload, Attributes attrib)
{
    float3 barycentrics = float3(1.f - attrib.barycentric.x - attrib.barycentric.y,
                                 attrib.barycentric.x,
                                 attrib.barycentric.y);

    float3 hitColor = float3(0.7, 0.7, 0.3);

    payload.colorAndDistance = float4(hitColor, RayTCurrent());
}