RWTexture2D<float4> gOutput : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    gOutput[DTid.xy] = float4(1.0, 0.0, 0.0, 1.0);
}
