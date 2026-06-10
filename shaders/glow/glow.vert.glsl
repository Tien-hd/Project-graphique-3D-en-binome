#version 330 core

layout (location = 0) in vec3 vertex_position;
layout (location = 1) in vec3 vertex_normal;
layout (location = 2) in vec3 vertex_color;
layout (location = 3) in vec2 vertex_uv;

out struct fragment_data
{
    vec3 normal;
} fragment;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    mat4 modelNormal = transpose(inverse(model));
    fragment.normal = normalize((modelNormal * vec4(vertex_normal, 0.0)).xyz);
    gl_Position = projection * view * model * vec4(vertex_position, 1.0);
}
