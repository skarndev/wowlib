#pragma once

#include <wowlib/core/lang.hpp>

namespace wowlib::formats
{
  /** The version-agnostic root of every file-level entity (welded as
      "FileEntity").

      Like the per-format `*Base` classes, this empty base exists ENTIRELY for
      the language bindings: it gives every file entity — the six versioned
      family bases (WMO, M2, Skeleton, ADT, WDT, WDL) and the unversioned BLP —
      one common welded supertype, so binding users can hold a heterogeneous
      collection and run the contract every entity shares
      (`Validate`/`EnsureValid`; the fs `Read`/`Write` pair stays on the family
      bases, because ADT's genuinely takes an extra alpha-format argument).
      In C# the family-surface synthesis derives that shared contract
      automatically — it hoists exactly the members every child hierarchy ends
      up binding identically. It has no role in the C++ API, where the
      statically-typed entities are used directly.

      @see https://wowdev.wiki/Main_Page */
  struct [[
    =welder::weld,
    =welder::weld_as("FileEntity"),
    WOWLIB_CS_FAMILY_SURFACE
    =welder::doc(R"(
        The version-agnostic root of every file-level entity: the WMO, M2,
        Skeleton, ADT, WDT and WDL family bases and the BLP all derive it.
        Holds the contract every entity shares (validate / ensure_valid), so a
        heterogeneous collection of loaded files can be processed uniformly;
        reading and writing stay on the per-format types, whose signatures
        differ.)")
  ]] FileEntityBase
  {
  };
}
