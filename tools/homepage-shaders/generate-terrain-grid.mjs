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

function setVertex(values, vertex, x, depth, surface) {
  const offset = vertex * 4;
  values[offset] = x;
  values[offset + 1] = depth;
  values[offset + 2] = 0;
  values[offset + 3] = surface;
}

function buildRows(sampling, vertexOffset) {
  const rows = [];
  let nextVertex = vertexOffset;
  const totalDecay = sampling.farColumns / sampling.nearColumns;
  for (let row = 0; row <= sampling.rows; row += 1) {
    const columns = row === sampling.rows
      ? sampling.farColumns
      : Math.round(sampling.nearColumns * totalDecay ** (row / sampling.rows));
    rows.push({
      depth: row / sampling.rows,
      columns,
      vertexOffset: nextVertex
    });
    nextVertex += columns + 1;
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

export function generateTerrainLodGrid(terrainSampling, waterSampling) {
  validateSampling(terrainSampling, "Terrain");
  validateSampling(waterSampling, "Water");

  const terrain = buildRows(terrainSampling, 3);
  const water = buildRows(waterSampling, 3 + terrain.vertexCount);
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
