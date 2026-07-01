Texture2D<float4> InputTex : register(t0);
RWTexture2D<float4> OutputTex : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 dstSize;
    OutputTex.GetDimensions(dstSize.x, dstSize.y);
    if (dispatchThreadID.x >= dstSize.x || dispatchThreadID.y >= dstSize.y) return;
    
    uint2 srcSize;
    InputTex.GetDimensions(srcSize.x, srcSize.y);
    
    float2 uv = (float2(dispatchThreadID.xy) + 0.5f) / float2(dstSize);
    uint2 srcPos = uint2(uv * float2(srcSize));
    OutputTex[dispatchThreadID.xy] = InputTex[srcPos];
}
