namespace SharedTextureProbe
{
    texture2D PublishedColor
    {
        Width = BUFFER_WIDTH;
        Height = BUFFER_HEIGHT;
        Format = RGBA8;
    };
}

void SharedProbeVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 PublishSharedColorPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return float4(0.25, 0.5, 0.75, 1.0);
}

technique SharedTextureProducer
{
    pass
    {
        VertexShader = SharedProbeVS;
        PixelShader = PublishSharedColorPS;
        RenderTarget = SharedTextureProbe::PublishedColor;
    }
}
