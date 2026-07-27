#pragma once

/** @file
    The offset-format storage vocabulary and serializer engine for the M2
    family, in one header. This machinery is used ONLY by M2 and its satellite
    files (that is why it lives under m2/ rather than common/); the fourcc+size
    chunk streams every other format uses are handled by common/chunked_file.hpp.

    Vocabulary. M2OffsetBlock is the CRTP mixin that gives an offset-addressed
    entity — M2Root (the MD20 body), Skin, the .skel chunk payloads — its
    read()/write() methods. An offset block is the in-memory face of a flat,
    positional layout made of inline scalars and `M2Array{count, offset}`
    references whose data lives elsewhere in the buffer. Members store
    `std::vector<T>` / `std::string`; the on-disk M2Array never surfaces as a
    user type. OffsetReadContext / OffsetWriteContext route a member's
    per-sequence data to and from an external buffer (the M2 .anim files).

    Unlike the chunk framework there is NO byte-perfect round-trip guarantee:
    a write always produces wowlib's canonical relayout (user decision
    2026-07-24, see .claude/context/m2-architecture.md); the tested guarantee is
    semantic — an entity written and re-read compares equal.

    Engine. The read/write logic lives as documented member functions of
    M2OffsetBlock rather than loose free functions. It walks a block's members
    and maps each onto the offset layout, dispatching on member KIND through the
    named concepts below:
      - OffsetArrayMember  (std::vector / std::string) -> an M2Array slot plus a
                            data block of element images (recursing when the
                            element is itself an array/string/record);
      - InlineRecordMember (a non-trivial class, e.g. M2Track) -> its members
                            recurse inline at the current cursor;
      - InlineScalarMember (anything trivially copyable) -> raw inline bytes.

    Layout order is the block's OWN member declaration order; a version-gated
    member living in a conditionally-inherited trait base carries
    `=offset_after("member")` naming the own member it follows, and is spliced
    in there (base flattening is by-base, never the interleaved layout order).
    Writes emit the image first, then data blocks depth-first, each 16-byte
    aligned with zero gap fill (Blizzard's preferred alignment). */

#include <meta>

#include <cstddef>
#include <cstring>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/formats/common/annotations.hpp>
#include <wowlib/formats/common/chunked_file.hpp>

namespace wowlib::formats::m2
{
  /** The non-template marker base every offset block carries (what the
      OffsetEntity concept detects). Stateless — an offset block has no
      round-trip bookkeeping to store, unlike a chunked file's journal. */
  struct M2OffsetBase
  {
    // excluded: the parameter type is this unwelded base, not the entity
    [[=welder::mark::exclude]]
    bool operator==(const M2OffsetBase&) const = default;
  };

  /** Resolution of `sequence_data` members while reading: where each outer
      element's nested data lives. Without a context (or with an empty
      function) everything is inline in the entity's own buffer. */
  struct OffsetReadContext
  {
    /** The base span the inner arrays of outer element @a i resolve their
        offsets against — the matching .anim file's bytes for an M2 sequence
        stored externally, or the entity's own buffer for an inline one.
        Returning an empty span skips the element (its vectors stay empty):
        the graceful path for a missing .anim file. */
    std::function<std::span<const std::byte>(std::size_t i)> sequence_base;
  };

  /** Resolution of `sequence_data` members while writing: which buffer each
      outer element's nested data blocks are appended to (offsets recorded
      relative to that buffer). Without a context (or a nullptr result) the
      data is written inline after the entity's own image. */
  struct OffsetWriteContext
  {
    /** The destination buffer for outer element @a i's nested data — the
        .anim file buffer being assembled for an external M2 sequence, or
        nullptr for inline. */
    std::function<FileBuffer*(std::size_t i)> sequence_sink;
  };

  // --- member-kind classification --------------------------------------------
  // The offset serializer is TYPE-driven, exactly like the chunk serializer:
  // a member's C++ type decides how it maps onto the layout. These concepts
  // name the three kinds so the dispatch reads as prose rather than as a raw
  // `is_vector_v<M> || is_same_v<M, std::string>` test.

  /** A member serialized as an M2Array<char>: its bytes (NUL included) live in
      a data block, referenced by an `M2Array{count, offset}` slot. */
  template <typename M>
  concept OffsetStringMember = std::is_same_v<M, std::string>;

  /** A member serialized as an `M2Array{count, offset}` reference to a separate
      data block — a std::vector (the block holds its element images) or a
      std::string. The one member kind whose payload is NOT inline. */
  template <typename M>
  concept OffsetArrayMember = formats::detail::is_vector_v<M> || OffsetStringMember<M>;

  /** A member serialized inline as a nested record: a non-trivial class the
      walker recurses into member-by-member at the current cursor (M2Track and
      friends). Trivially-copyable classes are raw scalars instead. */
  template <typename M>
  concept InlineRecordMember = std::is_class_v<M> && !std::is_trivially_copyable_v<M>
                               && !formats::detail::is_vector_v<M> && !OffsetStringMember<M>;

  /** A member serialized as inline raw bytes: anything trivially copyable that
      is not an array/string reference (scalars, C3Vector, M2Range, …). */
  template <typename M>
  concept InlineScalarMember = std::is_trivially_copyable_v<M> && !OffsetArrayMember<M>;

  /** The image footprint of @a T in an offset layout for client version @a V:
      the 8-byte M2Array slot for an array/string reference, sizeof for an
      inline scalar, and the version-active member sum for an inline record
      (client records have no implicit padding, so the sum IS the layout).
      Element kinds recurse, so `vector<vector<u32>>` and `vector<Record>` fall
      out naturally. gated_by members are rejected inside records: their
      presence is runtime state, supported only at the entity top level (see
      M2OffsetBlock::image_size).
      @tparam T the member/record type to measure.
      @tparam V the client version the layout targets.
      @return the number of bytes @a T occupies in place. */
  template <typename T, ClientVersion V>
  consteval std::size_t layout_size()
  {
    if constexpr (OffsetArrayMember<T>)
      return 8;  // an M2Array{count, offset} reference: two u32s
    else if constexpr (std::is_trivially_copyable_v<T>)
      return sizeof(T);
    else
    {
      static_assert(InlineRecordMember<T>);
      std::size_t total = 0;
      static constexpr auto members = formats::detail::members_of<T>();
      template for (constexpr auto m : members)
      {
        static_assert(
          !formats::detail::annotation<formats::detail::gated_by_spec, m>().has_value(),
          "gated_by members are only supported at the entity top level");
        if constexpr (formats::detail::version_active<V, m>())
        {
          using MT = [:std::meta::type_of(m):];
          total += layout_size<MT, V>();
        }
      }
      return total;
    }
  }

  /** The serialization face of an offset block, mixed in CRTP-style: an entity
      `struct E : M2OffsetBlock<E>` gains read()/write() plus the image_size()
      and member_offset() layout queries. The whole read/write engine lives here
      as protected member functions — encapsulated with the type it serves
      rather than scattered through a detail namespace.

      The `read(span)` / `write(FileBuffer&)` / `empty()` trio deliberately
      matches the chunk framework's SelfSerializing concept, so an offset block
      can sit directly behind a chunk member (the Legion+ MD21 chunk carries the
      whole MD20 image as its payload).
      @tparam Derived the entity itself (the CRTP pattern); it must declare a
              `static constexpr ClientVersion version`. */
  template <typename Derived>
  struct M2OffsetBlock : M2OffsetBase
  {
    [[=welder::doc("Deserialize file bytes into this entity, replacing its "
                   "contents. Offsets resolve against the given buffer; "
                   "sequence-gated data is read inline.")]]
    Result<void> read(std::span<const std::byte> data
                      [[=welder::doc("the file (or containing-chunk payload) bytes")]])
    {
      std::size_t cursor = 0;
      auto& self = static_cast<Derived&>(*this);
      return read_members(self, data, cursor, data, OffsetReadContext{});
    }

    /** Deserialize @a data with external sequence data resolved through @a ctx
        (the M2 .anim baking path).
        @param data the file (or containing-chunk payload) bytes.
        @param ctx  per-sequence base resolution for `sequence_data` members.
        @return nothing, or the first structural error. */
    [[=welder::mark::exclude]]
    Result<void> read(std::span<const std::byte> data, const OffsetReadContext& ctx)
    {
      std::size_t cursor = 0;
      auto& self = static_cast<Derived&>(*this);
      return read_members(self, data, cursor, data, ctx);
    }

    [[nodiscard]]
    [[=welder::doc("Serialize this entity in wowlib's canonical layout (an "
                   "offset format has no byte-perfect round-trip guarantee; "
                   "a written entity re-reads equal instead)."),
      =welder::returns("the file bytes")]]
    Result<FileBuffer> write() const
    {
      return write(OffsetWriteContext{});
    }

    /** Serialize with external sequence data routed through @a ctx (the M2
        .anim splitting path).
        @param ctx per-sequence sink resolution for `sequence_data` members.
        @return the file bytes, or the first error. */
    [[=welder::mark::exclude]]
    Result<FileBuffer> write(const OffsetWriteContext& ctx) const
    {
      FileBuffer out;
      if (auto r = write_image(static_cast<const Derived&>(*this), out, ctx); !r)
        return std::unexpected{r.error()};
      return out;
    }

    /** Append this entity's serialized image to @a out (the chunk serializer's
        SelfSerializing write hook — offsets are relative to the start of the
        appended image).
        @param out the destination buffer (appended, not cleared).
        @return nothing, or the first error. */
    [[=welder::mark::exclude]]
    Result<void> write(FileBuffer& out) const
    {
      return write_image(static_cast<const Derived&>(*this), out, OffsetWriteContext{});
    }

    /** Never empty: an offset block always has a header image worth writing
        (the SelfSerializing engagement hook — an MD21-style carrier chunk is
        always emitted).
        @return always false. */
    [[=welder::mark::exclude]]
    bool empty() const
    {
      return false;
    }

    /** The entity's image footprint in bytes for Derived's version: every
        version-active member's layout_size, plus any engaged gated_by member.
        This is the exact size of the header/inline image that precedes the
        data blocks.
        @return the image size in bytes. */
    [[=welder::mark::exclude]]
    std::size_t image_size() const
    {
      const auto& self = static_cast<const Derived&>(*this);
      static constexpr auto members = formats::detail::members_of<Derived>();
      std::size_t total = 0;
      template for (constexpr auto m : members)
      {
        if constexpr (formats::detail::version_active<Derived::version, m>())
          if (member_present<m>(self))
          {
            using MT = [:std::meta::type_of(m):];
            total += layout_size<MT, Derived::version>();
          }
      }
      return total;
    }

    /** The positional byte offset of member @a name within this entity's image,
        for Derived's version: the version-active members' layout_size summed in
        layout order up to (not including) @a name. Lets a caller stamp a derived
        field into an already-serialized image (the M2 assembly stamps
        num_skin_profiles from its skins vector) without mutating entity state.
        Valid only while no gated_by member precedes @a name — the offset would
        be runtime state then; both that and the member's existence are enforced
        at compile time.
        @param name the member identifier.
        @return the byte offset inside the entity's image.
        @throws (consteval) if @a name is unknown, or a gated_by member precedes it. */
    static consteval std::size_t member_offset(std::string_view name)
    {
      // static: `template for` needs a constant address for the range
      static constexpr auto members = formats::detail::members_of<Derived>();
      static constexpr auto order = member_order<Derived>();
      std::size_t off = 0;
      bool found = false;
      template for (constexpr std::size_t idx : order)
      {
        constexpr auto m = members[idx];
        if constexpr (formats::detail::version_active<Derived::version, m>())
        {
          if (!found && std::meta::identifier_of(m) == name)
            found = true;
          if (!found)
          {
            if (formats::detail::annotation<formats::detail::gated_by_spec, m>().has_value())
              throw "member_offset: a gated_by member precedes the target";
            using MT = [:std::meta::type_of(m):];
            off += layout_size<MT, Derived::version>();
          }
        }
      }
      if (!found)
        throw "member_offset: the entity has no such member";
      return off;
    }

    [[=welder::mark::exclude]]
    bool operator==(const M2OffsetBlock&) const = default;

  protected:
    /** The on-disk shape of an offset-array reference: element count and the
        byte offset of the data block, relative to the entity image base. */
    struct M2ArrayRef
    {
      std::uint32_t count = 0;
      std::uint32_t offset = 0;
    };
    static_assert(sizeof(M2ArrayRef) == 8 && std::is_trivially_copyable_v<M2ArrayRef>);

    // --- layout order ---------------------------------------------------------

    /** The first `offset_after` positional anchor on @a member, or nullopt.
        @param member the reflected member to inspect.
        @return the anchor spec, or nullopt when the member carries none. */
    static consteval std::optional<formats::detail::offset_after_spec>
    offset_anchor_of(std::meta::info member)
    {
      auto anns = std::meta::annotations_of_with_type(member, ^^formats::detail::offset_after_spec);
      if (anns.empty())
        return std::nullopt;
      return std::meta::extract<formats::detail::offset_after_spec>(anns[0]);
    }

    /** The member indices (into members_of<T>) in positional (layout) order:
        @a T's OWN members in declaration order, each followed by the trait-base
        members anchored to it with `=offset_after("name")`.

        A member flattened in from a (conditionally-inherited version-trait)
        base cannot keep its flatten position — bases flatten by-base, never the
        interleaved layout order — so each names the own member it follows and is
        spliced right after it (several members on one anchor keep their flatten
        order; in practice they are mutually exclusive version twins).
        Consteval-checked both ways: a trait-base member without an anchor, and
        an anchor naming no own member, are errors.
        @tparam T the record/entity type to order.
        @return a static array of member indices in layout order.
        @throws (consteval) if a trait member lacks an anchor, or an anchor is unmatched. */
    template <typename T>
    static consteval auto member_order()
    {
      constexpr auto members = formats::detail::members_of<T>();
      const auto own =
        std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked());
      const auto is_own = [&](std::meta::info m) {
        for (auto o : own)
          if (o == m)
            return true;
        return false;
      };
      for (std::size_t i = 0; i < members.size(); ++i)
        if (!is_own(members[i]) && !offset_anchor_of(members[i]).has_value())
          throw "an offset entity's trait-base member needs =offset_after(\"own member\")";
      std::vector<std::size_t> order;
      for (auto o : own)
      {
        for (std::size_t i = 0; i < members.size(); ++i)
          if (members[i] == o)
            order.push_back(i);
        if (!std::meta::has_identifier(o))
          continue;
        for (std::size_t i = 0; i < members.size(); ++i)
          if (!is_own(members[i]))
            if (auto a = offset_anchor_of(members[i]);
                a && a->view() == std::meta::identifier_of(o))
              order.push_back(i);
      }
      if (order.size() != members.size())
        throw "an offset_after anchor names no own member of the entity";
      return std::define_static_array(order);
    }

    // --- per-member presence predicates ---------------------------------------

    /** Whether member @a Mem occupies bytes in this instance of @a rec: always,
        unless it is `gated_by(mask)` and none of those `global_flags` bits are
        set. Shared by read, write and image_size so the gate has one definition.
        @tparam Mem the reflected member.
        @tparam T   the containing record/entity type.
        @param rec  the record instance whose global_flags gate the member.
        @return true when the member occupies bytes in the layout. */
    template <std::meta::info Mem, typename T>
    static bool member_present(const T& rec)
    {
      if constexpr (constexpr auto gate =
                      formats::detail::annotation<formats::detail::gated_by_spec, Mem>();
                    gate.has_value())
      {
        static_assert(requires { rec.global_flags; },
                      "gated_by members need a global_flags member to gate on");
        return (static_cast<std::uint32_t>(rec.global_flags) & gate->mask) != 0;
      }
      else
        return true;
    }

    /** Whether a `sequence_data` member of @a rec resolves its per-element data
        through the external I/O context on this call. A track bound to a global
        sequence keeps a single inline timeline even while other sequences live
        in .anim files, so it never resolves externally.
        @tparam IsSequenceData whether the member carries the `sequence_data` annotation.
        @tparam T the containing record type.
        @param rec the record instance (its global_sequence, if any, is read).
        @return true when element data should route through the I/O context. */
    template <bool IsSequenceData, typename T>
    static bool resolves_externally(const T& rec)
    {
      if constexpr (!IsSequenceData)
        return false;
      else if constexpr (requires { rec.global_sequence; })
        return rec.global_sequence == 0xFFFF;
      else
        return true;
    }

    // --- read engine ----------------------------------------------------------

    /** Read every version-active member of record/entity @a dst from @a image
        at @a cursor, in layout order (own declaration order with trait members
        spliced at their `offset_after` anchors). Array/string data blocks
        resolve against @a base. The gated_by check reads dst.global_flags,
        valid because the flags precede any gated member in layout order.
        @tparam T the record/entity type.
        @param dst    the destination record, overwritten member-by-member.
        @param image  the inline image bytes to read members from.
        @param cursor the read position within @a image; advanced past the members.
        @param base   the buffer array/string offsets resolve against.
        @param ctx    per-sequence base resolution for `sequence_data` members.
        @return nothing, or the first structural error. */
    template <typename T>
    static Result<void> read_members(T& dst, const std::span<const std::byte> image,
                                     std::size_t& cursor, const std::span<const std::byte> base,
                                     const OffsetReadContext& ctx)
    {
      static constexpr auto members = formats::detail::members_of<T>();
      static constexpr auto order = member_order<T>();
      std::optional<Error> failed;
      template for (constexpr std::size_t idx : order)
      {
        constexpr auto m = members[idx];
        if constexpr (formats::detail::version_active<Derived::version, m>())
        {
          constexpr std::string_view name = std::meta::identifier_of(m);
          constexpr bool is_seq =
            formats::detail::annotation<formats::detail::sequence_data_spec, m>().has_value();
          if (!failed && member_present<m>(dst))
            if (auto r = read_member(dst.[:m:], image, cursor, base, ctx,
                                     resolves_externally<is_seq>(dst), name);
                !r)
              failed = r.error();
        }
      }
      if (failed)
        return std::unexpected{*failed};
      return {};
    }

    /** Read one member @a dst — an array/string reference, an inline sub-record,
        or an inline scalar — advancing @a cursor over its in-image bytes.
        @tparam M the member type; the concept it satisfies picks the branch.
        @param dst      the destination member.
        @param image    the inline image bytes.
        @param cursor   the read position within @a image; advanced.
        @param base     the buffer array/string offsets resolve against.
        @param ctx      per-sequence base resolution.
        @param external set only for a `sequence_data` array resolving through @a ctx.
        @param what     the member name, for diagnostics.
        @return nothing, or the first structural error. */
    template <typename M>
    static Result<void> read_member(M& dst, const std::span<const std::byte> image, std::size_t& cursor,
                                    const std::span<const std::byte> base, const OffsetReadContext& ctx,
                                    const bool external, const std::string_view what)
    {
      if constexpr (OffsetArrayMember<M>)
        return read_array_member(dst, image, cursor, base, ctx, external, what);
      else if constexpr (InlineRecordMember<M>)
        return read_members(dst, image, cursor, base, ctx);
      else
      {
        static_assert(InlineScalarMember<M>, "offset members are arrays, records, or scalars");
        return read_scalar(dst, image, cursor, what);
      }
    }

    /** Read a trivially-copyable scalar @a dst from @a image at @a cursor.
        @tparam M the scalar type.
        @param dst    the destination value.
        @param image  the inline image bytes.
        @param cursor the read position; advanced by sizeof(M).
        @param what   the member name, for diagnostics.
        @return nothing, or OffsetOutOfBounds if the value overruns @a image. */
    template <typename M>
    static Result<void> read_scalar(M& dst, const std::span<const std::byte> image, std::size_t& cursor,
                                    const std::string_view what)
    {
      if (image.size() < sizeof(M) || image.size() - sizeof(M) < cursor)
        return offset_error(ErrorCode::OffsetOutOfBounds, what,
                            std::format("{}-byte value at image offset {:#x} overruns the image",
                                        sizeof(M), cursor));
      std::memcpy(&dst, image.data() + cursor, sizeof(M));
      cursor += sizeof(M);
      return {};
    }

    /** Read and bounds-check the 8-byte `M2Array{count, offset}` slot at
        @a cursor into @a ref, advancing @a cursor past it.
        @param ref    the destination reference.
        @param image  the inline image bytes.
        @param cursor the read position; advanced by 8.
        @param what   the member name, for diagnostics.
        @return nothing, or OffsetOutOfBounds if the slot overruns @a image. */
    static Result<void> read_array_ref(M2ArrayRef& ref, const std::span<const std::byte> image,
                                       std::size_t& cursor, const std::string_view what)
    {
      if (cursor > image.size() || image.size() - cursor < sizeof(M2ArrayRef))
        return offset_error(ErrorCode::OffsetOutOfBounds, what,
                            "M2Array slot overruns the image");
      std::memcpy(&ref, image.data() + cursor, sizeof ref);
      cursor += sizeof ref;
      return {};
    }

    /** Read an M2Array-referenced member: take its slot, then decode the data
        block as a string or a vector.
        @tparam M the member type (std::string or std::vector<U>).
        @param dst      the destination member.
        @param image    the inline image bytes.
        @param cursor   the read position; advanced past the slot.
        @param base     the buffer the slot offset resolves against.
        @param ctx      per-sequence base resolution.
        @param external set for an externally-resolved `sequence_data` array.
        @param what     the member name, for diagnostics.
        @return nothing, or the first structural error. */
    template <typename M>
    static Result<void> read_array_member(M& dst, const std::span<const std::byte> image,
                                          std::size_t& cursor, const std::span<const std::byte> base,
                                          const OffsetReadContext& ctx, const bool external,
                                          const std::string_view what)
    {
      M2ArrayRef ref;
      if (auto r = read_array_ref(ref, image, cursor, what); !r)
        return r;
      if constexpr (OffsetStringMember<M>)
        return read_string_block(dst, ref, base, what);
      else
        return read_vector_block(dst, ref, base, ctx, external, what);
    }

    /** Decode a string data block referenced by @a ref out of @a base. The
        stored count includes the NUL terminator (client buffer-size semantics),
        so the string is truncated at the first NUL within it.
        @param dst  the destination string, cleared first.
        @param ref  the {count, offset} slot.
        @param base the buffer the offset resolves against.
        @param what the member name, for diagnostics.
        @return nothing, or OffsetOutOfBounds if the block overruns @a base. */
    static Result<void> read_string_block(std::string& dst, const M2ArrayRef ref,
                                          const std::span<const std::byte> base, const std::string_view what)
    {
      dst.clear();
      if (ref.count == 0)
        return {};
      if (std::uint64_t{ref.offset} + ref.count > base.size())
        return offset_error(ErrorCode::OffsetOutOfBounds, what,
                            std::format("string [{} at {:#x}] overruns the {}-byte buffer",
                                        ref.count, ref.offset, base.size()));
      const auto* chars = reinterpret_cast<const char*>(base.data() + ref.offset);
      const auto* nul = static_cast<const char*>(std::memchr(chars, 0, ref.count));
      dst.assign(chars, nul ? static_cast<std::size_t>(nul - chars) : ref.count);
      return {};
    }

    /** Decode a vector data block referenced by @a ref out of @a base:
        `ref.count` element images of `layout_size<U>` bytes. Trivially-copyable
        elements bulk-copy; non-trivial ones recurse through read_array_elements.
        An externally-resolved sequence with an empty @a base (a missing .anim
        file) leaves the member empty rather than failing.
        @tparam M the vector type.
        @param dst      the destination vector, cleared first.
        @param ref      the {count, offset} slot.
        @param base     the buffer the offset resolves against.
        @param ctx      per-sequence base resolution.
        @param external set for an externally-resolved `sequence_data` array.
        @param what     the member name, for diagnostics.
        @return nothing, or OffsetOutOfBounds if the block overruns @a base. */
    template <typename M>
    static Result<void> read_vector_block(M& dst, const M2ArrayRef ref, const std::span<const std::byte> base,
                                          const OffsetReadContext& ctx, const bool external,
                                          const std::string_view what)
    {
      using U = typename M::value_type;
      dst.clear();
      if (ref.count == 0)
        return {};
      // An external sequence element whose context base is empty (missing .anim
      // file): leave the member empty rather than fail.
      if (base.empty())
        return {};
      constexpr std::size_t elem = layout_size<U, Derived::version>();
      if (std::uint64_t{ref.offset} + std::uint64_t{ref.count} * elem > base.size())
        return offset_error(ErrorCode::OffsetOutOfBounds, what,
                            std::format("array [{} x {} at {:#x}] overruns the {}-byte buffer",
                                        ref.count, elem, ref.offset, base.size()));
      dst.resize(ref.count);
      if constexpr (std::is_trivially_copyable_v<U>)
      {
        std::memcpy(dst.data(), base.data() + ref.offset, std::size_t{ref.count} * elem);
        return {};
      }
      else
        return read_array_elements(dst, ref, base, ctx, external, what);
    }

    /** Read the non-trivial elements of @a dst, each from its own element image
        inside @a base. For an externally-resolved `sequence_data` array, element
        i reads its nested blocks from ctx.sequence_base(i) (the matching .anim
        bytes) instead of @a base.
        @tparam M the vector type (already sized to the element count).
        @param dst      the destination vector, pre-resized.
        @param ref      the {count, offset} slot locating the element images.
        @param base     the buffer the element images live in.
        @param ctx      per-sequence base resolution.
        @param external whether this is an externally-resolved sequence array.
        @param what     the member name, for diagnostics.
        @return nothing, or the first structural error. */
    template <typename M>
    static Result<void> read_array_elements(M& dst, const M2ArrayRef ref,
                                            const std::span<const std::byte> base,
                                            const OffsetReadContext& ctx, const bool external,
                                            const std::string_view what)
    {
      using U = typename M::value_type;
      constexpr std::size_t elem = layout_size<U, Derived::version>();
      for (std::size_t i = 0; i < dst.size(); ++i)
      {
        const auto element_image = base.subspan(ref.offset + i * elem, elem);
        const std::span<const std::byte> element_base =
          external && ctx.sequence_base ? ctx.sequence_base(i) : base;
        std::size_t element_cursor = 0;
        if (auto r =
              read_member(dst[i], element_image, element_cursor, element_base, ctx, false, what);
            !r)
          return r;
      }
      return {};
    }

    // --- write engine ---------------------------------------------------------

    /** Write @a n raw bytes into @a image at @a image_pos + @a cursor, advancing
        the cursor. The image is pre-sized to the entity footprint, so this write
        stays within bounds.
        @param image    the pre-sized image buffer.
        @param image_pos the base offset of the record being written within @a image.
        @param cursor   the position within the record; advanced by @a n.
        @param bytes    the source bytes.
        @param n        the byte count. */
    static void put_bytes(FileBuffer& image, const std::size_t image_pos, std::size_t& cursor,
                          const void* bytes, const std::size_t n)
    {
      std::memcpy(image.data() + image_pos + cursor, bytes, n);
      cursor += n;
    }

    /** Reserve @a bytes at the (16-byte aligned, zero-filled) end of @a target
        and return the start index. Blizzard aligns every data block to 16 bytes;
        an index (not a pointer) is returned because the buffer reallocates as it
        grows.
        @param target the buffer to grow.
        @param bytes  the block size to reserve.
        @return the byte index of the reserved block. */
    static std::size_t alloc_block(FileBuffer& target, const std::size_t bytes)
    {
      target.resize((target.size() + 15) / 16 * 16);
      const std::size_t at = target.size();
      target.resize(target.size() + bytes);
      return at;
    }

    /** Serialize @a self in canonical layout — the image first (pre-sized to
        image_size()), then data blocks depth-first via write_members — and append
        the result to @a out. Offsets are recorded relative to the image start, so
        the image is built into a fresh buffer before appending.
        @param self the entity to serialize.
        @param out  the destination buffer (appended, not cleared).
        @param ctx  per-sequence sink resolution.
        @return nothing, or the first error. */
    static Result<void> write_image(const Derived& self, FileBuffer& out,
                                    const OffsetWriteContext& ctx)
    {
      FileBuffer buffer;
      buffer.resize(self.image_size());
      std::size_t cursor = 0;
      if (auto r = write_members(self, buffer, 0, cursor, buffer, ctx); !r)
        return r;
      out.insert(out.end(), buffer.begin(), buffer.end());
      return {};
    }

    /** Write every version-active member of record/entity @a src into @a image
        at @a image_pos + cursor, in layout order; data blocks append to
        @a blocks. Mirrors read_members exactly.
        @tparam T the record/entity type.
        @param src      the source record.
        @param image    the pre-sized image buffer.
        @param image_pos the base offset of @a src's image within @a image.
        @param cursor   the position within the record; advanced past the members.
        @param blocks   the buffer receiving array/string data blocks.
        @param ctx      per-sequence sink resolution.
        @return nothing, or the first error. */
    template <typename T>
    static Result<void> write_members(const T& src, FileBuffer& image, const std::size_t image_pos,
                                      std::size_t& cursor, FileBuffer& blocks,
                                      const OffsetWriteContext& ctx)
    {
      static constexpr auto members = formats::detail::members_of<T>();
      static constexpr auto order = member_order<T>();
      std::optional<Error> failed;
      template for (constexpr std::size_t idx : order)
      {
        constexpr auto m = members[idx];
        if constexpr (formats::detail::version_active<Derived::version, m>())
        {
          constexpr std::string_view name = std::meta::identifier_of(m);
          constexpr bool is_seq =
            formats::detail::annotation<formats::detail::sequence_data_spec, m>().has_value();
          if (!failed && member_present<m>(src))
            if (auto r = write_member(src.[:m:], image, image_pos, cursor, blocks, ctx,
                                      resolves_externally<is_seq>(src), name);
                !r)
              failed = r.error();
        }
      }
      if (failed)
        return std::unexpected{*failed};
      return {};
    }

    /** Write one member @a src — an array/string reference, an inline sub-record,
        or an inline scalar — advancing @a cursor over its in-image bytes.
        @tparam M the member type; the concept it satisfies picks the branch.
        @param src      the source member.
        @param image    the pre-sized image buffer.
        @param image_pos the base offset of the containing record within @a image.
        @param cursor   the position within the record; advanced.
        @param blocks   the buffer receiving data blocks.
        @param ctx      per-sequence sink resolution.
        @param external set only for a `sequence_data` array routing through @a ctx.
        @param what     the member name, for diagnostics.
        @return nothing, or the first error. */
    template <typename M>
    static Result<void> write_member(const M& src, FileBuffer& image, const std::size_t image_pos,
                                     std::size_t& cursor, FileBuffer& blocks,
                                     const OffsetWriteContext& ctx, const bool external,
                                     const std::string_view what)
    {
      if constexpr (OffsetArrayMember<M>)
        return write_array_member(src, image, image_pos, cursor, blocks, ctx, external, what);
      else if constexpr (InlineRecordMember<M>)
        return write_members(src, image, image_pos, cursor, blocks, ctx);
      else
      {
        static_assert(InlineScalarMember<M>, "offset members are arrays, records, or scalars");
        put_bytes(image, image_pos, cursor, &src, sizeof(M));
        return {};
      }
    }

    /** Write an M2Array-referenced member: allocate its data block, fill it, and
        emit the {count, offset} slot.
        @tparam M the member type (std::string or std::vector<U>).
        @param src      the source member.
        @param image    the pre-sized image buffer.
        @param image_pos the base offset of the containing record within @a image.
        @param cursor   the position within the record; advanced past the slot.
        @param blocks   the buffer receiving the data block.
        @param ctx      per-sequence sink resolution.
        @param external set for an externally-resolved `sequence_data` array.
        @param what     the member name, for diagnostics.
        @return nothing, or the first error. */
    template <typename M>
    static Result<void> write_array_member(const M& src, FileBuffer& image, const std::size_t image_pos,
                                           std::size_t& cursor, FileBuffer& blocks,
                                           const OffsetWriteContext& ctx, const bool external,
                                           const std::string_view what)
    {
      if constexpr (OffsetStringMember<M>)
        return write_string_block(src, image, image_pos, cursor, blocks);
      else
        return write_vector_block(src, image, image_pos, cursor, blocks, ctx, external, what);
    }

    /** Emit @a src as an M2Array<char> whose count includes the NUL terminator —
        the client reads these as buffer sizes and requires the terminator inside,
        so even the empty string writes {1, block}, never {0, 0}.
        @param src      the source string.
        @param image    the pre-sized image buffer.
        @param image_pos the base offset of the containing record within @a image.
        @param cursor   the position within the record; advanced past the slot.
        @param blocks   the buffer receiving the character block.
        @return nothing (never fails). */
    static Result<void> write_string_block(const std::string& src, FileBuffer& image,
                                           const std::size_t image_pos, std::size_t& cursor,
                                           FileBuffer& blocks)
    {
      const std::size_t at = alloc_block(blocks, src.size() + 1);
      std::memcpy(blocks.data() + at, src.data(), src.size());
      blocks[at + src.size()] = std::byte{0};
      const M2ArrayRef ref{static_cast<std::uint32_t>(src.size() + 1),
                           static_cast<std::uint32_t>(at)};
      put_bytes(image, image_pos, cursor, &ref, sizeof ref);
      return {};
    }

    /** Emit @a src as an M2Array: an empty vector writes {0, 0}; otherwise a
        `count * layout_size<U>` element block is reserved, filled (bulk memcpy for
        trivially-copyable elements, else write_array_elements), and the slot
        emitted.
        @tparam M the vector type.
        @param src      the source vector.
        @param image    the pre-sized image buffer.
        @param image_pos the base offset of the containing record within @a image.
        @param cursor   the position within the record; advanced past the slot.
        @param blocks   the buffer receiving the element block.
        @param ctx      per-sequence sink resolution.
        @param external set for an externally-resolved `sequence_data` array.
        @param what     the member name, for diagnostics.
        @return nothing, or the first error. */
    template <typename M>
    static Result<void> write_vector_block(const M& src, FileBuffer& image, const std::size_t image_pos,
                                           std::size_t& cursor, FileBuffer& blocks,
                                           const OffsetWriteContext& ctx, const bool external,
                                           const std::string_view what)
    {
      using U = typename M::value_type;
      if (src.empty())
      {
        const M2ArrayRef empty{0, 0};
        put_bytes(image, image_pos, cursor, &empty, sizeof empty);
        return {};
      }
      constexpr std::size_t elem = layout_size<U, Derived::version>();
      const std::size_t at = alloc_block(blocks, src.size() * elem);
      const M2ArrayRef ref{static_cast<std::uint32_t>(src.size()), static_cast<std::uint32_t>(at)};
      put_bytes(image, image_pos, cursor, &ref, sizeof ref);
      if constexpr (std::is_trivially_copyable_v<U>)
      {
        std::memcpy(blocks.data() + at, src.data(), src.size() * elem);
        return {};
      }
      else
        return write_array_elements(src, at, blocks, ctx, external, what);
    }

    /** Write the non-trivial elements of @a src into the block already reserved
        at @a block_at in @a blocks. Each element's inline image lands in that
        block; its own nested data blocks append to the sequence sink
        (ctx.sequence_sink(i), the .anim buffer) when this is an externally
        resolved `sequence_data` array, else to @a blocks.
        @tparam M the vector type.
        @param src      the source vector.
        @param block_at the byte index of the reserved element block in @a blocks.
        @param blocks   the buffer holding the element images (and, by default,
                        their nested data).
        @param ctx      per-sequence sink resolution.
        @param external whether this is an externally-resolved sequence array.
        @param what     the member name, for diagnostics.
        @return nothing, or the first error. */
    template <typename M>
    static Result<void> write_array_elements(const M& src, const std::size_t block_at,
                                             FileBuffer& blocks, const OffsetWriteContext& ctx,
                                             const bool external, const std::string_view what)
    {
      using U = typename M::value_type;
      constexpr std::size_t elem = layout_size<U, Derived::version>();
      for (std::size_t i = 0; i < src.size(); ++i)
      {
        FileBuffer* sink = external && ctx.sequence_sink ? ctx.sequence_sink(i) : nullptr;
        std::size_t element_cursor = 0;
        if (auto r = write_member(src[i], blocks, block_at + i * elem, element_cursor,
                                  sink ? *sink : blocks, ctx, false, what);
            !r)
          return r;
      }
      return {};
    }

    // --- diagnostics ----------------------------------------------------------

    /** Build an offset-scoped error naming the offending @a member.
        @param code   the error category (typically OffsetOutOfBounds).
        @param member the entity member being transferred.
        @param what   the failure description.
        @return the error, ready to return from a Result function. */
    static std::unexpected<Error> offset_error(const ErrorCode code, const std::string_view member,
                                               const std::string_view what)
    {
      return make_error(code, std::format("offset member '{}': {}", member, what));
    }
  };

  /** A type the offset engine can read and write: it is an M2 offset block and
      declares the client version it is laid out for. */
  template <typename E>
  concept OffsetEntity = std::derived_from<E, M2OffsetBase> && requires {
    { E::version } -> std::convertible_to<ClientVersion>;
  };
}
