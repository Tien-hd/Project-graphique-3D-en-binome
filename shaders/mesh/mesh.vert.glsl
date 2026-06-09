#version 330 core

// Vertex shader - this code is executed for every vertex of the shape

// Inputs coming from VBOs
layout (location = 0) in vec3 vertex_position; // vertex position in local space (x,y,z)
layout (location = 1) in vec3 vertex_normal;   // vertex normal in local space   (nx,ny,nz)
layout (location = 2) in vec3 vertex_color;    // vertex color      (r,g,b)
layout (location = 3) in vec2 vertex_uv;       // vertex uv-texture (u,v)
layout (location = 4) in vec4 instance_position_scale; // xyz: world position, w: uniform scale
layout (location = 5) in vec4 instance_rotation;       // x: yaw around z, y: sway around y, or y: alpha for foam

// Output variables sent to the fragment shader
out struct fragment_data
{
    vec3 position; // vertex position in world space
    vec3 normal;   // normal position in world space
    vec3 color;    // vertex color
    vec2 uv;       // vertex uv
    float alpha;   // per-instance alpha multiplier
} fragment;

// Uniform variables expected to receive from the C++ program
uniform mat4 model; // Model affine transform matrix associated to the current shape
uniform mat4 view;  // View matrix (rigid transform) of the camera
uniform mat4 projection; // Projection (perspective or orthogonal) matrix of the camera
uniform int use_instancing;
uniform int instancing_mode; // 0: regular instanced mesh, 1: shrub billboard, 2: foam billboard

vec3 rotate_y_then_z(vec3 p, float yaw, float sway)
{
	float cy = cos(sway);
	float sy = sin(sway);
	vec3 q = vec3(cy * p.x + sy * p.z, p.y, -sy * p.x + cy * p.z);

	float cz = cos(yaw);
	float sz = sin(yaw);
	return vec3(cz * q.x - sz * q.y, sz * q.x + cz * q.y, q.z);
}

vec3 rotate_z(vec3 p, float angle)
{
	float c = cos(angle);
	float s = sin(angle);
	return vec3(c * p.x - s * p.y, s * p.x + c * p.y, p.z);
}

vec3 camera_world_position()
{
	mat3 camera_orientation = transpose(mat3(view));
	vec3 camera_view_origin = vec3(view * vec4(0.0, 0.0, 0.0, 1.0));
	return -camera_orientation * camera_view_origin;
}


void main()
{
	mat4 modelNormal = transpose(inverse(model));

	// The position of the vertex in the world space
	vec4 position;
	vec4 normal;
	float alpha = 1.0;
	if (use_instancing == 1) {
		if (instancing_mode == 1 || instancing_mode == 2) {
			vec3 camera_position = camera_world_position();
			vec2 to_camera = camera_position.xy - instance_position_scale.xy;
			float facing = atan(to_camera.y, to_camera.x) + 1.57079632679 + instance_rotation.x;
			vec3 rotated_position = rotate_z(vertex_position, facing);
			vec3 rotated_normal = rotate_z(vertex_normal, facing);
			position = model * vec4(instance_position_scale.xyz + instance_position_scale.w * rotated_position, 1.0);
			normal = modelNormal * vec4(rotated_normal, 0.0);
			if (instancing_mode == 2) {
				alpha = instance_rotation.y;
			}
		}
		else {
			vec3 rotated_position = rotate_y_then_z(vertex_position, instance_rotation.x, instance_rotation.y);
			vec3 rotated_normal = rotate_y_then_z(vertex_normal, instance_rotation.x, instance_rotation.y);
			position = model * vec4(instance_position_scale.xyz + instance_position_scale.w * rotated_position, 1.0);
			normal = modelNormal * vec4(rotated_normal, 0.0);
		}
	}
	else {
		position = model * vec4(vertex_position, 1.0);

		// The normal of the vertex in the world space
		normal = modelNormal * vec4(vertex_normal, 0.0);
	}

	// The projected position of the vertex in the normalized device coordinates:
	vec4 position_projected = projection * view * position;

	// Fill the parameters sent to the fragment shader
	fragment.position = position.xyz;
	fragment.normal   = normal.xyz;
	fragment.color = vertex_color;
	fragment.uv = vertex_uv;
	fragment.alpha = alpha;

	// gl_Position is a built-in variable which is the expected output of the vertex shader
	gl_Position = position_projected; // gl_Position is the projected vertex position (in normalized device coordinates)
}
