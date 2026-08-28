namespace SharedTextureProbe
{
    texture2D PublishedColor
    {
        Width = BUFFER_WIDTH;
        Height = BUFFER_HEIGHT;
        Format = RGBA8;
    };

    sampler2D PublishedColorSampler { Texture = PublishedColor; };
}

void SharedProbeVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 ConsumeSharedColorPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return tex2D(SharedTextureProbe::PublishedColorSampler, uv);
}

technique SharedTextureConsumer
{
    pass
    {
        VertexShader = SharedProbeVS;
        PixelShader = ConsumeSharedColorPS;
    }
}
