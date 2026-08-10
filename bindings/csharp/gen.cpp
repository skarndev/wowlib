/** @file
    @brief The C#/.NET bindings generator: reflects namespace `wowlib` and emits
    the native shim + the managed wrapper.

    One macro line — `welder_csharp_generate_bindings()` (CMake) builds this
    executable, runs it into `shim.cpp` + `Bindings.cs`, and compiles the shim
    into `libwowlib_native.{so,dylib}` / `wowlib_native.dll`, which the generated
    `[LibraryImport]` declarations P/Invoke. No .NET is needed to BUILD any of
    this; a `dotnet` SDK is needed only to consume the emitted `Bindings.cs`. */

#include "surface.hpp"

#include <welder/rods/csharp/module.hpp>

WELDER_CSHARP_MAIN(wowlib, "surface.hpp", "wowlib_native")
