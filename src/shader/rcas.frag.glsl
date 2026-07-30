// LICENSE
// =======
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// -------
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
// -------
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
// Software.
// -------
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE
//
// Port of FsrRcasF from AMD FidelityFX Super Resolution 1 (ffx_fsr1.h, v1.20210629).
// RCAS solves for the maximum local sharpness before clipping, rather than CAS's
// simplified contrast-to-sharpness mapping, and limits sharpening of what it
// detects as noise. It deliberately has no scaling path: AMD specifies it to run
// after a scaling step, or at 1:1.
#version 450

layout(set=0, binding=0) uniform sampler2D img;

layout (constant_id = 0) const float sharpness = 0.4;
layout (constant_id = 1) const int denoise = 1;

layout(location = 0) in vec2 textureCoord;
layout(location = 0) out vec4 fragColor;

#define FSR_RCAS_LIMIT (0.25 - (1.0 / 16.0))

#define textureLod0Offset(img, coord, offset) textureLodOffset(img, coord, 0.0f, offset)
#define textureLod0(img, coord) textureLod(img, coord, 0.0f)

float rcasMax3(float x, float y, float z)
{
    return max(x, max(y, z));
}

float rcasMin3(float x, float y, float z)
{
    return min(x, min(y, z));
}

void main()
{
    // Cross of five taps around the centre pixel 'e'.
    //    b
    //  d e f
    //    h
    vec4 inputColor = textureLod0(img, textureCoord);
    float alpha = inputColor.a;

    vec3 b = textureLod0Offset(img, textureCoord, ivec2( 0, -1)).rgb;
    vec3 d = textureLod0Offset(img, textureCoord, ivec2(-1,  0)).rgb;
    vec3 e = inputColor.rgb;
    vec3 f = textureLod0Offset(img, textureCoord, ivec2( 1,  0)).rgb;
    vec3 h = textureLod0Offset(img, textureCoord, ivec2( 0,  1)).rgb;

    // Luma-weighted noise estimate: how far the centre sits from its neighbours,
    // normalised by the local range.
    float bL = b.b * 0.5 + (b.r * 0.5 + b.g);
    float dL = d.b * 0.5 + (d.r * 0.5 + d.g);
    float eL = e.b * 0.5 + (e.r * 0.5 + e.g);
    float fL = f.b * 0.5 + (f.r * 0.5 + f.g);
    float hL = h.b * 0.5 + (h.r * 0.5 + h.g);

    float nz = 0.25 * bL + 0.25 * dL + 0.25 * fL + 0.25 * hL - eL;
    float range = rcasMax3(rcasMax3(bL, dL, eL), fL, hL) - rcasMin3(rcasMin3(bL, dL, eL), fL, hL);
    nz = clamp(abs(nz) / max(range, 1.0e-6), 0.0, 1.0);
    nz = -0.5 * nz + 1.0;

    vec3 mn4 = min(min(min(b, d), f), h);
    vec3 mx4 = max(max(max(b, d), f), h);

    // Largest sharpening lobe that keeps the result inside [0, 1] per channel.
    vec3 hitMin = min(mn4, e) / (4.0 * mx4);
    vec3 hitMax = (1.0 - max(mx4, e)) / (4.0 * mn4 - 4.0);
    vec3 lobeRGB = max(-hitMin, hitMax);

    float lobe = max(-FSR_RCAS_LIMIT, min(rcasMax3(lobeRGB.r, lobeRGB.g, lobeRGB.b), 0.0)) * exp2(-sharpness);

    if (denoise != 0)
        lobe *= nz;

    float rcpL = 1.0 / (4.0 * lobe + 1.0);
    vec3 outColor = clamp((lobe * b + lobe * d + lobe * h + lobe * f + e) * rcpL, 0.0, 1.0);

    fragColor = vec4(outColor, alpha);
}
