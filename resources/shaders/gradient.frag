#version 440
// Fills text with a vertical two-colour gradient, using the text's own alpha.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 topColor;
    vec4 bottomColor;
};
layout(binding = 1) uniform sampler2D source;

void main()
{
    float alpha = texture(source, qt_TexCoord0).a;
    vec4 c = mix(topColor, bottomColor, qt_TexCoord0.y);
    fragColor = vec4(c.rgb * c.a, c.a) * alpha * qt_Opacity;
}
