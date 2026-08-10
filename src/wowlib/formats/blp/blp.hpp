#pragma once

/** @file
    The BLP entity (namespace wowlib::formats::blp): Blizzard's texture format.

    A BLP2 file is NOT chunked: a fixed 148-byte header + 1024-byte palette
    (0x494 bytes total) is followed by up to 16 mipmap payloads located by the
    header's offset/size tables. The pixel payload is palettized (256-color
    indices + a separate 0/1/4/8-bit alpha plane), DXT-compressed (BC1/BC2/BC3,
    and BC5 in later clients), or raw BGRA. The format is version-stable across
    every WoW release — vanilla through The War Within read the same layout —
    so unlike the chunked formats the entity carries no ClientVersion axis.

    wowlib stores the mip payloads verbatim (byte-perfect round-trip: the
    original mip placement, inter-mip gaps and trailing bytes are preserved and
    replayed while the payloads are unmodified) and moves pixels through the
    codec classes in codec.hpp: decode() produces a top-left row-major RGBA8
    Image from any stored level, encode() rebuilds the whole file — palette
    quantization, DXT compression (stb_dxt) and the mip chain — from one.

    @see https://wowdev.wiki/BLP */

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/formats/common/fourcc.hpp>
#include <wowlib/formats/common/types.hpp>
#include <wowlib/formats/common/validation.hpp>
#include <wowlib/fs/filesystem.hpp>

namespace wowlib::formats::blp
{
  /** The BLP2 magic: the literal bytes "BLP2" (a forward FourCC, unlike the
      reversed chunk ids). */
  inline constexpr std::uint32_t blp_magic = four_cc("BLP2", FourCCEndian::forward);
  /** The BLP2 header version field; always 1 in every shipped client. */
  inline constexpr std::uint32_t blp_version_1 = 1;
  /** The mip table capacity: a BLP addresses at most 16 levels. */
  inline constexpr std::size_t blp_max_mips = 16;
  /** The palette entry count (one byte-sized index space). */
  inline constexpr std::size_t blp_palette_size = 256;
  /** The full header region: 148-byte header + 1024-byte palette. */
  inline constexpr std::size_t blp_header_bytes = 0x494;

  enum class [[
    =welder::weld,
    =welder::doc("How a BLP's pixel payload is encoded (the header's "
                 "colorEncoding byte).")
  ]] ColorEncoding : std::uint8_t
  {
    Jpeg [[=welder::doc("JPEG-compressed content (Warcraft III heritage; never "
                        "shipped in WoW clients — wowlib preserves but cannot "
                        "decode it).")]] = 0,
    Palettized [[=welder::doc("256-color palette indices, one byte per pixel, "
                              "followed by a separate alpha plane of "
                              "alpha_depth bits per pixel.")]] = 1,
    Dxt [[=welder::doc("DXT/S3TC block compression; the variant (BC1/BC2/BC3/"
                       "BC5) follows from preferred_format and "
                       "alpha_depth.")]] = 2,
    Bgra [[=welder::doc("Raw 32-bit BGRA pixels (Cataclysm+; terrain cube "
                        "maps).")]] = 3,
    BgraAlt [[=welder::doc("Raw 32-bit BGRA under a different client-side "
                           "PIXEL_FORMAT; identical file content to "
                           "Bgra.")]] = 4,
  };

  enum class [[
    =welder::weld,
    =welder::doc("The client-side pixel format hint (the header's "
                 "preferredFormat byte). For DXT-encoded files it selects the "
                 "block format: Dxt1 -> BC1, Dxt3 -> BC2, Dxt5 -> BC3, "
                 "Bc5 -> BC5.")
  ]] PixelFormat : std::uint8_t
  {
    Dxt1 [[=welder::doc("BC1: 8-byte blocks, optional 1-bit punch-through "
                        "alpha.")]] = 0,
    Dxt3 [[=welder::doc("BC2: 16-byte blocks, explicit 4-bit alpha.")]] = 1,
    Argb8888 [[=welder::doc("Raw 32-bit upload hint (used by Bgra-encoded "
                            "files).")]] = 2,
    Argb1555 [[=welder::doc("16-bit 1555 upload hint; never a file "
                            "content layout.")]] = 3,
    Argb4444 [[=welder::doc("16-bit 4444 upload hint; never a file "
                            "content layout.")]] = 4,
    Rgb565 [[=welder::doc("16-bit 565 upload hint; never a file content "
                          "layout.")]] = 5,
    A8 [[=welder::doc("Alpha-only upload hint; never a file content "
                      "layout.")]] = 6,
    Dxt5 [[=welder::doc("BC3: 16-byte blocks, interpolated 8-bit alpha.")]] = 7,
    Unspecified [[=welder::doc("No preference recorded (typical for palettized "
                               "files).")]] = 8,
    Argb2565 [[=welder::doc("Component-texture upload hint; never a file "
                            "content layout.")]] = 9,
    Bc5 [[=welder::doc("BC5: two interpolated channels (normal maps, later "
                       "clients).")]] = 11,
  };

  namespace detail
  {
    /** The 148-byte BLP2 file header, exactly as on disk (the 1024-byte
        palette follows it). The entity decomposes these fields into welded
        members; this struct exists for layout-exact serialization. */
    struct BLPHeader
    {
      std::uint32_t magic = blp_magic;             ///< 'BLP2'.
      std::uint32_t version = blp_version_1;       ///< Always 1.
      std::uint8_t color_encoding = 2;             ///< ColorEncoding byte.
      std::uint8_t alpha_depth = 0;                ///< Alpha bits per pixel: 0/1/4/8.
      std::uint8_t preferred_format = 0;           ///< PixelFormat byte.
      std::uint8_t mip_flags = 0;                  ///< 0 = no mips, 1 = generated, 2 = handmade.
      std::uint32_t width = 0;                     ///< Level-0 width in pixels.
      std::uint32_t height = 0;                    ///< Level-0 height in pixels.
      std::array<std::uint32_t, blp_max_mips> mip_offsets{};  ///< Absolute file offsets, 0 = unused.
      std::array<std::uint32_t, blp_max_mips> mip_sizes{};    ///< Payload byte sizes, 0 = unused.
    };
    static_assert(sizeof(BLPHeader) == 0x94);
    static_assert(std::is_trivially_copyable_v<BLPHeader>);

    /** The on-disk placement a read recorded, replayed by write() while the
        payloads are unmodified so unusual files (inter-mip gaps, trailing
        bytes, non-contiguous or shared mip placement) still round-trip
        byte-perfectly. Disengaged (fresh entity, or payload sizes changed):
        write() lays the file out canonically — header, then the levels
        back-to-back in level order. */
    struct StoredLayout
    {
      /** One run of file bytes covered by neither the header region nor any
          mip payload (an inter-mip gap or a trailing tail). */
      struct Run
      {
        std::uint32_t offset = 0;  ///< Absolute file offset of the run.
        FileBuffer bytes;          ///< The run's verbatim bytes.

        bool operator==(const Run&) const = default;
      };

      std::array<std::uint32_t, blp_max_mips> offsets{};  ///< The header's offset table, verbatim.
      std::array<std::uint32_t, blp_max_mips> sizes{};    ///< The header's size table, verbatim.
      std::uint32_t file_size = 0;                        ///< The original total file size.
      std::vector<Run> gaps;                              ///< Uncovered byte runs, verbatim.
      bool engaged = false;                               ///< Whether a read recorded this layout.

      bool operator==(const StoredLayout&) const = default;
    };
  }

  struct [[
    =welder::weld,
    =welder::doc(R"(
        A decoded texture surface: 8-bit RGBA pixels in row-major order, row 0
        at the top. pixels holds width * height * 4 bytes (r, g, b, a per
        pixel) and maps to NumPy zero-copy; reshape to (height, width, 4).)")
  ]] Image
  {
    [[=welder::doc("The width in pixels.")]]
    std::uint32_t width = 0;

    [[=welder::doc("The height in pixels.")]]
    std::uint32_t height = 0;

    [[=welder::mark::no_reassign,
      =welder::doc("The RGBA8 pixel bytes, width * height * 4, row-major from "
                   "the top-left.")]]
    std::vector<std::uint8_t> pixels;

    bool operator==(const Image&) const = default;
  };

  struct [[
    =welder::weld,
    =welder::doc(R"(
        How encode() should build the file. The defaults produce what the
        client ships most: DXT compression with the block format chosen from
        the alpha depth (0 -> BC1, 1 -> BC1 punch-through, 4 -> BC2,
        8 -> BC3), with a full generated mip chain.)")
  ]] EncodeSettings
  {
    [[=welder::doc("The payload encoding to produce (Palettized, Dxt or "
                   "Bgra; Jpeg is not supported).")]]
    ColorEncoding encoding = ColorEncoding::Dxt;

    [[=welder::doc("The DXT block format for Dxt encoding (Dxt1, Dxt3, Dxt5 "
                   "or Bc5). Unspecified picks from alpha_depth: 0/1 -> Dxt1, "
                   "4 -> Dxt3, 8 -> Dxt5. Ignored for Palettized/Bgra.")]]
    PixelFormat format = PixelFormat::Unspecified;

    [[=welder::doc("Alpha bits per pixel: 0, 1, 4 or 8. Selects the alpha "
                   "plane depth for Palettized files and the block format for "
                   "Dxt when format is Unspecified.")]]
    std::uint8_t alpha_depth = 8;

    [[=welder::doc("Whether to generate the full mip chain down to 1x1 "
                   "(box-filtered). Off: the file holds only level 0.")]]
    bool mipmaps = true;

    bool operator==(const EncodeSettings&) const = default;
  };

  struct [[
    =welder::weld,
    =welder::doc(R"(
        A BLP2 texture file — every WoW client release reads the same layout,
        so the class carries no client-version axis. read()/write() move the
        file whole with a byte-perfect round-trip while the mip payloads are
        unmodified; decode(level) produces an RGBA8 Image from a stored level
        (palettized, DXT1/3/5, BC5 and raw BGRA all decode); encode(image,
        settings) rebuilds the palette/compression/mip chain from one. Raw
        payload access goes through mip()/set_mip(). See
        https://wowdev.wiki/BLP.)")
  ]] BLP
  {
    [[=welder::doc("The header version field; 1 in every shipped file.")]]
    std::uint32_t version = blp_version_1;

    [[=welder::doc("How the pixel payload is encoded.")]]
    ColorEncoding color_encoding = ColorEncoding::Dxt;

    [[=welder::doc("Alpha bits per pixel (0, 1, 4 or 8): the alpha plane "
                   "depth for Palettized files, and a selection hint for "
                   "DXT.")]]
    std::uint8_t alpha_depth = 8;

    [[=welder::doc("The client-side pixel format hint; selects the DXT block "
                   "format for Dxt-encoded files.")]]
    PixelFormat preferred_format = PixelFormat::Dxt5;

    [[=welder::doc("The header's mip byte: 0 = level 0 only, 1 = generated "
                   "mips, 2 = handmade mips (plus rare high flag bits, "
                   "preserved verbatim).")]]
    std::uint8_t mip_flags = 1;

    [[=welder::doc("The level-0 width in pixels.")]]
    std::uint32_t width = 0;

    [[=welder::doc("The level-0 height in pixels.")]]
    std::uint32_t height = 0;

    [[=welder::doc("The 256-entry color table of Palettized files (b, g, r + "
                   "a padding byte, preserved verbatim). Present but unused "
                   "for Dxt/Bgra files.")]]
    std::array<CImVector, blp_palette_size> palette{};

    /** The raw mip payloads, indexed by level (empty vector = level absent).
        Exposed to the bindings through mip()/set_mip(); kept verbatim for the
        byte-perfect round-trip. */
    [[=welder::mark::exclude]]
    std::vector<FileBuffer> mips;

    /** The read-recorded on-disk placement (see detail::StoredLayout). */
    [[=welder::mark::exclude]]
    detail::StoredLayout stored_layout;

    // --- serialization --------------------------------------------------------

    [[=welder::doc("Parse a BLP2 file from memory, replacing this entity's "
                   "contents.")]]
    Result<void> read(std::span<const std::byte> data
                      [[=welder::doc("the complete file bytes")]]);

    [[nodiscard]]
    [[=welder::doc("Serialize this entity. While the mip payloads are "
                   "unmodified the original file's exact layout is replayed, "
                   "so an unmodified read rewrites byte-for-byte."),
      =welder::returns("the file bytes")]]
    Result<FileBuffer> write() const;

    [[=welder::doc("Load the BLP from a client filesystem, replacing this "
                   "entity's contents.")]]
    Result<void> read(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                      const FileKey& key
                      [[=welder::doc("the file identity (path and/or FileDataID)")]]);

    [[=welder::doc("Serialize and store the BLP through the filesystem's "
                   "project overlay.")]]
    Result<void> write(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                       const FileKey& key
                       [[=welder::doc("the file identity; must resolve to a path")]]) const;

    // --- the image surface ----------------------------------------------------

    [[nodiscard]]
    [[=welder::doc("Decode mip level 0 to an RGBA8 Image."),
      =welder::returns("the decoded image")]]
    Result<Image> decode() const;

    [[nodiscard]]
    [[=welder::doc("Decode one stored mip level to an RGBA8 Image."),
      =welder::returns("the decoded image")]]
    Result<Image> decode(std::uint32_t level
                         [[=welder::doc("the mip level to decode")]]) const;

    [[=welder::doc("Rebuild the whole texture from an RGBA8 image with the "
                   "default settings (DXT, alpha depth 8 -> BC3, full mip "
                   "chain).")]]
    Result<void> encode(const Image& image
                        [[=welder::doc("the level-0 image; width * height * 4 "
                                       "pixel bytes")]]);

    [[=welder::doc("Rebuild the whole texture from an RGBA8 image: sets the "
                   "header fields, quantizes/compresses every level and "
                   "generates the mip chain per the settings.")]]
    Result<void> encode(const Image& image
                        [[=welder::doc("the level-0 image; width * height * 4 "
                                       "pixel bytes")]],
                        const EncodeSettings& settings
                        [[=welder::doc("encoding, block format, alpha depth "
                                       "and mip generation choices")]]);

    // --- raw mip access -------------------------------------------------------

    [[=welder::getter,
      =welder::doc("The number of stored mip levels (level indices 0 .. "
                   "count - 1).")]]
    std::size_t mip_count() const { return mips.size(); }

    [[nodiscard]]
    [[=welder::doc("One level's raw payload bytes (palette indices + alpha "
                   "plane, DXT blocks, or BGRA pixels, per color_encoding)."),
      =welder::returns("a copy of the payload bytes")]]
    Result<FileBuffer> mip(std::uint32_t level
                           [[=welder::doc("the mip level")]]) const;

    [[=welder::doc("Replace one level's raw payload bytes verbatim. The "
                   "caller owns their consistency with the header fields; "
                   "changing a payload's size switches write() to the "
                   "canonical contiguous layout.")]]
    Result<void> set_mip(std::uint32_t level [[=welder::doc("the mip level")]],
                         std::span<const std::byte> data
                         [[=welder::doc("the payload bytes")]]);

    [[nodiscard]]
    [[=welder::doc("The pixel width of a mip level (level 0 halves per step, "
                   "floored at 1).")]]
    std::uint32_t mip_width(std::uint32_t level
                            [[=welder::doc("the mip level")]]) const
    {
      return std::max<std::uint32_t>(1, width >> level);
    }

    [[nodiscard]]
    [[=welder::doc("The pixel height of a mip level (level 0 halves per step, "
                   "floored at 1).")]]
    std::uint32_t mip_height(std::uint32_t level
                             [[=welder::doc("the mip level")]]) const
    {
      return std::max<std::uint32_t>(1, height >> level);
    }

    [[nodiscard]]
    [[=welder::doc(R"(
        Check the logical integrity contracts this texture must satisfy for the
        client to decode it — the base level's presence, the dimensions, and
        every stored level covering the pixels its size implies. write() never
        runs this; call it before writing when you want to know the result will
        load.)"),
      =welder::returns("every violated contract, in level order")]]
    ValidationReport validate() const;

    [[nodiscard]]
    [[=welder::doc("Validate and raise on the first error instead of returning "
                   "a report — the assert-style face of validate()."),
      =welder::returns("nothing; raises when validate() finds any error")]]
    Result<void> ensure_valid() const { return validate().to_result(); }

    bool operator==(const BLP&) const = default;
  };
}
