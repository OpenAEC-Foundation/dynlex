# Managed NURBS surface collections

`cad nurbs surface3` has fields whose types are inferred when a surface is
constructed. The bare declaration `a cad nurbs surface3` does not provide a
complete storage layout to the list destructor. A list of that bare type is
therefore rejected at compile time with a concrete-type diagnostic.

Construct a surface and its containing entry first, then use the entry's
inferred type for the collection:

```dynlex
set entry to @intrinsic("construct", a layout surface entry, made's value)
set surfaces to a cad topology list of the type of entry
append entry to surfaces's values
```

The positive `cadkernel_managed_surface_layout` fixture repeats construction,
assignment and destruction 100 times and checks a point on a retained surface
after the original control net is freed. The negative
`cadkernel_managed_surface_incomplete` fixture pins the diagnostic for a list
of the bare incomplete type. Both fixtures pass in O0 and O2 with the current
compiler; no compiler source change is needed for this case.
