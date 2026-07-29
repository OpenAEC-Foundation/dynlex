const TERRAIN_GENERATOR = "camera-lod-grid";
const geometryCache = new WeakMap();

function validateSampling(sampling, name) {
  if (
    typeof sampling !== "object"
    || sampling === null
    || !Number.isInteger(sampling.rows)
    || sampling.rows <= 0
    || !Number.isInteger(sampling.nearColumns)
    || !Number.isInteger(sampling.farColumns)
    || sampling.farColumns <= 0
    || sampling.nearColumns <= sampling.farColumns
  ) {
    throw new Error(`${name} sampling requires positive rows and decreasing near-to-far columns`);
  }
}

function validateCameraDistance(cameraDistance) {
  if (
    typeof cameraDistance !== "object"
    || cameraDistance === null
    || typeof cameraDistance.near !== "number"
    || !Number.isFinite(cameraDistance.near)
    || typeof cameraDistance.far !== "number"
    || !Number.isFinite(cameraDistance.far)
    || cameraDistance.near <= 0
    || cameraDistance.far <= cameraDistance.near
  ) {
    throw new Error("Camera distance requires positive increasing near and far values");
  }
}

export function isGeneratedTerrainGeometryDescriptor(geometry) {
  return geometry?.generator === TERRAIN_GENERATOR;
}

export function validateTerrainGeometryDescriptor(geometry) {
  if (
    !isGeneratedTerrainGeometryDescriptor(geometry)
    || !Number.isInteger(geometry.referenceWidthPixels)
    || geometry.referenceWidthPixels <= 0
  ) {
    throw new Error("Terrain geometry requires a positive reference framebuffer width");
  }
  validateCameraDistance(geometry.cameraDistance);
  validateSampling(geometry.terrainSampling, "Terrain");
  validateSampling(geometry.waterSampling, "Water");
  return geometry;
}

function scaledSampling(sampling, horizontalPixels, referenceWidthPixels) {
  const scale = horizontalPixels / referenceWidthPixels;
  const rows = Math.max(1, Math.round(sampling.rows * scale));
  const farColumns = Math.max(1, Math.round(sampling.farColumns * scale));
  const nearColumns = Math.max(farColumns + 1, Math.round(sampling.nearColumns * scale));
  return Object.freeze({ rows, nearColumns, farColumns });
}

function setVertex(values, vertex, ray, cameraDistance, surface) {
  const offset = vertex * 4;
  values[offset] = ray;
  values[offset + 1] = cameraDistance;
  values[offset + 2] = 0;
  values[offset + 3] = surface;
}

function buildRows(sampling, cameraDistance, vertexOffset) {
  const rows = [];
  let nextVertex = vertexOffset;
  const columnDecay = sampling.farColumns / sampling.nearColumns;
  const distanceGrowth = cameraDistance.far / cameraDistance.near;
  for (let row = 0; row <= sampling.rows; row += 1) {
    const columns = row === sampling.rows
      ? sampling.farColumns
      : Math.round(sampling.nearColumns * columnDecay ** (row / sampling.rows));
    const distance = row === sampling.rows
      ? cameraDistance.far
      : cameraDistance.near * distanceGrowth ** (row / sampling.rows);
    rows.push({ cameraDistance: distance, columns, vertexOffset: nextVertex });
    nextVertex += columns + 1;
  }
  if (!Number.isSafeInteger(nextVertex)) {
    throw new Error("Terrain grid vertex count exceeds the JavaScript integer range");
  }
  return Object.freeze({ rows, vertexCount: nextVertex - vertexOffset });
}

function surfaceIndexCount(layout) {
  let triangleCount = 0;
  for (let row = 0; row + 1 < layout.rows.length; row += 1) {
    triangleCount += layout.rows[row].columns + layout.rows[row + 1].columns;
  }
  return triangleCount * 3;
}

function appendTriangle(indices, offset, first, second, third) {
  indices[offset] = first;
  indices[offset + 1] = second;
  indices[offset + 2] = third;
  return offset + 3;
}

function connectRows(indices, offset, near, far, stripIndex) {
  let nearColumn = 0;
  let farColumn = 0;
  let nextIndex = offset;
  while (nearColumn < near.columns || farColumn < far.columns) {
    const nearStep = nearColumn < near.columns
      ? (nearColumn + 1) * far.columns
      : Number.POSITIVE_INFINITY;
    const farStep = farColumn < far.columns
      ? (farColumn + 1) * near.columns
      : Number.POSITIVE_INFINITY;
    const nearVertex = near.vertexOffset + nearColumn;
    const farVertex = far.vertexOffset + farColumn;
    if (nearStep < farStep) {
      nextIndex = appendTriangle(indices, nextIndex, nearVertex, nearVertex + 1, farVertex);
      nearColumn += 1;
      continue;
    }
    if (farStep < nearStep) {
      nextIndex = appendTriangle(indices, nextIndex, nearVertex, farVertex + 1, farVertex);
      farColumn += 1;
      continue;
    }
    if ((stripIndex + nearColumn + farColumn) % 2 === 0) {
      nextIndex = appendTriangle(indices, nextIndex, nearVertex, nearVertex + 1, farVertex + 1);
      nextIndex = appendTriangle(indices, nextIndex, nearVertex, farVertex + 1, farVertex);
    } else {
      nextIndex = appendTriangle(indices, nextIndex, nearVertex, nearVertex + 1, farVertex);
      nextIndex = appendTriangle(indices, nextIndex, nearVertex + 1, farVertex + 1, farVertex);
    }
    nearColumn += 1;
    farColumn += 1;
  }
  return nextIndex;
}

function writeSurface(values, layout, surface) {
  for (const row of layout.rows) {
    for (let column = 0; column <= row.columns; column += 1) {
      setVertex(
        values,
        row.vertexOffset + column,
        (column / row.columns) * 2 - 1,
        row.cameraDistance,
        surface
      );
    }
  }
}

function appendSurfaceIndices(indices, offset, layout) {
  const indexOffset = offset;
  let nextIndex = offset;
  for (let row = 0; row + 1 < layout.rows.length; row += 1) {
    nextIndex = connectRows(indices, nextIndex, layout.rows[row], layout.rows[row + 1], row);
  }
  return Object.freeze({ indexOffset, indexCount: nextIndex - indexOffset });
}

function generateTerrainLodGrid(cameraDistance, terrainSampling, waterSampling) {
  const terrain = buildRows(terrainSampling, cameraDistance, 3);
  const water = buildRows(waterSampling, cameraDistance, 3 + terrain.vertexCount);
  const vertexCount = 3 + terrain.vertexCount + water.vertexCount;
  const values = new Float32Array(vertexCount * 4);
  setVertex(values, 0, -1, -1, 0);
  setVertex(values, 1, 3, -1, 0);
  setVertex(values, 2, -1, 3, 0);
  writeSurface(values, terrain, 1);
  writeSurface(values, water, 2);

  const indexCount = 3 + surfaceIndexCount(terrain) + surfaceIndexCount(water);
  const indices = new Uint32Array(indexCount);
  let nextIndex = appendTriangle(indices, 0, 0, 1, 2);
  const terrainIndices = appendSurfaceIndices(indices, nextIndex, terrain);
  nextIndex += terrainIndices.indexCount;
  const waterIndices = appendSurfaceIndices(indices, nextIndex, water);
  nextIndex += waterIndices.indexCount;
  if (nextIndex !== indexCount) {
    throw new Error("Terrain topology generation produced an inconsistent index count");
  }

  return Object.freeze({
    vertexCount,
    data: values.buffer,
    indices: Object.freeze({
      format: "uint32",
      count: indexCount,
      data: indices.buffer
    }),
    surfaces: Object.freeze({
      sky: Object.freeze({
        value: 0,
        vertexOffset: 0,
        vertexCount: 3,
        indexOffset: 0,
        indexCount: 3
      }),
      terrain: Object.freeze({
        value: 1,
        vertexOffset: 3,
        vertexCount: terrain.vertexCount,
        ...terrainIndices,
        sampling: terrainSampling
      }),
      water: Object.freeze({
        value: 2,
        vertexOffset: 3 + terrain.vertexCount,
        vertexCount: water.vertexCount,
        ...waterIndices,
        sampling: waterSampling
      })
    })
  });
}

export function resolveTerrainGeometryDescriptor(geometry, horizontalPixels) {
  validateTerrainGeometryDescriptor(geometry);
  if (!Number.isSafeInteger(horizontalPixels) || horizontalPixels <= 0) {
    throw new Error("Terrain geometry requires a positive horizontal framebuffer size");
  }
  const cached = geometryCache.get(geometry);
  if (cached?.horizontalPixels === horizontalPixels) {
    return cached.resolved;
  }

  const terrainSampling = scaledSampling(
    geometry.terrainSampling,
    horizontalPixels,
    geometry.referenceWidthPixels
  );
  const waterSampling = scaledSampling(
    geometry.waterSampling,
    horizontalPixels,
    geometry.referenceWidthPixels
  );
  const generated = generateTerrainLodGrid(
    geometry.cameraDistance,
    terrainSampling,
    waterSampling
  );
  const resolved = Object.freeze({
    format: geometry.format,
    attributeEncoding: geometry.attributeEncoding,
    primitive: geometry.primitive,
    render: geometry.render,
    horizontalPixels,
    ...generated
  });
  geometryCache.set(geometry, Object.freeze({ horizontalPixels, resolved }));
  return resolved;
}
