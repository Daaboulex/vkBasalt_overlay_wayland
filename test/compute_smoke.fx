texture2D SmokeBackBufferTex : COLOR;
sampler2D SmokeBackBuffer { Texture = SmokeBackBufferTex; };

texture2D SmokeWorkTex { Width = 1280; Height = 720; Format = RGBA8; };
sampler2D SmokeWorkSampler { Texture = SmokeWorkTex; };
storage2D SmokeWorkStorage { Texture = SmokeWorkTex; };

groupshared float SmokeScratch[64];

void SmokeVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

void SmokeCS(uint3 tid : SV_DispatchThreadID, uint3 gid : SV_GroupThreadID)
{
    float4 colour = tex2Dfetch(SmokeBackBuffer, int2(tid.xy));

    SmokeScratch[gid.x] = colour.r;
    barrier();

    tex2Dstore(SmokeWorkStorage, int2(tid.xy), float4(SmokeScratch[gid.x], colour.g, colour.b, colour.a));
    tex2Dstore(SmokeWorkStorage, int2(tid.xy) + int2(1, 0), 0.5);
}

float4 SmokePS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return tex2D(SmokeWorkSampler, uv);
}

technique ComputeSmoke
{
    pass
    {
        ComputeShader = SmokeCS<8, 8>;
        DispatchSizeX = 160;
        DispatchSizeY = 90;
    }
    pass
    {
        VertexShader = SmokeVS;
        PixelShader  = SmokePS;
    }
}
