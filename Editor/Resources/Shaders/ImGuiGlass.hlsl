// Frosted-glass backdrops behind ImGui popups and overlays (ImGuiRenderer, render features plan
// phase 6). A full-screen triangle the scissor limits to the window's region; one mode per pass:
//   DOWN       dual-Kawase downsample (Bjorge, SIGGRAPH 2015) of the UI target or a blur level
//   UP         dual-Kawase upsample back one level
//   COMPOSITE  the blurred backdrop inside the window's rounded rectangle, over the UI target; the
//              window's own translucent background then draws over it as the tint
//   PRESENT    the UI target into the back buffer
// Sampling is clamped to the region the pass's source holds, so content outside it never bleeds in.
#pragma stage : vert

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
};

VS_OUTPUT main(uint vertexID : SV_VertexID)
{
    VS_OUTPUT output;
    float2 position = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(position * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

#pragma stage : frag

#define MODE_DOWN      0
#define MODE_UP        1
#define MODE_COMPOSITE 2
#define MODE_PRESENT   3

struct Constants
{
    float4 SourceRegion; // xy min, zw max: the source's valid region in UV, half a texel inside
    float4 Rect;         // COMPOSITE: the window, xy min and zw max in target pixels
    float2 SourceTexel;  // 1 / source size
    float  PixelScale;   // source pixels per target pixel
    float  Rounding;     // COMPOSITE: corner radius in target pixels
    uint   Mode;
    float  Offset;       // DOWN / UP: tap offset in source texels
};

[[vk::push_constant]] ConstantBuffer<Constants> g_Const : register(b0, space0);

[[vk::binding(0)]] Texture2D u_Texture : register(t0);
[[vk::binding(1)]] sampler u_Sampler : register(s0);

float4 Tap(float2 uv)
{
    return u_Texture.SampleLevel(u_Sampler, clamp(uv, g_Const.SourceRegion.xy, g_Const.SourceRegion.zw), 0.0);
}

float RoundedRectDistance(float2 position, float4 rect, float rounding)
{
    float2 halfSize = (rect.zw - rect.xy) * 0.5;
    float radius = min(rounding, min(halfSize.x, halfSize.y));
    float2 q = abs(position - (rect.xy + rect.zw) * 0.5) - halfSize + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

float4 main(float4 position : SV_POSITION) : SV_Target
{
    if (g_Const.Mode == MODE_PRESENT)
        return u_Texture.Load(int3(position.xy, 0));

    float2 uv = position.xy * g_Const.PixelScale * g_Const.SourceTexel;
    float2 offset = g_Const.SourceTexel * g_Const.Offset;

    if (g_Const.Mode == MODE_DOWN)
    {
        float4 sum = Tap(uv) * 4.0;
        sum += Tap(uv - offset);
        sum += Tap(uv + offset);
        sum += Tap(uv + float2(offset.x, -offset.y));
        sum += Tap(uv + float2(-offset.x, offset.y));
        return sum / 8.0;
    }

    if (g_Const.Mode == MODE_UP)
    {
        float4 sum = Tap(uv + float2(-offset.x * 2.0, 0.0));
        sum += Tap(uv + float2(-offset.x, offset.y)) * 2.0;
        sum += Tap(uv + float2(0.0, offset.y * 2.0));
        sum += Tap(uv + float2(offset.x, offset.y)) * 2.0;
        sum += Tap(uv + float2(offset.x * 2.0, 0.0));
        sum += Tap(uv + float2(offset.x, -offset.y)) * 2.0;
        sum += Tap(uv + float2(0.0, -offset.y * 2.0));
        sum += Tap(uv + float2(-offset.x, -offset.y)) * 2.0;
        return sum / 12.0;
    }

    // COMPOSITE: antialiased coverage of the window's rounded rectangle.
    float coverage = saturate(0.5 - RoundedRectDistance(position.xy, g_Const.Rect, g_Const.Rounding));
    return float4(Tap(uv).rgb, coverage);
}
