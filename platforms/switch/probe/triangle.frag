// SPDX-License-Identifier: CC0-1.0
#version 460
layout(location = 0) in vec3 color;
layout(location = 0) out vec4 outputColor;
void main() {
    outputColor = vec4(color, 1.0);
}
