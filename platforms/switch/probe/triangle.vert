// SPDX-License-Identifier: CC0-1.0
// Authored synthetic data only. No game shaders or caches.
#version 460
layout(location = 0) out vec3 color;
void main() {
    const vec2 positions[3] = vec2[](vec2(-0.75, -0.6), vec2(0.75, -0.6), vec2(0.0, 0.75));
    const vec3 colors[3] = vec3[](vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1));
    gl_Position = vec4(positions[gl_VertexID], 0.5, 1.0);
    color = colors[gl_VertexID];
}
