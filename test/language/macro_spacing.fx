#define SPACING_RUN(stmt) { stmt }

float4 SpacingVS(uint id : SV_VertexID) : SV_Position
{
    return float4(0.0, 0.0, 0.0, 1.0);
}

float4 SpacingPS(float4 pos : SV_Position) : SV_Target
{
    float total = 0.0;
    SPACING_RUN(float val = 1.0; total += val;)
    return float4(total, 0.0, 0.0, 1.0);
}

technique MacroSpacing
{
    pass
    {
        VertexShader = SpacingVS;
        PixelShader  = SpacingPS;
    }
}
