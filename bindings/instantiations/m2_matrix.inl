/** @file
    The M2 instantiation matrix — the single spelling of every entity and
    serializer instantiation for the family RANGE lists (one row per real
    content permutation, driven by the same WOWLIB_M2_RANGES_* X-macros the
    welded aliases use), expanded TWICE: instantiations/m2.hpp defines
    WOWLIB_INSTANTIATION_KEYWORD as `extern template` (declarations for every
    binding TU) and instantiations/m2.cpp as `template` (the one-TU
    definitions), so the two sides can never drift. Explicit instantiations
    must name the real templates (root::M2Root, chunked::M2ChunkedFile,
    the detail:: assemblies) — an alias template cannot head an explicit
    instantiation. Deliberately no include guard. */

namespace wowlib::formats::m2
{
#define WOWLIB_M2_DATA_ROW(Suffix, version_)                                                       \
  WOWLIB_INSTANTIATION_KEYWORD struct root::M2Root<versions::version_>;
  WOWLIB_M2_RANGES_DATA(WOWLIB_M2_DATA_ROW)
#undef WOWLIB_M2_DATA_ROW

#define WOWLIB_M2_ASSEMBLY_ROW(Suffix, version_)                                                   \
  WOWLIB_INSTANTIATION_KEYWORD struct detail::M2<versions::version_>;
  WOWLIB_M2_RANGES_ASSEMBLY(WOWLIB_M2_ASSEMBLY_ROW)
#undef WOWLIB_M2_ASSEMBLY_ROW

#define WOWLIB_M2_SKIN_ROW(Suffix, version_)                                                       \
  WOWLIB_INSTANTIATION_KEYWORD struct skin::detail::Skin<versions::version_>;
  WOWLIB_M2_RANGES_SKIN(WOWLIB_M2_SKIN_ROW)
#undef WOWLIB_M2_SKIN_ROW

#define WOWLIB_M2_FILE_ROW(Suffix, version_)                                                       \
  WOWLIB_INSTANTIATION_KEYWORD struct chunked::M2ChunkedFile<versions::version_>;
  WOWLIB_M2_RANGES_FILE(WOWLIB_M2_FILE_ROW)
#undef WOWLIB_M2_FILE_ROW

  // the skeleton collapses to a single chunked-era instantiation
#define WOWLIB_M2_SKELETON_ROW(Suffix, version_)                                                   \
  WOWLIB_INSTANTIATION_KEYWORD struct detail::Skeleton<versions::version_>;
  WOWLIB_M2_RANGES_CHUNK_PAYLOADS(WOWLIB_M2_SKELETON_ROW)
#undef WOWLIB_M2_SKELETON_ROW
}

namespace wowlib::formats
{
  // The M2OffsetBlock/ChunkedFile bases carry the read()/write() definitions
  // that expand the serializers; instantiating them here (an explicit
  // instantiation must sit in the template's enclosing namespace) confines
  // that expansion to instantiations/m2.cpp. The template ARGUMENTS may (and
  // do) go through the canonicalizing aliases.
#define WOWLIB_M2_SERIALIZER_ROW(Suffix, version_)                                                 \
  WOWLIB_INSTANTIATION_KEYWORD struct m2::M2OffsetBlock<m2::M2Root<versions::version_>>;
  WOWLIB_M2_RANGES_DATA(WOWLIB_M2_SERIALIZER_ROW)
#undef WOWLIB_M2_SERIALIZER_ROW

#define WOWLIB_M2_SKIN_SERIALIZER_ROW(Suffix, version_)                                            \
  WOWLIB_INSTANTIATION_KEYWORD struct m2::M2OffsetBlock<m2::skin::Skin<versions::version_>>;
  WOWLIB_M2_RANGES_SKIN(WOWLIB_M2_SKIN_SERIALIZER_ROW)
#undef WOWLIB_M2_SKIN_SERIALIZER_ROW

#define WOWLIB_M2_FILE_SERIALIZER_ROW(Suffix, version_)                                            \
  WOWLIB_INSTANTIATION_KEYWORD struct ChunkedFile<m2::M2ChunkedFile<versions::version_>>;
  WOWLIB_M2_RANGES_FILE(WOWLIB_M2_FILE_SERIALIZER_ROW)
#undef WOWLIB_M2_FILE_SERIALIZER_ROW

#define WOWLIB_M2_SKELETON_SERIALIZER_ROW(Suffix, version_)                                        \
  WOWLIB_INSTANTIATION_KEYWORD struct ChunkedFile<m2::Skeleton<versions::version_>>;
  WOWLIB_M2_RANGES_CHUNK_PAYLOADS(WOWLIB_M2_SKELETON_SERIALIZER_ROW)
#undef WOWLIB_M2_SKELETON_SERIALIZER_ROW

  WOWLIB_INSTANTIATION_KEYWORD struct ChunkedFile<m2::bone::BoneFile>;

  // The payload offset entities are welded (their whole read/write surface
  // binds), so every overload needs a definition even where the library only
  // exercises the context forms.
#define WOWLIB_M2_PAYLOAD_SERIALIZER_ROW(Suffix, version_)                                         \
  WOWLIB_INSTANTIATION_KEYWORD struct m2::M2OffsetBlock<m2::SkelHeader<versions::version_>>;              \
  WOWLIB_INSTANTIATION_KEYWORD struct m2::M2OffsetBlock<m2::SkelSequences<versions::version_>>;           \
  WOWLIB_INSTANTIATION_KEYWORD struct m2::M2OffsetBlock<m2::SkelBones<versions::version_>>;               \
  WOWLIB_INSTANTIATION_KEYWORD struct m2::M2OffsetBlock<m2::SkelAttachments<versions::version_>>;
  WOWLIB_M2_RANGES_CHUNK_PAYLOADS(WOWLIB_M2_PAYLOAD_SERIALIZER_ROW)
#undef WOWLIB_M2_PAYLOAD_SERIALIZER_ROW

  // The shell payloads are non-templated (fixed WotLK+ layout) — one each.
  WOWLIB_INSTANTIATION_KEYWORD struct m2::M2OffsetBlock<m2::chunked::record::Exp2Data>;
  WOWLIB_INSTANTIATION_KEYWORD struct m2::M2OffsetBlock<m2::chunked::record::PabcData>;
  WOWLIB_INSTANTIATION_KEYWORD struct m2::M2OffsetBlock<m2::chunked::record::PsbcData>;
  WOWLIB_INSTANTIATION_KEYWORD struct m2::M2OffsetBlock<m2::chunked::record::Pgd1Data>;
}
