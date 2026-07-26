#pragma once

/** @file
    @brief Extern-template declarations for the FULL ADT version matrix.

    The library ships no explicit instantiations — C++ consumers implicitly
    instantiate only the versions they use. The bindings weld every targeted
    release, so they expand the whole matrix ONCE (in instantiations/adt.cpp)
    and every other binding TU includes this header to suppress its own
    re-instantiation. See wdt.hpp for the pattern. */

#include <wowlib/formats/adt/adt.hpp>

#include "instantiations/adt_ranges.hpp"

#define WOWLIB_INSTANTIATION_KEYWORD extern template
#include "instantiations/adt_matrix.inl"
#undef WOWLIB_INSTANTIATION_KEYWORD
