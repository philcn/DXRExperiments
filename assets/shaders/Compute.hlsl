RWTexture2D<float4> gOutput : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint3 Gid : SV_GroupID)
{
    gOutput[DTid.xy] = float4(Gid.x / 256.0, Gid.y / 256.0, 1.0, 1.0);
}
