/** @file
    @brief Explicit instantiation of the FULL ADT version matrix.

    The single translation unit that expands every ADT and MapChunk entity (and
    their inline fs/slice serializers) for all targeted releases, kept alone so
    it parallelizes against the welder walk and the facade TUs (which include
    instantiations/adt.hpp and re-use these symbols). */

#include "instantiations/adt.hpp"

#define WOWLIB_INSTANTIATION_KEYWORD template
#include "instantiations/adt_matrix.inl"
#undef WOWLIB_INSTANTIATION_KEYWORD
