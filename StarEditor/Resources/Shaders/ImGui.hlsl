#pragma stage : vert

struct Constants
{
    float2 Scale;
    float2 Translate;
};

[[vk::push_constant]] ConstantBuffer<Constants> g_Const : register(b0, space0);

struct VS_INPUT
{
    float2 pos : POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
};

struct PS_INPUT
{
    float4 out_pos : SV_POSITION;
    float4 out_col : COLOR0;
    float2 out_uv  : TEXCOORD0;
};

PS_INPUT main(VS_INPUT input)
{
    PS_INPUT output;
    output.out_pos.xy = input.pos.xy * g_Const.Scale + g_Const.Translate;
    output.out_pos.zw = float2(0, 1);
    output.out_col = input.col;
    output.out_uv = input.uv;
    return output;
}

#pragma stage : frag

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv  : TEXCOORD0;
};

[[vk::binding(0)]] Texture2D u_Texture : register(t0);
[[vk::binding(1)]] sampler u_Sampler : register(s0);

float4 main(PS_INPUT input) : SV_Target
{
    return input.col * u_Texture.Sample(u_Sampler, input.uv);
}
