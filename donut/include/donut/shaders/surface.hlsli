/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#ifndef SURFACE_HLSLI
#define SURFACE_HLSLI

struct MaterialSample
{
    float3 shadingNormal;
    float3 geometryNormal;
    float3 diffuseAlbedo; // BRDF input Cdiff
    float3 specularF0; // BRDF input F0
    float3 emissiveColor;
    float opacity;
    float occlusion;
    float roughness;
    float3 baseColor; // native in metal-rough, derived in spec-gloss
    float metalness; // native in metal-rough, derived in spec-gloss
    float transmission;
    float diffuseTransmission;
    bool hasMetalRoughParams; // indicates that 'baseColor' and 'metalness' are valid
    float ior;
    float shadowNoLFadeout;
};

MaterialSample DefaultMaterialSample()
{
    MaterialSample result;
    result.shadingNormal = 0;
    result.geometryNormal = 0;
    result.diffuseAlbedo = 0;
    result.specularF0 = 0;
    result.emissiveColor = 0;
    result.opacity = 1;
    result.occlusion = 1;
    result.roughness = 0;
    result.baseColor = 0;
    result.metalness = 0;
    result.transmission = 0;
    result.diffuseTransmission = 0;
    result.hasMetalRoughParams = false;
    result.ior = 1.5;
    return result;
}

#endif // SURFACE_HLSLI
