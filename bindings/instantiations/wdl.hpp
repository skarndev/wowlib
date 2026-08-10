#pragma once

/** @file
    @brief Extern-template declarations for the FULL WDL version matrix.

    See wmo.hpp for the pattern: the matrix expands once in
    instantiations/wdl.cpp; every other binding TU includes this header to
    suppress its own re-instantiation. */

#include <wowlib/formats/wdl/wdl.hpp>

#include "instantiations/wdl_ranges.hpp"

#define WOWLIB_INSTANTIATION_KEYWORD extern template
#include "instantiations/wdl_matrix.inl"
#undef WOWLIB_INSTANTIATION_KEYWORD
