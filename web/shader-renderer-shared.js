import {
  isGeneratedTerrainGeometryDescriptor,
  validateTerrainGeometryDescriptor
} from "./terrain-geometry.js";

export function validateShaderGeometryDescriptor(geometry, requireData = false) {
  const validInterface = (
    geometry
    && geometry.format === "float32x4"
    && typeof geometry.attributeEncoding === "string"
    && geometry.attributeEncoding.length > 0
    && geometry.primitive === "triangles"
    && typeof geometry.render?.backgroundPass === "boolean"
    && ["opaque", "additive"].includes(geometry.render.blendMode)
    && typeof geometry.render.depthTest === "boolean"
  );
  if (isGeneratedTerrainGeometryDescriptor(geometry)) {
    if (
      !validInterface
      || requireData
      || geometry.vertexCount !== undefined
      || geometry.data !== undefined
      || geometry.indices !== undefined
    ) {
      throw new Error("Invalid shader geometry");
    }
    validateTerrainGeometryDescriptor(geometry);
    return geometry;
  }

  const indices = geometry?.indices;
  const validIndices = indices === undefined || (
    indices
    && indices.format === "uint32"
    && Number.isInteger(indices.count)
    && indices.count > 0
    && (!requireData || indices.data instanceof ArrayBuffer)
    && (
      !(indices.data instanceof ArrayBuffer)
      || indices.data.byteLength === indices.count * Uint32Array.BYTES_PER_ELEMENT
    )
  );
  if (
    !validInterface
    || !Number.isInteger(geometry.vertexCount)
    || geometry.vertexCount <= 0
    || (requireData && !(geometry.data instanceof ArrayBuffer))
    || (
      geometry.data instanceof ArrayBuffer
      && geometry.data.byteLength !== geometry.vertexCount * 4 * Float32Array.BYTES_PER_ELEMENT
    )
    || !validIndices
  ) {
    throw new Error("Invalid shader geometry");
  }
  return geometry;
}
