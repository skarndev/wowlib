#pragma once

/** @file
    @brief Extern-template declarations for the FULL M2 version matrix.

    The library itself ships no explicit instantiations — C++ consumers
    implicitly instantiate only the versions they use. The bindings weld
    every targeted release, so they expand the whole matrix ONCE (in
    instantiations/m2.cpp) and every other binding TU includes this header
    to suppress its own re-instantiation. The matrix rows live in
    m2_matrix.inl, expanded here as declarations and in the .cpp as
    definitions from the SAME spelling, driven by the same
    WOWLIB_M2_FOR_EACH_*VERSION x-macros the welded aliases use — neither
    the two sides nor the version list can drift. */

#include <wowlib/formats/m2/m2.hpp>

#include "instantiations/m2_ranges.hpp"

#define WOWLIB_INSTANTIATION_KEYWORD extern template
#include "instantiations/m2_matrix.inl"
#undef WOWLIB_INSTANTIATION_KEYWORD
