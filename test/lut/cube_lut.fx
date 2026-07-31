texture3D CubeLutTex < source = "identity.cube"; >
{
    Width  = 2;
    Height = 2;
    Depth  = 2;
    Format = RGBA8;
};

sampler3D CubeLutSampler { Texture = CubeLutTex; };

float4 CubeLutVS(uint id : SV_VertexID) : SV_Position
{
    return float4(0.0, 0.0, 0.0, 1.0);
}

float4 CubeLutPS(float4 pos : SV_Position) : SV_Target
{
    return tex3D(CubeLutSampler, float3(0.5, 0.5, 0.5));
}

technique CubeLut
{
    pass
    {
        VertexShader = CubeLutVS;
        PixelShader  = CubeLutPS;
    }
}
