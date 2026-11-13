# Shader stage interfaces

Vertex attributes and vertex-to-fragment values use explicit locations and types.
A fragment shader may consume any subset of the vertex shader's outputs; declaration
order and unused outputs do not change the remaining locations.

```dynlex
import lib/shader.dl

if this is a vertex shader:
    set position to the vertex input named "position" at location 0 of type an aggregate of 4 floating-point number values
    set material to the vertex input named "material" at location 1 of type an unsigned 32 bit integer
    set the flat shader interpolant named "material" at location 3 to material
    set the output position with x (the element at 0 in position), y (the element at 1 in position), z (the element at 2 in position) and w (the element at 3 in position)

if this is a fragment shader:
    set material to the flat shader interpolant named "material" at location 3 of type an unsigned 32 bit integer
    set shade to 0.25
    if material = 1 and the fragment is front facing:
        set shade to 0.75
    set the fragment color with a red channel of shade, a green channel of shade, a blue channel of shade and an alpha channel of 1.0
```

Use `smooth shader interpolant` (also spelled `perspective shader interpolant`)
for perspective-correct floating-point values and `flat shader interpolant` for
values that stay constant across each primitive. Flat values come from its first
vertex. Integer interpolants must be flat. Supported types are float32, int32 and
uint32, either scalar or vectors of two through four components. Pointer, Boolean,
matrix and wider numeric interpolants produce source errors.

Both stages must agree on location, type and interpolation. Within one stage, a
name identifies one declaration and a location belongs to one name. Repeated reads
and writes with the same declaration share one interface value. Names can contain
spaces or Unicode; the compiler encodes their bytes into valid shader identifiers.

`the fragment is front facing` is a Boolean, available only in fragment shaders.
It follows the renderer's front-face convention, including when culling is disabled
for a two-sided material.

The intrinsic boundary is independent of library syntax:

```dynlex
@intrinsic("shader interpolant output", name, location, "flat", value)
@intrinsic("shader interpolant input", name, location, "flat", type)
@intrinsic("shader input", "FrontFacing")
```

Names, locations, interpolation modes and input types are compile-time values.
The output value keeps its exact type, with no implicit conversion to float.
These operations require SPIR-V output and the appropriate vertex or fragment stage.
