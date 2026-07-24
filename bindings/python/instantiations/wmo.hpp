#pragma once

/** @file
    @brief Extern-template declarations for the FULL WMO version matrix.

    The library itself ships no explicit instantiations — C++ consumers
    implicitly instantiate only the versions they use. The bindings weld
    every targeted release, so they expand the whole matrix ONCE (in
    instantiations/wmo.cpp) and every other binding TU includes this header
    to suppress its own re-instantiation. Driven by the same
    WOWLIB_WMO_FOR_EACH_VERSION X-macro the welded aliases use, so the
    matrix cannot drift from the version list. */

#include <wowlib/formats/wmo/io.hpp>

namespace wowlib::formats::wmo::root
{
#define WOWLIB_WMO_ROOT_EXTERN(Suffix, version_) extern template struct WMORoot<versions::version_>;
  WOWLIB_WMO_FOR_EACH_VERSION(WOWLIB_WMO_ROOT_EXTERN)
#undef WOWLIB_WMO_ROOT_EXTERN
}

namespace wowlib::formats::wmo::group
{
#define WOWLIB_WMO_GROUP_EXTERN(Suffix, version_)                                                  \
  extern template struct WMOGroupBody<versions::version_>;                                         \
  extern template struct WMOGroup<versions::version_>;
  WOWLIB_WMO_FOR_EACH_VERSION(WOWLIB_WMO_GROUP_EXTERN)
#undef WOWLIB_WMO_GROUP_EXTERN
}

namespace wowlib::formats::wmo
{
#define WOWLIB_WMO_EXTERN(Suffix, version_) extern template struct WMO<versions::version_>;
  WOWLIB_WMO_FOR_EACH_VERSION(WOWLIB_WMO_EXTERN)
#undef WOWLIB_WMO_EXTERN
}

namespace wowlib::formats
{
  // The ChunkedFile bases carry the read()/write() definitions that expand the
  // chunk serializer; extern-ing them here (an explicit instantiation must sit
  // in the template's enclosing namespace) confines that expansion to
  // instantiations/wmo.cpp.
#define WOWLIB_WMO_EXTERN_SERIALIZER(Suffix, version_)                                             \
  extern template struct ChunkedFile<wmo::root::WMORoot<versions::version_>>;                      \
  extern template struct ChunkedFile<wmo::group::WMOGroupBody<versions::version_>>;                \
  extern template struct ChunkedFile<wmo::group::WMOGroup<versions::version_>>;

  WOWLIB_WMO_FOR_EACH_VERSION(WOWLIB_WMO_EXTERN_SERIALIZER)
#undef WOWLIB_WMO_EXTERN_SERIALIZER
}
