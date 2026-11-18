# Pinned Naga correction

The translator uses published Naga 30.0.1 with the source correction in
`naga-early-merges.patch`. Upstream revision:
`40f4a34ebaf56f9a046231f54125ad046239d3f3`, `naga/` in
<https://github.com/gfx-rs/wgpu>. The package URL and SHA-256 are pinned in
`metadata/SHADER_TRANSLATOR`. Its MIT and Apache licenses remain in the prepared
source package.

Run `python3 scripts/prepare_shader_translator.py` from the compiler checkout
before invoking Cargo directly. `scripts/build_web.sh` runs this preparation
automatically. Cargo uses the corrected source in `.cache/shader-translator/naga`;
the shared Cargo registry is never modified. Preparation verifies the archive,
applies the patch without fuzzy matching, and records every prepared file's hash.
`--check` verifies that the prepared source matches both its inputs and receipt.

The published SPIR-V parser assigns a non-merge block to the body of its first
incoming edge, assuming there can be no second incoming branch. SPIR-V permits
branches to reconverge before the declared selection merge. In that case, this
assignment moves shared work into one branch and silently changes shader output.

The added `early_merges.rs` computes immediate postdominators from complete
function control-flow graphs. The frontend registers a proven earlier join as
the header's continuation before parsing child bodies. The declared merge remains
registered too. Early returns and loop exits keep their original boundaries.
Analysis is iterative and independent of binary block ordering. Changes to
existing upstream files are limited to input buffering, the analysis call and
continuation registration in `mod.rs` and `next_block.rs`.

Regression coverage includes the translator's native tests, the compiler's
import-free `tests/runtime/graphics_shaders/switch_fallthrough.dl`, reordered
SPIR-V predecessors, and dynlex-ui's actual WebGPU/WebGL resource pixel tests.
WGSL switch fall-through and GLSL image/sampler pairing remain separate backend
operations in the translator.
