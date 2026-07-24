/** @file
    @brief Explicit instantiation of the FULL WMO version matrix.

    The single translation unit that expands every WMO entity and its chunk
    serializer for all targeted releases — the heaviest compile in the
    bindings, kept alone here so it parallelizes against the welder walk and
    the facade TUs (which all include instantiations/wmo.hpp and re-use these
    symbols instead of expanding their own). The extern declarations from the
    header are legal (and deliberate) before the definitions the same matrix
    expansion produces below. */

#include "instantiations/wmo.hpp"

#define WOWLIB_INSTANTIATION_KEYWORD template
#include "instantiations/wmo_matrix.inl"
#undef WOWLIB_INSTANTIATION_KEYWORD
