struct BoolVaryingVSOUT
{
    float4 pos   : SV_Position;
    nointerpolation bool2 flags : TEXCOORD0;
};

BoolVaryingVSOUT BoolVaryingVS(uint id : SV_VertexID)
{
    BoolVaryingVSOUT o;
    o.pos   = float4(0.0, 0.0, 0.0, 1.0);
    o.flags = bool2(true, false);
    return o;
}

float4 BoolVaryingPS(BoolVaryingVSOUT i) : SV_Target
{
    return float4(i.flags.x ? 1.0 : 0.0, i.flags.y ? 1.0 : 0.0, 0.0, 1.0);
}

technique BoolVarying
{
    pass
    {
        VertexShader = BoolVaryingVS;
        PixelShader  = BoolVaryingPS;
    }
}
