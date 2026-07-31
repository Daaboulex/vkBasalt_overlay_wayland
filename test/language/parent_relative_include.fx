#include "../lang_parent_header.fxh"

float4 ParentIncludeVS(uint id : SV_VertexID) : SV_Position
{
    return float4(0.0, 0.0, 0.0, 1.0);
}

float4 ParentIncludePS(float4 pos : SV_Position) : SV_Target
{
    return float4(PARENT_HEADER_VALUE, 0.0, 0.0, 1.0);
}

technique ParentRelativeInclude
{
    pass
    {
        VertexShader = ParentIncludeVS;
        PixelShader  = ParentIncludePS;
    }
}
