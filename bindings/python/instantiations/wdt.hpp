#pragma once

/** @file
    @brief Extern-template declarations for the FULL WDT version matrix.

    The library itself ships no explicit instantiations — C++ consumers
    implicitly instantiate only the versions they use. The bindings weld
    every targeted release, so they expand the whole matrix ONCE (in
    instantiations/wdt.cpp) and every other binding TU includes this header
    to suppress its own re-instantiation. See wmo.hpp for the pattern. */

#include <wowlib/formats/wdt/wdt.hpp>

#include "instantiations/wdt_ranges.hpp"

#define WOWLIB_INSTANTIATION_KEYWORD extern template
#include "instantiations/wdt_matrix.inl"
#undef WOWLIB_INSTANTIATION_KEYWORD
