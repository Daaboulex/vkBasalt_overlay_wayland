uniform float Strength < ui_min = 0.0; ui_max = 1.0; > = 0.5;

float4 MinPrecVS(uint id : SV_VertexID) : SV_Position
{
    return float4(0.0, 0.0, 0.0, 1.0);
}

float4 MinPrecPS(float4 pos : SV_Position) : SV_Target
{
    min16float a = (min16float)Strength;
    min16float b = a * a + (min16float)0.25;
    return float4(b, b, b, 1.0);
}

technique MinPrecision
{
    pass
    {
        VertexShader = MinPrecVS;
        PixelShader  = MinPrecPS;
    }
}
