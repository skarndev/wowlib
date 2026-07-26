/** @file
    The WDL instantiation matrix — the single spelling of the entity and
    serializer instantiations for the WDL RANGE list, expanded TWICE:
    instantiations/wdl.hpp as `extern template` declarations and
    instantiations/wdl.cpp as the one-TU definitions (see wdt_matrix.inl).
    Deliberately no include guard. */

namespace wowlib::formats::wdl
{
#define WOWLIB_WDL_ROW(Suffix, version_)                                                           \
  WOWLIB_INSTANTIATION_KEYWORD struct detail::WDL<versions::version_>;
  WOWLIB_WDL_RANGES(WOWLIB_WDL_ROW)
#undef WOWLIB_WDL_ROW
}

namespace wowlib::formats
{
#define WOWLIB_WDL_SERIALIZER_ROW(Suffix, version_)                                                \
  WOWLIB_INSTANTIATION_KEYWORD struct ChunkedFile<wdl::WDL<versions::version_>>;
  WOWLIB_WDL_RANGES(WOWLIB_WDL_SERIALIZER_ROW)
#undef WOWLIB_WDL_SERIALIZER_ROW
}
