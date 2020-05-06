#version 460 core

vec2 Positions[3] = vec2[](
    vec2(0.0f, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5)
);

vec3 Colors[3] = vec3[](
    vec3(1.0f, 0.0f, 0.0f),
    vec3(0.0f, 1.0f, 0.0f),
    vec3(0.0f, 0.0f, 1.0f)
);

layout(location = 0) out vec3 Color;

void main()
{
    gl_Position = vec4(Positions[gl_VertexIndex], 0, 1);
    Color = Colors[gl_VertexIndex];
}