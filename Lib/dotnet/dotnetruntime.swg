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
%insert(runtime) "dotnetrun.swg";

%insert(initbeforefunc) "swiginit.swg"
%insert(initbeforefunc) "dotnetapi.swg"

/* C# side of the runtime, spliced into the generated .cs */
%insert(csruntime) "csrun.cs"
