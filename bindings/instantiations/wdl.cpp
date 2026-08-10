/** @file
    @brief Explicit instantiation of the FULL WDL version matrix.

    The single translation unit that expands the WDL entity and its chunk
    serializer for all targeted releases (see wdt.cpp). */

#include "instantiations/wdl.hpp"

#define WOWLIB_INSTANTIATION_KEYWORD template
#include "instantiations/wdl_matrix.inl"
#undef WOWLIB_INSTANTIATION_KEYWORD
