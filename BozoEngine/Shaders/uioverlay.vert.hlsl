struct PushConstants { float2 scale; float2 translate; };
[[vk::push_constant]] PushConstants pc;

struct VSInput {
    [[vk::location(0)]] float2 inPos   : POSITION;
    [[vk::location(1)]] float2 inUV    : TEXCOORD0;
    [[vk::location(2)]] float4 inColor : COLOR;
};

struct VSOutput {
    float4 Position : SV_Position;
    [[vk::location(0)]] float2 outUV    : TEXCOORD0;
    [[vk::location(1)]] float4 outColor : COLOR;
};

VSOutput main(VSInput input) {
    VSOutput output;
    output.outUV    = input.inUV;
    output.outColor = input.inColor;
    output.Position = float4(input.inPos * pc.scale + pc.translate, 0.0, 1.0);
    return output;
}
