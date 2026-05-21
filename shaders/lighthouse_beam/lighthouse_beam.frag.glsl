#version 330 core

in vec3 local_pos;
layout(location = 0) out vec4 FragColor;

uniform vec3 beam_color;
uniform float beam_alpha;
uniform float beam_length;
uniform float beam_radius_near;
uniform float beam_radius_far;

void main()
{
	float along = clamp(local_pos.x / beam_length, 0.0, 1.0);

	float current_radius = mix(beam_radius_near, beam_radius_far, along);
	float radial_dist = length(local_pos.yz);
	float radial_ratio = radial_dist / max(current_radius, 1e-5);

	// This beam mesh is a shell (frustum surface), not a filled volume.
	// Keep a non-zero baseline alpha on the shell, and softly reduce near the outer rim.
	float shell_core = 1.0 - smoothstep(0.75, 1.10, radial_ratio);
	float edge_fade = 0.28 + 0.72 * shell_core;

	// Fade along distance: brighter near the lighthouse, weaker at the far end.
	float distance_fade = 1.0 - smoothstep(0.58, 1.0, along);

	float alpha = beam_alpha * edge_fade * distance_fade;
	if (alpha < 0.002)
		discard;

	FragColor = vec4(beam_color, alpha);
}
