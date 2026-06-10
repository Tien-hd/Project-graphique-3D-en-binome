#version 330 core

// Vertex shader - this code is executed for every vertex of the shape

// Inputs coming from VBOs
layout (location = 0) in vec3 vertex_position; // vertex position in local space (x,y,z)
layout (location = 1) in vec3 vertex_normal;   // vertex normal in local space   (nx,ny,nz)
layout (location = 2) in vec3 vertex_color;    // vertex color      (r,g,b)
layout (location = 3) in vec2 vertex_uv;       // vertex uv-texture (u,v)
layout (location = 4) in vec4 instance_position_scale; // xyz: world position, w: uniform scale
layout (location = 5) in vec4 instance_rotation;       // x: yaw around z, y: sway around y, or y: alpha for foam, z: rote around x, w: pivot_offset for rotate

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

vec3 rotation_y(vec3 p, float a)
{
    float c = cos(a);
    float s = sin(a);
    return vec3(c*p.x + s*p.z, p.y, -s*p.x + c*p.z);
}

vec3 rotation_z(vec3 p, float a)
{
    float c = cos(a);
    float s = sin(a);
    return vec3(c*p.x - s*p.y, s*p.x + c*p.y, p.z);
}

vec3 rotate_y_then_z(vec3 p, float yaw, float sway, float pitch)
{	
    float cx = cos(pitch);
    float sx = sin(pitch);
    vec3 r = vec3(p.x, cx * p.y - sx * p.z, sx * p.y + cx * p.z);
    
    float cy = cos(sway);
    float sy = sin(sway);
    vec3 q = vec3(cy * r.x + sy * r.z, r.y, -sy * r.x + cy * r.z);

    float cz = cos(yaw);
    float sz = sin(yaw);
    return vec3(cz * q.x - sz * q.y, sz * q.x + cz * q.y, q.z);
}

mat3 camera_world_orientation()
{
	return transpose(mat3(view));
}

void main()
{
	mat4 modelNormal = transpose(inverse(model));
	mat3 modelNormal3 = mat3(modelNormal);

	// The position of the vertex in the world space
	vec4 position;
	vec4 normal;
	float alpha = 1.0;
	if (use_instancing == 1) {
		if (instancing_mode == 1 || instancing_mode == 2) {
			mat3 camera_orientation = camera_world_orientation();
			vec3 camera_right = normalize(camera_orientation * vec3(1.0, 0.0, 0.0));
			vec3 camera_up = normalize(camera_orientation * vec3(0.0, 1.0, 0.0));

			float spin = instance_rotation.x;
			float c = cos(spin);
			float s = sin(spin);
			vec2 billboard_coord = vec2(
				c * vertex_position.x - s * vertex_position.z,
				s * vertex_position.x + c * vertex_position.z
			);

			vec3 billboard_position = instance_position_scale.xyz +
				instance_position_scale.w * (billboard_coord.x * camera_right + billboard_coord.y * camera_up);
			vec3 billboard_normal = normalize(cross(camera_right, camera_up));
			position = model * vec4(billboard_position, 1.0);
			normal = vec4(normalize(modelNormal3 * billboard_normal), 0.0);
			if (instancing_mode == 2) {
				alpha = instance_rotation.y;
			}
		}

		else if (instancing_mode == 3)
		{
			float heading = instance_rotation.x;
			float flap    = instance_rotation.y;
			float pivot_x = instance_rotation.w;

			vec3 pivot = vec3(pivot_x, 0.0, 0.0);

			vec3 p = vertex_position;

			p = p - pivot;
			p = rotation_y(p, flap);
			p = p + pivot;

			p = rotation_z(p, heading);

			position = model * vec4(
				instance_position_scale.xyz + instance_position_scale.w * p,
				1.0
			);

			vec3 n = vertex_normal;
			n = rotation_y(n, flap);
			n = rotation_z(n, heading);

			normal = modelNormal * vec4(n, 0.0);
		}

		else {
			float pivot_offset = instance_rotation.w;
			vec3 pivot = vec3(0.0, 0.0, pivot_offset);
			vec3 to_pivot = vertex_position - pivot;
			vec3 rotated_position = rotate_y_then_z(to_pivot, instance_rotation.x, instance_rotation.y, instance_rotation.z);
			vec3 final_position = rotated_position + pivot;
			
			vec3 rotated_normal = rotate_y_then_z(vertex_normal, instance_rotation.x, instance_rotation.y, instance_rotation.z);
			
			position = model * vec4(instance_position_scale.xyz + instance_position_scale.w * final_position, 1.0);
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
