#version 330 core

layout (location = 0) in vec3 vertex_position;
layout (location = 1) in vec3 vertex_normal;
layout (location = 2) in vec3 vertex_color;
layout (location = 3) in vec2 vertex_uv;

out struct fragment_data
{
    vec3 position;
    vec3 normal;
    vec3 color;
    vec2 uv;
} fragment;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float time;

float saturate(float x)
{
    return clamp(x, 0.0, 1.0);
}

float smootherstep(float edge0, float edge1, float x)
{
    float t = saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3.0 - 2.0 * t);
}

vec3 mod289(vec3 x)
{
    return x - floor(x * (1.0 / 289.0)) * 289.0;
}

vec2 mod289(vec2 x)
{
    return x - floor(x * (1.0 / 289.0)) * 289.0;
}

vec3 permute(vec3 x)
{
    return mod289(((x * 34.0) + 1.0) * x);
}

float simplex_noise(vec2 v)
{
    const vec4 C = vec4(0.211324865405187,
                        0.366025403784439,
                       -0.577350269189626,
                        0.024390243902439);
    vec2 i = floor(v + dot(v, C.yy));
    vec2 x0 = v - i + dot(i, C.xx);

    vec2 i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec4 x12 = x0.xyxy + C.xxzz;
    x12.xy -= i1;

    i = mod289(i);
    vec3 p = permute(permute(i.y + vec3(0.0, i1.y, 1.0))
                   + i.x + vec3(0.0, i1.x, 1.0));

    vec3 m = max(0.5 - vec3(dot(x0, x0),
                            dot(x12.xy, x12.xy),
                            dot(x12.zw, x12.zw)), 0.0);
    m = m * m;
    m = m * m;

    vec3 x = 2.0 * fract(p * C.www) - 1.0;
    vec3 h = abs(x) - 0.5;
    vec3 ox = floor(x + 0.5);
    vec3 a0 = x - ox;

    m *= 1.79284291400159 - 0.85373472095314 * (a0 * a0 + h * h);

    vec3 g;
    g.x = a0.x * x0.x + h.x * x0.y;
    g.yz = a0.yz * x12.xz + h.yz * x12.yw;
    return 130.0 * dot(m, g);
}

float noise_perlin(vec2 p)
{
    float value = 0.0;
    float a = 1.0;
    float f = 1.0;
    for (int k = 0; k < 5; ++k) {
        value += a * (0.5 + 0.5 * simplex_noise(p * f));
        f *= 2.0;
        a *= 0.3;
    }
    return value;
}

float terrain_height(float x, float y)
{
    float island_radius = 18.0;
    float r2 = x * x + y * y;
    float h = -3.8;
    h += 7.1 * exp(-r2 / 115.0);
    h += 1.8 * exp(-((x - 2.0) * (x - 2.0) + (y + 0.8) * (y + 0.8)) / 42.0);
    h += 0.55 * noise_perlin(vec2(0.18 * x, 0.18 * y)) * exp(-r2 / 250.0);
    h += 0.16 * noise_perlin(vec2(0.95 * x + 1.7, 0.95 * y - 2.2));

    float r = sqrt(r2);
    if (r > island_radius + 3.0)
        h -= 0.45 * (r - island_radius - 3.0);

    return h;
}

float water_height(float x, float y, float t)
{
    float sea_level = 0.0;
    float r = sqrt(x * x + y * y);
    float boundary_freeze = 1.0 - smootherstep(98.0, 118.0, r);

    vec2 p2 = vec2(x, y);
    vec2 dirs[5] = vec2[5](
        normalize(vec2(0.92, 0.38)),
        normalize(vec2(-0.27, 0.96)),
        normalize(vec2(0.66, -0.75)),
        normalize(vec2(-0.84, -0.54)),
        normalize(vec2(0.18, -0.98))
    );
    float amp[5] = float[5](0.11, 0.07, 0.06, 0.05, 0.04);
    float freq[5] = float[5](0.52, 0.80, 1.15, 1.55, 1.95);
    float speed[5] = float[5](0.95, -1.25, 1.62, -1.92, 2.20);
    float phase[5] = float[5](0.4, 1.3, 2.1, 3.7, 5.2);

    float wave = 0.0;
    for (int k = 0; k < 5; ++k) {
        float u = dot(dirs[k], p2);
        wave += amp[k] * sin(freq[k] * u + speed[k] * t + phase[k]);
    }

    float phi = 1.65 * dot(p2, normalize(vec2(0.61, -0.79))) - 2.05 * t + 0.8;
    float inverted = 1.0 - 2.0 * abs(sin(phi));
    wave += 0.065 * inverted;

    float chop = sin(2.8 * phi + 0.9) * sin(1.3 * dot(p2, normalize(vec2(-0.88, 0.47))) - 1.5 * t);
    wave += 0.028 * chop;

    float shore_d = abs(terrain_height(x, y) - sea_level);
    float shore_boost = exp(-(shore_d * shore_d) / 0.11);
    float shore_wave = 0.05 * shore_boost * sin(3.8 * dot(p2, normalize(vec2(0.86, 0.51))) - 3.1 * t);

    return sea_level + boundary_freeze * wave + shore_wave;
}

vec3 water_normal(vec2 p, float t)
{
    float eps = 0.18;
    float h = water_height(p.x, p.y, t);
    float hx = water_height(p.x + eps, p.y, t);
    float hy = water_height(p.x, p.y + eps, t);

    vec3 tx = vec3(eps, 0.0, hx - h);
    vec3 ty = vec3(0.0, eps, hy - h);
    return normalize(cross(tx, ty));
}

void main()
{
    vec3 displaced_position = vertex_position;
    displaced_position.z = water_height(vertex_position.x, vertex_position.y, time);

    vec4 position = model * vec4(displaced_position, 1.0);
    mat4 modelNormal = transpose(inverse(model));
    vec4 normal = modelNormal * vec4(water_normal(vertex_position.xy, time), 0.0);

    fragment.position = position.xyz;
    fragment.normal = normal.xyz;
    fragment.color = vertex_color;
    fragment.uv = vertex_uv;

    gl_Position = projection * view * position;
}
