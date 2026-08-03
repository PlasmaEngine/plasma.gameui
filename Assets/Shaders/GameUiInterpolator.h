#pragma once

// Shared VS/PS plumbing of the GameUi shaders. The shared MaterialInterpolator caps
// TexCoord1 at float2, so the SDF brush parameters (see plUiVertex) get their own structs.
//
// SdfCoord:  localU, localV (0..1 across the quad), mode (0 plain / 1 fill / 2 border / 3 shadow), softness px
// SdfParams: halfWidth px, halfHeight px, borderWidth px, unused
// SdfRadii:  corner radii px (TL, TR, BR, BL)
// ColorB:    border color (mode 2)

struct VS_IN
{
  float3 Position : POSITION;
  float2 TexCoord0 : TEXCOORD0;
  float4 Color0 : COLOR0;
  float4 SdfCoord : TEXCOORD1;
  float4 SdfParams : TEXCOORD2;
  float4 SdfRadii : TEXCOORD3;
  float4 ColorB : COLOR1;
};

struct VS_OUT
{
  float4 Position : SV_Position;
  float2 TexCoord0 : TEXCOORD0;
  float4 Color0 : COLOR0;
  float4 SdfCoord : TEXCOORD1;
  float4 SdfParams : TEXCOORD2;
  float4 SdfRadii : TEXCOORD3;
  float4 ColorB : COLOR1;
};

#define PS_IN VS_OUT

// signed distance to a rounded box with per-corner radii; p = px from the rect center
float GameUiRoundedBoxSdf(float2 p, float2 halfSize, float4 radii)
{
  // quadrant-select the radius: radii = (TL, TR, BR, BL); +y points DOWN in UI space
  float r = (p.x < 0.0) ? ((p.y < 0.0) ? radii.x : radii.w) : ((p.y < 0.0) ? radii.y : radii.z);

  float2 q = abs(p) - (halfSize - r.xx);
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
