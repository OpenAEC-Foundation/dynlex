# cadkernel in DynLex

## Objective
Port the geometry library at upstream revision 953d546b68aef4b6692566a1a9b077fc5bd9fb4f to native DynLex, with behavior checked against the pinned Rust implementation, an interactive validation application, and reproducible findings.

## Boundaries
The port belongs in lib/cadkernel/, with module boundaries corresponding to the upstream geometry, spatial-curve and B-rep modules. The delivery is one pull request covering all 83 main-crate source files and their 672 tests, including optional modules. The separate cadkernel-constraints workspace member is outside the main-crate scope. A port coverage manifest identifies each source module and its verification state. No module is called ported before its relevant behavioral tests run successfully. External codec and offset dependencies must be explicitly accounted for; a native wrapper does not count as a DynLex port.

The implementation preserves binary64 coordinates, explicit operation tolerances, generational topology identity, provenance, validation and explicit failures. Geometry algorithms remain separate from the validation application's controls and drawing code. Existing DynLex library types may be used only where their behavior matches the upstream contract.

The [DynLex design basis](../../cadkernel-dynlex-design.md) governs the native API and ownership model: readable typed patterns, data-only records, explicit action/value contracts, standard-library reuse where equivalent, and minimal intrinsics. Record justified numerical or lifetime differences. Close material language/API review findings as well as numerical coverage before declaring the port complete.

## Verification
First translate independently checkable tests, then the functions they exercise. Test normal inputs, degeneracies, reversed orientations, invalid handles and large-coordinate cases. Execute compiled code with an explicit -O2 flag and also check optimization parity on the numerical fixtures. Use upstream Rust as a differential reference while retaining independently calculated expected values.

The application must draw geometry produced by the port and permit inspection of different operations and parameters. Automated tests import the same geometry modules. Record compile timings, compiler revision, source revision and any unsupported operations. Numerical agreement alone is not proof of valid topology.

## Integration
Target OpenAEC-Foundation/dynlex, based on 332dac1385edcbe6458386a5119b4cc62d010581. Preserve the existing checkout's uncommitted work. Publish implementation, tests, source/license attribution and technical findings together in the requested pull request. Do not include conversation records.
