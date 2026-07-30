float scaled(float x, float factor = 2.0, bool negate = false)
{
    float r = x * factor;
    return negate ? -r : r;
}

float4 DefVS(uint id : SV_VertexID) : SV_Position { return float4(0,0,0,1); }

float4 DefPS(float4 pos : SV_Position) : SV_Target
{
    float a = scaled(1.0);
    float b = scaled(1.0, 3.0);
    float c = scaled(1.0, 3.0, true);
    return float4(a, b, c, 1.0);
}

technique DefaultParam { pass { VertexShader = DefVS; PixelShader = DefPS; } }
