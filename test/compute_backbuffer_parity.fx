texture2D ParityBackBufferTex : COLOR;
sampler2D ParityBackBuffer { Texture = ParityBackBufferTex; };

void ParityVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

void ParityNoOpCS(uint3 tid : SV_DispatchThreadID)
{
}

float4 ParityCopyPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return tex2D(ParityBackBuffer, uv);
}

technique ComputeBackbufferParity
{
    pass { ComputeShader = ParityNoOpCS<1, 1>; DispatchSizeX = 1; DispatchSizeY = 1; }
    pass { ComputeShader = ParityNoOpCS<1, 1>; DispatchSizeX = 1; DispatchSizeY = 1; }
    pass { ComputeShader = ParityNoOpCS<1, 1>; DispatchSizeX = 1; DispatchSizeY = 1; }
    pass { ComputeShader = ParityNoOpCS<1, 1>; DispatchSizeX = 1; DispatchSizeY = 1; }
    pass { ComputeShader = ParityNoOpCS<1, 1>; DispatchSizeX = 1; DispatchSizeY = 1; }
    pass { VertexShader = ParityVS; PixelShader = ParityCopyPS; }
}
