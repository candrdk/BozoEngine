#pragma pack_matrix(column_major)

struct VSOutput {
    float4 Position : SV_Position;
    [[vk::location(0)]] float2 UV : TEXCOORD0;
};

VSOutput main(uint vertexIndex : SV_VertexID) {
    VSOutput output;
    output.UV = float2((vertexIndex << 1) & 2, vertexIndex & 2);
    output.Position = float4(output.UV * 2.0f - 1.0f, 0.0f, 1.0f);
    return output;
}
