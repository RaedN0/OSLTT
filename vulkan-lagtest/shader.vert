#version 450

layout(push_constant) uniform PushConstants {
    vec4 color;
} pc;

layout(location = 0) out vec4 outColor;

const vec2 positions[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    outColor = pc.color;
}
