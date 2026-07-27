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

function render(backgroundPass, blendMode, depthTest) {
  return Object.freeze({ backgroundPass, blendMode, depthTest });
}

function lodSampling(rows, nearColumns, farColumns) {
  return Object.freeze({ rows, nearColumns, farColumns });
}

export const shaderConfig = Object.freeze({
  durationSeconds: 11,
  manifest: "web/shaders/manifest.json",
  scenes: Object.freeze([
    scene("event-horizon", "Into the Event Horizon"),
    scene("endless-terrain", "Endless Terrain", {
      generator: "camera-lod-grid",
      path: "web/shaders/geometry/terrain-grid.f32",
      indexPath: "web/shaders/geometry/terrain-grid.u16",
      terrainSampling: lodSampling(100, 448, 34),
      waterSampling: lodSampling(52, 224, 28),
      attributeEncoding: "terrain-lod-grid",
      render: render(false, "opaque", true)
    }),
    scene("nano-choreography", "Nano Choreography", {
      generator: "paired-point-cloud",
      path: "web/shaders/geometry/vitruvian-points.f32",
      metadata: "web/shaders/geometry/vitruvian-points.json",
      attributeEncoding: "paired-unorm12",
      render: render(true, "additive", false)
    })
  ])
});
