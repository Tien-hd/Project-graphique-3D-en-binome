#pragma once

#include "cgp/cgp.hpp"
#include "environment.hpp"

#include <vector>

using cgp::mesh;
using cgp::mesh_drawable;

struct gui_parameters {
	bool display_frame = false;
	bool display_wireframe = false;
	bool animate_scene = true;
	bool display_skybox = true;
	bool display_water = true;
	bool display_vegetation = true;
	bool display_fauna = false;
	bool display_foam = false;
	bool display_lighthouse_beam = true;
	bool moving_sun = true;
	bool display_sun_disc = true;
	bool display_moon_disc = true;
	bool display_clouds = true;
	bool display_wind_streaks = true;
	bool use_textured_skybox = true;
	float wind = 1.0f;
};

struct palm_instance {
	cgp::vec3 root;
	float scale = 1.0f;
	float yaw = 0.0f;
	float sway_phase = 0.0f;
};

struct shrub_instance {
	cgp::vec3 root;
	float scale = 1.0f;
	float yaw = 0.0f;
};

struct bird_instance {
	cgp::vec3 center;
	float orbit_radius = 3.0f;
	float speed = 0.7f;
	float phase = 0.0f;
	float altitude = 4.5f;
	float scale = 0.25f;
};

struct foam_instance {
	cgp::vec3 anchor;
	float phase = 0.0f;
	float scale = 1.0f;
};

struct glow_instance {
	cgp::vec3 center;
	float radius = 0.04f;
	float rise = 0.25f;
	float scale = 1.0f;
	float phase = 0.0f;
};

struct cloud_instance {
	cgp::vec3 center;
	float scale = 1.0f;
	float phase = 0.0f;
	float spin = 0.0f;
};

struct wind_streak_instance {
	cgp::vec3 center;
	float length = 1.0f;
	float width = 1.0f;
	float phase = 0.0f;
	float bend = 1.0f;
};

struct scene_structure : cgp::scene_inputs_generic {
	void initialize();
	void display_frame();
	void display_gui();

	environment_structure environment;
	window_structure window;
	input_devices inputs;
	gui_parameters gui;

	void display_info();

	camera_controller_orbit_euler camera_control;
	camera_projection_perspective camera_projection;

	mesh_drawable global_frame;
	cgp::skybox_drawable skybox{};
	cgp::skybox_drawable skybox_night{};
	cgp::opengl_shader_structure skybox_day_night_shader;
	cgp::timer_basic timer;

	float sea_level = 0.0f;
	float island_radius = 18.0f;
	float time_of_day = 0.0f;      // Day-night cycle in [0,1]
	float day_night_speed = 0.02f; // Cycle speed
	float fog_day_density = 0.0012f;
	float fog_night_density = 0.016f;
	float lighthouse_rotation_speed = 0.85f;
	float lighthouse_beam_strength = 0.82f;
	float lighthouse_beam_length = 16.0f;
	float lighthouse_beam_radius_near = 0.22f;
	float lighthouse_beam_radius_far = 2.20f;
	float beam_visibility_debug = 0.0f;
	float day_factor = 1.0f;
	float dusk_factor = 0.0f;
	float night_factor = 0.0f;
	float sun_distance = 160.0f;
	float sun_size = 2.3f;
	cgp::vec3 sun_direction = {0.52f, -0.28f, 0.81f};
	float moon_distance = 165.0f;
	float moon_size = 2.0f;
	float moon_opacity = 0.78f;
	float cloud_speed = 1.8f;
	float wind_streak_opacity = 0.22f;
	float wind_streak_speed = 3.0f;
	bool skybox_day_texture_loaded = false;
	bool skybox_night_texture_loaded = false;
	bool skybox_using_procedural_fallback = true;

	mesh island_cpu;
	mesh water_cpu;

	mesh_drawable island;
	mesh_drawable water;
	mesh_drawable beach_strip;
	cgp::opengl_shader_structure water_shader;

	mesh_drawable lighthouse_tower;
	mesh_drawable lighthouse_roof;
	mesh_drawable lighthouse_ring;
	mesh_drawable lighthouse_window;
	mesh_drawable lighthouse_beam;
	mesh_drawable lighthouse_bulb;
	cgp::opengl_shader_structure lighthouse_beam_shader;

	mesh_drawable dock_plank;
	mesh_drawable dock_pile;

	mesh_drawable palm_tree;
	mesh_drawable palm_fallback_trunk;
	mesh_drawable palm_fallback_leaf;
	bool has_palm_model = false;
	bool palm_instance_buffers_initialized = false;
	bool shrub_instance_buffers_initialized = false;
	bool foam_instance_buffers_initialized = false;
	bool bird_instance_buffers_initialized = false;
	bool cloud_instance_buffers_initialized = false;

	mesh_drawable shrub_billboard;
	mesh_drawable foam_billboard;
	mesh_drawable cloud_billboard;
	mesh_drawable wind_streak_ribbon;

	mesh_drawable bird_body;
	mesh_drawable bird_wing;

	mesh_drawable glow_orb;
	mesh_drawable sun_disc;
	mesh_drawable moon_disc;

	std::vector<palm_instance> palms;
	cgp::numarray<cgp::vec4> palm_instance_position_scale;
	cgp::numarray<cgp::vec4> palm_instance_rotation;
	std::vector<shrub_instance> shrubs;
	cgp::numarray<cgp::vec4> shrub_instance_position_scale;
	cgp::numarray<cgp::vec4> shrub_instance_rotation;
	std::vector<bird_instance> birds;
	std::vector<foam_instance> foams;
	cgp::numarray<cgp::vec4> foam_instance_position_scale;
	cgp::numarray<cgp::vec4> foam_instance_rotation_alpha;
	std::vector<glow_instance> glows;
	std::vector<cloud_instance> clouds;
	cgp::numarray<cgp::vec4> cloud_instance_position_scale;
	cgp::numarray<cgp::vec4> cloud_instance_rotation_alpha;
	std::vector<wind_streak_instance> wind_streaks;
	cgp::numarray<cgp::vec4> bird_body_position_scale;
	cgp::numarray<cgp::vec4> bird_body_rotation;

	cgp::numarray<cgp::vec4> bird_left_wing_position_scale;
	cgp::numarray<cgp::vec4> bird_left_wing_rotation;

	cgp::numarray<cgp::vec4> bird_right_wing_position_scale;
	cgp::numarray<cgp::vec4> bird_right_wing_rotation;

	float terrain_height(float x, float y) const;
	float water_height(float x, float y, float t) const;

	void initialize_terrain();
	void initialize_water();
	void initialize_skybox();
	void initialize_structures();
	void initialize_vegetation();
	void initialize_fauna();
	void initialize_particles();
	void initialize_weather_effects();
	void initialize_palm_instance_buffers();
	void update_palm_instance_rotation_buffer(float t);
	void initialize_shrub_instance_buffers();
	void initialize_foam_instance_buffers();
	void update_foam_instance_buffers(float t, cgp::vec3 const& camera_pos);
	void initialize_cloud_instance_buffers();
	void update_cloud_instance_buffers(float t, cgp::vec3 const& camera_pos);
	cgp::vec3 compute_sun_direction(float time_of_day);
	cgp::vec3 compute_moon_direction(float time_of_day);
	void update_day_night_cycle(float t);
	void initialize_bird_instance_buffers();
	void update_bird_instance_buffers(float t);

	void draw_structures(float t);
	void draw_vegetation(float t);
	void draw_fauna(float t);
	void draw_foam(float t);
	void draw_glows(float t);
	void draw_clouds(float t);
	void draw_wind_streaks(float t);
	void draw_lighthouse_beam_effect(float t, cgp::vec3 const& lighthouse_pos);
	cgp::vec3 lighthouse_world_position() const;
	void draw_sky_elements(float t);

	void mouse_move_event();
	void mouse_click_event();
	void keyboard_event();
	void idle_frame();
};
