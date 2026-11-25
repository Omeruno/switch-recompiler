#version 460
layout(binding = 0) uniform sampler2D decodedTexture;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

void main()
{
    vec4 sampleColor = texture(decodedTexture, uv);
    color = vec4(mix(vec3(0.16), sampleColor.rgb, sampleColor.a), 1.0);
}
