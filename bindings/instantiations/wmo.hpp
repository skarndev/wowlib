#pragma once

/** @file
    @brief Extern-template declarations for the FULL WMO version matrix.

    The library itself ships no explicit instantiations — C++ consumers
    implicitly instantiate only the versions they use. The bindings weld
    every targeted release, so they expand the whole matrix ONCE (in
    instantiations/wmo.cpp) and every other binding TU includes this header
    to suppress its own re-instantiation. The matrix rows live in
    wmo_matrix.inl, expanded here as declarations and in the .cpp as
    definitions from the SAME spelling, driven by the same
    WOWLIB_WMO_FOR_EACH_VERSION x-macro the welded aliases use — neither
    the two sides nor the version list can drift. */

#include <wowlib/formats/wmo/wmo.hpp>

#include "instantiations/wmo_ranges.hpp"

#define WOWLIB_INSTANTIATION_KEYWORD extern template
#include "instantiations/wmo_matrix.inl"
#undef WOWLIB_INSTANTIATION_KEYWORD
