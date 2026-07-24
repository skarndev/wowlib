#pragma once

/** @file
    @brief Extern-template declarations for the FULL M2 version matrix.

    The library itself ships no explicit instantiations — C++ consumers
    implicitly instantiate only the versions they use. The bindings weld
    every targeted release, so they expand the whole matrix ONCE (in
    instantiations/m2.cpp) and every other binding TU includes this header
    to suppress its own re-instantiation. Driven by the same
    WOWLIB_M2_FOR_EACH_*VERSION X-macros the welded aliases use, so the
    matrix cannot drift from the version list. */

#include <wowlib/formats/m2/io.hpp>

namespace wowlib::formats::m2
{
#define WOWLIB_M2_EXTERN(Suffix, version_)                                                         \
  extern template struct body::M2Data<versions::version_>;                                         \
  extern template struct M2<versions::version_>;
  WOWLIB_M2_FOR_EACH_VERSION(WOWLIB_M2_EXTERN)
#undef WOWLIB_M2_EXTERN

#define WOWLIB_M2_SKIN_EXTERN(Suffix, version_)                                                    \
  extern template struct skin::Skin<versions::version_>;
  WOWLIB_M2_FOR_EACH_SKIN_VERSION(WOWLIB_M2_SKIN_EXTERN)
#undef WOWLIB_M2_SKIN_EXTERN

#define WOWLIB_M2_FILE_EXTERN(Suffix, version_)                                                    \
  extern template struct body::M2File<versions::version_>;                                         \
  extern template struct Skeleton<versions::version_>;
  WOWLIB_M2_FOR_EACH_CHUNKED_VERSION(WOWLIB_M2_FILE_EXTERN)
#undef WOWLIB_M2_FILE_EXTERN
}

namespace wowlib::formats
{
  // The OffsetFile/ChunkedFile bases carry the read()/write() definitions
  // that expand the serializers; extern-ing them here (an explicit
  // instantiation must sit in the template's enclosing namespace) confines
  // that expansion to instantiations/m2.cpp.
#define WOWLIB_M2_EXTERN_SERIALIZER(Suffix, version_)                                              \
  extern template struct OffsetFile<m2::body::M2Data<versions::version_>>;
  WOWLIB_M2_FOR_EACH_VERSION(WOWLIB_M2_EXTERN_SERIALIZER)
#undef WOWLIB_M2_EXTERN_SERIALIZER

#define WOWLIB_M2_EXTERN_SKIN_SERIALIZER(Suffix, version_)                                         \
  extern template struct OffsetFile<m2::skin::Skin<versions::version_>>;
  WOWLIB_M2_FOR_EACH_SKIN_VERSION(WOWLIB_M2_EXTERN_SKIN_SERIALIZER)
#undef WOWLIB_M2_EXTERN_SKIN_SERIALIZER

#define WOWLIB_M2_EXTERN_FILE_SERIALIZER(Suffix, version_)                                         \
  extern template struct ChunkedFile<m2::body::M2File<versions::version_>>;                        \
  extern template struct ChunkedFile<m2::Skeleton<versions::version_>>;
  WOWLIB_M2_FOR_EACH_CHUNKED_VERSION(WOWLIB_M2_EXTERN_FILE_SERIALIZER)
#undef WOWLIB_M2_EXTERN_FILE_SERIALIZER

  extern template struct ChunkedFile<m2::bone::BoneFile>;

  // The payload offset entities are welded (their whole read/write surface
  // binds), so every overload needs a definition even where the library only
  // exercises the context forms.
#define WOWLIB_M2_EXTERN_PAYLOAD_SERIALIZER(Suffix, version_)                                      \
  extern template struct OffsetFile<m2::SkelHeader<versions::version_>>;                           \
  extern template struct OffsetFile<m2::SkelSequences<versions::version_>>;                        \
  extern template struct OffsetFile<m2::SkelBones<versions::version_>>;                            \
  extern template struct OffsetFile<m2::SkelAttachments<versions::version_>>;                      \
  extern template struct OffsetFile<m2::body::records::Exp2Data<versions::version_>>;              \
  extern template struct OffsetFile<m2::body::records::PabcData<versions::version_>>;              \
  extern template struct OffsetFile<m2::body::records::PsbcData<versions::version_>>;              \
  extern template struct OffsetFile<m2::body::records::Pgd1Data<versions::version_>>;
  WOWLIB_M2_FOR_EACH_CHUNKED_VERSION(WOWLIB_M2_EXTERN_PAYLOAD_SERIALIZER)
#undef WOWLIB_M2_EXTERN_PAYLOAD_SERIALIZER
}
