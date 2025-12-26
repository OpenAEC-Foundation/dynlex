conventions:
# any language


## proper function names
describe what it does. don't give any unnecessary information about inner workings or what it's needed for. you never know if it might be used for something else.

## DRY

## keep it as simple as possible
beware for unnecessary complexity. do not make a function like this:

void setFrameRate(int frameRate){
    frameRate = 60;
}

it just adds complexity and increases compile time. we can easily change this into a function when the time comes that something else needs to update when framerate changes.

# PHP
functions: snake_case
classes: PascalCase
files: snake_case

# C++
functions: camelCase
classes: PascalCase
files: camelCase

make all functions constexpr where possible. don't add overloads for arguments which can be combined into arguments for an existing overload.
setData(x,y) is redundant when we have setData(pos) because we can call setData({x,y})

use as short initializers as possible.
int exampleVal{};
glm::vec3 exampleVector{0,1,2};
do not fill in any template parameters when not necessary.

do not write expansive code when the same thing can be done with the same performance in less code:

int sampleInt = static_cast<int>(sampleFloat);
you can just write int sampleInt = (int)sampleFloat;

avoid using global variables. a source file can have global variables, but refrain from using those in other source files. you probably need to pass them in a function as (const) reference.

to exit an outer loop without needing to set data, just use a label and goto. it's one of the few cases where goto is valid.

# javascript
functions: camelCase
classes: PascalCase
files: snake_case

# any language

## proper function names
describe what it does. don't give any unnecessary information about inner workings or what it's needed for. you never know if it might be used for something else.
bad:
do not abbreviate names except if they're used a lot and it's obvious what it means.
bad:
for(int i = 0; i < 100; i++){}
good:
for(int cellIndex = 0; cellIndex < 100; cellIndex++){}

## use enums

int state = 0; is bad. use enums!

## DRY
