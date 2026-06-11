#include "scene.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <iostream>

using namespace cgp;

namespace
{
struct transparent_draw_call {
	int pass = 0;
	float distance2 = 0.0f;
};

struct transparent_instance {
	int index = 0;
	float distance2 = 0.0f;
	vec3 position;
	float alpha = 1.0f;
};

struct cubemap_cross_faces {
	image_structure x_neg;
	image_structure x_pos;
	image_structure y_neg;
	image_structure y_pos;
	image_structure z_neg;
	image_structure z_pos;
};

std::string const skybox_blend_vertex_shader = R"(#version 330 core
layout (location = 0) in vec3 position;

out struct fragment_data
{
	vec3 position;
} fragment;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
	fragment.position = position.xyz;

	mat4 modelView = mat4(mat3(view * model));
	gl_Position = projection * modelView * vec4(position, 1.0);
}
)";

std::string const skybox_blend_fragment_shader = R"(#version 330 core
in struct fragment_data
{
	vec3 position;
} fragment;

layout(location=0) out vec4 FragColor;

uniform samplerCube image_skybox_day;
uniform samplerCube image_skybox_night;
uniform mat3 skybox_rotation;
uniform float night_blend;
uniform int use_night_texture;
uniform vec3 night_fallback_color;
uniform float alpha_color_blending;
uniform vec3 color_blending;

void main()
{
	vec3 p = skybox_rotation * fragment.position;
	vec3 day_color = texture(image_skybox_day, p).rgb;
	vec3 night_color = use_night_texture == 1 ? texture(image_skybox_night, p).rgb : night_fallback_color;
	vec3 texture_color = mix(day_color, night_color, night_blend);
	vec3 color_blend = mix(texture_color, color_blending, alpha_color_blending);
	FragColor = vec4(color_blend, 1.0);
}
)";

float distance_squared(vec3 const& a, vec3 const& b)
{
	vec3 const d = a - b;
	return d.x * d.x + d.y * d.y + d.z * d.z;
}

float saturate(float x)
{
	return std::clamp(x, 0.0f, 1.0f);
}

float smoothstep(float edge0, float edge1, float x)
{
	float const t = saturate((x - edge0) / (edge1 - edge0));
	return t * t * (3.0f - 2.0f * t);
}

vec3 mix_color(vec3 const& a, vec3 const& b, float t)
{
	return (1.0f - t) * a + t * b;
}

bool file_exists(std::string const& path)
{
	std::ifstream f(path);
	return f.good();
}


bool extract_cubemap_cross_faces(image_structure const& cross, cubemap_cross_faces& faces)
{
	int N = 0;
	bool horizontal_cross = false;
	bool vertical_cross = false;

	if (cross.width % 4 == 0 && cross.height % 3 == 0 && cross.width / 4 == cross.height / 3) {
		N = cross.width / 4;
		horizontal_cross = true;
	}
	else if (cross.width % 3 == 0 && cross.height % 4 == 0 && cross.width / 3 == cross.height / 4) {
		N = cross.width / 3;
		vertical_cross = true;
	}
	else {
		return false;
	}

	auto tile = [&cross, N](int tx, int ty) {
		return cross.subimage(tx * N, ty * N, (tx + 1) * N, (ty + 1) * N);
	};

	// Horizontal cross in project Z-up convention:
	//                       +Z
	//                   -X  +Y  +X  -Y
	//                       -Z
	// Rotation/mirror calls only fix each OpenGL cubemap face orientation.
	if (horizontal_cross) {
		faces.x_neg = tile(0, 1).rotate_90_degrees_counterclockwise().mirror_horizontal();
		faces.x_pos = tile(2, 1).rotate_90_degrees_clockwise().mirror_horizontal();
		faces.y_neg = tile(3, 1).rotate_90_degrees_clockwise().rotate_90_degrees_clockwise().mirror_vertical();
		faces.y_pos = tile(1, 1).mirror_vertical();
		faces.z_neg = tile(1, 2).mirror_horizontal();
		faces.z_pos = tile(1, 0).mirror_vertical();
	}

	// Vertical cross fallback. The project assets use the horizontal form above.
	if (vertical_cross) {
		faces.x_neg = tile(0, 1);
		faces.x_pos = tile(2, 1);
		faces.y_neg = tile(1, 2);
		faces.y_pos = tile(1, 0);
		faces.z_neg = tile(1, 3);
		faces.z_pos = tile(1, 1);
	}

	return true;
}

bool initialize_cubemap_from_cross(cgp::opengl_texture_image_structure& texture, std::string const& path)
{
	if (!file_exists(path))
		return false;

	image_structure const cross = image_load_file(path);
	cubemap_cross_faces faces;
	if (!extract_cubemap_cross_faces(cross, faces))
		return false;

	texture.initialize_cubemap_on_gpu(faces.x_neg, faces.x_pos, faces.y_neg, faces.y_pos, faces.z_neg, faces.z_pos);
	return true;
}

void draw_skybox_day_night(skybox_drawable const& day_skybox,
                           skybox_drawable const& night_skybox,
                           opengl_shader_structure const& shader,
                           environment_structure const& environment,
                           float night_blend,
                           bool use_night_texture,
                           vec3 const& night_fallback_color)
{
	if (day_skybox.vbo_position.size == 0 || day_skybox.ebo_connectivity.size == 0 || day_skybox.texture.id == 0)
		return;

	glUseProgram(shader.id);

	opengl_uniform(shader, "model", day_skybox.model.matrix());
	opengl_uniform(shader, "skybox_rotation", day_skybox.skybox_rotation);
	opengl_uniform(shader, "night_blend", night_blend);
	opengl_uniform(shader, "use_night_texture", use_night_texture ? 1 : 0);
	opengl_uniform(shader, "night_fallback_color", night_fallback_color);
	opengl_uniform(shader, "alpha_color_blending", day_skybox.alpha_color_blending);
	opengl_uniform(shader, "color_blending", day_skybox.color_blending);
	environment.send_opengl_uniform(shader);

	glActiveTexture(GL_TEXTURE0);
	day_skybox.texture.bind();
	opengl_uniform(shader, "image_skybox_day", 0);

	glActiveTexture(GL_TEXTURE1);
	if (use_night_texture)
		night_skybox.texture.bind();
	else
		day_skybox.texture.bind();
	opengl_uniform(shader, "image_skybox_night", 1);

	glBindVertexArray(day_skybox.vao);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, day_skybox.ebo_connectivity.id);
	glDrawElements(GL_TRIANGLES, GLsizei(day_skybox.ebo_connectivity.size * 3), GL_UNSIGNED_INT, nullptr);

	glBindVertexArray(0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
	glUseProgram(0);
}

vec3 ocean_near_tint()
{
	return {0.30f, 0.64f, 0.81f};
}

vec3 ocean_far_tint()
{
	return {0.38f, 0.70f, 0.84f};
}

float pulse_cycle(float x)
{
	// 0 -> 1 -> 0 over x in [0,1]
	return std::sin(Pi * saturate(x));
}

uint32_t hash_u32(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}

float hash01(uint32_t x)
{
	return (hash_u32(x) & 0x00ffffffU) / 16777215.0f;
}

unsigned char to_byte(float x)
{
	return static_cast<unsigned char>(255.0f * saturate(x));
}

vec3 cubemap_direction_from_face(int face_id, float u, float v)
{
	switch (face_id) {
	case 0: return normalize(vec3{-1.0f, -v, u});  // X-
	case 1: return normalize(vec3{1.0f, -v, -u});  // X+
	case 2: return normalize(vec3{-u, -1.0f, v});  // Y-
	case 3: return normalize(vec3{u, 1.0f, v});    // Y+
	case 4: return normalize(vec3{-u, -v, -1.0f}); // Z-
	default: return normalize(vec3{u, v, 1.0f});  // Z+
	}
}

vec3 sky_color(vec3 const& dir, vec3 const& water_tint)
{
	vec3 const zenith = mix_color(water_tint, vec3{0.15f, 0.40f, 0.72f}, 0.48f);
	vec3 const horizon_sky = mix_color(water_tint, vec3{0.58f, 0.76f, 0.86f}, 0.24f);
	vec3 const deep_ocean = mix_color(0.24f * water_tint, vec3{0.01f, 0.06f, 0.12f}, 0.75f);

	vec3 c;
	if (dir.z >= 0.0f) {
		float const t = std::pow(saturate(dir.z), 0.42f);
		c = mix_color(horizon_sky, zenith, t);
	}
	else {
		float const t = std::pow(saturate(-dir.z), 0.62f);
		c = mix_color(0.88f * water_tint, deep_ocean, t);
	}

	// Seamless horizon transition toward sea tint.
	float const horizon_band = std::exp(-std::abs(dir.z) / 0.14f);
	c = mix_color(c, 1.00f * water_tint, 0.30f * horizon_band);

	vec3 const sun_dir = normalize(vec3{0.52f, -0.28f, 0.81f});
	float const sun_d = std::max(0.0f, dot(dir, sun_dir));
	float const sun_core = std::pow(sun_d, 1050.0f);
	float const sun_halo = std::pow(sun_d, 72.0f);
	c += 1.0f * sun_core * vec3{1.0f, 0.95f, 0.80f};
	c += 0.11f * sun_halo * vec3{1.0f, 0.80f, 0.52f};

	float const wisp = 0.5f + 0.5f * std::sin(8.0f * dir.x + 5.0f * dir.y + 2.0f * std::sin(6.0f * dir.z));
	c += 0.012f * std::pow(wisp, 5.0f) * saturate((dir.z - 0.25f) / 0.75f) * vec3{1.0f, 0.98f, 0.96f};

	return c;
}

image_structure generate_cubemap_face(int face_id, int N, vec3 const& water_tint)
{
	numarray<unsigned char> data;
	data.resize(3 * N * N);

	for (int j = 0; j < N; ++j) {
		for (int i = 0; i < N; ++i) {
			float const u = 2.0f * (i + 0.5f) / N - 1.0f;
			float const v = 2.0f * (j + 0.5f) / N - 1.0f;
			vec3 const dir = cubemap_direction_from_face(face_id, u, v);
				vec3 c = sky_color(dir, water_tint);

				uint32_t const key = static_cast<uint32_t>(i + N * (j + N * face_id));
				float const dither = hash01(key * 191U + 71U) - 0.5f;
				c += 0.005f * dither * vec3{1.0f, 1.0f, 1.0f};

			int const idx = 3 * (i + N * j);
			data[idx + 0] = to_byte(c.x);
			data[idx + 1] = to_byte(c.y);
			data[idx + 2] = to_byte(c.z);
		}
	}

	return image_structure(N, N, image_color_type::rgb, data);
}

mesh make_lighthouse_beam_frustum(float radius_near, float radius_far, float length, int Nu, int Nv)
{
	assert_cgp(radius_near > 0.0f, "Lighthouse beam near radius must be > 0");
	assert_cgp(radius_far > radius_near, "Lighthouse beam far radius must be > near radius");
	assert_cgp(length > 0.0f, "Lighthouse beam length must be > 0");
	assert_cgp(Nu > 2, "Lighthouse beam Nu must be > 2");
	assert_cgp(Nv > 1, "Lighthouse beam Nv must be > 1");

	mesh shape;
	float const slope = (radius_far - radius_near) / length;
	for (int ku = 0; ku < Nu; ++ku) {
		float const u = ku / (Nu - 1.0f);
		float const theta = 2.0f * Pi * u;
		float const cs = std::cos(theta);
		float const sn = std::sin(theta);

		for (int kv = 0; kv < Nv; ++kv) {
			float const along = kv / (Nv - 1.0f);
			float const x = length * along;
			float const r = radius_near + (radius_far - radius_near) * along;

			vec3 const p = {x, r * cs, r * sn};
			vec3 const n = normalize(vec3{-slope, cs, sn});

			shape.position.push_back(p);
			shape.normal.push_back(n);
			shape.uv.push_back({along, u});
		}
	}

	for (int ku = 0; ku < Nu - 1; ++ku) {
		for (int kv = 0; kv < Nv - 1; ++kv) {
			unsigned int const p00 = static_cast<unsigned int>(kv + Nv * ku);
			unsigned int const p01 = static_cast<unsigned int>(kv + 1 + Nv * ku);
			unsigned int const p10 = static_cast<unsigned int>(kv + Nv * (ku + 1));
			unsigned int const p11 = static_cast<unsigned int>(kv + 1 + Nv * (ku + 1));
			shape.connectivity.push_back({p00, p10, p11});
			shape.connectivity.push_back({p00, p11, p01});
		}
	}
	shape.flip_connectivity();
	shape.fill_empty_field();
	return shape;
}

vec2 wind_direction_xy()
{
	return normalize(vec2{0.92f, 0.38f});
}

float wrap_coordinate(float x, float bound)
{
	float const width = 2.0f * bound;
	while (x > bound)
		x -= width;
	while (x < -bound)
		x += width;
	return x;
}

mesh make_cloud_billboard_mesh()
{
	mesh shape;

	auto add_lobe = [&shape](float cx, float cz, float sx, float sz, vec3 const& color) {
		unsigned int const offset = static_cast<unsigned int>(shape.position.size());
		shape.position.push_back({cx - sx, 0.0f, cz - sz});
		shape.position.push_back({cx + sx, 0.0f, cz - sz});
		shape.position.push_back({cx + sx, 0.0f, cz + sz});
		shape.position.push_back({cx - sx, 0.0f, cz + sz});
		for (int k = 0; k < 4; ++k) {
			shape.normal.push_back({0.0f, -1.0f, 0.0f});
			shape.color.push_back(color);
			shape.uv.push_back({0.0f, 0.0f});
		}
		shape.connectivity.push_back({offset + 0, offset + 1, offset + 2});
		shape.connectivity.push_back({offset + 0, offset + 2, offset + 3});
		shape.connectivity.push_back({offset + 0, offset + 2, offset + 1});
		shape.connectivity.push_back({offset + 0, offset + 3, offset + 2});
	};

	add_lobe(-0.38f, 0.00f, 0.48f, 0.16f, {0.92f, 0.95f, 0.98f});
	add_lobe(0.00f, 0.06f, 0.55f, 0.22f, {0.98f, 0.99f, 1.00f});
	add_lobe(0.42f, 0.01f, 0.45f, 0.17f, {0.90f, 0.94f, 0.98f});
	add_lobe(-0.05f, -0.12f, 0.72f, 0.13f, {0.86f, 0.91f, 0.96f});

	shape.fill_empty_field();
	return shape;
}

mesh make_wind_streak_ribbon_mesh()
{
	mesh shape;
	int const N = 18;
	float const half_width = 0.025f;
	for (int k = 0; k < N; ++k) {
		float const u = k / float(N - 1);
		float const x = -0.5f + u;
		float const curve = 0.09f * std::sin(2.0f * Pi * u);
		float const fade = std::sin(Pi * u);
		float const w = half_width * (0.30f + 0.70f * fade);

		shape.position.push_back({x, curve - w, 0.0f});
		shape.position.push_back({x, curve + w, 0.0f});
		shape.normal.push_back({0.0f, 0.0f, 1.0f});
		shape.normal.push_back({0.0f, 0.0f, 1.0f});
		shape.color.push_back({1.0f, 1.0f, 1.0f});
		shape.color.push_back({1.0f, 1.0f, 1.0f});
		shape.uv.push_back({u, 0.0f});
		shape.uv.push_back({u, 1.0f});
	}

	for (int k = 0; k < N - 1; ++k) {
		unsigned int const i = static_cast<unsigned int>(2 * k);
		shape.connectivity.push_back({i, i + 1, i + 3});
		shape.connectivity.push_back({i, i + 3, i + 2});
	}
	shape.fill_empty_field();
	return shape;
}

mesh create_boat_hull_mesh()
{
	mesh shape;
	shape.position = {
	    {-1.25f, -0.45f, 0.25f},
	    {-1.25f,  0.45f, 0.25f},
	    { 0.40f, -0.40f, 0.32f},
	    { 0.40f,  0.40f, 0.32f},
	    { 1.35f,  0.00f, 0.24f},
	    {-0.95f,  0.00f, -0.18f},
	    { 0.30f,  0.00f, -0.24f},
	    { 1.05f,  0.00f, -0.08f},
	};

	shape.connectivity = {
	    {0, 2, 3}, {0, 3, 1},      // open top rim
	    {2, 4, 3},                 // pointed bow top
	    {0, 5, 6}, {0, 6, 2},      // left side
	    {3, 6, 5}, {3, 5, 1},      // right side
	    {2, 6, 7}, {2, 7, 4},      // bow left
	    {4, 7, 6}, {4, 6, 3},      // bow right
	    {0, 1, 5},                 // stern
	};

	shape.fill_empty_field();
	shape.normal_update();
	return shape;
}

mesh create_boat_cabin_mesh()
{
	mesh shape = mesh_primitive_cube({-0.25f, 0.0f, 0.52f}, 1.0f);
	for (vec3& p : shape.position) {
		p.x = -0.25f + 0.55f * (p.x + 0.25f);
		p.y = 0.34f * p.y;
		p.z = 0.52f + 0.36f * (p.z - 0.52f);
	}
	shape.normal_update();
	shape.fill_empty_field();
	return shape;
}

mesh create_wake_ellipse_mesh(int N = 48)
{
	mesh shape;
	shape.position.push_back({0.0f, 0.0f, 0.0f});
	shape.normal.push_back({0.0f, 0.0f, 1.0f});
	shape.color.push_back({1.0f, 1.0f, 1.0f});
	shape.uv.push_back({0.5f, 0.5f});

	for (int k = 0; k < N; ++k) {
		float const u = 2.0f * Pi * k / float(N);
		shape.position.push_back({std::cos(u), std::sin(u), 0.0f});
		shape.normal.push_back({0.0f, 0.0f, 1.0f});
		shape.color.push_back({1.0f, 1.0f, 1.0f});
		shape.uv.push_back({0.5f + 0.5f * std::cos(u), 0.5f + 0.5f * std::sin(u)});
	}

	for (int k = 0; k < N; ++k) {
		unsigned int const a = 1u + static_cast<unsigned int>(k);
		unsigned int const b = 1u + static_cast<unsigned int>((k + 1) % N);
		shape.connectivity.push_back({0u, a, b});
	}
	shape.fill_empty_field();
	return shape;
}

mesh create_grass_billboard_mesh()
{
	mesh shape;
	shape.position = {
	    {-0.5f, 0.0f, 0.0f},
	    { 0.5f, 0.0f, 0.0f},
	    { 0.5f, 0.0f, 1.0f},
	    {-0.5f, 0.0f, 1.0f},
	};
	shape.normal = {
	    {0.0f, -1.0f, 0.0f},
	    {0.0f, -1.0f, 0.0f},
	    {0.0f, -1.0f, 0.0f},
	    {0.0f, -1.0f, 0.0f},
	};
	shape.uv = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
	shape.connectivity = {{0, 1, 2}, {0, 2, 3}};
	shape.fill_empty_field();
	return shape;
}

float fract(float x)
{
	return x - std::floor(x);
}
}

float scene_structure::terrain_height(float x, float y) const
{
    float h = -3.8f;

    std::vector<vec2> centers = {
        {0.0f, 0.0f},
        {10.0f, 6.0f},
        {-12.0f, 4.0f}
    };

    std::vector<float> heights = {
        7.1f,
        3.6f,
        2.3f
    };

    std::vector<float> sigmas = {
        12.5f,
        6.0f,
        4.5f
    };

    for (int i = 0; i < 3; ++i) {
        float dx = x - centers[i].x;
        float dy = y - centers[i].y;
        h += heights[i] * std::exp(-(dx * dx + dy * dy) / (sigmas[i] * sigmas[i]));
    }

    float const r2 = x * x + y * y;
    float const r = std::sqrt(r2);

    h += 0.55f * noise_perlin({0.18f * x, 0.18f * y}) * std::exp(-r2 / 250.0f);
    h += 0.16f * noise_perlin({0.95f * x + 1.7f, 0.95f * y - 2.2f});

    if (r > island_radius + 3.0f)
        h -= 0.45f * (r - island_radius - 3.0f);

    return h;
}

float scene_structure::water_height(float x, float y, float t) const
{
	float const r = std::sqrt(x * x + y * y);
	float const boundary_freeze = 1.0f - smoothstep(98.0f, 118.0f, r);

	// Elliptical distance around the island. gamma stretches the rings along X so
	// they follow the island footprint better than circular waves.
	float const gamma = 1.5f;
	float const d = std::sqrt((x / gamma) * (x / gamma) + y * y);

	std::array<vec2, 5> const dirs = {
	    normalize(vec2{0.92f, 0.38f}),
	    normalize(vec2{-0.27f, 0.96f}),
	    normalize(vec2{0.66f, -0.75f}),
	    normalize(vec2{-0.84f, -0.54f}),
	    normalize(vec2{0.18f, -0.98f}),
	};
	std::array<float, 5> const amp = {0.11f, 0.07f, 0.06f, 0.05f, 0.04f};
	std::array<float, 5> const freq = {0.52f, 0.80f, 1.15f, 1.55f, 1.95f};
	std::array<float, 5> const speed = {0.95f, -1.25f, 1.62f, -1.92f, 2.20f};
	std::array<float, 5> const phase = {0.4f, 1.3f, 2.1f, 3.7f, 5.2f};

	float offshore_wave = 0.0f;
	vec2 const p2 = {x, y};
	for (int k = 0; k < 5; ++k) {
		float const u = dot(dirs[k], p2);
		offshore_wave += amp[k] * std::sin(freq[k] * u + speed[k] * t + phase[k]);
	}

	// Inverted sinus component for sharper alternating crests.
	float const phi = 1.65f * dot(p2, normalize(vec2{0.61f, -0.79f})) - 2.05f * t + 0.8f;
	float const inverted = 1.0f - 2.0f * std::abs(std::sin(phi)); // in [-1,1]
	offshore_wave += 0.065f * inverted;

	float const chop = std::sin(2.8f * phi + 0.9f) * std::sin(1.3f * dot(p2, normalize(vec2{-0.88f, 0.47f})) - 1.5f * t);
	offshore_wave += 0.028f * chop;

	// Inward concentric shore waves: beta*d + c*t makes a crest move toward
	// smaller d as time increases.
	float const B = 0.08f;
	float const beta = 3.5f;
	float const c = 1.5f;
	float const concentric_wave = B * std::cos(beta * d + c * t);

	// Smooth transition to the existing offshore directional wave field.
	float const blend_to_offshore = smoothstep(18.0f, 32.0f, d);
	float const wave = (1.0f - blend_to_offshore) * concentric_wave + blend_to_offshore * offshore_wave;

	return sea_level + boundary_freeze * wave;
}

vec3 scene_structure::compute_sun_direction(float current_time_of_day)
{
	float const sun_angle = 2.0f * Pi * current_time_of_day;

	// One simple arc: high in the sky at time 0, below the horizon at time 0.5.
	// The small y component keeps the path diagonal instead of perfectly vertical.
	return normalize(vec3{
	    0.90f * std::sin(sun_angle),
	    -0.25f * std::cos(sun_angle),
	    std::cos(sun_angle)
	});
}

vec3 scene_structure::compute_moon_direction(float)
{
	return normalize(-sun_direction + vec3{0.0f, 0.0f, 0.25f});
}

void scene_structure::initialize_terrain()
{
	float const L = 48.0f;
	island_cpu = mesh_primitive_grid({-L, -L, 0.0f}, {L, -L, 0.0f}, {L, L, 0.0f}, {-L, L, 0.0f}, 220, 220);

	for (vec3& p : island_cpu.position)
		p.z = terrain_height(p.x, p.y);

	island_cpu.normal_update();
	island_cpu.fill_empty_field();

	for (size_t k = 0; k < island_cpu.position.size(); ++k) {
		vec3 const p = island_cpu.position[k];
		float const h = p.z;
		float const slope = 1.0f - std::max(0.0f, dot(island_cpu.normal[k], {0.0f, 0.0f, 1.0f}));

		vec3 c = {0.06f, 0.17f, 0.30f};
		if (h > -0.35f)
			c = {0.92f, 0.86f, 0.70f};
		if (h > 0.25f)
			c = {0.22f, 0.54f, 0.22f};
		if (h > 1.6f)
			c = {0.38f, 0.42f, 0.31f};
		if (h > 2.7f)
			c = {0.46f, 0.26f, 0.16f};

		c = c + vec3{0.09f, 0.08f, 0.06f} * slope;
		island_cpu.color[k] = c;

		island_cpu.uv[k] *= 10.0f;
	}

	island.initialize_data_on_gpu(island_cpu);
	island.material.texture_settings.active = false;
	island.material.phong = {0.48f, 0.65f, 0.12f, 36.0f};
}

void scene_structure::initialize_water()
{
	float const L = 120.0f;
	water_cpu = mesh_primitive_grid({-L, -L, 0.0f}, {L, -L, 0.0f}, {L, L, 0.0f}, {-L, L, 0.0f}, 220, 220);

	for (vec3& p : water_cpu.position)
		p.z = sea_level;
	water_cpu.normal_update();
	water_cpu.fill_empty_field();

	for (vec2& uv : water_cpu.uv)
		uv *= 18.0f;

	vec3 const water_near = ocean_near_tint();
	vec3 const water_far = ocean_far_tint();
	for (size_t k = 0; k < water_cpu.position.size(); ++k) {
		vec3 const p = water_cpu.position[k];
		float const rr = std::sqrt(p.x * p.x + p.y * p.y);
		float const fog_t = smoothstep(36.0f, 120.0f, rr);
		water_cpu.color[k] = mix_color(water_near, water_far, fog_t);
	}

	water_shader.load(project::path + "shaders/water/water.vert.glsl",
	                  project::path + "shaders/water/water.frag.glsl");
	water.initialize_data_on_gpu(water_cpu, water_shader);
	water.texture.load_and_initialize_texture_2d_on_gpu(project::path + "assets/sea.png");
	water.material.texture_settings.active = false;
	water.material.color = {1.0f, 1.0f, 1.0f};
	water.material.alpha = 1.0f;
	water.material.phong = {0.38f, 0.54f, 0.24f, 36.0f};
}

void scene_structure::initialize_skybox()
{
	// skybox_drawable has raw OpenGL handles; enforce zero-initialized state before first GPU init.
	skybox = skybox_drawable{};
	skybox.initialize_data_on_gpu();
	skybox_night = skybox_drawable{};
	skybox_night.initialize_data_on_gpu();
	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

	bool shader_ok = false;
	skybox_day_night_shader.load_from_inline_text(skybox_blend_vertex_shader, skybox_blend_fragment_shader, &shader_ok);
	if (!shader_ok)
		warning_cgp("Could not load day-night skybox shader", "Using default skybox shader instead.");

	std::string const day_path = project::path + "assets/skybox_02.jpg";
	std::string const night_path = project::path + "assets/skybox_night.jpg";
	skybox_day_texture_loaded = initialize_cubemap_from_cross(skybox.texture, day_path);
	skybox_night_texture_loaded = initialize_cubemap_from_cross(skybox_night.texture, night_path);
	skybox_using_procedural_fallback = !skybox_day_texture_loaded;

	if (skybox_using_procedural_fallback) {
		int const N = 1024;
		vec3 const water_tint = ocean_near_tint();
		image_structure const x_neg = generate_cubemap_face(0, N, water_tint);
		image_structure const x_pos = generate_cubemap_face(1, N, water_tint);
		image_structure const y_neg = generate_cubemap_face(2, N, water_tint);
		image_structure const y_pos = generate_cubemap_face(3, N, water_tint);
		image_structure const z_neg = generate_cubemap_face(4, N, water_tint);
		image_structure const z_pos = generate_cubemap_face(5, N, water_tint);
		skybox.texture.initialize_cubemap_on_gpu(x_neg, x_pos, y_neg, y_pos, z_neg, z_pos);
	}

	skybox.alpha_color_blending = 0.0f;
	skybox.color_blending = {1.0f, 1.0f, 1.0f};
	skybox_night.alpha_color_blending = 0.0f;
	skybox_night.color_blending = {1.0f, 1.0f, 1.0f};
}

void scene_structure::initialize_structures()
{
	lighthouse_tower.initialize_data_on_gpu(mesh_primitive_cylinder(0.85f, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 5.0f}, 40, 24, true));
	lighthouse_tower.material.texture_settings.active = false;
	lighthouse_tower.material.color = {0.97f, 0.97f, 0.96f};
	lighthouse_tower.material.phong = {0.55f, 0.58f, 0.30f, 82.0f};

	lighthouse_roof.initialize_data_on_gpu(mesh_primitive_cone(1.05f, 1.1f, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, true, 40, 10));
	lighthouse_roof.material.texture_settings.active = false;
	lighthouse_roof.material.color = {0.86f, 0.18f, 0.13f};
	lighthouse_roof.material.phong = {0.50f, 0.62f, 0.22f, 32.0f};

	lighthouse_ring.initialize_data_on_gpu(mesh_primitive_torus(1.05f, 0.08f));
	lighthouse_ring.material.texture_settings.active = false;
	lighthouse_ring.material.color = {0.12f, 0.12f, 0.13f};
	lighthouse_ring.material.phong = {0.45f, 0.38f, 0.10f, 12.0f};

	lighthouse_window.initialize_data_on_gpu(mesh_primitive_cube({0.0f, 0.0f, 0.0f}, 1.0f));
	lighthouse_window.material.texture_settings.active = false;
	lighthouse_window.material.color = {0.48f, 0.79f, 0.95f};
	lighthouse_window.material.phong = {0.40f, 0.52f, 0.50f, 128.0f};

	// Lighthouse volumetric beam
	lighthouse_beam_shader.load(project::path + "shaders/lighthouse_beam/lighthouse_beam.vert.glsl",
	                            project::path + "shaders/lighthouse_beam/lighthouse_beam.frag.glsl");
	lighthouse_beam.initialize_data_on_gpu(make_lighthouse_beam_frustum(lighthouse_beam_radius_near, lighthouse_beam_radius_far, lighthouse_beam_length, 44, 18), lighthouse_beam_shader);
	lighthouse_beam.material.texture_settings.active = false;
	lighthouse_beam.material.texture_settings.two_sided = true;
	lighthouse_beam.material.alpha = 1.0f;

	lighthouse_bulb.initialize_data_on_gpu(mesh_primitive_sphere(1.0f));
	lighthouse_bulb.material.texture_settings.active = false;
	lighthouse_bulb.material.color = {1.0f, 0.90f, 0.68f};
	lighthouse_bulb.material.alpha = 0.85f;
	lighthouse_bulb.material.phong = {0.85f, 0.20f, 0.10f, 6.0f};

	dock_plank.initialize_data_on_gpu(mesh_primitive_cube({0.0f, 0.0f, 0.0f}, 1.0f));
	dock_plank.texture.load_and_initialize_texture_2d_on_gpu(project::path + "assets/wood.jpg");
	dock_plank.material.color = {0.88f, 0.78f, 0.62f};
	dock_plank.material.phong = {0.48f, 0.50f, 0.10f, 16.0f};

	dock_pile.initialize_data_on_gpu(mesh_primitive_cylinder(0.12f, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 20, 16, true));
	dock_pile.texture.load_and_initialize_texture_2d_on_gpu(project::path + "assets/wood.jpg");
	dock_pile.material.color = {0.72f, 0.58f, 0.36f};
	dock_pile.material.phong = {0.48f, 0.52f, 0.08f, 10.0f};
}

void scene_structure::initialize_vegetation()
{
	try {
		mesh palm = mesh_load_file_obj(project::path + "assets/palm_tree/palm_tree.obj");
		//Stand all the trees upright from the initial
		for (vec3& p : palm.position) {
			float y = p.y;
			float z = p.z;
			p.y = -z;
			p.z = -y;
		}
		for (vec3& n : palm.normal) {
			float y = n.y;
			float z = n.z;
			n.y = -z;
			n.z = -y;
		}
		palm.centered();
		palm.normalize_size_to_position();
		palm.fill_empty_field();
		palm_tree.initialize_data_on_gpu(palm);
		palm_tree.texture.load_and_initialize_texture_2d_on_gpu(project::path + "assets/palm_tree/palm_tree.jpg");
		palm_tree.material.color = {1.0f, 1.0f, 1.0f};
		palm_tree.material.phong = {0.45f, 0.62f, 0.12f, 24.0f};
		has_palm_model = true;
	}
	catch (...) {
		has_palm_model = false;
	}

	// palm_fallback_trunk.initialize_data_on_gpu(mesh_primitive_cylinder(0.12f, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 16, 16, true));
	// palm_fallback_trunk.material.texture_settings.active = false;
	// palm_fallback_trunk.material.color = {0.56f, 0.39f, 0.22f};
	// palm_fallback_trunk.material.phong = {0.45f, 0.52f, 0.08f, 10.0f};

	// palm_fallback_leaf.initialize_data_on_gpu(mesh_primitive_cone(0.16f, 0.9f, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, true, 14, 7));
	// palm_fallback_leaf.material.texture_settings.active = false;
	// palm_fallback_leaf.material.color = {0.16f, 0.56f, 0.21f};
	// palm_fallback_leaf.material.phong = {0.45f, 0.56f, 0.08f, 8.0f};

	mesh shrub_billboard_mesh = mesh_primitive_quadrangle({-0.18f, 0.0f, 0.0f}, {0.18f, 0.0f, 0.0f}, {0.15f, 0.0f, 0.52f}, {-0.15f, 0.0f, 0.52f});
	mesh shrub_back = shrub_billboard_mesh;
	shrub_back.flip_connectivity();
	shrub_billboard_mesh.push_back(shrub_back);
	shrub_billboard_mesh.fill_empty_field();
	shrub_billboard.initialize_data_on_gpu(shrub_billboard_mesh);
	shrub_billboard.material.texture_settings.active = false;
	shrub_billboard.material.texture_settings.two_sided = true;
	shrub_billboard.material.color = {0.22f, 0.57f, 0.24f};
	shrub_billboard.material.phong = {0.40f, 0.45f, 0.04f, 6.0f};

	// palms.clear();
	for (int trials = 0; trials < 10000 && palms.size() < 240; ++trials) {
		float const x = rand_uniform(-23.0f, 23.0f);
		float const y = rand_uniform(-23.0f, 23.0f);
		float const h = terrain_height(x, y);
		float const r = std::sqrt(x * x + y * y);
		if (h < sea_level + 0.32f || h > 2.7f || r > island_radius + 1.2f)
			continue;

		if (rand_uniform() < 0.30f + 0.45f * std::exp(-std::pow(h - 0.4f, 2.0f) / 0.45f)) {
			palms.push_back({
			    {x, y, h},
			    rand_uniform(0.72f, 1.45f),
			    rand_uniform(0.0f, 2.0f * Pi),
			    rand_uniform(0.0f, 2.0f * Pi),
			});
		}
	}
	initialize_palm_instance_buffers();

	shrubs.clear();
	for (int trials = 0; trials < 12000 && shrubs.size() < 460; ++trials) {
		float const x = rand_uniform(-24.0f, 24.0f);
		float const y = rand_uniform(-24.0f, 24.0f);
		float const h = terrain_height(x, y);
		if (h < sea_level + 0.15f || h > 1.8f)
			continue;

		shrubs.push_back({
		    {x, y, h},
		    rand_uniform(0.6f, 1.2f),
		    rand_uniform(0.0f, 2.0f * Pi),
		});
	}
	initialize_shrub_instance_buffers();
}

void scene_structure::initialize_summit_tree()
{
	try {
		mesh trunk_mesh = mesh_load_file_obj(project::path + "assets/trunk.obj");
		mesh branches_mesh = mesh_load_file_obj(project::path + "assets/branches.obj");
		mesh foliage_mesh = mesh_load_file_obj(project::path + "assets/foliage.obj");

		trunk_mesh.fill_empty_field();
		branches_mesh.fill_empty_field();
		foliage_mesh.fill_empty_field();

		mesh_transparency_shader.load(
		    project::path + "shaders/mesh_transparency/mesh_transparency.vert.glsl",
		    project::path + "shaders/mesh_transparency/mesh_transparency.frag.glsl"
		);

		summit_trunk.initialize_data_on_gpu(trunk_mesh);
		summit_branches.initialize_data_on_gpu(branches_mesh);
		summit_foliage.initialize_data_on_gpu(foliage_mesh, mesh_transparency_shader);

		summit_trunk.texture.load_and_initialize_texture_2d_on_gpu(project::path + "assets/trunk.png");
		summit_branches.texture.load_and_initialize_texture_2d_on_gpu(project::path + "assets/trunk.png");
		summit_foliage.texture.load_and_initialize_texture_2d_on_gpu(project::path + "assets/pine.png");

		summit_trunk.material.color = {1.0f, 1.0f, 1.0f};
		summit_branches.material.color = {1.0f, 1.0f, 1.0f};
		summit_foliage.material.color = {1.0f, 1.0f, 1.0f};

		summit_trunk.material.phong = {0.45f, 0.55f, 0.10f, 20.0f};
		summit_branches.material.phong = {0.45f, 0.55f, 0.10f, 20.0f};
		summit_foliage.material.phong = {0.45f, 0.55f, 0.06f, 12.0f};

		summit_foliage.material.texture_settings.two_sided = true;

		float const x = -1.0f;
		float const y = 1.0f;
		float const z = terrain_height(x, y);

		summit_tree_position = {x, y, z + 0.05f};
		summit_tree_scale = 0.55f * 2.0f;

		has_summit_tree = true;
	}
	catch (...) {
		has_summit_tree = false;
		std::cout << "Could not load summit tree model." << std::endl;
	}
}

void scene_structure::initialize_palm_instance_buffers()
{
	palm_instance_buffers_initialized = false;
	palm_instance_position_scale.clear();
	palm_instance_rotation.clear();

	if (!has_palm_model || palms.empty())
		return;

	palm_instance_position_scale.resize(static_cast<int>(palms.size()));
	palm_instance_rotation.resize(static_cast<int>(palms.size()));
	for (int k = 0; k < static_cast<int>(palms.size()); ++k) {
		palm_instance const& p = palms[k];
		palm_instance_position_scale[k] = {p.root.x, p.root.y, p.root.z + 0.6f, -2.0f * p.scale};
		palm_instance_rotation[k] = {p.yaw, 0.08f * gui.wind * std::sin(p.sway_phase), 0.0f, 0.6f};
	}

	palm_tree.initialize_supplementary_data_on_gpu(palm_instance_position_scale, 4, 1);
	palm_tree.initialize_supplementary_data_on_gpu(palm_instance_rotation, 5, 1);
	palm_instance_buffers_initialized = true;
}

void scene_structure::update_palm_instance_rotation_buffer(float t)
{
	if (!palm_instance_buffers_initialized)
		return;

	for (int k = 0; k < static_cast<int>(palms.size()); ++k) {
		palm_instance const& p = palms[k];
		float const sway = 0.08f * gui.wind * std::sin(1.15f * t + p.sway_phase);
		palm_instance_rotation[k] = {p.yaw, sway, 0.0f, 0.6f};
	}
	palm_tree.update_supplementary_data_on_gpu(palm_instance_rotation, 5, static_cast<int>(palms.size()));
}

void scene_structure::initialize_shrub_instance_buffers()
{
	shrub_instance_buffers_initialized = false;
	shrub_instance_position_scale.clear();
	shrub_instance_rotation.clear();

	if (shrubs.empty())
		return;

	shrub_instance_position_scale.resize(static_cast<int>(shrubs.size()));
	shrub_instance_rotation.resize(static_cast<int>(shrubs.size()));
	for (int k = 0; k < static_cast<int>(shrubs.size()); ++k) {
		shrub_instance const& s = shrubs[k];
		shrub_instance_position_scale[k] = {s.root.x, s.root.y, s.root.z, s.scale};
		shrub_instance_rotation[k] = {s.yaw, 0.0f, 0.0f, 0.0f};
	}

	shrub_billboard.initialize_supplementary_data_on_gpu(shrub_instance_position_scale, 4, 1);
	shrub_billboard.initialize_supplementary_data_on_gpu(shrub_instance_rotation, 5, 1);
	shrub_instance_buffers_initialized = true;
}

void scene_structure::initialize_fauna()
{
	bird_body.initialize_data_on_gpu(mesh_primitive_ellipsoid({1.0f, 0.3f, 0.18f}));
	bird_body.material.texture_settings.active = false;
	bird_body.material.color = {0.90f, 0.90f, 0.92f};
	bird_body.material.phong = {0.45f, 0.45f, 0.12f, 20.0f};

	bird_wing.initialize_data_on_gpu(mesh_primitive_ellipsoid({1.0f, 0.24f, 0.08f}));
	bird_wing.material.texture_settings.active = false;
	bird_wing.material.color = {0.80f, 0.82f, 0.84f};
	bird_wing.material.phong = {0.45f, 0.42f, 0.06f, 10.0f};

	birds.clear();
	for (int k = 0; k < 16; ++k) {
		birds.push_back({
		    {0.0f, 0.0f, 0.0f},
		    rand_uniform(7.0f, 19.0f),
		    rand_uniform(0.18f, 0.45f),
		    rand_uniform(0.0f, 2.0f * Pi),
		    rand_uniform(3.8f, 8.6f),
		    rand_uniform(0.20f, 0.42f),
		});
	}
	initialize_bird_instance_buffers();
}

void scene_structure::initialize_bird_instance_buffers()
{
    bird_instance_buffers_initialized = false;

    int const N = static_cast<int>(birds.size());
    if (N == 0)
        return;

    bird_body_position_scale.resize(N);
    bird_body_rotation.resize(N);

    bird_left_wing_position_scale.resize(N);
    bird_left_wing_rotation.resize(N);

    bird_right_wing_position_scale.resize(N);
    bird_right_wing_rotation.resize(N);

    update_bird_instance_buffers(0.0f);

    bird_body.initialize_supplementary_data_on_gpu(bird_body_position_scale, 4, 1);
    bird_body.initialize_supplementary_data_on_gpu(bird_body_rotation, 5, 1);

    bird_wing.initialize_supplementary_data_on_gpu(bird_left_wing_position_scale, 4, 1);
    bird_wing.initialize_supplementary_data_on_gpu(bird_left_wing_rotation, 5, 1);

    bird_instance_buffers_initialized = true;
}

void scene_structure::update_bird_instance_buffers(float t)
{
    int const N = static_cast<int>(birds.size());

    for (int k = 0; k < N; ++k) {
        bird_instance const& b = birds[k];

        float const a = b.speed * t + b.phase;
        vec3 const pos = b.center + vec3{
            b.orbit_radius * std::cos(a),
            b.orbit_radius * std::sin(a),
            b.altitude + 0.25f * std::sin(2.5f * a)
        };

        float const heading = a + Pi / 2.0f;
        float const wing = 0.9f * std::sin(8.0f * a);

        rotation_transform const R =
            rotation_transform::from_axis_angle({0.0f, 0.0f, 1.0f}, heading);

        bird_body_position_scale[k] = {pos.x, pos.y, pos.z, b.scale};
        bird_body_rotation[k] = {heading, 0.0f, 0.0f, 0.0f};

        float const wing_forward = 0.40f * b.scale;
		float const wing_side    = 0.76f * b.scale;
		float const wing_up      = 0.12f * b.scale;

		vec3 const left_pos =
			pos + R * vec3{wing_forward, wing_side, wing_up};

		vec3 const right_pos =
			pos + R * vec3{wing_forward, -wing_side, wing_up};

        bird_left_wing_position_scale[k] =
            {left_pos.x, left_pos.y, left_pos.z, b.scale};

        bird_right_wing_position_scale[k] =
            {right_pos.x, right_pos.y, right_pos.z, b.scale};

        bird_left_wing_rotation[k]  = {heading + Pi/2.0f, wing, 0.0f, -0.9f};
		bird_right_wing_rotation[k] = {heading - Pi/2.0f, wing, 0.0f, -0.9f};
    }
}


void scene_structure::initialize_particles()
{
	mesh foam_quad = mesh_primitive_quadrangle({-0.18f, 0.0f, 0.0f}, {0.18f, 0.0f, 0.0f}, {0.18f, 0.0f, 0.28f}, {-0.18f, 0.0f, 0.28f});
	mesh foam_back = foam_quad;
	foam_back.flip_connectivity();
	foam_quad.push_back(foam_back);
	foam_quad.fill_empty_field();
	foam_billboard.initialize_data_on_gpu(foam_quad);
	foam_billboard.material.texture_settings.active = false;
	foam_billboard.material.texture_settings.two_sided = true;
	foam_billboard.material.color = {0.96f, 0.99f, 1.0f};
	foam_billboard.material.alpha = 0.45f;
	foam_billboard.material.phong = {0.35f, 0.20f, 0.02f, 6.0f};

	sun_disc.initialize_data_on_gpu(mesh_primitive_disc(1.0f, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 64));
	sun_disc.material.texture_settings.active = false;
	sun_disc.material.texture_settings.two_sided = true;
	sun_disc.material.color =  {1.0f, 0.92f, 0.45f};
	sun_disc.material.phong = {1.0f, 0.0f, 0.0f, 1.0f};

	moon_disc.initialize_data_on_gpu(mesh_primitive_disc(1.0f, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 64));
	moon_disc.material.texture_settings.active = false;
	moon_disc.material.texture_settings.two_sided = true;
	moon_disc.material.color = {0.84f, 0.91f, 1.0f};
	moon_disc.material.phong = {1.0f, 0.0f, 0.0f, 1.0f};

	// Load glow shader (thêm dòng này nếu chưa có)
	glow_shader.load(project::path + "shaders/glow/glow.vert.glsl",
					project::path + "shaders/glow/glow.frag.glsl");
	glow_orb.initialize_data_on_gpu(mesh_primitive_sphere(1.0f), glow_shader);
	glow_orb.material.texture_settings.active = false;
	glow_orb.material.color = {1.0f, 0.88f, 0.42f};
	glow_orb.material.alpha = 1.0f;
	glow_orb.material.phong = {1.0f, 0.0f, 0.0f, 1.0f};

	foams.clear();
	for (int k = 0; k < 420; ++k) {
		float const a = rand_uniform(0.0f, 2.0f * Pi);
		float const r = rand_uniform(island_radius - 1.2f, island_radius + 2.5f);
		float const x = r * std::cos(a);
		float const y = r * std::sin(a);
		foams.push_back({
		    {x, y, sea_level + 0.02f},
		    rand_uniform(0.0f, 8.0f),
		    rand_uniform(0.55f, 1.35f),
		});
	}
	initialize_foam_instance_buffers();

	glows.clear();
	for (int k = 0; k < 220; ++k) {
		float const a = rand_uniform(0.0f, 2.0f * Pi);
		float const r = rand_uniform(4.0f, island_radius - 1.8f);
		float const x = r * std::cos(a);
		float const y = r * std::sin(a);
		float const z = terrain_height(x, y) + rand_uniform(0.3f, 1.4f);
		if (z < sea_level + 0.3f)
			continue;

		glows.push_back({
			{x, y, z},
			rand_uniform(0.045f, 0.105f),  // radius lớn hơn
			rand_uniform(0.10f, 0.35f),
			rand_uniform(0.9f, 1.4f),
			rand_uniform(0.0f, 10.0f),
		});
			}
}

void scene_structure::initialize_weather_effects()
{
	cloud_billboard.initialize_data_on_gpu(make_cloud_billboard_mesh());
	cloud_billboard.material.texture_settings.active = false;
	cloud_billboard.material.texture_settings.two_sided = true;
	cloud_billboard.material.color = {1.0f, 1.0f, 1.0f};
	cloud_billboard.material.alpha = 1.0f;
	cloud_billboard.material.phong = {1.0f, 0.05f, 0.0f, 1.0f};

	wind_streak_ribbon.initialize_data_on_gpu(make_wind_streak_ribbon_mesh());
	wind_streak_ribbon.material.texture_settings.active = false;
	wind_streak_ribbon.material.texture_settings.two_sided = true;
	wind_streak_ribbon.material.color = {1.0f, 1.0f, 1.0f};
	wind_streak_ribbon.material.alpha = 0.45f;
	wind_streak_ribbon.material.phong = {0.95f, 0.04f, 0.0f, 4.0f};

	clouds.clear();
	for (int k = 0; k < 9; ++k) {
		clouds.push_back({
		    {rand_uniform(-48.0f, 48.0f), rand_uniform(-42.0f, 42.0f), rand_uniform(10.5f, 17.5f)},
		    rand_uniform(3.2f, 6.8f),
		    rand_uniform(0.0f, 12.0f),
		    rand_uniform(-0.18f, 0.18f),
		});
	}
	initialize_cloud_instance_buffers();

	wind_streaks.clear();

	int const N_base_streaks = 14;
	vec2 const wind_dir = wind_direction_xy();
	vec3 const side = vec3{-wind_dir.y, wind_dir.x, 0.0f};

	for (int k = 0; k < N_base_streaks; ++k) {
		vec3 const base_pos = {
			rand_uniform(-44.0f, 44.0f),
			rand_uniform(-36.0f, 36.0f),
			rand_uniform(8.0f, 15.5f)
		};

		float const base_length = rand_uniform(7.0f, 12.0f);
		float const base_width  = rand_uniform(2.4f, 3.6f);
		float const base_phase  = rand_uniform(0.0f, 10.0f);
		float const base_bend   = rand_uniform(0.75f, 1.25f);

		float const offset = rand_uniform(0.05f, 0.08f);

		wind_streaks.push_back({
			base_pos - offset * side,
			base_length,
			base_width,
			base_phase,
			base_bend,
		});

		wind_streaks.push_back({
			base_pos + offset * side,
			base_length * rand_uniform(0.85f, 1.05f),
			base_width * rand_uniform(0.75f, 0.95f),
			base_phase + rand_uniform(0.4f, 1.2f),
			base_bend * rand_uniform(0.90f, 1.10f),
		});
	}
}

void scene_structure::initialize_boats()
{
	boat_hull.initialize_data_on_gpu(create_boat_hull_mesh());
	boat_hull.material.texture_settings.active = false;
	boat_hull.material.color = {0.34f, 0.17f, 0.07f};
	boat_hull.material.phong = {0.45f, 0.50f, 0.10f, 18.0f};

	boat_cabin.initialize_data_on_gpu(create_boat_cabin_mesh());
	boat_cabin.material.texture_settings.active = false;
	boat_cabin.material.color = {0.84f, 0.78f, 0.62f};
	boat_cabin.material.phong = {0.50f, 0.42f, 0.10f, 22.0f};

	wake_ellipse.initialize_data_on_gpu(create_wake_ellipse_mesh());
	wake_ellipse.material.texture_settings.active = false;
	wake_ellipse.material.texture_settings.two_sided = true;
	wake_ellipse.material.color = {0.76f, 0.91f, 1.0f};
	wake_ellipse.material.alpha = 0.22f;
	wake_ellipse.material.phong = {0.40f, 0.12f, 0.0f, 4.0f};

	boats = {
	    {34.0f,  0.13f, 0.0f, 1.0f,  0.0f},
	    {43.0f, -0.09f, 2.1f, 0.8f,  1.8f},
	    {52.0f,  0.07f, 4.3f, 1.2f, -2.5f},
	};
}

void scene_structure::initialize_character()
{
	character.position = {0.0f, 0.0f, terrain_height(0.0f, 0.0f)};
	character.heading = 0.0f;
	character.enabled = false;
	character.is_jumping = false;
	character.jump_key_was_pressed = false;
	character.vertical_velocity = 0.0f;

	character_body.initialize_data_on_gpu(mesh_primitive_cylinder(0.22f, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.75f}, 18, 8, true));
	character_body.material.texture_settings.active = false;
	character_body.material.color = {0.18f, 0.36f, 0.76f};
	character_body.material.phong = {0.48f, 0.55f, 0.12f, 24.0f};

	character_head.initialize_data_on_gpu(mesh_primitive_sphere(0.20f, {0.0f, 0.0f, 0.0f}, 18, 12));
	character_head.material.texture_settings.active = false;
	character_head.material.color = {0.88f, 0.66f, 0.48f};
	character_head.material.phong = {0.48f, 0.50f, 0.10f, 18.0f};

	character_left_arm.initialize_data_on_gpu(mesh_primitive_cylinder(0.055f, {0.0f, 0.0f, -0.25f}, {0.0f, 0.0f, 0.25f}, 12, 4, true));
	character_right_arm.initialize_data_on_gpu(mesh_primitive_cylinder(0.055f, {0.0f, 0.0f, -0.25f}, {0.0f, 0.0f, 0.25f}, 12, 4, true));
	character_left_leg.initialize_data_on_gpu(mesh_primitive_cylinder(0.070f, {0.0f, 0.0f, -0.32f}, {0.0f, 0.0f, 0.32f}, 12, 4, true));
	character_right_leg.initialize_data_on_gpu(mesh_primitive_cylinder(0.070f, {0.0f, 0.0f, -0.32f}, {0.0f, 0.0f, 0.32f}, 12, 4, true));

	character_left_arm.material.texture_settings.active = false;
	character_right_arm.material.texture_settings.active = false;
	character_left_leg.material.texture_settings.active = false;
	character_right_leg.material.texture_settings.active = false;
	character_left_arm.material.color = {0.18f, 0.36f, 0.76f};
	character_right_arm.material.color = {0.18f, 0.36f, 0.76f};
	character_left_leg.material.color = {0.12f, 0.16f, 0.22f};
	character_right_leg.material.color = {0.12f, 0.16f, 0.22f};
	character_left_arm.material.phong = {0.45f, 0.48f, 0.08f, 12.0f};
	character_right_arm.material.phong = character_left_arm.material.phong;
	character_left_leg.material.phong = {0.45f, 0.48f, 0.08f, 12.0f};
	character_right_leg.material.phong = character_left_leg.material.phong;
}

void scene_structure::initialize_magic_effect()
{
	numarray<vec3> outer;
	numarray<vec3> inner;
	numarray<vec3> tri_a;
	numarray<vec3> tri_b;

	int const N = 128;
	for (int k = 0; k <= N; ++k) {
		float const u = 2.0f * Pi * k / float(N);
		outer.push_back({std::cos(u), std::sin(u), 0.0f});
		inner.push_back({0.65f * std::cos(u), 0.65f * std::sin(u), 0.0f});
	}

	for (int k = 0; k <= 3; ++k) {
		float const u = 2.0f * Pi * k / 3.0f;
		tri_a.push_back({0.78f * std::cos(u), 0.78f * std::sin(u), 0.0f});
	}
	for (int k = 0; k <= 3; ++k) {
		float const u = Pi / 3.0f + 2.0f * Pi * k / 3.0f;
		tri_b.push_back({0.78f * std::cos(u), 0.78f * std::sin(u), 0.0f});
	}

	magic_outer_ring.initialize_data_on_gpu(outer);
	magic_inner_ring.initialize_data_on_gpu(inner);
	magic_triangle_a.initialize_data_on_gpu(tri_a);
	magic_triangle_b.initialize_data_on_gpu(tri_b);
	magic_outer_ring.color = {1.0f, 0.78f, 0.20f};
	magic_inner_ring.color = {0.50f, 0.90f, 1.0f};
	magic_triangle_a.color = {1.0f, 0.84f, 0.28f};
	magic_triangle_b.color = {0.58f, 0.92f, 1.0f};

	magic_cylinder.initialize_data_on_gpu(mesh_primitive_cylinder(1.0f, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 64, 8, false));
	magic_cylinder.material.texture_settings.active = false;
	magic_cylinder.material.texture_settings.two_sided = true;
	magic_cylinder.material.color = {0.55f, 0.85f, 1.0f};
	magic_cylinder.material.alpha = 0.18f;
	magic_cylinder.material.phong = {0.85f, 0.05f, 0.0f, 4.0f};

	magic_particle.initialize_data_on_gpu(mesh_primitive_sphere(1.0f));
	magic_particle.material.texture_settings.active = false;
	magic_particle.material.color = {0.72f, 0.95f, 1.0f};
	magic_particle.material.alpha = 0.75f;
	magic_particle.material.phong = {0.90f, 0.08f, 0.0f, 4.0f};

	grass_billboard.initialize_data_on_gpu(create_grass_billboard_mesh());
	grass_billboard.texture.load_and_initialize_texture_2d_on_gpu(project::path + "assets/grass.png");
	grass_billboard.material.texture_settings.active = true;
	grass_billboard.material.texture_settings.two_sided = true;
	grass_billboard.material.color = {1.0f, 1.0f, 1.0f};
	grass_billboard.material.alpha = 0.75f;
	grass_billboard.material.phong = {0.35f, 0.25f, 0.05f, 8.0f};
}

void scene_structure::initialize_foam_instance_buffers()
{
	foam_instance_buffers_initialized = false;
	foam_instance_position_scale.clear();
	foam_instance_rotation_alpha.clear();

	if (foams.empty())
		return;

	foam_instance_position_scale.resize(static_cast<int>(foams.size()));
	foam_instance_rotation_alpha.resize(static_cast<int>(foams.size()));
	update_foam_instance_buffers(0.0f, camera_control.camera_model.position());

	foam_billboard.initialize_supplementary_data_on_gpu(foam_instance_position_scale, 4, 1);
	foam_billboard.initialize_supplementary_data_on_gpu(foam_instance_rotation_alpha, 5, 1);
	foam_instance_buffers_initialized = true;
}

void scene_structure::initialize_cloud_instance_buffers()
{
	cloud_instance_buffers_initialized = false;
	cloud_instance_position_scale.clear();
	cloud_instance_rotation_alpha.clear();

	if (clouds.empty())
		return;

	cloud_instance_position_scale.resize(static_cast<int>(clouds.size()));
	cloud_instance_rotation_alpha.resize(static_cast<int>(clouds.size()));
	update_cloud_instance_buffers(0.0f, camera_control.camera_model.position());

	cloud_billboard.initialize_supplementary_data_on_gpu(cloud_instance_position_scale, 4, 1);
	cloud_billboard.initialize_supplementary_data_on_gpu(cloud_instance_rotation_alpha, 5, 1);
	cloud_instance_buffers_initialized = true;
}

void scene_structure::update_cloud_instance_buffers(float t, vec3 const& camera_pos)
{
	if (clouds.empty())
		return;

	if (cloud_instance_position_scale.size() != static_cast<int>(clouds.size()))
		cloud_instance_position_scale.resize(static_cast<int>(clouds.size()));
	if (cloud_instance_rotation_alpha.size() != static_cast<int>(clouds.size()))
		cloud_instance_rotation_alpha.resize(static_cast<int>(clouds.size()));

	vec2 const wind_dir = wind_direction_xy();
	float const travel = t * cloud_speed * (0.20f + gui.wind);

	std::vector<transparent_instance> sorted_clouds;
	sorted_clouds.reserve(clouds.size());
	for (int k = 0; k < static_cast<int>(clouds.size()); ++k) {
		cloud_instance const& c = clouds[k];
		vec3 pos = c.center + vec3{wind_dir.x, wind_dir.y, 0.0f} * (travel + 9.0f * c.phase);
		pos.x = wrap_coordinate(pos.x, 56.0f);
		pos.y = wrap_coordinate(pos.y, 48.0f);

		float const pulse = 0.5f + 0.5f * std::sin(0.45f * t + c.phase);
		float const daylight = 0.42f + 0.58f * (1.0f - night_factor);
		sorted_clouds.push_back({k, distance_squared(camera_pos, pos), pos, daylight * (0.30f + 0.10f * pulse)});
	}

	std::sort(sorted_clouds.begin(), sorted_clouds.end(),
	          [](transparent_instance const& a, transparent_instance const& b) { return a.distance2 > b.distance2; });

	for (int k = 0; k < static_cast<int>(sorted_clouds.size()); ++k) {
		transparent_instance const& instance = sorted_clouds[k];
		cloud_instance const& c = clouds[instance.index];
		cloud_instance_position_scale[k] = {instance.position.x, instance.position.y, instance.position.z, c.scale};
		cloud_instance_rotation_alpha[k] = {c.spin, instance.alpha, 0.0f, 0.0f};
	}
}

void scene_structure::update_foam_instance_buffers(float t, vec3 const& camera_pos)
{
	if (foams.empty())
		return;

	if (foam_instance_position_scale.size() != static_cast<int>(foams.size()))
		foam_instance_position_scale.resize(static_cast<int>(foams.size()));
	if (foam_instance_rotation_alpha.size() != static_cast<int>(foams.size()))
		foam_instance_rotation_alpha.resize(static_cast<int>(foams.size()));

	std::vector<transparent_instance> sorted_foams;
	sorted_foams.reserve(foams.size());
	for (int k = 0; k < static_cast<int>(foams.size()); ++k) {
		foam_instance const& f = foams[k];
		float const pulse = 0.5f + 0.5f * std::sin(2.6f * t + f.phase);
		float const angle = std::atan2(f.anchor.y, f.anchor.x);
		vec3 pos = f.anchor;
		pos.x += 0.28f * std::cos(angle) * std::sin(1.3f * t + f.phase);
		pos.y += 0.28f * std::sin(angle) * std::sin(1.3f * t + f.phase);
		pos.z = water_height(pos.x, pos.y, t) + 0.03f;

		sorted_foams.push_back({k, distance_squared(camera_pos, pos), pos, 0.18f + 0.32f * pulse});
	}

	std::sort(sorted_foams.begin(), sorted_foams.end(),
	          [](transparent_instance const& a, transparent_instance const& b) { return a.distance2 > b.distance2; });

	for (int k = 0; k < static_cast<int>(sorted_foams.size()); ++k) {
		transparent_instance const& instance = sorted_foams[k];
		foam_instance const& f = foams[instance.index];
		foam_instance_position_scale[k] = {instance.position.x, instance.position.y, instance.position.z, f.scale};
		foam_instance_rotation_alpha[k] = {0.0f, instance.alpha, 0.0f, 0.0f};
	}
}

void scene_structure::update_day_night_cycle(float t)
{
	// Day-night cycle
	time_of_day = std::fmod(std::max(0.0f, day_night_speed) * t, 1.0f);
	sun_direction = gui.moving_sun ? compute_sun_direction(time_of_day) : normalize(vec3{0.52f, -0.28f, 0.81f});

	day_factor = 1.0f - pulse_cycle(time_of_day);
	night_factor = pulse_cycle(time_of_day);
	dusk_factor = std::pow(std::sin(2.0f * Pi * time_of_day), 2.0f);

	// Sky/background colors
	vec3 const sky_day = {0.60f, 0.82f, 0.96f};
	vec3 const sky_dusk = {0.72f, 0.46f, 0.58f};
	vec3 const sky_night = {0.04f, 0.07f, 0.14f};
	vec3 sky = mix_color(sky_day, sky_dusk, dusk_factor);
	sky = mix_color(sky, sky_night, night_factor);
	environment.background_color = sky;

	// Light direction/intensity (sun by day, weak moon-like light at night)
	float const sun_elevation = sun_direction.z;
	environment.light = sun_distance * sun_direction;
	environment.light_intensity = 0.14f + 1.10f * std::max(0.0f, sun_elevation);

	// Cold ambient at night to avoid black crush
	vec3 const ambient_day = {1.0f, 0.98f, 0.94f};
	vec3 const ambient_night = {0.46f, 0.55f, 0.74f};
	environment.ambient_tint = mix_color(ambient_day, ambient_night, night_factor);
	environment.ambient_intensity = 0.46f + 0.22f * night_factor;

	// Fog uniforms
	float const fog_d0 = std::min(fog_day_density, fog_night_density);
	float const fog_d1 = std::max(fog_day_density, fog_night_density);
	environment.use_fog = true;
	environment.fog_density = fog_d0 + (fog_d1 - fog_d0) * saturate(0.65f * night_factor + 0.35f * dusk_factor);
	vec3 const fog_day = {0.74f, 0.84f, 0.92f};
	vec3 const fog_dusk = {0.58f, 0.61f, 0.70f};
	vec3 const fog_night = {0.11f, 0.14f, 0.20f};
	environment.fog_color = mix_color(mix_color(fog_day, fog_dusk, dusk_factor), fog_night, night_factor);

	// Keep skybox synchronized to the cycle without regenerating cubemap every frame
	if (gui.use_textured_skybox && skybox_day_texture_loaded) {
		skybox.alpha_color_blending = (skybox_night_texture_loaded ? 0.03f : 0.10f) * dusk_factor;
	}
	else {
		skybox.alpha_color_blending = 0.08f + 0.62f * night_factor + 0.20f * dusk_factor;
	}
	skybox.alpha_color_blending = saturate(skybox.alpha_color_blending);
	skybox.color_blending = mix_color(mix_color(sky_day, sky_dusk, dusk_factor), sky_night, night_factor);
	skybox_night.alpha_color_blending = skybox.alpha_color_blending;
	skybox_night.color_blending = skybox.color_blending;
}

void scene_structure::draw_lighthouse_beam_effect(float t, vec3 const& lighthouse_pos)
{
	beam_visibility_debug = 0.0f;
	if (!gui.display_lighthouse_beam)
		return;

	float const fog_gate = smoothstep(fog_day_density * 1.2f, fog_night_density * 0.42f, environment.fog_density);
	float const night_gate = saturate(0.55f * night_factor + 0.50f * dusk_factor);
	float const visibility = saturate((0.20f + lighthouse_beam_strength) * fog_gate * night_gate);
	beam_visibility_debug = visibility;
	if (visibility < 0.01f)
		return;

	float const lighthouse_angle = t * lighthouse_rotation_speed;
	vec3 const beam_origin = lighthouse_pos + vec3{0.0f, 0.0f, 4.85f};
	vec3 const beam_dir = normalize(vec3{std::cos(lighthouse_angle), std::sin(lighthouse_angle), -0.08f});
	rotation_transform const R_scan = rotation_transform::from_vector_transform({1.0f, 0.0f, 0.0f}, beam_dir);

	// Lighthouse bulb (optional emissive-like sphere)
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);
	lighthouse_bulb.material.alpha = 0.30f + 0.70f * visibility;
	lighthouse_bulb.material.color = mix_color(vec3{0.95f, 0.90f, 0.75f}, vec3{1.0f, 0.62f, 0.38f}, 0.55f * night_factor);
	lighthouse_bulb.model.translation = beam_origin;
	lighthouse_bulb.model.rotation = rotation_transform();
	lighthouse_bulb.model.scaling = 0.16f;
	lighthouse_bulb.model.scaling_xyz = {1.0f, 1.0f, 1.0f};
	draw(lighthouse_bulb, environment);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);

	// Volumetric beam (translucent mesh)
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);

	// Lighthouse volumetric beam (unlit transparent shader)
	uniform_generic_structure beam_uniforms;
	beam_uniforms.uniform_vec3["beam_color"] = {1.0f, 0.88f, 0.45f};
	beam_uniforms.uniform_float["beam_alpha"] = 0.30f + 0.22f * visibility;
	beam_uniforms.uniform_float["beam_length"] = lighthouse_beam_length;
	beam_uniforms.uniform_float["beam_radius_near"] = lighthouse_beam_radius_near;
	beam_uniforms.uniform_float["beam_radius_far"] = lighthouse_beam_radius_far;

	lighthouse_beam.model.translation = beam_origin;
	lighthouse_beam.model.rotation = R_scan;
	lighthouse_beam.model.scaling = 1.0f;
	lighthouse_beam.model.scaling_xyz = {1.0f, 1.0f, 1.0f};
	draw(lighthouse_beam, environment, 1, false, beam_uniforms);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
}

vec3 scene_structure::lighthouse_world_position() const
{
	return {2.8f, -1.9f, terrain_height(2.8f, -1.9f)};
}

void scene_structure::draw_structures(float t)
{
	vec3 const lighthouse_pos = lighthouse_world_position();
	float const blink = 0.65f + 0.35f * std::sin(3.0f * t);
	float const window_night = saturate(0.85f * night_factor + 0.25f * dusk_factor);

	lighthouse_tower.model.translation = lighthouse_pos;
	lighthouse_tower.model.rotation = rotation_transform();
	lighthouse_tower.model.scaling = 1.0f;
	lighthouse_tower.model.scaling_xyz = {1.0f, 1.0f, 1.0f};
	draw(lighthouse_tower, environment);

	lighthouse_ring.model.translation = lighthouse_pos + vec3{0.0f, 0.0f, 4.2f};
	lighthouse_ring.model.rotation = rotation_transform();
	lighthouse_ring.model.scaling = 1.0f;
	lighthouse_ring.model.scaling_xyz = {1.0f, 1.0f, 1.0f};
	draw(lighthouse_ring, environment);

	lighthouse_roof.model.translation = lighthouse_pos + vec3{0.0f, 0.0f, 4.6f};
	lighthouse_roof.model.rotation = rotation_transform();
	lighthouse_roof.model.scaling = 1.0f;
	lighthouse_roof.model.scaling_xyz = {1.0f, 1.0f, 1.0f};
	draw(lighthouse_roof, environment);

	for (int k = 0; k < 6; ++k) {
		float const a = 2.0f * Pi * k / 6.0f;
		lighthouse_window.material.color = mix_color(vec3{0.45f, 0.75f, 0.94f}, vec3{1.0f, 0.74f, 0.46f}, window_night * blink);
		lighthouse_window.model.translation = lighthouse_pos + vec3{0.75f * std::cos(a), 0.75f * std::sin(a), 3.2f};
		lighthouse_window.model.rotation = rotation_transform::from_axis_angle({0.0f, 0.0f, 1.0f}, a);
		lighthouse_window.model.scaling = 1.0f;
		lighthouse_window.model.scaling_xyz = {0.20f, 0.06f, 0.28f};
		draw(lighthouse_window, environment);
	}

	vec3 const dock_origin = {6.5f, -5.0f, terrain_height(6.5f, -5.0f) + 0.06f};
	vec3 const dock_dir = normalize(vec3{1.0f, -0.4f, 0.0f});
	vec3 const dock_side = {-dock_dir.y, dock_dir.x, 0.0f};
	float const dock_yaw = std::atan2(dock_dir.y, dock_dir.x);
	for (int k = 0; k < 9; ++k) {
		vec3 const c = dock_origin + 0.95f * k * dock_dir;
		float const cz = std::max(sea_level + 0.08f, terrain_height(c.x, c.y) + 0.07f);

		dock_plank.model.translation = {c.x, c.y, cz};
		dock_plank.model.rotation = rotation_transform::from_axis_angle({0.0f, 0.0f, 1.0f}, dock_yaw);
		dock_plank.model.scaling = 1.0f;
		dock_plank.model.scaling_xyz = {0.9f, 0.45f, 0.08f};
		draw(dock_plank, environment);

		for (int side = -1; side <= 1; side += 2) {
			vec3 const pile_pos = c + 0.22f * float(side) * dock_side;
			float const pile_bottom = terrain_height(pile_pos.x, pile_pos.y) - 0.5f;
			float const pile_top = cz - 0.03f;

			dock_pile.model.translation = {pile_pos.x, pile_pos.y, pile_bottom};
			dock_pile.model.rotation = rotation_transform();
			dock_pile.model.scaling = pile_top - pile_bottom;
			dock_pile.model.scaling_xyz = {1.0f, 1.0f, 1.0f};
			draw(dock_pile, environment);
		}
	}
}

void scene_structure::draw_vegetation(float t)
{
	if (!gui.display_vegetation)
		return;

	if (has_palm_model && palm_instance_buffers_initialized) {
		update_palm_instance_rotation_buffer(t);
		palm_tree.model.translation = {0.0f, 0.0f, 0.0f};
		palm_tree.model.rotation = rotation_transform();
		palm_tree.model.scaling = 1.0f;
		palm_tree.model.scaling_xyz = {1.0f, 1.0f, 1.0f};
		draw(palm_tree, environment, static_cast<int>(palms.size()));
	}
	else {
		for (palm_instance const& p : palms) {
			float const sway = 0.08f * gui.wind * std::sin(1.15f * t + p.sway_phase);
			rotation_transform const R = rotation_transform::from_axis_angle({0.0f, 0.0f, 1.0f}, p.yaw) *
			                             rotation_transform::from_axis_angle({0.0f, 1.0f, 0.0f}, sway);
			palm_fallback_trunk.model.translation = p.root;
			palm_fallback_trunk.model.rotation = R;
			palm_fallback_trunk.model.scaling = 1.8f * p.scale;
			palm_fallback_trunk.model.scaling_xyz = {1.0f, 1.0f, 1.0f};
			draw(palm_fallback_trunk, environment);

			for (int k = 0; k < 6; ++k) {
				float const a = 2.0f * Pi * k / 6.0f;
				palm_fallback_leaf.model.translation = p.root + vec3{0.0f, 0.0f, 1.7f * p.scale};
				palm_fallback_leaf.model.rotation = R * rotation_transform::from_axis_angle({0.0f, 0.0f, 1.0f}, a) *
				                                rotation_transform::from_axis_angle({1.0f, 0.0f, 0.0f}, 0.95f);
				palm_fallback_leaf.model.scaling = 0.85f * p.scale;
				palm_fallback_leaf.model.scaling_xyz = {1.0f, 1.0f, 1.0f};
				draw(palm_fallback_leaf, environment);
			}
		}
	}

	if (shrub_instance_buffers_initialized) {
		shrub_billboard.model.translation = {0.0f, 0.0f, 0.0f};
		shrub_billboard.model.rotation = rotation_transform();
		shrub_billboard.model.scaling = 1.0f;
		shrub_billboard.model.scaling_xyz = {1.0f, 1.0f, 1.0f};

		uniform_generic_structure shrub_uniforms;
		shrub_uniforms.uniform_int["instancing_mode"] = 1;
		draw(shrub_billboard, environment, static_cast<int>(shrubs.size()), true, shrub_uniforms);
	}
}

void scene_structure::draw_summit_tree()
{
	if (!has_summit_tree)
		return;

	if (!gui.display_summit_tree)
		return;

	rotation_transform const Rx =
		rotation_transform::from_axis_angle(
			{1.0f, 0.0f, 0.0f},
			Pi/2.0f
		);

	rotation_transform const Rz =
		rotation_transform::from_axis_angle(
			{0.0f, 0.0f, 1.0f},
			0.35f
		);

	rotation_transform const R = Rz * Rx;

	summit_trunk.model.translation = summit_tree_position;
	summit_trunk.model.rotation = R;
	summit_trunk.model.scaling = summit_tree_scale;
	summit_trunk.model.scaling_xyz = {1.0f, 1.0f, 1.0f};
	draw(summit_trunk, environment);

	summit_branches.model.translation = summit_tree_position;
	summit_branches.model.rotation = R;
	summit_branches.model.scaling = summit_tree_scale;
	summit_branches.model.scaling_xyz = {1.0f, 1.0f, 1.0f};
	draw(summit_branches, environment);

	if (gui.display_summit_tree_foliage) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDepthMask(GL_FALSE);

		summit_foliage.model.translation = summit_tree_position;
		summit_foliage.model.rotation = R;
		summit_foliage.model.scaling = summit_tree_scale;
		summit_foliage.model.scaling_xyz = {1.0f, 1.0f, 1.0f};
		draw(summit_foliage, environment);

		glDepthMask(GL_TRUE);
		glDisable(GL_BLEND);
	}
}

void scene_structure::draw_fauna(float t)
{
    if (!gui.display_fauna)
        return;

    if (!bird_instance_buffers_initialized)
        return;

    int const N = static_cast<int>(birds.size());

    update_bird_instance_buffers(t);

    bird_body.update_supplementary_data_on_gpu(bird_body_position_scale, 4, N);
    bird_body.update_supplementary_data_on_gpu(bird_body_rotation, 5, N);

    uniform_generic_structure bird_uniforms;
    bird_uniforms.uniform_int["instancing_mode"] = 0;
	uniform_generic_structure bird_wing_uniforms;
	bird_wing_uniforms.uniform_int["instancing_mode"] = 3;

    bird_body.model.translation = {0.0f, 0.0f, 0.0f};
    bird_body.model.rotation = rotation_transform();
    bird_body.model.scaling = 1.0f;
    bird_body.model.scaling_xyz = {1.0f, 1.0f, 1.0f};

    draw(bird_body, environment, N, true, bird_uniforms);

    bird_wing.model.translation = {0.0f, 0.0f, 0.0f};
    bird_wing.model.rotation = rotation_transform();
    bird_wing.model.scaling = 1.0f;
    bird_wing.model.scaling_xyz = {1.0f, 1.0f, 1.0f};

    bird_wing.update_supplementary_data_on_gpu(bird_left_wing_position_scale, 4, N);
    bird_wing.update_supplementary_data_on_gpu(bird_left_wing_rotation, 5, N);
    draw(bird_wing, environment, N, true, bird_wing_uniforms);

    bird_wing.update_supplementary_data_on_gpu(bird_right_wing_position_scale, 4, N);
    bird_wing.update_supplementary_data_on_gpu(bird_right_wing_rotation, 5, N);
    draw(bird_wing, environment, N, true, bird_wing_uniforms);
}

void scene_structure::draw_foam(float t)
{
	if (!gui.display_foam)
		return;

	if (!foam_instance_buffers_initialized)
		return;

	vec3 const camera_pos = camera_control.camera_model.position();
	update_foam_instance_buffers(t, camera_pos);
	foam_billboard.update_supplementary_data_on_gpu(foam_instance_position_scale, 4, static_cast<int>(foams.size()));
	foam_billboard.update_supplementary_data_on_gpu(foam_instance_rotation_alpha, 5, static_cast<int>(foams.size()));

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);

	foam_billboard.material.alpha = 1.0f;
	foam_billboard.model.translation = {0.0f, 0.0f, 0.0f};
	foam_billboard.model.rotation = rotation_transform();
	foam_billboard.model.scaling = 1.0f;
	foam_billboard.model.scaling_xyz = {1.0f, 1.0f, 1.0f};

	uniform_generic_structure foam_uniforms;
	foam_uniforms.uniform_int["instancing_mode"] = 2;
	draw(foam_billboard, environment, static_cast<int>(foams.size()), true, foam_uniforms);

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
}

void scene_structure::draw_glows(float t)
{
    if (!gui.display_vegetation)
        return;

    vec3 const camera_pos = camera_control.camera_model.position();
    std::vector<transparent_instance> sorted_glows;
    sorted_glows.reserve(glows.size());
    for (int k = 0; k < static_cast<int>(glows.size()); ++k) {
        glow_instance const& g = glows[k];
        vec3 p = g.center;
        p.x += 0.16f * std::sin(0.9f * t + g.phase);
        p.y += 0.16f * std::cos(1.1f * t + g.phase);
        p.z += g.rise * (0.5f + 0.5f * std::sin(1.6f * t + g.phase));

        float const pulse = 0.45f + 0.55f * std::sin(2.3f * t + g.phase);
        sorted_glows.push_back({k, distance_squared(camera_pos, p), p, 0.16f + 0.42f *pulse});
    }

    std::sort(sorted_glows.begin(), sorted_glows.end(),
              [](transparent_instance const& a, transparent_instance const& b) { return a.distance2 > b.distance2; });

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // Đổi từ ONE_MINUS_SRC_ALPHA thành GL_ONE
    glDepthMask(GL_FALSE);

    for (transparent_instance const& instance : sorted_glows) {
        glow_instance const& g = glows[instance.index];
        uniform_generic_structure glow_uniforms;
        glow_uniforms.uniform_vec3["glow_color"] = {1.0f, 0.78f, 0.28f};
        glow_uniforms.uniform_float["glow_alpha"] = instance.alpha;  // Dùng uniform thay vì material.alpha

        glow_orb.model.translation = instance.position;
        glow_orb.model.rotation = rotation_transform();
        glow_orb.model.scaling = g.radius;
        glow_orb.model.scaling_xyz = {1.0f, 1.0f, 1.0f};
        draw(glow_orb, environment, 1, false, glow_uniforms);  // Thêm uniforms
    }

    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
}

void scene_structure::draw_clouds(float t)
{
	if (!gui.display_clouds || !cloud_instance_buffers_initialized)
		return;

	vec3 const camera_pos = camera_control.camera_model.position();
	update_cloud_instance_buffers(t, camera_pos);
	cloud_billboard.update_supplementary_data_on_gpu(cloud_instance_position_scale, 4, static_cast<int>(clouds.size()));
	cloud_billboard.update_supplementary_data_on_gpu(cloud_instance_rotation_alpha, 5, static_cast<int>(clouds.size()));

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);

	cloud_billboard.model.translation = {0.0f, 0.0f, 0.0f};
	cloud_billboard.model.rotation = rotation_transform();
	cloud_billboard.model.scaling = 1.0f;
	cloud_billboard.model.scaling_xyz = {1.0f, 1.0f, 1.0f};

	uniform_generic_structure cloud_uniforms;
	cloud_uniforms.uniform_int["instancing_mode"] = 2;
	draw(cloud_billboard, environment, static_cast<int>(clouds.size()), true, cloud_uniforms);

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
}

void scene_structure::draw_wind_streaks(float t)
{
	if (!gui.display_wind_streaks || wind_streaks.empty())
		return;

	vec2 const wind_dir = wind_direction_xy();
	float const heading = std::atan2(wind_dir.y, wind_dir.x);
	float const travel = t * wind_streak_speed * (0.15f + gui.wind);
	float const daylight = 0.35f + 0.65f * (1.0f - night_factor);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);

	for (wind_streak_instance const& s : wind_streaks) {
		vec3 pos = s.center + vec3{wind_dir.x, wind_dir.y, 0.0f} * (travel + 7.0f * s.phase);
		pos.x = wrap_coordinate(pos.x, 54.0f);
		pos.y = wrap_coordinate(pos.y, 44.0f);

		float const pulse = 0.5f + 0.5f * std::sin(0.9f * t + s.phase);
		// wind_streak_ribbon.material.alpha = wind_streak_opacity * daylight * (0.35f + 0.65f * pulse);
		wind_streak_ribbon.material.alpha = wind_streak_opacity * daylight * (0.60f + 0.40f * pulse);
		wind_streak_ribbon.model.translation = pos;
		wind_streak_ribbon.model.rotation = rotation_transform::from_axis_angle({0.0f, 0.0f, 1.0f}, heading);
		wind_streak_ribbon.model.scaling = 1.0f;
		wind_streak_ribbon.model.scaling_xyz = {s.length, s.width * s.bend, 1.0f};
		draw(wind_streak_ribbon, environment);
	}

	wind_streak_ribbon.material.alpha = 0.45f;
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
}

void scene_structure::draw_boats(float t)
{
	if (boats.empty())
		return;

	struct boat_draw_state {
		boat_instance const* boat = nullptr;
		vec3 position;
		vec3 tangent;
		float heading = 0.0f;
	};

	std::vector<boat_draw_state> states;
	states.reserve(boats.size());

	for (boat_instance const& b : boats) {
		float const a = b.speed * t + b.phase;
		float const radius = b.radius + b.lateral_offset;
		float const x = radius * std::cos(a);
		float const y = 0.78f * radius * std::sin(a);
		float const z = water_height(x, y, t) + 0.25f * b.scale;

		vec3 tangent = normalize(vec3{
		    -radius * std::sin(a),
		     0.78f * radius * std::cos(a),
		     0.0f
		});
		if (b.speed < 0.0f)
			tangent = -tangent;

		float const heading = std::atan2(tangent.y, tangent.x);
		states.push_back({&b, {x, y, z}, tangent, heading});
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);

	for (boat_draw_state const& state : states) {
		boat_instance const& b = *state.boat;
		vec3 const backward = -state.tangent;
		for (int k = 0; k < 7; ++k) {
			float const dist = b.scale * (1.5f + 1.4f * k);
			vec3 p = state.position + dist * backward;
			p.z = water_height(p.x, p.y, t) + 0.04f;

			float const pulse = 0.5f + 0.5f * std::sin(3.0f * t - 0.8f * k);
			float const sx = b.scale * (0.8f + 0.35f * k) * (0.92f + 0.10f * pulse);
			float const sy = b.scale * (0.10f + 0.07f * k);
			float const alpha = 0.20f * (1.0f - k / 7.0f) * (0.75f + 0.25f * pulse);

			wake_ellipse.material.alpha = alpha;
			wake_ellipse.model.translation = p;
			wake_ellipse.model.rotation = rotation_transform::from_axis_angle({0.0f, 0.0f, 1.0f}, state.heading);
			wake_ellipse.model.scaling = 1.0f;
			wake_ellipse.model.scaling_xyz = {sx, sy, 1.0f};
			draw(wake_ellipse, environment);
		}
	}

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	wake_ellipse.material.alpha = 0.22f;

	for (boat_draw_state const& state : states) {
		boat_instance const& b = *state.boat;
		rotation_transform const R = rotation_transform::from_axis_angle({0.0f, 0.0f, 1.0f}, state.heading);

		boat_hull.model.translation = state.position;
		boat_hull.model.rotation = R;
		boat_hull.model.scaling = b.scale;
		boat_hull.model.scaling_xyz = {1.0f, 1.0f, 1.0f};
		draw(boat_hull, environment);

		boat_cabin.model.translation = state.position;
		boat_cabin.model.rotation = R;
		boat_cabin.model.scaling = b.scale;
		boat_cabin.model.scaling_xyz = {1.0f, 1.0f, 1.0f};
		draw(boat_cabin, environment);
	}
}

void scene_structure::update_character(float dt)
{
	if (!character.enabled)
		return;

	vec3 const forward = {std::cos(character.heading), std::sin(character.heading), 0.0f};
	vec3 new_position = character.position;

	if (inputs.keyboard.is_pressed(GLFW_KEY_W) || inputs.keyboard.up)
		new_position += character.speed * dt * forward;
	if (inputs.keyboard.is_pressed(GLFW_KEY_S) || inputs.keyboard.down)
		new_position -= character.speed * dt * forward;
	if (inputs.keyboard.is_pressed(GLFW_KEY_A) || inputs.keyboard.left)
		character.heading += character.angular_speed * dt;
	if (inputs.keyboard.is_pressed(GLFW_KEY_D) || inputs.keyboard.right)
		character.heading -= character.angular_speed * dt;

	float const candidate_ground_z = terrain_height(new_position.x, new_position.y);
	float const margin = 0.15f;
	if (candidate_ground_z > sea_level + margin) {
		character.position.x = new_position.x;
		character.position.y = new_position.y;
	}

	bool const jump_key_pressed = inputs.keyboard.is_pressed(GLFW_KEY_SPACE);
	if (jump_key_pressed && !character.jump_key_was_pressed && !character.is_jumping) {
		character.is_jumping = true;
		character.vertical_velocity = character.jump_speed;
	}
	character.jump_key_was_pressed = jump_key_pressed;

	float const ground_z = terrain_height(character.position.x, character.position.y) + character.ground_clearance;
	if (character.is_jumping) {
		character.position.z += character.vertical_velocity * dt;
		character.vertical_velocity -= character.gravity * dt;

		if (character.position.z <= ground_z) {
			character.position.z = ground_z;
			character.vertical_velocity = 0.0f;
			character.is_jumping = false;
		}
	}
	else {
		character.position.z = ground_z;
	}
}

void scene_structure::update_third_person_camera()
{
	if (!character.enabled)
		return;

	vec3 const target = character.position + vec3{0.0f, 0.0f, 1.1f};
	vec3 const forward = {std::cos(character.heading), std::sin(character.heading), 0.0f};
	float const horizontal_distance = character.camera_distance * std::cos(character.camera_pitch);
	float const vertical_distance = character.camera_distance * std::sin(character.camera_pitch);
	vec3 const camera_position = target - horizontal_distance * forward + vec3{0.0f, 0.0f, vertical_distance};

	camera_control.look_at(camera_position, target, {0.0f, 0.0f, 1.0f});
}

void scene_structure::display_character()
{
	vec3 const p = character.position;
	float const s = 0.5f;
	rotation_transform const R = rotation_transform::from_axis_angle({0.0f, 0.0f, 1.0f}, character.heading);
	rotation_transform const R_left_arm =
		R * rotation_transform::from_axis_angle({1,0,0}, 0.18f);

	rotation_transform const R_right_arm =
		R * rotation_transform::from_axis_angle({1,0,0}, -0.18f);

	character_body.model.translation = p + s * vec3{0.0f, 0.0f, 0.32f};
	character_body.model.rotation = R;
	character_body.model.scaling = s;
	character_body.model.scaling_xyz = {1.0f, 1.0f, 1.0f};
	draw(character_body, environment);

	character_head.model.translation = p + s * vec3{0.0f, 0.0f, 1.14f};
	character_head.model.rotation = R;
	character_head.model.scaling = s;
	character_head.model.scaling_xyz = {1.0f, 1.0f, 1.0f};
	draw(character_head, environment);

	character_left_arm.model.translation = p + s * R * vec3{0.0f, 0.30f, 0.62f};
	character_left_arm.model.rotation = R_left_arm;
	character_left_arm.model.scaling = s;
	character_left_arm.model.scaling_xyz = {1.0f, 1.0f, 1.0f};
	draw(character_left_arm, environment);

	character_right_arm.model.translation = p + s * R * vec3{0.0f, -0.30f, 0.62f};
	character_right_arm.model.rotation = R_right_arm;
	character_right_arm.model.scaling = s;
	character_right_arm.model.scaling_xyz = {1.0f, 1.0f, 1.0f};
	draw(character_right_arm, environment);

	character_left_leg.model.translation = p + s * R * vec3{0.0f, 0.11f, 0.04f};
	character_left_leg.model.rotation = R;
	character_left_leg.model.scaling = s;
	character_left_leg.model.scaling_xyz = {1.0f, 1.0f, 1.0f};
	draw(character_left_leg, environment);

	character_right_leg.model.translation = p + s * R * vec3{0.0f, -0.11f, 0.04f};
	character_right_leg.model.rotation = R;
	character_right_leg.model.scaling = s;
	character_right_leg.model.scaling_xyz = {1.0f, 1.0f, 1.0f};
	draw(character_right_leg, environment);
}

void scene_structure::trigger_magic_effect()
{
	magic.active = true;
	magic.start_time = timer.t;
	magic.center = character.position;
	magic.center.z = terrain_height(magic.center.x, magic.center.y) + 0.035f;

	spawn_summoned_grass(magic.center, magic.radius);

	if (has_summit_tree && distance_squared(magic.center, summit_tree_position) < magic.radius * magic.radius)
		gui.display_summit_tree_foliage = true;
}

void scene_structure::update_magic_effect(float t)
{
	if (!magic.active)
		return;

	if (t - magic.start_time > magic.duration)
		magic.active = false;
}

void scene_structure::spawn_summoned_grass(vec3 const& center, float radius)
{
	int const N = 48;
	for (int k = 0; k < N; ++k) {
		float const a = rand_uniform(0.0f, 2.0f * Pi);
		float const r = radius * std::sqrt(rand_uniform(0.0f, 1.0f));
		float const x = center.x + r * std::cos(a);
		float const y = center.y + r * std::sin(a);
		float const z = terrain_height(x, y);

		if (z < sea_level + 0.15f)
			continue;

		summoned_grass.push_back({
			{x, y, z + 0.015f},
			rand_uniform(0.45f, 0.85f),
			rand_uniform(0.0f, 2.0f * Pi),
		});
	}

	if (summoned_grass.size() > 640)
		summoned_grass.erase(summoned_grass.begin(), summoned_grass.begin() + (summoned_grass.size() - 640));
}

void scene_structure::display_summoned_grass()
{
	if (summoned_grass.empty())
		return;

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);

	for (grass_instance const& g : summoned_grass) {
		for (int k = 0; k < 2; ++k) {
			float const angle = g.angle + k * Pi / 2.0f;
			grass_billboard.model.translation = g.position;
			grass_billboard.model.rotation = rotation_transform::from_axis_angle({0.0f, 0.0f, 1.0f}, angle);
			grass_billboard.model.scaling = 1.0f;
			grass_billboard.model.scaling_xyz = {0.62f * g.scale, 1.0f, 1.18f * g.scale};
			draw(grass_billboard, environment);
		}
	}

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
}

void scene_structure::display_magic_effect()
{
	if (!magic.active)
		return;

	float const age = timer.t - magic.start_time;
	float const u = saturate(age / magic.duration);
	float const fade = 1.0f - smoothstep(0.55f, 1.0f, u);
	float const pulse = 1.0f + 0.05f * std::sin(16.0f * age);
	vec3 const center = magic.center;
	float const radius = magic.radius * pulse;

	// curve_drawable has no material alpha; keep the symbols stable and fade only mesh effects.
	affine circle_transform;
	circle_transform.translation = center;
	circle_transform.scaling = radius;
	circle_transform.scaling_xyz = {1.0f, 1.0f, 1.0f};
	magic_outer_ring.model = circle_transform;
	draw(magic_outer_ring, environment);

	circle_transform.translation = center + vec3{0.0f, 0.0f, 0.012f};
	magic_inner_ring.model = circle_transform;
	draw(magic_inner_ring, environment);

	circle_transform.translation = center + vec3{0.0f, 0.0f, 0.024f};
	circle_transform.rotation = rotation_transform::from_axis_angle({0.0f, 0.0f, 1.0f}, 0.35f * age);
	magic_triangle_a.model = circle_transform;
	draw(magic_triangle_a, environment);

	circle_transform.translation = center + vec3{0.0f, 0.0f, 0.036f};
	circle_transform.rotation = rotation_transform::from_axis_angle({0.0f, 0.0f, 1.0f}, -0.28f * age);
	magic_triangle_b.model = circle_transform;
	draw(magic_triangle_b, environment);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	glDepthMask(GL_FALSE);

	// Transparent additive column, fading before the curve symbols disappear.
	magic_cylinder.model.translation = center;
	magic_cylinder.model.rotation = rotation_transform();
	magic_cylinder.model.scaling = 1.0f;
	magic_cylinder.model.scaling_xyz = {0.82f * radius, 0.82f * radius, magic.cylinder_height};
	magic_cylinder.material.alpha = 0.16f * fade;
	draw(magic_cylinder, environment);

	for (int k = 0; k < magic.particle_count; ++k) {
		float const seed = 17.17f * float(k);
		float const phase = fract(0.23f * age + fract(0.137f * seed));
		float const a = seed + 1.8f * age;
		float const r = magic.radius * (0.15f + 0.78f * fract(0.311f * seed));
		vec3 const p = center + vec3{
			r * std::cos(a),
			r * std::sin(a),
			0.18f + magic.cylinder_height * phase
		};

		float const particle_fade = fade * std::sin(Pi * phase);
		if (particle_fade <= 0.0f)
			continue;

		magic_particle.model.translation = p;
		magic_particle.model.rotation = rotation_transform();
		magic_particle.model.scaling = 0.035f + 0.055f * fract(0.071f * seed);
		magic_particle.model.scaling_xyz = {1.0f, 1.0f, 1.0f};
		magic_particle.material.alpha = 0.75f * particle_fade;
		draw(magic_particle, environment);
	}

	glDepthMask(GL_TRUE);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_BLEND);
	magic_cylinder.material.alpha = 0.18f;
	magic_particle.material.alpha = 0.75f;
}

void scene_structure::reset_camera_overview()
{
	character.camera_pitch = 0.45f;
	camera_control.look_at({-34.0f, -29.0f, 16.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f});
}

void scene_structure::toggle_third_person_mode()
{
	character.enabled = !character.enabled;
}

void scene_structure::draw_sky_elements(float t)
{
	if (!gui.display_sun_disc && !gui.display_moon_disc)
		return;

	vec3 const camera_pos = camera_control.camera_model.position();

	glEnable(GL_BLEND);
	glDepthMask(GL_FALSE);

	if (gui.display_sun_disc) {
		vec3 const sun_pos = camera_pos + sun_distance * sun_direction;
		float const sun_visibility = saturate(1.0f - 1.35f * night_factor);

		if (sun_visibility >= 0.01f) {
			vec3 const view_dir = normalize(camera_pos - sun_pos);
			rotation_transform const R_billboard = rotation_transform::from_vector_transform({0.0f, 0.0f, 1.0f}, view_dir);
			float const dusk_warmth = saturate(0.35f + 0.65f * dusk_factor);

			for (int k = 0; k < 3; ++k) {
				float const pulse = 0.08f * std::sin(0.7f * t + 0.9f * k);
				float const layer_scale = sun_size * (k == 0 ? 3.7f : (k == 1 ? 2.1f : 1.0f)) + pulse;
				float const layer_alpha = sun_visibility * (k == 0 ? 0.08f : (k == 1 ? 0.18f : 0.95f));

				if (k < 2)
					glBlendFunc(GL_SRC_ALPHA, GL_ONE); // glow/halo
				else
					glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // core

				sun_disc.material.alpha = layer_alpha;
				if (k == 2)
					sun_disc.material.color = mix_color(vec3{1.0f, 0.93f, 0.62f}, vec3{1.0f, 0.98f, 0.86f}, 1.0f - dusk_factor);
				else
					sun_disc.material.color = mix_color(vec3{1.0f, 0.58f, 0.22f}, vec3{1.0f, 0.82f, 0.45f}, dusk_warmth);

				sun_disc.model.translation = sun_pos;
				sun_disc.model.rotation = R_billboard;
				sun_disc.model.scaling = layer_scale;
				sun_disc.model.scaling_xyz = {1.0f, 1.0f, 1.0f};
				draw(sun_disc, environment);
			}
		}
	}

	if (gui.display_moon_disc) {
		vec3 const moon_direction = compute_moon_direction(time_of_day);
		vec3 const moon_pos = camera_pos + moon_distance * moon_direction;
		float const moon_visibility = moon_opacity * saturate((night_factor - 0.25f) / 0.75f);

		if (moon_visibility >= 0.01f) {
			vec3 const view_dir = normalize(camera_pos - moon_pos);
			rotation_transform const R_billboard = rotation_transform::from_vector_transform({0.0f, 0.0f, 1.0f}, view_dir);

			for (int k = 0; k < 3; ++k) {
				float const pulse = 0.04f * std::sin(0.55f * t + 0.6f * k);
				float const layer_scale = moon_size * (k == 0 ? 3.0f : (k == 1 ? 1.75f : 1.0f)) + pulse;
				float const layer_alpha = moon_visibility * (k == 0 ? 0.09f : (k == 1 ? 0.18f : 0.82f));

				if (k < 2)
					glBlendFunc(GL_SRC_ALPHA, GL_ONE);
				else
					glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

				moon_disc.material.alpha = layer_alpha;
				moon_disc.material.color = (k == 2) ? vec3{0.86f, 0.93f, 1.0f} : vec3{0.42f, 0.62f, 1.0f};
				moon_disc.model.translation = moon_pos;
				moon_disc.model.rotation = R_billboard;
				moon_disc.model.scaling = layer_scale;
				moon_disc.model.scaling_xyz = {1.0f, 1.0f, 1.0f};
				draw(moon_disc, environment);
			}
		}
	}

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	sun_disc.material.alpha = 1.0f;
	sun_disc.material.color = {1.0f, 0.88f, 0.52f};
	moon_disc.material.alpha = 1.0f;
	moon_disc.material.color = {0.84f, 0.91f, 1.0f};
}

void scene_structure::initialize()
{
	std::cout << "Start function scene_structure::initialize()" << std::endl;

	camera_control.initialize(inputs, window);
	camera_control.set_rotation_axis_z();
	camera_control.look_at({-34.0f, -29.0f, 16.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f});

	camera_projection = camera_projection_perspective{
	    52.0f * Pi / 180.0f,
	    1.0f,
	    0.01f,
	    1000.0f,
	};

	display_info();
	global_frame.initialize_data_on_gpu(mesh_primitive_frame());
	rand_initialize_generator();

	initialize_terrain();
	initialize_water();
	initialize_skybox();
	initialize_structures();
	initialize_vegetation();
	initialize_summit_tree();
	initialize_fauna();
	initialize_particles();
	initialize_weather_effects();
	initialize_boats();
	initialize_character();
	initialize_magic_effect();

	environment.background_color = ocean_far_tint();
	gui.display_frame = false;

	std::cout << "End function scene_structure::initialize()" << std::endl;
}

void scene_structure::display_frame()
{
	camera_projection.aspect_ratio = window.aspect_ratio();
	environment.camera_projection = camera_projection.matrix();
	environment.camera_view = camera_control.camera_model.matrix_view();

	if (gui.display_frame)
		draw(global_frame, environment);

	timer.update();
	float const t = gui.animate_scene ? timer.t : 0.0f;
	update_day_night_cycle(t);
	update_magic_effect(timer.t);

	uniform_generic_structure water_uniforms;
	water_uniforms.uniform_float["time"] = t;
	water_uniforms.uniform_vec3["camera_position"] = camera_control.camera_model.position();

	if (gui.display_skybox) {
		glDepthMask(GL_FALSE);
		if (gui.use_textured_skybox && skybox_day_texture_loaded && skybox_day_night_shader.id != 0) {
			vec3 const sky_night = {0.04f, 0.07f, 0.14f};
			draw_skybox_day_night(skybox, skybox_night, skybox_day_night_shader, environment,
			                       saturate(night_factor), skybox_night_texture_loaded, sky_night);
		}
		else {
			draw(skybox, environment);
		}
		glDepthMask(GL_TRUE);
	}
	draw_sky_elements(t);
	draw(island, environment);

	draw_structures(t);
	display_summoned_grass();
	display_character();
	display_magic_effect();
	draw_vegetation(t);
	draw_summit_tree();
	draw_fauna(t);
	draw_boats(t);
	draw_clouds(t);
	draw_wind_streaks(t);

	vec3 const camera_pos = camera_control.camera_model.position();
	std::vector<transparent_draw_call> transparent_passes;
	if (gui.display_water)
		transparent_passes.push_back({0, 1.0e12f});
	if (gui.display_vegetation && !glows.empty()) {
		float glow_distance2 = 0.0f;
		for (glow_instance const& g : glows)
			glow_distance2 = std::max(glow_distance2, distance_squared(camera_pos, g.center));
		transparent_passes.push_back({1, glow_distance2});
	}
	if (gui.display_foam && !foams.empty()) {
		float foam_distance2 = 0.0f;
		for (foam_instance const& f : foams)
			foam_distance2 = std::max(foam_distance2, distance_squared(camera_pos, f.anchor));
		transparent_passes.push_back({2, foam_distance2});
	}
	if (gui.display_lighthouse_beam) {
		vec3 const lighthouse_pos = lighthouse_world_position();
		float const lighthouse_angle = t * lighthouse_rotation_speed;
		vec3 const beam_midpoint = lighthouse_pos + vec3{0.0f, 0.0f, 4.85f} +
		                           0.5f * lighthouse_beam_length * normalize(vec3{std::cos(lighthouse_angle), std::sin(lighthouse_angle), -0.08f});
		transparent_passes.push_back({3, distance_squared(camera_pos, beam_midpoint)});
	}

	std::sort(transparent_passes.begin(), transparent_passes.end(),
	          [](transparent_draw_call const& a, transparent_draw_call const& b) { return a.distance2 > b.distance2; });

	beam_visibility_debug = 0.0f;
	for (transparent_draw_call const& pass : transparent_passes) {
		if (pass.pass == 0) {
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDepthMask(GL_FALSE);
			draw(water, environment, 1, true, water_uniforms);
			glDepthMask(GL_TRUE);
			glDisable(GL_BLEND);
		}
		else if (pass.pass == 1) {
			draw_glows(t);
		}
		else if (pass.pass == 2) {
			draw_foam(t);
		}
		else {
			draw_lighthouse_beam_effect(t, lighthouse_world_position());
		}
	}

	if (gui.display_wireframe) {
		draw_wireframe(island, environment, {0.2f, 0.2f, 0.2f});
		draw_wireframe(water, environment, {0.1f, 0.3f, 0.6f}, 1, true, water_uniforms);
	}
}

void scene_structure::display_gui()
{
	ImGui::Checkbox("Frame", &gui.display_frame);
	ImGui::Checkbox("Wireframe", &gui.display_wireframe);
	ImGui::Checkbox("Animate", &gui.animate_scene);
	ImGui::Checkbox("Skybox", &gui.display_skybox);
	ImGui::Checkbox("Textured skybox", &gui.use_textured_skybox);
	ImGui::Checkbox("Moving sun", &gui.moving_sun);
	ImGui::Checkbox("Sun disc", &gui.display_sun_disc);
	ImGui::SliderFloat("Sun distance", &sun_distance, 2.0f, 300.0f);
	ImGui::SliderFloat("Sun size", &sun_size, 1.0f, 20.0f);
	ImGui::Checkbox("Moon disc", &gui.display_moon_disc);
	ImGui::SliderFloat("Moon distance", &moon_distance, 50.0f, 300.0f);
	ImGui::SliderFloat("Moon size", &moon_size, 0.5f, 12.0f);
	ImGui::SliderFloat("Moon opacity", &moon_opacity, 0.0f, 1.0f);
	ImGui::SliderFloat("Wind", &gui.wind, 0.0f, 2.0f);
	ImGui::Checkbox("Clouds", &gui.display_clouds);
	ImGui::SliderFloat("Cloud speed", &cloud_speed, 0.0f, 6.0f);
	ImGui::Checkbox("Wind streaks", &gui.display_wind_streaks);
	ImGui::SliderFloat("Wind streak opacity", &wind_streak_opacity, 0.0f, 1.0f);
	ImGui::SliderFloat("Wind streak speed", &wind_streak_speed, 0.0f, 8.0f);
	ImGui::Checkbox("Water", &gui.display_water);
	ImGui::Checkbox("Vegetation", &gui.display_vegetation);
	ImGui::Checkbox("Summit tree", &gui.display_summit_tree);
	if (gui.display_summit_tree) {
    	ImGui::Checkbox("Summit tree foliage", &gui.display_summit_tree_foliage);
	}	
	ImGui::Checkbox("Fauna", &gui.display_fauna);
	ImGui::Checkbox("Foam", &gui.display_foam);
	ImGui::Checkbox("Lighthouse beam", &gui.display_lighthouse_beam);
	ImGui::SliderFloat("Day-night speed", &day_night_speed, 0.001f, 0.08f);
	ImGui::SliderFloat("Fog day density", &fog_day_density, 0.0f, 0.01f);
	ImGui::SliderFloat("Fog night density", &fog_night_density, 0.002f, 0.05f);
	ImGui::SliderFloat("Beam rotation", &lighthouse_rotation_speed, 0.1f, 4.0f);
	ImGui::SliderFloat("Beam strength", &lighthouse_beam_strength, 0.0f, 1.5f);

	ImGui::Separator();
	ImGui::Text("Tropical Volcanic Island");
	char const* skybox_mode = "off";
	if (gui.display_skybox) {
		if (gui.use_textured_skybox && skybox_day_texture_loaded)
			skybox_mode = skybox_night_texture_loaded ? "textured day/night" : "textured day + night tint";
		else
			skybox_mode = skybox_using_procedural_fallback ? "procedural fallback" : "procedural debug";
	}
	ImGui::Text("Skybox: %s", skybox_mode);
	ImGui::Text("Time of day: %.2f | Night: %.2f | Fog: %.4f", time_of_day, night_factor, environment.fog_density);
	ImGui::Text("Beam visibility: %.3f", beam_visibility_debug);
	ImGui::Text("Palm model: %s", has_palm_model ? "loaded" : "fallback");
	ImGui::Text("Palms: %d  Shrubs: %d", static_cast<int>(palms.size()), static_cast<int>(shrubs.size()));
	ImGui::Text("Birds: %d  Glow particles: %d", static_cast<int>(birds.size()), static_cast<int>(glows.size()));
	ImGui::Text("Foam particles: %d", static_cast<int>(foams.size()));

	ImGui::Separator();
	
	ImGui::Text("Character mode");
	ImGui::BulletText("C: Toggle third-person mode");
	ImGui::BulletText("ESC: Exit third-person mode");
	ImGui::BulletText("R: Reset camera overview");

	ImGui::Text("Movement");
	ImGui::BulletText("W/Up: Move forward");
	ImGui::BulletText("S/Down: Move backward");
	ImGui::BulletText("A/Left: Rotate left");
	ImGui::BulletText("D/Right: Rotate right");
	ImGui::BulletText("Mouse: Camera yaw/pitch");
	ImGui::BulletText("Space: Jump");

	ImGui::Text("Skill");
	ImGui::BulletText("Z: Magic summon circle, particles and grass");
}

void scene_structure::mouse_move_event()
{
	if (character.enabled) {
		if (inputs.mouse.on_gui)
			return;

		vec2 const dp = inputs.mouse.position.current - inputs.mouse.position.previous;
		character.camera_pitch += character.camera_pitch_sensitivity * dp.y;
		character.camera_pitch = std::clamp(character.camera_pitch, character.camera_pitch_min, character.camera_pitch_max);
		character.heading -= character.camera_yaw_sensitivity * dp.x;
		return;
	}

	if (!inputs.keyboard.shift)
		camera_control.action_mouse_move();
}

void scene_structure::mouse_click_event()
{
	if (!character.enabled)
		camera_control.action_mouse_click();
}

void scene_structure::keyboard_event()
{
	if (inputs.keyboard.last_action.is_pressed(GLFW_KEY_C))
		toggle_third_person_mode();

	if (inputs.keyboard.last_action.is_pressed(GLFW_KEY_ESCAPE))
		character.enabled = false;

	if (inputs.keyboard.last_action.is_pressed(GLFW_KEY_R))
		reset_camera_overview();

	if (inputs.keyboard.last_action.is_pressed(GLFW_KEY_Z))
		trigger_magic_effect();

	if (!character.enabled)
		camera_control.action_keyboard();
}

void scene_structure::idle_frame()
{
	if (character.enabled) {
		update_character(inputs.time_interval);
		update_third_person_camera();
	}
	else {
		camera_control.idle_frame();
	}
}

void scene_structure::display_info()
{
	std::cout << "\nCAMERA CONTROL:" << std::endl;
	std::cout << "-----------------------------------------------" << std::endl;
	std::cout << camera_control.doc_usage() << std::endl;
	std::cout << "-----------------------------------------------\n" << std::endl;

	std::cout << "\nSCENE INFO:" << std::endl;
	std::cout << "-----------------------------------------------" << std::endl;
	std::cout << "Original scene: Tropical Volcanic Island with animated ocean, shoreline foam, vegetation, fish schools and seabirds." << std::endl;
	std::cout << "-----------------------------------------------\n" << std::endl;
}
