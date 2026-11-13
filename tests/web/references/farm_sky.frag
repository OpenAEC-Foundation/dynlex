#version 300 es
// Reference from the original Farm createSkyReflection fragment shader.
// Only the GLSL stage interface differs: an input uniform and an ES 3.00 output.
precision highp float;
uniform vec3 skyDirection;
out vec4 result;
void main() {
    vec3 direction = normalize(skyDirection);
    vec3 color = mix(vec3(.28, .38, .25), vec3(.56, .76, .90), smoothstep(-.1, .7, direction.y));
    color = mix(color, vec3(.95, .90, .72), pow(1.0 - abs(direction.y), 6.0) * .5);
    float sun = smoothstep(.985, .999, dot(direction, normalize(vec3(-.5, .9, .2))));
    color += sun * vec3(5.0, 4.3, 2.9);
    result = vec4(color, 1.0);
}
