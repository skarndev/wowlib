#pragma once

#include <wowlib/core/lang.hpp>
#include <wowlib/wowlib.hpp>

/** @file
    @brief The wowlib namespaces' C# spellings, where the style's default is
    not the wanted name.

    The dotnet name style PascalCases a namespace identifier, which coerces
    `formats::wmo` into `Formats.Wmo` (the C# surface wants the acronyms
    uppercase, `Formats.WMO`) and leaves the terse core namespaces terse
    (`Db`/`Fs` — the C# surface spells them out, `Database`/`Filesystem`).
    welder reads a namespace's `weld_as` through ANY of its declarations, so
    these annotated reopenings carry the lang-scoped names — included only by
    the C# generator's main TU (the namespace WALK is what consumes naming;
    the contributor TUs spell their `at()` paths explicitly, and Python/Lua
    builds never include this header, so their module names stay snake_case).

    `m2` needs no entry: the style already yields `M2`. Keep this list, the
    contributor TUs' `at()` paths, tools/gen_cs_format_facades.py's FAMILIES
    map and dbdgen's facade namespace in sync. (The annotation must sit on a
    reopening, not a nested-namespace-definition — C++ grammar allows
    attributes only on the single-name form.) */

namespace wowlib
{
  namespace [[=welder::weld_as(wowlib::lang::cs, "Database")]] db
  {
  }
  namespace [[=welder::weld_as(wowlib::lang::cs, "Filesystem")]] fs
  {
  }
}

namespace wowlib::formats
{
  namespace [[=welder::weld_as(wowlib::lang::cs, "WMO")]] wmo
  {
  }
  namespace [[=welder::weld_as(wowlib::lang::cs, "ADT")]] adt
  {
  }
  namespace [[=welder::weld_as(wowlib::lang::cs, "WDT")]] wdt
  {
  }
  namespace [[=welder::weld_as(wowlib::lang::cs, "WDL")]] wdl
  {
  }
  namespace [[=welder::weld_as(wowlib::lang::cs, "BLP")]] blp
  {
  }
}
