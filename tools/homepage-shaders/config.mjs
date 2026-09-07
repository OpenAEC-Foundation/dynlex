function scene(id, title, geometry = null) {
  const stagePaths = (stage) => Object.freeze({
    webgpu: `web/shaders/generated/${id}.${stage}.wgsl`,
    webgl: `web/shaders/generated/${id}.${stage}.glsl`
  });
  return Object.freeze({
    id,
    title,
    source: `tools/homepage-shaders/shaders/${id}.dl`,
    fragment: stagePaths("fragment"),
    ...(geometry
      ? {
          vertex: stagePaths("vertex"),
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

function cameraDistance(near, far) {
  return Object.freeze({ near, far });
}

export const shaderConfig = Object.freeze({
  durationSeconds: 11,
  manifest: "web/shaders/manifest.json",
  scenes: Object.freeze([
    scene("event-horizon", "Into the Event Horizon"),
    scene("endless-terrain", "Endless Terrain", {
      generator: "camera-lod-grid",
      referenceWidthPixels: 1440,
      cameraDistance: cameraDistance(0.45, 377.8),
      terrainSampling: lodSampling(200, 896, 68),
      waterSampling: lodSampling(104, 448, 56),
      attributeEncoding: "perspective-radial-ray-grid",
      render: render(false, "opaque", true)
    }),
    scene("nano-choreography", "Nano Choreography", {
      generator: "paired-point-cloud",
      path: "web/shaders/geometry/vitruvian-points.f32",
      metadata: "web/shaders/geometry/vitruvian-points.json",
      attributeEncoding: "paired-unorm12-wheel-corner",
      render: render(true, "additive", false)
    })
  ])
});
