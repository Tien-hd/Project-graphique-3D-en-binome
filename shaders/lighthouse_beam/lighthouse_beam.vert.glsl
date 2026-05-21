#version 330 core

layout(location = 0) in vec3 vertex_position;
layout(location = 1) in vec3 vertex_normal;
layout(location = 2) in vec3 vertex_color;
layout(location = 3) in vec2 vertex_uv;

out vec3 local_pos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
	local_pos = vertex_position;
	gl_Position = projection * view * model * vec4(vertex_position, 1.0);
}
