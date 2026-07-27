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

function lodBand(depthEnd, rows, columns) {
  return Object.freeze({ depthEnd, rows, columns });
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
      terrainBands: Object.freeze([
        lodBand(0.14, 18, 224),
        lodBand(0.27, 16, 192),
        lodBand(0.40, 14, 160),
        lodBand(0.53, 12, 128),
        lodBand(0.65, 10, 104),
        lodBand(0.76, 9, 80),
        lodBand(0.86, 8, 60),
        lodBand(0.94, 7, 45),
        lodBand(1.00, 6, 34)
      ]),
      waterBands: Object.freeze([
        lodBand(0.25, 12, 112),
        lodBand(0.45, 10, 96),
        lodBand(0.62, 8, 80),
        lodBand(0.76, 7, 64),
        lodBand(0.87, 6, 48),
        lodBand(0.95, 5, 36),
        lodBand(1.00, 4, 28)
      ]),
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
