#version 430 core

layout(location = 0) in vec3 a_vertex_position;
layout(location = 1) in vec3 a_instance_position;
layout(location = 2) in vec3 a_instance_color;
layout(location = 3) in float a_transform_index;

uniform mat4 u_view;
uniform mat4 u_projection;
uniform float u_voxel_size;
uniform bool u_equirectangular;

#define MAX_TRANSFORMS 64
uniform mat4 u_model_transforms[MAX_TRANSFORMS];
uniform int u_num_transforms;

uniform bool u_crop_enabled;
uniform mat4 u_crop_transform;
uniform vec3 u_crop_min;
uniform vec3 u_crop_max;
uniform bool u_crop_inverse;
uniform bool u_crop_desaturate;

#define PI 3.14159265359

out vec3 v_color;
out vec3 v_normal;
out float v_ndc_x;
out int v_equirectangular;
out float v_outside_crop;

void main() {
    mat4 model = mat4(1.0);
    int idx = int(a_transform_index);
    if (u_num_transforms > 0 && idx >= 0 && idx < u_num_transforms) {
        model = u_model_transforms[idx];
    }

    vec3 world_position = (model * vec4(a_instance_position, 1.0)).xyz;

    v_outside_crop = 0.0;
    if (u_crop_enabled && u_crop_desaturate) {
        vec3 local_pos = (u_crop_transform * vec4(world_position, 1.0)).xyz;
        bool inside = local_pos.x >= u_crop_min.x && local_pos.x <= u_crop_max.x &&
                      local_pos.y >= u_crop_min.y && local_pos.y <= u_crop_max.y &&
                      local_pos.z >= u_crop_min.z && local_pos.z <= u_crop_max.z;
        bool outside = u_crop_inverse ? inside : !inside;
        v_outside_crop = outside ? 1.0 : 0.0;
    }

    world_position += a_vertex_position * u_voxel_size;

    vec4 view_pos = u_view * vec4(world_position, 1.0);

    v_equirectangular = u_equirectangular ? 1 : 0;
    v_ndc_x = 0.0;

    if (u_equirectangular) {
        vec3 dir = normalize(view_pos.xyz);
        float theta = atan(dir.x, -dir.z);
        float phi = asin(clamp(dir.y, -1.0, 1.0));
        float depth = length(view_pos.xyz);
        v_ndc_x = theta / PI;
        gl_Position = vec4(v_ndc_x, -phi / (PI * 0.5), -1.0 / depth, 1.0);
    } else {
        gl_Position = u_projection * view_pos;
    }

    v_color = a_instance_color;
    v_normal = normalize(mat3(model) * normalize(a_vertex_position));
}
