texture2D LiveUniformBackBufferTex : COLOR;
sampler2D LiveUniformBackBuffer { Texture = LiveUniformBackBufferTex; };

uniform float LiveStrength <
    ui_label = "Strength";
    ui_tooltip = "Runtime float slider fixture";
    ui_min = 0.0;
    ui_max = 2.0;
> = 1.0;

uniform int LiveMode <
    ui_label = "Mode";
    ui_type = "combo";
    ui_items = "Pass through\0Tint\0";
> = 0;

uniform bool LiveEnabled < ui_label = "Enabled"; > = true;

void LiveUniformVS(uint id : SV_VertexID, out float4 pos : SV_Position,
                   out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0),
                 0.0, 1.0);
}

float4 LiveUniformPS(float4 pos : SV_Position,
                     float2 uv : TEXCOORD) : SV_Target
{
    float4 color = tex2D(LiveUniformBackBuffer, uv);
    if (!LiveEnabled)
        return color;
    if (LiveMode == 1)
        color.rgb *= float3(1.0, 0.75, 0.5);
    color.rgb *= LiveStrength;
    return color;
}

technique LiveUniformControls
{
    pass { VertexShader = LiveUniformVS; PixelShader = LiveUniformPS; }
}
