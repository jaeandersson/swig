%insert(runtime) %{
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <limits.h>
#include <string>
#include <vector>
%}

%insert(runtime) "swigrun.swg";
%insert(runtime) "swigerrors.swg";
%insert(runtime) "rustrun.swg";

%insert(initbeforefunc) "swiginit.swg"
%insert(initbeforefunc) "rustapi.swg"

/* Rust side of the runtime, spliced into the generated .rs */
%insert(rustruntime) "rustrun.rs"
