/** @file
    @brief Explicit instantiation of the FULL WDT version matrix.

    The single translation unit that expands every WDT entity and its chunk
    serializer for all targeted releases, kept alone so it parallelizes
    against the welder walk and the facade TUs (which include
    instantiations/wdt.hpp and re-use these symbols). */

#include "instantiations/wdt.hpp"

#define WOWLIB_INSTANTIATION_KEYWORD template
#include "instantiations/wdt_matrix.inl"
#undef WOWLIB_INSTANTIATION_KEYWORD
