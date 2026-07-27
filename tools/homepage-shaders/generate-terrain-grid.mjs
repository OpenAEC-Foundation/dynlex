function requireGridDimension(value, name) {
  if (!Number.isInteger(value) || value <= 0) {
    throw new Error(`Terrain grid ${name} must be a positive integer`);
  }
}

function setVertex(values, vertex, x, depth, surface) {
  const offset = vertex * 4;
  values[offset] = x;
  values[offset + 1] = depth;
  values[offset + 2] = 0;
  values[offset + 3] = surface;
}

export function generateTerrainGrid(columns, rows) {
  requireGridDimension(columns, "columns");
  requireGridDimension(rows, "rows");

  const vertexCount = 3 + (columns + 1) * (rows + 1);
  if (vertexCount > 65536) {
    throw new Error("Terrain grid exceeds the uint16 index range");
  }
  const values = new Float32Array(vertexCount * 4);
  setVertex(values, 0, -1, -1, 0);
  setVertex(values, 1, 3, -1, 0);
  setVertex(values, 2, -1, 3, 0);

  for (let row = 0; row <= rows; row += 1) {
    const depth = row / rows;
    for (let column = 0; column <= columns; column += 1) {
      const x = (column / columns) * 2 - 1;
      const vertex = 3 + row * (columns + 1) + column;
      setVertex(values, vertex, x, depth, 1);
    }
  }

  const indexCount = 3 + columns * rows * 6;
  const indices = new Uint16Array(indexCount);
  indices.set([0, 1, 2]);
  let index = 3;
  for (let row = 0; row < rows; row += 1) {
    for (let column = 0; column < columns; column += 1) {
      const topLeft = 3 + row * (columns + 1) + column;
      const topRight = topLeft + 1;
      const bottomLeft = topLeft + columns + 1;
      const bottomRight = bottomLeft + 1;
      if ((row + column) % 2 === 0) {
        indices.set([topLeft, topRight, bottomRight, topLeft, bottomRight, bottomLeft], index);
      } else {
        indices.set([topLeft, topRight, bottomLeft, topRight, bottomRight, bottomLeft], index);
      }
      index += 6;
    }
  }

  if (index !== indexCount) {
    throw new Error("Terrain grid generation produced an inconsistent index count");
  }
  return Object.freeze({
    vertexCount,
    data: Buffer.from(values.buffer, values.byteOffset, values.byteLength),
    indexCount,
    indices: Buffer.from(indices.buffer, indices.byteOffset, indices.byteLength)
  });
}
