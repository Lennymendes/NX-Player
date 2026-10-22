// Aura Neon — fond animé (GLSL, builds OpenGL). Uniforms fournis par NX Player :
//   u_time (s), u_resolution (px), u_accent, u_accent2 (vec4), u_level (0..1, niveau audio lissé)
// JUCE fournit pixelPos (coordonnées écran) et pixelAlpha.
uniform float u_time;
uniform vec2 u_resolution;
uniform vec4 u_accent;
uniform vec4 u_accent2;
uniform float u_level;

void main()
{
    vec2 uv = pixelPos / max (u_resolution, vec2 (1.0));
    float t = u_time * 0.12;
    float w1 = sin (uv.x * 6.0 + t * 3.0 + sin (uv.y * 3.0 + t)) * 0.5 + 0.5;
    float w2 = sin (uv.y * 5.0 - t * 2.0 + cos (uv.x * 4.0 - t)) * 0.5 + 0.5;
    float glow = smoothstep (0.0, 1.0, w1 * w2) * (0.35 + 0.35 * u_level);
    vec3 base = vec3 (0.071, 0.075, 0.102);
    vec3 col = mix (base, u_accent.rgb, glow * 0.55 * (1.0 - uv.y));
    col = mix (col, u_accent2.rgb, glow * 0.25 * uv.x);
    float vignette = smoothstep (1.25, 0.2, length (uv - vec2 (0.5)));
    gl_FragColor = pixelAlpha * vec4 (col * vignette, 1.0);
}
