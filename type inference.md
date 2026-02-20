type inference
class patterns should be parsed first.
class patterns can only accept macro class patterns as argument.
a macro class is basically a macro expression returning a type, but we need a way for the compiler to see which patterns should be parsed first.


macro expression int:
	replacement:
		@intrinsic("type", "i32")

macro expression [a|] pointer to type:
    replacement:
        @intrinsic("add pointer depth", type)

i guess classes should be parsed with expressions. since these macro expressions will resolve, it shouldn't be that big of a problem.

when an expression returns a type, it should be tokenized as a type.

