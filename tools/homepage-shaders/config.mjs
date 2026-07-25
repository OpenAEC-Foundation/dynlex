function scene(id, title, geometry = null) {
  return Object.freeze({
    id,
    title,
    source: `tools/homepage-shaders/shaders/${id}.dl`,
    fragment: `web/shaders/generated/${id}.fragment.glsl`,
    ...(geometry
      ? {
          vertex: `web/shaders/generated/${id}.vertex.glsl`,
          geometry: Object.freeze(geometry)
        }
      : {})
  });
}

export const shaderConfig = Object.freeze({
  durationSeconds: 11,
  manifest: "web/shaders/manifest.json",
  scenes: Object.freeze([
    scene("event-horizon", "Into the Event Horizon"),
    scene("endless-terrain", "Endless Terrain"),
    scene("nano-choreography", "Nano Choreography", {
      path: "web/shaders/geometry/vitruvian-points.f32",
      metadata: "web/shaders/geometry/vitruvian-points.json"
    })
  ])
});
