# TODO

- Add a direct compiler diagnostic for repeated parameter-like words in a pattern definition. Example: `function a zero padded string from value with width width:` should fail at the definition site instead of later surfacing as an unresolved pattern call because the second `width` is treated as a literal.
