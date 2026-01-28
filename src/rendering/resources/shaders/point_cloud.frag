#version 430 core

in vec3 g_color;
in vec3 g_normal;
in float g_outside_crop;

out vec4 frag_color;

void main() {
    vec3 light_dir = normalize(vec3(0.5, 0.5, 1.0));
    float diff = max(dot(g_normal, light_dir), 0.0);

    vec3 ambient = 0.6 * g_color;
    vec3 diffuse = 0.4 * diff * g_color;
    vec3 result = max(ambient + diffuse, g_color * 0.5);

    if (g_outside_crop > 0.5) {
        float gray = dot(result, vec3(0.299, 0.587, 0.114));
        result = vec3(gray * 0.4);
    }

    frag_color = vec4(result, 1.0);
}
