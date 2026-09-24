# Planar region winding and hole validation

This differential checks `planar_region` against the pinned Rust source at O0
and O2. Its eleven cases cover both outer-ring directions, independently
reversed holes, disjoint holes, and holes that leave, touch, cross, overlap or
nest within another boundary. The probe compares validity, topology, face
orientation and signed loop areas. The source revision and hashes are pinned
in `verify.py`.

```powershell
python -B tests/cadkernel/brep_presspull_region_review/verify.py --source PATH_TO_PINNED_KERNEL
```

This is a focused planar-region check. The broader press-pull differential and
required fixtures cover other operation paths.
