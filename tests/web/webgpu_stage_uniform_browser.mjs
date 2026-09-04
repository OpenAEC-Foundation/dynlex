import { evaluate } from "./browser_test_driver.mjs";

export async function verifyWebGpuStageUniformBindings() {
  await evaluate(`(async () => {
    const canvas = document.createElement('canvas');
    canvas.style.cssText = 'position:fixed;left:0;top:0;width:4px;height:4px';
    document.body.append(canvas);
    const { createShaderPreview } = await import('/shader-renderer.js');
    const preview = await createShaderPreview(canvas, { running: false });
    try {
      await preview.replaceProgram({
        fragmentSource: \`
          struct FragmentUniform { value: f32 }
          @group(0) @binding(1) var<uniform> fragmentUniform: FragmentUniform;
          @fragment
          fn main() -> @location(0) vec4<f32> {
            return vec4<f32>(fragmentUniform.value, 0.0, 0.0, 1.0);
          }
        \`,
        fragmentUniforms: [{ name: 'time', group: 0, binding: 1 }],
        vertexSource: \`
          struct VertexUniform { value: f32 }
          struct VertexInput { @location(0) position: vec4<f32> }
          @group(0) @binding(0) var<uniform> vertexUniform: VertexUniform;
          @vertex
          fn main(input: VertexInput) -> @builtin(position) vec4<f32> {
            return vec4<f32>(input.position.xyz, input.position.w + vertexUniform.value * 0.0);
          }
        \`,
        vertexUniforms: [{ name: 'width', group: 0, binding: 0 }],
        geometry: {
          format: 'float32x4',
          attributeEncoding: 'test-position',
          primitive: 'triangles',
          vertexCount: 3,
          data: new Float32Array([
            -1, -1, 0, 1,
             3, -1, 0, 1,
            -1,  3, 0, 1
          ]).buffer,
          render: { backgroundPass: true, blendMode: 'opaque', depthTest: false }
        }
      });
      await preview.whenNextFrameRendered();
    } finally {
      preview.destroy();
      canvas.remove();
    }
  })()`);
}
