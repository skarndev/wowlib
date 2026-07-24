/** @file
    @brief Explicit instantiation of the FULL M2 version matrix.

    The single translation unit that expands every M2 entity and its offset/
    chunk serializers for all targeted releases — kept alone here so it
    parallelizes against the welder walk and the facade TUs (which all
    include instantiations/m2.hpp and re-use these symbols instead of
    expanding their own). */

#include "instantiations/m2.hpp"

namespace wowlib::formats::m2
{
#define WOWLIB_M2_INSTANTIATE(Suffix, version_)                                                    \
  template struct body::M2Data<versions::version_>;                                                \
  template struct M2<versions::version_>;
  WOWLIB_M2_FOR_EACH_VERSION(WOWLIB_M2_INSTANTIATE)
#undef WOWLIB_M2_INSTANTIATE

#define WOWLIB_M2_INSTANTIATE_SKIN(Suffix, version_)                                               \
  template struct skin::Skin<versions::version_>;
  WOWLIB_M2_FOR_EACH_SKIN_VERSION(WOWLIB_M2_INSTANTIATE_SKIN)
#undef WOWLIB_M2_INSTANTIATE_SKIN

#define WOWLIB_M2_INSTANTIATE_FILE(Suffix, version_)                                               \
  template struct body::M2File<versions::version_>;                                                \
  template struct Skeleton<versions::version_>;
  WOWLIB_M2_FOR_EACH_CHUNKED_VERSION(WOWLIB_M2_INSTANTIATE_FILE)
#undef WOWLIB_M2_INSTANTIATE_FILE
}

namespace wowlib::formats
{
#define WOWLIB_M2_INSTANTIATE_SERIALIZER(Suffix, version_)                                         \
  template struct OffsetFile<m2::body::M2Data<versions::version_>>;
  WOWLIB_M2_FOR_EACH_VERSION(WOWLIB_M2_INSTANTIATE_SERIALIZER)
#undef WOWLIB_M2_INSTANTIATE_SERIALIZER

#define WOWLIB_M2_INSTANTIATE_SKIN_SERIALIZER(Suffix, version_)                                    \
  template struct OffsetFile<m2::skin::Skin<versions::version_>>;
  WOWLIB_M2_FOR_EACH_SKIN_VERSION(WOWLIB_M2_INSTANTIATE_SKIN_SERIALIZER)
#undef WOWLIB_M2_INSTANTIATE_SKIN_SERIALIZER

#define WOWLIB_M2_INSTANTIATE_FILE_SERIALIZER(Suffix, version_)                                    \
  template struct ChunkedFile<m2::body::M2File<versions::version_>>;                               \
  template struct ChunkedFile<m2::Skeleton<versions::version_>>;
  WOWLIB_M2_FOR_EACH_CHUNKED_VERSION(WOWLIB_M2_INSTANTIATE_FILE_SERIALIZER)
#undef WOWLIB_M2_INSTANTIATE_FILE_SERIALIZER

  template struct ChunkedFile<m2::bone::BoneFile>;

#define WOWLIB_M2_INSTANTIATE_PAYLOAD_SERIALIZER(Suffix, version_)                                 \
  template struct OffsetFile<m2::SkelHeader<versions::version_>>;                                  \
  template struct OffsetFile<m2::SkelSequences<versions::version_>>;                               \
  template struct OffsetFile<m2::SkelBones<versions::version_>>;                                   \
  template struct OffsetFile<m2::SkelAttachments<versions::version_>>;                             \
  template struct OffsetFile<m2::body::records::Exp2Data<versions::version_>>;                     \
  template struct OffsetFile<m2::body::records::PabcData<versions::version_>>;                     \
  template struct OffsetFile<m2::body::records::PsbcData<versions::version_>>;                     \
  template struct OffsetFile<m2::body::records::Pgd1Data<versions::version_>>;
  WOWLIB_M2_FOR_EACH_CHUNKED_VERSION(WOWLIB_M2_INSTANTIATE_PAYLOAD_SERIALIZER)
#undef WOWLIB_M2_INSTANTIATE_PAYLOAD_SERIALIZER
}
