#version 440
// Album-art backdrop: a soft blur of a tiny cover, desaturated and darkened,
// fading to fully transparent in an ellipse that touches every edge.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 center;      // vignette centre, 0..1
    float brightness; // multiplier, < 1 darkens
    float saturation; // 0 = grey, 1 = original
};
layout(binding = 1) uniform sampler2D source;

void main()
{
    vec2 uv = qt_TexCoord0;
    vec3 c = vec3(0.0);
    for (int i = -2; i <= 2; ++i)
        for (int j = -2; j <= 2; ++j)
            c += texture(source, uv + vec2(float(i), float(j)) * 0.03).rgb;
    c /= 25.0;
    float luma = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(luma), c, saturation) * brightness;
    vec2 d = (uv - center) * 2.0;
    float a = smoothstep(1.0, 0.2, length(d));
    fragColor = vec4(c * a, a) * qt_Opacity;
}
