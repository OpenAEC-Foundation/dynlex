import * as THREE from "./vendor/three/three.module.min.js";

// A prefiltered sky reflection for the lake, generated once without image assets.
export function createSkyReflection(renderer) {
  const sky = new THREE.Scene();
  const geometry = new THREE.SphereGeometry(30, 24, 12);
  const material = new THREE.ShaderMaterial({
    side: THREE.BackSide,
    vertexShader: `varying vec3 skyDirection;
      void main() {
        skyDirection = position;
        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
      }`,
    fragmentShader: `varying vec3 skyDirection;
      void main() {
        vec3 direction = normalize(skyDirection);
        vec3 color = mix(vec3(.28, .38, .25), vec3(.56, .76, .90), smoothstep(-.1, .7, direction.y));
        color = mix(color, vec3(.95, .90, .72), pow(1.0 - abs(direction.y), 6.0) * .5);
        float sun = smoothstep(.985, .999, dot(direction, normalize(vec3(-.5, .9, .2))));
        color += sun * vec3(5.0, 4.3, 2.9);
        gl_FragColor = vec4(color, 1.0);
      }`
  });
  sky.add(new THREE.Mesh(geometry, material));
  const generator = new THREE.PMREMGenerator(renderer);
  const reflection = generator.fromScene(sky, 0, .1, 100);
  generator.dispose();
  geometry.dispose();
  material.dispose();
  return reflection;
}
