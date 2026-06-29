Texture2D shaderTexture : register(t0);
SamplerState sampleType : register(s0);

struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float2 tex : TEXCOORD0;
};

VS_OUTPUT VS(uint id : SV_VertexID) {
    VS_OUTPUT output;
    output.tex = float2((id << 1) & 2, id & 2);
    output.position = float4(output.tex * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

float4 PS(VS_OUTPUT input) : SV_TARGET {
    float4 ui = shaderTexture.Sample(sampleType, input.tex);
    float alpha = ui.a;
    if (ui.r < 0.05 && ui.g < 0.05 && ui.b < 0.05) {
        alpha = 0.0;
    }
    return float4(ui.rgb, alpha);
}
