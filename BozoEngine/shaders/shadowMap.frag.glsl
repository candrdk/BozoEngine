#version 450

// TODO: temporary to get rid of errors w/ pipelinelayout
layout(push_constant) uniform PushConstants {
    mat4 model;
    uint parallaxMode;
    uint parallaxSteps;
    float parallaxScale;
} primitive;

void main() {}
