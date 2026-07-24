/** @file
    The WMO instantiation matrix — the single spelling of every entity and
    serializer instantiation for the FULL version list, expanded TWICE:
    instantiations/wmo.hpp defines WOWLIB_INSTANTIATION_KEYWORD as
    `extern template` (declarations for every binding TU) and
    instantiations/wmo.cpp as `template` (the one-TU definitions), so the two
    sides can never drift — an entity added here is simultaneously declared
    and defined. Deliberately no include guard. */

namespace wowlib::formats::wmo::root
{
#define WOWLIB_WMO_ROOT_ROW(Suffix, version_)                                                      \
  WOWLIB_INSTANTIATION_KEYWORD struct WMORoot<versions::version_>;
  WOWLIB_WMO_FOR_EACH_VERSION(WOWLIB_WMO_ROOT_ROW)
#undef WOWLIB_WMO_ROOT_ROW
}

namespace wowlib::formats::wmo::group
{
#define WOWLIB_WMO_GROUP_ROW(Suffix, version_)                                                     \
  WOWLIB_INSTANTIATION_KEYWORD struct WMOGroupBody<versions::version_>;                            \
  WOWLIB_INSTANTIATION_KEYWORD struct WMOGroup<versions::version_>;
  WOWLIB_WMO_FOR_EACH_VERSION(WOWLIB_WMO_GROUP_ROW)
#undef WOWLIB_WMO_GROUP_ROW
}

namespace wowlib::formats::wmo
{
#define WOWLIB_WMO_ROW(Suffix, version_)                                                           \
  WOWLIB_INSTANTIATION_KEYWORD struct WMO<versions::version_>;
  WOWLIB_WMO_FOR_EACH_VERSION(WOWLIB_WMO_ROW)
#undef WOWLIB_WMO_ROW
}

namespace wowlib::formats
{
  // The ChunkedFile bases carry the read()/write() definitions that expand the
  // chunk serializer; instantiating them here (an explicit instantiation must
  // sit in the template's enclosing namespace) confines that expansion to
  // instantiations/wmo.cpp.
#define WOWLIB_WMO_SERIALIZER_ROW(Suffix, version_)                                                \
  WOWLIB_INSTANTIATION_KEYWORD struct ChunkedFile<wmo::root::WMORoot<versions::version_>>;         \
  WOWLIB_INSTANTIATION_KEYWORD struct ChunkedFile<wmo::group::WMOGroupBody<versions::version_>>;   \
  WOWLIB_INSTANTIATION_KEYWORD struct ChunkedFile<wmo::group::WMOGroup<versions::version_>>;
  WOWLIB_WMO_FOR_EACH_VERSION(WOWLIB_WMO_SERIALIZER_ROW)
#undef WOWLIB_WMO_SERIALIZER_ROW
}
