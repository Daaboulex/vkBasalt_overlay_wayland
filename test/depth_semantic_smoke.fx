texture2D DepthSemanticTex : DEPTH;
sampler2D DepthSemanticSampler { Texture = DepthSemanticTex; };

void DepthSemanticVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 DepthSemanticPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    const float depth = tex2D(DepthSemanticSampler, uv).r;
    return float4(depth, depth, depth, 1.0);
}

technique DepthSemanticSmoke
{
    pass
    {
        VertexShader = DepthSemanticVS;
        PixelShader = DepthSemanticPS;
    }
}
