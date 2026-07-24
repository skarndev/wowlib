/** @file
    @brief Explicit instantiation of the FULL M2 version matrix.

    The single translation unit that expands every M2 entity and its offset/
    chunk serializers for all targeted releases — kept alone here so it
    parallelizes against the welder walk and the facade TUs (which all
    include instantiations/m2.hpp and re-use these symbols instead of
    expanding their own). The extern declarations from the header are legal
    (and deliberate) before the definitions the same matrix expansion
    produces below. */

#include "instantiations/m2.hpp"

#define WOWLIB_INSTANTIATION_KEYWORD template
#include "instantiations/m2_matrix.inl"
#undef WOWLIB_INSTANTIATION_KEYWORD
