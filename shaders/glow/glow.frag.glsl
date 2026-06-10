#version 330 core

in struct fragment_data
{
    vec3 normal;
} fragment;

layout(location=0) out vec4 FragColor;

uniform mat4 view;
uniform vec3 glow_color;
uniform float glow_alpha;

void main()
{
    vec3 camera_forward = normalize(transpose(mat3(view)) * vec3(0.0, 0.0, 1.0));
    float facing = abs(dot(normalize(fragment.normal), camera_forward));
    float soft_edge = pow(clamp(1.0 - facing, 0.0, 1.0), 1.7);
    float core = pow(clamp(1.0 - 0.42 * facing, 0.0, 1.0), 3.0);
    float alpha = glow_alpha * (0.18 + 0.82 * soft_edge);
    vec3 color = glow_color * (0.45 + 0.95 * core);
    FragColor = vec4(color, alpha);
}
