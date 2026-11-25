#version 460
layout(binding = 0) uniform sampler2D sourceTexture;
layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 tint;
layout(location = 0) out vec4 color;

void main()
{
    color = texture(sourceTexture, uv) * tint;
}
