/** @file
    The WDT instantiation matrix — the single spelling of every entity and
    serializer instantiation for the family RANGE lists (one row per real
    content permutation, driven by the same WOWLIB_WDT_RANGES_* X-macros the
    welded aliases use), expanded TWICE: instantiations/wdt.hpp defines
    WOWLIB_INSTANTIATION_KEYWORD as `extern template` (declarations for every
    binding TU) and instantiations/wdt.cpp as `template` (the one-TU
    definitions), so the two sides can never drift. Explicit instantiations
    must name the real detail:: templates — an alias template cannot head an
    explicit instantiation. Deliberately no include guard. */

namespace wowlib::formats::wdt
{
#define WOWLIB_WDT_ROOT_ROW(Suffix, version_)                                                      \
  WOWLIB_INSTANTIATION_KEYWORD struct root::detail::WDTRoot<versions::version_>;
  WOWLIB_WDT_RANGES_ROOT(WOWLIB_WDT_ROOT_ROW)
#undef WOWLIB_WDT_ROOT_ROW

#define WOWLIB_WDT_OCCLUSION_ROW(Suffix, version_)                                                 \
  WOWLIB_INSTANTIATION_KEYWORD struct occlusion::detail::WDTOcclusion<versions::version_>;
  WOWLIB_WDT_RANGES_OCCLUSION(WOWLIB_WDT_OCCLUSION_ROW)
#undef WOWLIB_WDT_OCCLUSION_ROW

#define WOWLIB_WDT_LIGHTS_ROW(Suffix, version_)                                                    \
  WOWLIB_INSTANTIATION_KEYWORD struct lights::detail::WDTLights<versions::version_>;
  WOWLIB_WDT_RANGES_LIGHTS(WOWLIB_WDT_LIGHTS_ROW)
#undef WOWLIB_WDT_LIGHTS_ROW

#define WOWLIB_WDT_FOGS_ROW(Suffix, version_)                                                      \
  WOWLIB_INSTANTIATION_KEYWORD struct fogs::detail::WDTFogs<versions::version_>;
  WOWLIB_WDT_RANGES_FOGS(WOWLIB_WDT_FOGS_ROW)
#undef WOWLIB_WDT_FOGS_ROW

#define WOWLIB_WDT_MPV_ROW(Suffix, version_)                                                       \
  WOWLIB_INSTANTIATION_KEYWORD struct mpv::detail::WDTParticulates<versions::version_>;
  WOWLIB_WDT_RANGES_MPV(WOWLIB_WDT_MPV_ROW)
#undef WOWLIB_WDT_MPV_ROW

#define WOWLIB_WDT_ROW(Suffix, version_)                                                           \
  WOWLIB_INSTANTIATION_KEYWORD struct detail::WDT<versions::version_>;
  WOWLIB_WDT_RANGES_ASSEMBLY(WOWLIB_WDT_ROW)
#undef WOWLIB_WDT_ROW
}

namespace wowlib::formats
{
  // The ChunkedFile bases carry the read()/write() definitions that expand the
  // chunk serializer; instantiating them here (an explicit instantiation must
  // sit in the template's enclosing namespace) confines that expansion to
  // instantiations/wdt.cpp. The template ARGUMENTS go through the
  // canonicalizing aliases.
#define WOWLIB_WDT_ROOT_SERIALIZER_ROW(Suffix, version_)                                           \
  WOWLIB_INSTANTIATION_KEYWORD struct ChunkedFile<wdt::root::WDTRoot<versions::version_>>;
  WOWLIB_WDT_RANGES_ROOT(WOWLIB_WDT_ROOT_SERIALIZER_ROW)
#undef WOWLIB_WDT_ROOT_SERIALIZER_ROW

#define WOWLIB_WDT_OCCLUSION_SERIALIZER_ROW(Suffix, version_)                                      \
  WOWLIB_INSTANTIATION_KEYWORD struct ChunkedFile<wdt::occlusion::WDTOcclusion<versions::version_>>;
  WOWLIB_WDT_RANGES_OCCLUSION(WOWLIB_WDT_OCCLUSION_SERIALIZER_ROW)
#undef WOWLIB_WDT_OCCLUSION_SERIALIZER_ROW

#define WOWLIB_WDT_LIGHTS_SERIALIZER_ROW(Suffix, version_)                                         \
  WOWLIB_INSTANTIATION_KEYWORD struct ChunkedFile<wdt::lights::WDTLights<versions::version_>>;
  WOWLIB_WDT_RANGES_LIGHTS(WOWLIB_WDT_LIGHTS_SERIALIZER_ROW)
#undef WOWLIB_WDT_LIGHTS_SERIALIZER_ROW

#define WOWLIB_WDT_FOGS_SERIALIZER_ROW(Suffix, version_)                                           \
  WOWLIB_INSTANTIATION_KEYWORD struct ChunkedFile<wdt::fogs::WDTFogs<versions::version_>>;
  WOWLIB_WDT_RANGES_FOGS(WOWLIB_WDT_FOGS_SERIALIZER_ROW)
#undef WOWLIB_WDT_FOGS_SERIALIZER_ROW

#define WOWLIB_WDT_MPV_SERIALIZER_ROW(Suffix, version_)                                            \
  WOWLIB_INSTANTIATION_KEYWORD struct ChunkedFile<wdt::mpv::WDTParticulates<versions::version_>>;
  WOWLIB_WDT_RANGES_MPV(WOWLIB_WDT_MPV_SERIALIZER_ROW)
#undef WOWLIB_WDT_MPV_SERIALIZER_ROW
}
