#version 440
// Convex lens: magnifies a circular region around `center`, strongest at the middle.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 center;    // lens center, 0..1 item coordinates
    float radius;   // lens radius as a fraction of item height
    float strength; // 0 = flat, approaching 1 = strong bulge
    float aspect;   // item width / height
};
layout(binding = 1) uniform sampler2D source;

void main()
{
    vec2 d = qt_TexCoord0 - center;
    d.x *= aspect;
    float r = length(d) / radius;
    vec2 uv = qt_TexCoord0;
    if (r < 1.0) {
        float k = 1.0 - strength * (1.0 - r * r);
        vec2 nd = d * k;
        nd.x /= aspect;
        uv = center + nd;
    }
    fragColor = texture(source, uv) * qt_Opacity;
}
