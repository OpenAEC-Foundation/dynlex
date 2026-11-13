#version 300 es
// Coat and water formulas from the original Farm material shader patches.
// The caller supplies the water point after its model/instance transformation.
precision highp float;
uniform vec3 point, base, normal;
uniform float time;
uniform int mode;
out vec4 result;
void main() {
    vec3 color;
    if (mode == 0) {
        vec3 p = normalize(point);
        vec2 leftPatch = vec2((p.y - .13) / .56, (p.z + .28) / .58);
        vec2 rightPatch = vec2((p.y + .04) / .49, (p.z - .19) / .52);
        vec2 backPatch = vec2((p.x + .3) / .54, (p.z + .47) / .55);
        float leftInk = step(p.x, -.45) * (1.0 - smoothstep(.90, 1.02, dot(leftPatch, leftPatch) + .13 * sin(p.y * 18.0 + p.z * 8.0)));
        float rightInk = step(.50, p.x) * (1.0 - smoothstep(.90, 1.02, dot(rightPatch, rightPatch) + .12 * sin(p.z * 19.0)));
        float backInk = step(.45, p.y) * (1.0 - smoothstep(.90, 1.02, dot(backPatch, backPatch)));
        color = mix(base, vec3(.021, .020, .016), max(leftInk, max(rightInk, backInk)));
    } else if (mode == 1) {
        color = normal;
        color.x += .10 * sin(point.x * 21.0 + time);
        color.z += .07 * cos(point.z * 18.0 - time);
    } else {
        color = mix(base, normal, smoothstep(55.0, 100.0, point.x));
    }
    result = vec4(color, 1.0);
}
