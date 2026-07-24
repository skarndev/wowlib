/** @file
    @brief Explicit instantiation of the FULL WMO version matrix.

    The single translation unit that expands every WMO entity and its chunk
    serializer for all targeted releases — the heaviest compile in the
    bindings, kept alone here so it parallelizes against the welder walk and
    the facade TUs (which all include instantiations/wmo.hpp and re-use these
    symbols instead of expanding their own). */

#include "instantiations/wmo.hpp"

namespace wowlib::formats::wmo
{
#define WOWLIB_WMO_INSTANTIATE(Suffix, version_)                                                   \
  template struct root::WMORoot<versions::version_>;                                               \
  template struct group::WMOGroupBody<versions::version_>;                                         \
  template struct group::WMOGroup<versions::version_>;                                             \
  template struct WMO<versions::version_>;

  WOWLIB_WMO_FOR_EACH_VERSION(WOWLIB_WMO_INSTANTIATE)
#undef WOWLIB_WMO_INSTANTIATE
}

namespace wowlib::formats
{
#define WOWLIB_WMO_INSTANTIATE_SERIALIZER(Suffix, version_)                                        \
  template struct ChunkedFile<wmo::root::WMORoot<versions::version_>>;                             \
  template struct ChunkedFile<wmo::group::WMOGroupBody<versions::version_>>;                       \
  template struct ChunkedFile<wmo::group::WMOGroup<versions::version_>>;

  WOWLIB_WMO_FOR_EACH_VERSION(WOWLIB_WMO_INSTANTIATE_SERIALIZER)
#undef WOWLIB_WMO_INSTANTIATE_SERIALIZER
}
