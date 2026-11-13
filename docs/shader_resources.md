# Shader resource bindings

`lib/shader.dl` defines sampled resources as compile-time data. A binding identifies
a texture or sampler in descriptor set zero; it does not allocate a host GPU object.
The renderer supplies that object's storage separately. The same declarations are
used when compiling vertex and fragment shaders.

```dynlex
import lib/shader.dl

set paint to the shader image named "paint" at binding 1
set filtering to the shader sampler named "filtering" at binding 2
set uv to the shader uniform named "uv" at binding 0 as an aggregate of 2 floating-point number values
set color to the sample from paint using filtering at uv with level 0.0
@intrinsic("shader output", color)
```

Available declarations:

- `the shader image named "paint" at binding 1`: float color, 2D coordinates.
- `the shader cube image named "sky" at binding 2`: float color, 3D direction.
- `the shader depth image named "shadow" at binding 3`: depth, 2D coordinates.
- `the shader sampler named "filtering" at binding 4`: ordinary sampling.
- `the shader comparison sampler named "comparison" at binding 5`: depth comparison.

Color sampling returns four float32 components. Its explicit level can select a
mipmap for either a 2D image or a cube image. Depth comparison returns float32
visibility, using the sampler's comparison function and level zero:

```dynlex
set shadow to the shader depth image named "shadow" at binding 3
set comparison to the shader comparison sampler named "comparison" at binding 5
set visibility to the visibility from shadow using comparison at uv with depth 0.5
```

Images and samplers can be named, returned by ordinary pure functions, and passed
through fixed parameters. Their immutable identities include the name, binding and
resource kind. Reusing a sampler for different images shares its GPU binding.
Neither resource references nor their declaration strings have runtime storage.

The intrinsic boundary is:

```dynlex
@intrinsic("shader image", name, binding, "2d", false)
@intrinsic("shader image", name, binding, "cube", false)
@intrinsic("shader image", name, binding, "2d", true)
@intrinsic("shader sampler", name, binding, comparison)
@intrinsic("shader sample level", image, sampler, coordinates, level)
@intrinsic("shader sample compare", image, sampler, coordinates, reference)
```

Declaration operands and sampling references must be compile-time known. Names
must be nonempty strings; binding slots must fit an unsigned 32-bit integer.
Flags are booleans. Coordinates, levels and comparison references are float32.
A color image requires an ordinary sampler; a depth comparison requires a 2D
depth image and comparison sampler. Invalid declarations and sampling operands
produce source diagnostics. Texture, sampler and numeric uniform bindings share
one namespace and cannot disagree about a name or slot.

The backend lowers sampling alongside the SPIR-V interface, emitting separate
image/sampler globals and `OpImageSampleExplicitLod` or
`OpImageSampleDrefExplicitLod`. Opaque resource values stay within the sampling
block. Binding names never become runtime byte arrays or require an Int8 shader
capability. Browser
reflection reports a sorted `resources` array alongside `uniforms` and
`vertexInputs`. Texture entries contain `name`, `group`, `binding`, `kind`,
`dimension` and `sampleType`; sampler entries contain `name`, `group`, `binding`,
`kind` and `comparison`.
