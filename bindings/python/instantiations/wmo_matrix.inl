/** @file
    The WMO instantiation matrix — the single spelling of every entity and
    serializer instantiation for the family RANGE lists (one row per real
    content permutation, driven by the same WOWLIB_WMO_RANGES_* X-macros the
    welded aliases use), expanded TWICE: instantiations/wmo.hpp defines
    WOWLIB_INSTANTIATION_KEYWORD as `extern template` (declarations for every
    binding TU) and instantiations/wmo.cpp as `template` (the one-TU
    definitions), so the two sides can never drift. Explicit instantiations
    must name the real detail:: templates — an alias template cannot head an
    explicit instantiation. Deliberately no include guard. */

namespace wowlib::formats::wmo
{
#define WOWLIB_WMO_ROOT_ROW(Suffix, version_)                                                      \
  WOWLIB_INSTANTIATION_KEYWORD struct root::detail::WMORoot<versions::version_>;
  WOWLIB_WMO_RANGES_ROOT(WOWLIB_WMO_ROOT_ROW)
#undef WOWLIB_WMO_ROOT_ROW

#define WOWLIB_WMO_GROUP_ROW(Suffix, version_)                                                     \
  WOWLIB_INSTANTIATION_KEYWORD struct group::detail::WMOGroupBody<versions::version_>;             \
  WOWLIB_INSTANTIATION_KEYWORD struct group::detail::WMOGroup<versions::version_>;
  WOWLIB_WMO_RANGES_GROUP(WOWLIB_WMO_GROUP_ROW)
#undef WOWLIB_WMO_GROUP_ROW

#define WOWLIB_WMO_ROW(Suffix, version_)                                                           \
  WOWLIB_INSTANTIATION_KEYWORD struct detail::WMO<versions::version_>;
  WOWLIB_WMO_RANGES_ASSEMBLY(WOWLIB_WMO_ROW)
#undef WOWLIB_WMO_ROW
}

namespace wowlib::formats
{
  // The ChunkedFile bases carry the read()/write() definitions that expand the
  // chunk serializer; instantiating them here (an explicit instantiation must
  // sit in the template's enclosing namespace) confines that expansion to
  // instantiations/wmo.cpp. The template ARGUMENTS go through the
  // canonicalizing aliases.
#define WOWLIB_WMO_ROOT_SERIALIZER_ROW(Suffix, version_)                                           \
  WOWLIB_INSTANTIATION_KEYWORD struct ChunkedFile<wmo::root::WMORoot<versions::version_>>;
  WOWLIB_WMO_RANGES_ROOT(WOWLIB_WMO_ROOT_SERIALIZER_ROW)
#undef WOWLIB_WMO_ROOT_SERIALIZER_ROW

#define WOWLIB_WMO_GROUP_SERIALIZER_ROW(Suffix, version_)                                          \
  WOWLIB_INSTANTIATION_KEYWORD struct ChunkedFile<wmo::group::WMOGroupBody<versions::version_>>;   \
  WOWLIB_INSTANTIATION_KEYWORD struct ChunkedFile<wmo::group::WMOGroup<versions::version_>>;
  WOWLIB_WMO_RANGES_GROUP(WOWLIB_WMO_GROUP_SERIALIZER_ROW)
#undef WOWLIB_WMO_GROUP_SERIALIZER_ROW
}
