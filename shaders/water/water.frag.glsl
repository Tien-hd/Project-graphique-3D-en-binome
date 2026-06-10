#version 330 core

// Water fragment shader: preserves the standard material, lighting, alpha, and
// fog behavior, with an added Fresnel reflection term for grazing angles.

in struct fragment_data
{
    vec3 position;
    vec3 normal;
    vec3 color;
    vec2 uv;
    float alpha;
} fragment;

layout(location=0) out vec4 FragColor;

uniform sampler2D image_texture;

uniform vec3 camera_position;
uniform vec3 light;
uniform float light_intensity;
uniform vec3 ambient_tint;
uniform float ambient_intensity;

uniform bool use_fog;
uniform float fog_density;
uniform vec3 fog_color;

struct phong_structure {
	float ambient;
	float diffuse;
	float specular;
	float specular_exponent;
};

struct texture_settings_structure {
	bool use_texture;
	bool texture_inverse_v;
	bool two_sided;
};

struct material_structure
{
	vec3 color;
	float alpha;

	phong_structure phong;
	texture_settings_structure texture_settings;
};

uniform material_structure material;

void main()
{
	vec3 N = normalize(fragment.normal);
	if (material.texture_settings.two_sided && gl_FrontFacing == false) {
		N = -N;
	}

	vec3 L = normalize(light - fragment.position);
	float diffuse_component = max(dot(N, L), 0.0);

	vec3 V = normalize(camera_position - fragment.position);
	float specular_component = 0.0;
	if (diffuse_component > 0.0) {
		vec3 R = reflect(-L, N);
		specular_component = pow(max(dot(R, V), 0.0), material.phong.specular_exponent);
	}

	vec2 uv_image = vec2(fragment.uv.x, fragment.uv.y);
	if (material.texture_settings.texture_inverse_v) {
		uv_image.y = 1.0 - uv_image.y;
	}

	vec4 color_image_texture = texture(image_texture, uv_image);
	if (material.texture_settings.use_texture == false) {
		color_image_texture = vec4(1.0, 1.0, 1.0, 1.0);
	}

	vec3 color_object = fragment.color * material.color * color_image_texture.rgb;

	float Ka = material.phong.ambient;
	float Kd = material.phong.diffuse;
	float Ks = material.phong.specular;
	vec3 color_ambient = Ka * ambient_intensity * ambient_tint * color_object;
	vec3 color_diffuse_specular = light_intensity * (Kd * diffuse_component * color_object + Ks * specular_component * vec3(1.0));
	vec3 color_shading = color_ambient + color_diffuse_specular;

	// Fresnel reflection term: shallow/grazing view angles become brighter and
	// more sky-reflective, while top-down views keep the base water color.
	float F0 = 0.02;
	float fresnel = F0 + (1.0 - F0) * pow(1.0 - max(dot(N, V), 0.0), 5.0);
	vec3 fresnel_color = vec3(0.65, 0.85, 1.0);
	color_shading = mix(color_shading, fresnel_color, 0.35 * fresnel);

	if (use_fog) {
		float dist = length(camera_position - fragment.position);
		float fog = 1.0 - exp(-fog_density * dist);
		fog = clamp(fog, 0.0, 1.0);
		color_shading = mix(color_shading, fog_color, fog);
	}

	FragColor = vec4(color_shading, material.alpha * color_image_texture.a * fragment.alpha);
}
