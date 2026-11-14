import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";

export function assertNanoGeometry(nano, directory, projectDir) {
  assert.equal(nano.geometry.kind, 'points');
  assert.deepEqual(nano.stages, ['fragment', 'vertex']);
  const [background, cloud] = nano.program.passes;
  assert.equal(background.draws[0].count, 3);
  assert.equal(background.color.load, 'clear');
  assert.equal(cloud.color.load, 'load');
  assert.equal(cloud.topology, 'triangle-list');
  assert.equal(cloud.vertexBuffers[0].arrayStride, 16);
  assert.equal(cloud.vertexBuffers[0].attributes[0].format, 'float32x4');
  assert.equal(cloud.vertexBuffers[0].source, nano.geometry.path);
  assert.equal(cloud.depth, null);
  assert.equal(cloud.pipelines[0].depthWrite, false);
  assert.equal(cloud.pipelines[0].depthCompare, 'always');
  assert.deepEqual(cloud.pipelines[0].blend, {
    color: {srcFactor:'one',dstFactor:'one',operation:'add'},
    alpha: {srcFactor:'one',dstFactor:'one',operation:'add'}
  });
  for (const backend of ["webgpu", "webgl"]) {
    for (const stage of ['vertex','fragment']) {
      const source = cloud[stage][backend];
      assert.equal(source.kind, 'url');
      assert.equal(source.value, `assets/shaders/nano-choreography.${stage}.${backend === 'webgpu' ? 'wgsl' : 'glsl'}`);
      assert(fs.readFileSync(path.join(directory, source.value), 'utf8').length > 0);
    }
  }
  const geometryMetadata = JSON.parse(fs.readFileSync(path.join(directory, nano.geometry.metadata), 'utf8'));
  assert.equal(geometryMetadata.format, 'float32x4');
  assert.equal(geometryMetadata.attributeEncoding, 'paired-unorm12-wheel-corner');
  assert.equal(geometryMetadata.primitive, 'triangles');
  assert.ok(geometryMetadata.pointCount >= 30000);
  assert.equal(geometryMetadata.vertexCount, geometryMetadata.pointCount * 3);
  const geometryBytes = fs.readFileSync(path.join(directory, nano.geometry.path));
  assert.equal(geometryBytes.byteLength, geometryMetadata.vertexCount * 4 * Float32Array.BYTES_PER_ELEMENT);
  const geometryValues = new Float32Array(
    geometryBytes.buffer,
    geometryBytes.byteOffset,
    geometryBytes.byteLength / Float32Array.BYTES_PER_ELEMENT
  );
  const targetPoints = [];
  const motorcyclePoints = [];
  const motorcycleWheelPoints = [];
  for (let vertex = 0; vertex < geometryMetadata.vertexCount; vertex += 1) {
    for (let axis = 0; axis < 3; axis += 1) {
      const packedCoordinate = geometryValues[vertex * 4 + axis];
      assert.ok(
        Number.isInteger(packedCoordinate)
          && packedCoordinate >= 0
          && packedCoordinate <= 16777215,
        "Each geometry axis must exactly pack one motorcycle and one Vitruvian coordinate"
      );
    }
    if (vertex % 3 === 0) {
      const target = [];
      const motorcycle = [];
      for (let axis = 0; axis < 3; axis += 1) {
        const packedCoordinate = geometryValues[vertex * 4 + axis];
        const targetQuantized = Math.floor(packedCoordinate / 4096);
        const motorcycleQuantized = packedCoordinate - targetQuantized * 4096;
        target.push((targetQuantized / 4095) * 4 - 2);
        motorcycle.push((motorcycleQuantized / 4095) * 4 - 2);
      }
      targetPoints.push(target);
      motorcyclePoints.push(motorcycle);
    }
    const encodedCorner = geometryValues[vertex * 4 + 3];
    const wheelPoint = Math.floor(encodedCorner / 4);
    assert.equal(
      encodedCorner - wheelPoint * 4,
      vertex % 3,
      "Geometry must preserve the micro-triangle corner"
    );
    assert.ok(wheelPoint === 0 || wheelPoint === 1);
    if (vertex % 3 === 0) motorcycleWheelPoints.push(wheelPoint);
  }
  assert.ok(motorcycleWheelPoints.some((wheelPoint) => wheelPoint === 0));
  assert.ok(motorcycleWheelPoints.filter((wheelPoint) => wheelPoint === 1).length > 30000);
  for (let point = 0; point < motorcyclePoints.length; point += 1) {
    if (motorcycleWheelPoints[point] === 0) continue;
    const [x, y, z] = motorcyclePoints[point];
    const centerX = x < 0 ? -0.72 : 0.72;
    assert.ok(Math.hypot(x - centerX, y + 0.42) < 0.38);
    assert.ok(Math.abs(z) < 0.15);
  }

  function coordinateCorrelation(firstPoints, secondPoints, axis) {
    const count = firstPoints.length;
    const firstMean = firstPoints.reduce((sum, point) => sum + point[axis], 0) / count;
    const secondMean = secondPoints.reduce((sum, point) => sum + point[axis], 0) / count;
    let covariance = 0;
    let firstVariance = 0;
    let secondVariance = 0;
    for (let index = 0; index < count; index += 1) {
      const firstDelta = firstPoints[index][axis] - firstMean;
      const secondDelta = secondPoints[index][axis] - secondMean;
      covariance += firstDelta * secondDelta;
      firstVariance += firstDelta * firstDelta;
      secondVariance += secondDelta * secondDelta;
    }
    return covariance / Math.sqrt(firstVariance * secondVariance);
  }

  const horizontalCorrelation = coordinateCorrelation(targetPoints, motorcyclePoints, 0);
  const verticalCorrelation = coordinateCorrelation(targetPoints, motorcyclePoints, 1);
  const planarRootMeanSquareTravel = Math.sqrt(
    targetPoints.reduce((sum, target, index) => (
      sum
        + (target[0] - motorcyclePoints[index][0]) ** 2
        + (target[1] - motorcyclePoints[index][1]) ** 2
    ), 0) / targetPoints.length
  );
  assert.ok(
    horizontalCorrelation > 0.7 && verticalCorrelation > 0.7,
    "Point correspondence must preserve neighboring horizontal and vertical regions"
  );
  assert.ok(
    planarRootMeanSquareTravel < 0.6,
    "Point correspondence must form coherent flights instead of a random dissolving cloud"
  );
  assert.equal(geometryMetadata.schemaVersion, 5);
  assert.equal(geometryMetadata.attributeEncoding, "paired-unorm12-wheel-corner");
  assert.deepEqual(geometryMetadata.coordinateEncoding, {
    name: "paired-unorm12",
    quantizationLevels: 4095,
    coordinateMinimum: -2,
    coordinateMaximum: 2
  });
  assert.deepEqual(geometryMetadata.triangleCornerEncoding, {
    name: "wheel-part-plus-corner",
    wheelOffset: 4,
    cornerCount: 3
  });
  assert.deepEqual(geometryMetadata.pointPairing, {
    name: "recursive-spatial-bisection",
    leafPointCount: 64,
    axisOrder: ["x", "y", "z"]
  });
  assert.equal(geometryMetadata.source.uid, "6c0b99ce8463468fbd00f304dbe7e105");
  assert.equal(geometryMetadata.source.title, "The Vitruvian Man");
  assert.equal(geometryMetadata.source.author, "Fri");
  assert.equal(geometryMetadata.source.license, "CC-BY-4.0");
  assert.equal(
    geometryMetadata.source.url,
    "https://sketchfab.com/3d-models/the-vitruvian-man-6c0b99ce8463468fbd00f304dbe7e105"
  );
  assert.match(geometryMetadata.source.archiveSha256, /^[a-f0-9]{64}$/);
  assert.match(geometryMetadata.source.meshSha256, /^[a-f0-9]{64}$/);
  assert.equal(geometryMetadata.source.modelVertexCount, 241794);
  assert.equal(geometryMetadata.source.modelTriangleCount, 483637);
  assert.ok(Array.isArray(geometryMetadata.modifications));
  assert.equal(geometryMetadata.pointCount, targetPoints.length);
  assert.equal(geometryMetadata.motorcyclePointCount, geometryMetadata.pointCount);
  assert.equal(
    geometryMetadata.motorcycleWheelPointCount,
    motorcycleWheelPoints.filter((wheelPoint) => wheelPoint === 1).length
  );
  assert.ok(geometryMetadata.surfacePointCount >= 60000);
  assert.ok(geometryMetadata.densityPointCount >= 5000);
  assert.equal("detailPointCount" in geometryMetadata, false);
  assert.equal("alternateArmPointCount" in geometryMetadata, false);
  assert.equal("alternateLegPointCount" in geometryMetadata, false);

  const pointCloudGenerator = fs.readFileSync(
    path.join(projectDir, "tools/homepage-shaders/generate-point-cloud.py"),
    "utf8"
  );
  assert.match(pointCloudGenerator, /6c0b99ce8463468fbd00f304dbe7e105/);
  assert.match(pointCloudGenerator, /bpy\.ops\.wm\.stl_import/);
  for (const pairedGeometryOperation of [
    "motorcycle_points",
    "sample_torus",
    "sample_ellipsoid",
    "sample_segment_tube",
    "spatially_pair_points",
    "pack_coordinate_pair"
  ]) {
    assert.match(
      pointCloudGenerator,
      new RegExp(`def ${pairedGeometryOperation}\\(`),
      `The geometry generator must define ${pairedGeometryOperation.replaceAll("_", " ")}`
    );
  }
  assert.doesNotMatch(
    pointCloudGenerator,
    /limb_weights|alternate_arms|alternate_legs|append_frame|BODY_OBJECT|EYE_OBJECTS|DETAIL_POINT_COUNT|detail_sampler|detail_points|predicate/,
    "The selected Vitruvian model must provide its own anatomy with uniform sampling"
  );

  const shaderReadme = fs.readFileSync(
    path.join(projectDir, "tools/homepage-shaders/README.md"),
    "utf8"
  );
  assert.match(shaderReadme, /--stl/);
  assert.doesNotMatch(shaderReadme, /--blend/);

  const attributionPage = fs.readFileSync(
    path.join(projectDir, "site/pages/docs/attributions.dl"),
    "utf8"
  );
  assert.match(attributionPage, /The Vitruvian Man/);
  assert.match(attributionPage, /link "Fri"/);
  assert.match(attributionPage, /CC BY 4\.0/);
  assert.match(attributionPage, /6c0b99ce8463468fbd00f304dbe7e105/);
}
