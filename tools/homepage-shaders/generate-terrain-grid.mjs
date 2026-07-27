function validateBands(bands, name) {
  if (!Array.isArray(bands) || bands.length < 2) {
    throw new Error(`${name} LOD must contain at least two bands`);
  }
  let previousDepthEnd = 0;
  let previousColumns = null;
  for (const band of bands) {
    if (
      typeof band?.depthEnd !== "number"
      || band.depthEnd <= previousDepthEnd
      || band.depthEnd > 1
      || !Number.isInteger(band.rows)
      || band.rows <= 0
      || !Number.isInteger(band.columns)
      || band.columns <= 0
      || (previousColumns !== null && band.columns >= previousColumns)
    ) {
      throw new Error(`${name} LOD bands must increase in depth and decrease their column count`);
    }
    previousDepthEnd = band.depthEnd;
    previousColumns = band.columns;
  }
  if (previousDepthEnd !== 1) {
    throw new Error(`${name} LOD bands must cover the complete depth range`);
  }
}

function setVertex(values, vertex, x, depth, surface) {
  const offset = vertex * 4;
  values[offset] = x;
  values[offset + 1] = depth;
  values[offset + 2] = 0;
  values[offset + 3] = surface;
}

function buildRows(bands, vertexOffset) {
  const rows = [{ depth: 0, columns: bands[0].columns, vertexOffset }];
  let depthStart = 0;
  let nextVertex = vertexOffset + bands[0].columns + 1;
  for (const band of bands) {
    for (let row = 1; row <= band.rows; row += 1) {
      const depth = row === band.rows
        ? band.depthEnd
        : depthStart + ((band.depthEnd - depthStart) * row) / band.rows;
      rows.push({ depth, columns: band.columns, vertexOffset: nextVertex });
      nextVertex += band.columns + 1;
    }
    depthStart = band.depthEnd;
  }
  return Object.freeze({ rows, vertexCount: nextVertex - vertexOffset });
}

function appendTriangle(indices, first, second, third) {
  indices.push(first, second, third);
}

function connectRows(indices, near, far, stripIndex) {
  let nearColumn = 0;
  let farColumn = 0;
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
      appendTriangle(indices, nearVertex, nearVertex + 1, farVertex);
      nearColumn += 1;
      continue;
    }
    if (farStep < nearStep) {
      appendTriangle(indices, nearVertex, farVertex + 1, farVertex);
      farColumn += 1;
      continue;
    }
    if ((stripIndex + nearColumn + farColumn) % 2 === 0) {
      appendTriangle(indices, nearVertex, nearVertex + 1, farVertex + 1);
      appendTriangle(indices, nearVertex, farVertex + 1, farVertex);
    } else {
      appendTriangle(indices, nearVertex, nearVertex + 1, farVertex);
      appendTriangle(indices, nearVertex + 1, farVertex + 1, farVertex);
    }
    nearColumn += 1;
    farColumn += 1;
  }
}

function writeSurface(values, layout, surface) {
  for (const row of layout.rows) {
    for (let column = 0; column <= row.columns; column += 1) {
      const x = (column / row.columns) * 2 - 1;
      setVertex(values, row.vertexOffset + column, x, row.depth, surface);
    }
  }
}

function appendSurfaceIndices(indices, layout) {
  const indexOffset = indices.length;
  for (let row = 0; row + 1 < layout.rows.length; row += 1) {
    connectRows(indices, layout.rows[row], layout.rows[row + 1], row);
  }
  return Object.freeze({ indexOffset, indexCount: indices.length - indexOffset });
}

export function generateTerrainLodGrid(terrainBands, waterBands) {
  validateBands(terrainBands, "Terrain");
  validateBands(waterBands, "Water");

  const terrain = buildRows(terrainBands, 3);
  const water = buildRows(waterBands, 3 + terrain.vertexCount);
  const vertexCount = 3 + terrain.vertexCount + water.vertexCount;
  if (vertexCount > 65536) {
    throw new Error("Terrain grid exceeds the uint16 index range");
  }
  const values = new Float32Array(vertexCount * 4);
  setVertex(values, 0, -1, -1, 0);
  setVertex(values, 1, 3, -1, 0);
  setVertex(values, 2, -1, 3, 0);
  writeSurface(values, terrain, 1);
  writeSurface(values, water, 2);

  const indices = [0, 1, 2];
  const terrainIndices = appendSurfaceIndices(indices, terrain);
  const waterIndices = appendSurfaceIndices(indices, water);
  const indexValues = Uint16Array.from(indices);
  return Object.freeze({
    vertexCount,
    data: Buffer.from(values.buffer, values.byteOffset, values.byteLength),
    indexCount: indexValues.length,
    indices: Buffer.from(indexValues.buffer, indexValues.byteOffset, indexValues.byteLength),
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
        bands: terrainBands
      }),
      water: Object.freeze({
        value: 2,
        vertexOffset: 3 + terrain.vertexCount,
        vertexCount: water.vertexCount,
        ...waterIndices,
        bands: waterBands
      })
    })
  });
}
