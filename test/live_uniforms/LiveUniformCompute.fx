texture2D LiveComputeWorkTex { Width = 16; Height = 16; Format = R32F; };
sampler2D LiveComputeWorkSampler { Texture = LiveComputeWorkTex; };
storage2D LiveComputeWorkStorage { Texture = LiveComputeWorkTex; };

uniform float LiveComputeStrength <
    ui_label = "Compute Strength";
    ui_min = 0.0;
    ui_max = 1.0;
> = 0.5;

void LiveComputeVS(uint id : SV_VertexID, out float4 pos : SV_Position,
                   out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0),
                 0.0, 1.0);
}

void LiveComputeCS(uint3 tid : SV_DispatchThreadID)
{
    tex2Dstore(LiveComputeWorkStorage, int2(tid.xy), LiveComputeStrength);
}

float4 LiveComputePS(float4 pos : SV_Position,
                     float2 uv : TEXCOORD) : SV_Target
{
    return tex2D(LiveComputeWorkSampler, uv).xxxx;
}

technique LiveUniformCompute
{
    pass
    {
        ComputeShader = LiveComputeCS<8, 8>;
        DispatchSizeX = 2;
        DispatchSizeY = 2;
    }
    pass
    {
        VertexShader = LiveComputeVS;
        PixelShader = LiveComputePS;
    }
}
