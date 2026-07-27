/** @file
    The ADT instantiation matrix — the single spelling of every entity
    instantiation for the family RANGE lists (one row per real content
    permutation, driven by the WOWLIB_ADT_RANGES_* X-macros the welded aliases
    use), expanded TWICE: instantiations/adt.hpp defines
    WOWLIB_INSTANTIATION_KEYWORD as `extern template` (declarations for every
    binding TU) and instantiations/adt.cpp as `template` (the one-TU
    definitions), so the two sides can never drift.

    ADT and MapChunk are NOT ChunkedFile/M2OffsetBlock entities — they own bespoke
    serializers — so there is no separate serializer-base row: an explicit
    instantiation of the entity expands its inline fs read/write with it. The
    MH2OData/MCLQData liquid entities are non-templated (one definition each,
    reached through the entities). Explicit instantiations name the real
    detail:: templates — an alias template cannot head one. No include guard. */

namespace wowlib::formats::adt
{
#define WOWLIB_ADT_MAPCHUNK_ROW(Suffix, version_)                                                  \
  WOWLIB_INSTANTIATION_KEYWORD struct detail::MapChunk<versions::version_>;
  WOWLIB_ADT_RANGES_MAPCHUNK(WOWLIB_ADT_MAPCHUNK_ROW)
#undef WOWLIB_ADT_MAPCHUNK_ROW

#define WOWLIB_ADT_ASSEMBLY_ROW(Suffix, version_)                                                  \
  WOWLIB_INSTANTIATION_KEYWORD struct detail::ADT<versions::version_>;
  WOWLIB_ADT_RANGES_ASSEMBLY(WOWLIB_ADT_ASSEMBLY_ROW)
#undef WOWLIB_ADT_ASSEMBLY_ROW
}
