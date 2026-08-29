texture2D DepthLayoutTex : DEPTH;
sampler2D DepthLayoutSampler { Texture = DepthLayoutTex; };

void DepthLayoutVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 DepthLayoutPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    const float depth = tex2D(DepthLayoutSampler, uv).r;
    return float4(depth, depth, depth, 1.0);
}

technique DepthLayoutSmoke
{
    pass
    {
        VertexShader = DepthLayoutVS;
        PixelShader = DepthLayoutPS;
    }
}
