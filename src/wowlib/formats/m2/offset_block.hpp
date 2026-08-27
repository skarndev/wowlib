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
    `=offsetAfter("member")` naming the own member it follows, and is spliced
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

namespace wowlib::formats::m2 {
  /** The non-template marker base every offset block carries (what the
      OffsetEntity concept detects). Stateless — an offset block has no
      round-trip bookkeeping to store, unlike a chunked file's journal. */
  struct M2OffsetBase {
    // excluded: the parameter type is this unwelded base, not the entity
    [[=welder::mark::exclude]]
    bool operator==(const M2OffsetBase&) const = default;
  };

  /** Resolution of `SequenceData` members while reading: where each outer
      element's nested data lives. Without a context (or with an empty
      function) everything is inline in the entity's own buffer. */
  struct OffsetReadContext {
    /** The base span the inner arrays of outer element @a i resolve their
        offsets against — the matching .anim file's bytes for an M2 sequence
        stored externally, or the entity's own buffer for an inline one.
        Returning an empty span skips the element (its vectors stay empty):
        the graceful path for a missing .anim file. */
    std::function<std::span<const std::byte>(std::size_t i)> sequenceBase;
  };

  /** Resolution of `SequenceData` members while writing: which buffer each
      outer element's nested data blocks are appended to (offsets recorded
      relative to that buffer). Without a context (or a nullptr result) the
      data is written inline after the entity's own image. */
  struct OffsetWriteContext {
    /** The destination buffer for outer element @a i's nested data — the
        .anim file buffer being assembled for an external M2 sequence, or
        nullptr for inline. */
    std::function<FileBuffer * (std::size_t i
    )
    >
    sequenceSink;
  };

  // --- member-kind classification --------------------------------------------
  // The offset serializer is TYPE-driven, exactly like the chunk serializer:
  // a member's C++ type decides how it maps onto the layout. These concepts
  // name the three kinds so the dispatch reads as prose rather than as a raw
  // `IsVectorV<M> || is_same_v<M, std::string>` test.

  /** A member serialized as an M2Array<char>: its bytes (NUL included) live in
      a data block, referenced by an `M2Array{count, offset}` slot. */
  template <typename M> concept OffsetStringMember = std::is_same_v<M, std::string>;

  /** A member serialized as an `M2Array{count, offset}` reference to a separate
      data block — a std::vector (the block holds its element images) or a
      std::string. The one member kind whose payload is NOT inline. */
  template <typename M> concept OffsetArrayMember = formats::detail::IsVectorV<M> || OffsetStringMember<M>;

  /** A member serialized inline as a nested record: a non-trivial class the
      walker recurses into member-by-member at the current cursor (M2Track and
      friends). Trivially-copyable classes are raw scalars instead. */
  template <typename M> concept InlineRecordMember = std::is_class_v<M> && !std::is_trivially_copyable_v<M> && !
    formats::detail::IsVectorV<M> && !OffsetStringMember<M>;

  /** A member serialized as inline raw bytes: anything trivially copyable that
      is not an array/string reference (scalars, C3Vector, M2Range, …). */
  template <typename M> concept InlineScalarMember = std::is_trivially_copyable_v<M> && !OffsetArrayMember<M>;

  /** The image footprint of @a T in an offset layout for client version @a V:
      the 8-byte M2Array slot for an array/string reference, sizeof for an
      inline scalar, and the version-active member sum for an inline record
      (client records have no implicit padding, so the sum IS the layout).
      Element kinds recurse, so `vector<vector<u32>>` and `vector<Record>` fall
      out naturally. gatedBy members are rejected inside records: their
      presence is runtime state, supported only at the entity top level (see
      M2OffsetBlock::imageSize).
      @tparam T the member/record type to measure.
      @tparam V the client version the layout targets.
      @return the number of bytes @a T occupies in place. */
  template <typename T, ClientVersion V>
  consteval std::size_t layoutSize() {
    if constexpr (OffsetArrayMember<T>) return 8; // an M2Array{count, offset} reference: two u32s
    else if constexpr (std::is_trivially_copyable_v<T>) return sizeof(T);
    else {
      static_assert(InlineRecordMember<T>);
      std::size_t total = 0;
      static constexpr auto Members = formats::detail::membersOf<T>();
      template for (constexpr auto m : Members) {
        static_assert(!formats::detail::annotation<formats::detail::GatedBySpec, m>().has_value(),
                      "gated_by members are only supported at the entity top level");
        if constexpr (formats::detail::versionActive<V, m>()) {
          using MT = [:std::meta::type_of(m):];
          total += layoutSize<MT, V>();
        }
      }
      return total;
    }
  }

  /** The serialization face of an offset block, mixed in CRTP-style: an entity
      `struct E : M2OffsetBlock<E>` gains read()/write() plus the imageSize()
      and memberOffset() layout queries. The whole read/write engine lives here
      as protected member functions — encapsulated with the type it serves
      rather than scattered through a detail namespace.

      The `read(span)` / `write(FileBuffer&)` / `empty()` trio deliberately
      matches the chunk framework's SelfSerializing concept, so an offset block
      can sit directly behind a chunk member (the Legion+ MD21 chunk carries the
      whole MD20 image as its payload).
      @tparam Derived the entity itself (the CRTP pattern); it must declare a
              `static constexpr ClientVersion version`. */
  template <typename Derived>
  struct M2OffsetBlock : M2OffsetBase {
    [[=welder::doc("Deserialize file bytes into this entity, replacing its "
      "contents. Offsets resolve against the given buffer; "
      "sequence-gated data is read inline.")]]
    Result<void>
    read(std::span<const std::byte> data [[=welder::doc("the file (or containing-chunk payload) bytes")]]) {
      std::size_t cursor = 0;
      auto& self = static_cast<Derived&>(*this);
      return _readMembers(self, data, cursor, data, OffsetReadContext{});
    }

    /** Deserialize @a data with external sequence data resolved through @a ctx
        (the M2 .anim baking path).
        @param data the file (or containing-chunk payload) bytes.
        @param ctx  per-sequence base resolution for `SequenceData` members.
        @return nothing, or the first structural error. */
    [[=welder::mark::exclude]]
    Result<void> read(std::span<const std::byte> data, const OffsetReadContext& ctx) {
      std::size_t cursor = 0;
      auto& self = static_cast<Derived&>(*this);
      return _readMembers(self, data, cursor, data, ctx);
    }

    [[nodiscard]]
    [[=welder::doc("Serialize this entity in wowlib's canonical layout (an "
        "offset format has no byte-perfect round-trip guarantee; "
        "a written entity re-reads equal instead)."),
      =welder::returns("the file bytes")]]
    Result<FileBuffer> write() const {
      return write(OffsetWriteContext{});
    }

    /** Serialize with external sequence data routed through @a ctx (the M2
        .anim splitting path).
        @param ctx per-sequence sink resolution for `SequenceData` members.
        @return the file bytes, or the first error. */
    [[=welder::mark::exclude]]
    Result<FileBuffer> write(const OffsetWriteContext& ctx) const {
      FileBuffer out;
      if (auto r = _writeImage(static_cast<const Derived&>(*this), out, ctx); !r) return std::unexpected{r.error()};
      return out;
    }

    /** Append this entity's serialized image to @a out (the chunk serializer's
        SelfSerializing write hook — offsets are relative to the start of the
        appended image).
        @param out the destination buffer (appended, not cleared).
        @return nothing, or the first error. */
    [[=welder::mark::exclude]]
    Result<void> write(FileBuffer& out) const {
      return _writeImage(static_cast<const Derived&>(*this), out, OffsetWriteContext{});
    }

    /** Never empty: an offset block always has a header image worth writing
        (the SelfSerializing engagement hook — an MD21-style carrier chunk is
        always emitted).
        @return always false. */
    [[=welder::mark::exclude]]
    bool empty() const {
      return false;
    }

    [[nodiscard]]
    [[=welder::doc(R"(
        Check the logical integrity contracts this file must satisfy to LOAD in
        the client — companion-array counts, lookup ranges — which write()
        deliberately never enforces. Call it before writing when you want to
        know the result will load. A file read from a client and left
        unmodified reports no errors; warnings mark states real client files
        ship.)"),
      =welder::returns("every violated contract, in member order")]]
    ValidationReport validate() const {
      ValidationReport report;
      formats::detail::validateEntity(static_cast<const Derived&>(*this), report);
      return report;
    }

    [[nodiscard]]
    [[=welder::doc("Validate and raise on the first error instead of returning "
        "a report — the assert-style face of validate()."),
      =welder::returns("nothing; raises when validate() finds any error")]]
    Result<void> ensureValid() const {
      return validate().toResult();
    }

    /** The entity's image footprint in bytes for Derived's version: every
        version-active member's layoutSize, plus any engaged gatedBy member.
        This is the exact size of the header/inline image that precedes the
        data blocks.
        @return the image size in bytes. */
    [[=welder::mark::exclude]]
    std::size_t imageSize() const {
      const auto& self = static_cast<const Derived&>(*this);
      static constexpr auto Members = formats::detail::membersOf<Derived>();
      std::size_t total = 0;
      template for (constexpr auto m : Members) {
        if constexpr (formats::detail::versionActive<Derived::Version, m>())
          if (_memberPresent<m>(self)) {
            using MT = [:std::meta::type_of(m):];
            total += layoutSize<MT, Derived::Version>();
          }
      }
      return total;
    }

    /** The positional byte offset of member @a name within this entity's image,
        for Derived's version: the version-active members' layoutSize summed in
        layout order up to (not including) @a name. Lets a caller stamp a derived
        field into an already-serialized image (the M2 assembly stamps
        numSkinProfiles from its skins vector) without mutating entity state.
        Valid only while no gatedBy member precedes @a name — the offset would
        be runtime state then; both that and the member's existence are enforced
        at compile time.
        @param name the member identifier.
        @return the byte offset inside the entity's image.
        @throws (consteval) if @a name is unknown, or a gatedBy member precedes it. */
    [[=welder::mark::exclude]]
    static consteval std::size_t memberOffset(std::string_view name) {
      // static: `template for` needs a constant address for the range
      static constexpr auto Members = formats::detail::membersOf<Derived>();
      static constexpr auto Order = _memberOrder<Derived>();
      std::size_t off = 0;
      bool found = false;
      template for (constexpr std::size_t idx : Order) {
        constexpr auto m = Members[idx];
        if constexpr (formats::detail::versionActive<Derived::Version, m>()) {
          if (!found && std::meta::identifier_of(m) == name) found = true;
          if (!found) {
            if (formats::detail::annotation<formats::detail::GatedBySpec, m>().has_value()) throw
              "member_offset: a gated_by member precedes the target";
            using MT = [:std::meta::type_of(m):];
            off += layoutSize<MT, Derived::Version>();
          }
        }
      }
      if (!found) throw "member_offset: the entity has no such member";
      return off;
    }

    [[=welder::mark::exclude]]
    bool operator==(const M2OffsetBlock&) const = default;

  protected:
    /** The on-disk shape of an offset-array reference: element count and the
        byte offset of the data block, relative to the entity image base. */
    struct M2ArrayRef {
      std::uint32_t count = 0;
      std::uint32_t offset = 0;
    };

    static_assert(sizeof(M2ArrayRef) == 8 && std::is_trivially_copyable_v<M2ArrayRef>);

    // --- layout order ---------------------------------------------------------

    /** The first `offsetAfter` positional anchor on @a member, or nullopt.
        @param member the reflected member to inspect.
        @return the anchor spec, or nullopt when the member carries none. */
    static consteval std::optional<formats::detail::OffsetAfterSpec>
    _offsetAnchorOf(std::meta::info member) {
      auto anns = std::meta::annotations_of_with_type(member, ^^formats::detail::OffsetAfterSpec);
      if (anns.empty()) return std::nullopt;
      return std::meta::extract<formats::detail::OffsetAfterSpec>(anns[0]);
    }

    /** The member indices (into membersOf<T>) in positional (layout) order:
        @a T's OWN members in declaration order, each followed by the trait-base
        members anchored to it with `=offsetAfter("name")`.

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
    static consteval auto _memberOrder() {
      constexpr auto members = formats::detail::membersOf<T>();
      const auto own = std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked());
      const auto isOwn = [&](std::meta::info m) {
        for (auto o : own)
          if (o == m) return true;
        return false;
      };
      for (std::size_t i = 0; i < members.size(); ++i)
        if (!isOwn(members[i]) && !_offsetAnchorOf(members[i]).has_value())
          throw "an offset entity's trait-base member needs =offset_after(\"own member\")";
      std::vector<std::size_t> order;
      for (auto o : own) {
        for (std::size_t i = 0; i < members.size(); ++i)
          if (members[i] == o) order.push_back(i);
        if (!std::meta::has_identifier(o)) continue;
        for (std::size_t i = 0; i < members.size(); ++i)
          if (!isOwn(members[i]))
            if (auto a = _offsetAnchorOf(members[i]); a && a->view() == std::meta::identifier_of(o)) order.
              push_back(i);
      }
      if (order.size() != members.size()) throw "an offset_after anchor names no own member of the entity";
      return std::define_static_array(order);
    }

    // --- per-member presence predicates ---------------------------------------

    /** Whether member @a Mem occupies bytes in this instance of @a rec: always,
        unless it is `gatedBy(mask)` and none of those `globalFlags` bits are
        set. Shared by read, write and imageSize so the gate has one definition.
        @tparam Mem the reflected member.
        @tparam T   the containing record/entity type.
        @param rec  the record instance whose globalFlags gate the member.
        @return true when the member occupies bytes in the layout. */
    template <std::meta::info Mem, typename T>
    static bool _memberPresent(const T& rec) {
      if constexpr (constexpr auto gate = formats::detail::annotation<formats::detail::GatedBySpec, Mem>(); gate.
        has_value()) {
        static_assert(requires { rec.globalFlags; }, "gated_by members need a global_flags member to gate on");
        return (static_cast<std::uint32_t>(rec.globalFlags) & gate->mask) != 0;
      }
      else return true;
    }

    /** Whether a `SequenceData` member of @a rec resolves its per-element data
        through the external I/O context on this call. A track bound to a global
        sequence keeps a single inline timeline even while other sequences live
        in .anim files, so it never resolves externally.
        @tparam IsSequenceData whether the member carries the `SequenceData` annotation.
        @tparam T the containing record type.
        @param rec the record instance (its globalSequence, if any, is read).
        @return true when element data should route through the I/O context. */
    template <bool IsSequenceData, typename T>
    static bool _resolvesExternally(const T& rec) {
      if constexpr (!IsSequenceData) return false;
      else if constexpr (requires { rec.globalSequence; }) return rec.globalSequence == 0xFFFF;
      else return true;
    }

    // --- read engine ----------------------------------------------------------

    /** Read every version-active member of record/entity @a dst from @a image
        at @a cursor, in layout order (own declaration order with trait members
        spliced at their `offsetAfter` anchors). Array/string data blocks
        resolve against @a base. The gatedBy check reads dst.globalFlags,
        valid because the flags precede any gated member in layout order.
        @tparam T the record/entity type.
        @param dst    the destination record, overwritten member-by-member.
        @param image  the inline image bytes to read members from.
        @param cursor the read position within @a image; advanced past the members.
        @param base   the buffer array/string offsets resolve against.
        @param ctx    per-sequence base resolution for `SequenceData` members.
        @return nothing, or the first structural error. */
    template <typename T>
    static Result<void> _readMembers(T& dst,
                                      const std::span<const std::byte> image,
                                      std::size_t& cursor,
                                      const std::span<const std::byte> base,
                                      const OffsetReadContext& ctx) {
      static constexpr auto Members = formats::detail::membersOf<T>();
      static constexpr auto Order = _memberOrder<T>();
      std::optional<Error> failed;
      template for (constexpr std::size_t idx : Order) {
        constexpr auto m = Members[idx];
        if constexpr (formats::detail::versionActive<Derived::Version, m>()) {
          constexpr std::string_view name = std::meta::identifier_of(m);
          constexpr bool isSeq = formats::detail::annotation<formats::detail::SequenceDataSpec, m>().has_value();
          if (!failed && _memberPresent<m>(dst))
            if (auto r = _readMember(dst.[:m:], image, cursor, base, ctx, _resolvesExternally<isSeq>(dst), name); !r)
              failed = r.error();
        }
      }
      if (failed) return std::unexpected{*failed};
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
        @param external set only for a `SequenceData` array resolving through @a ctx.
        @param what     the member name, for diagnostics.
        @return nothing, or the first structural error. */
    template <typename M>
    static Result<void> _readMember(M& dst,
                                     const std::span<const std::byte> image,
                                     std::size_t& cursor,
                                     const std::span<const std::byte> base,
                                     const OffsetReadContext& ctx,
                                     const bool external,
                                     const std::string_view what) {
      if constexpr (OffsetArrayMember<M>)
        return _readArrayMember(dst, image, cursor, base, ctx, external, what);
      else if constexpr (InlineRecordMember<M>) return _readMembers(dst, image, cursor, base, ctx);
      else {
        static_assert(InlineScalarMember<M>, "offset members are arrays, records, or scalars");
        return _readScalar(dst, image, cursor, what);
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
    static Result<void> _readScalar(M& dst,
                                     const std::span<const std::byte> image,
                                     std::size_t& cursor,
                                     const std::string_view what) {
      if (image.size() < sizeof(M) || image.size() - sizeof(M) < cursor)
        return _offsetError(ErrorCode::OffsetOutOfBounds, what,
                             std::format("{}-byte value at image offset {:#x} overruns the image", sizeof(M), cursor));
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
    static Result<void> _readArrayRef(M2ArrayRef& ref,
                                        const std::span<const std::byte> image,
                                        std::size_t& cursor,
                                        const std::string_view what) {
      if (cursor > image.size() || image.size() - cursor < sizeof(M2ArrayRef))
        return _offsetError(ErrorCode::OffsetOutOfBounds, what, "M2Array slot overruns the image");
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
        @param external set for an externally-resolved `SequenceData` array.
        @param what     the member name, for diagnostics.
        @return nothing, or the first structural error. */
    template <typename M>
    static Result<void> _readArrayMember(M& dst,
                                           const std::span<const std::byte> image,
                                           std::size_t& cursor,
                                           const std::span<const std::byte> base,
                                           const OffsetReadContext& ctx,
                                           const bool external,
                                           const std::string_view what) {
      M2ArrayRef ref;
      if (auto r = _readArrayRef(ref, image, cursor, what); !r) return r;
      if constexpr (OffsetStringMember<M>) return _readStringBlock(dst, ref, base, what);
      else return _readVectorBlock(dst, ref, base, ctx, external, what);
    }

    /** Decode a string data block referenced by @a ref out of @a base. The
        stored count includes the NUL terminator (client buffer-size semantics),
        so the string is truncated at the first NUL within it.
        @param dst  the destination string, cleared first.
        @param ref  the {count, offset} slot.
        @param base the buffer the offset resolves against.
        @param what the member name, for diagnostics.
        @return nothing, or OffsetOutOfBounds if the block overruns @a base. */
    static Result<void> _readStringBlock(std::string& dst,
                                           const M2ArrayRef ref,
                                           const std::span<const std::byte> base,
                                           const std::string_view what) {
      dst.clear();
      if (ref.count == 0) return {};
      if (std::uint64_t{ref.offset} + ref.count > base.size())
        return _offsetError(ErrorCode::OffsetOutOfBounds, what,
                             std::format("string [{} at {:#x}] overruns the {}-byte buffer", ref.count, ref.offset,
                                         base.size()));
      const auto* chars = reinterpret_cast<const char*>(base.data() + ref.offset);
      const auto* nul = static_cast<const char*>(std::memchr(chars, 0, ref.count));
      dst.assign(chars, nul ? static_cast<std::size_t>(nul - chars) : ref.count);
      return {};
    }

    /** Decode a vector data block referenced by @a ref out of @a base:
        `ref.count` element images of `layoutSize<U>` bytes. Trivially-copyable
        elements bulk-copy; non-trivial ones recurse through _readArrayElements.
        An externally-resolved sequence with an empty @a base (a missing .anim
        file) leaves the member empty rather than failing.
        @tparam M the vector type.
        @param dst      the destination vector, cleared first.
        @param ref      the {count, offset} slot.
        @param base     the buffer the offset resolves against.
        @param ctx      per-sequence base resolution.
        @param external set for an externally-resolved `SequenceData` array.
        @param what     the member name, for diagnostics.
        @return nothing, or OffsetOutOfBounds if the block overruns @a base. */
    template <typename M>
    static Result<void> _readVectorBlock(M& dst,
                                           const M2ArrayRef ref,
                                           const std::span<const std::byte> base,
                                           const OffsetReadContext& ctx,
                                           const bool external,
                                           const std::string_view what) {
      using U = typename M::value_type;
      dst.clear();
      if (ref.count == 0) return {};
      // An external sequence element whose context base is empty (missing .anim
      // file): leave the member empty rather than fail.
      if (base.empty()) return {};
      constexpr std::size_t elem = layoutSize<U, Derived::Version>();
      if (std::uint64_t{ref.offset} + std::uint64_t{ref.count} * elem > base.size())
        return _offsetError(ErrorCode::OffsetOutOfBounds, what,
                             std::format("array [{} x {} at {:#x}] overruns the {}-byte buffer", ref.count, elem,
                                         ref.offset, base.size()));
      dst.resize(ref.count);
      if constexpr (std::is_trivially_copyable_v<U>) {
        std::memcpy(dst.data(), base.data() + ref.offset, std::size_t{ref.count} * elem);
        return {};
      }
      else return _readArrayElements(dst, ref, base, ctx, external, what);
    }

    /** Read the non-trivial elements of @a dst, each from its own element image
        inside @a base. For an externally-resolved `SequenceData` array, element
        i reads its nested blocks from ctx.sequenceBase(i) (the matching .anim
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
    static Result<void> _readArrayElements(M& dst,
                                             const M2ArrayRef ref,
                                             const std::span<const std::byte> base,
                                             const OffsetReadContext& ctx,
                                             const bool external,
                                             const std::string_view what) {
      using U = typename M::value_type;
      constexpr std::size_t elem = layoutSize<U, Derived::Version>();
      for (std::size_t i = 0; i < dst.size(); ++i) {
        const auto elementImage = base.subspan(ref.offset + i * elem, elem);
        const std::span<const std::byte> elementBase = external && ctx.sequenceBase ? ctx.sequenceBase(i) : base;
        std::size_t elementCursor = 0;
        if (auto r = _readMember(dst[i], elementImage, elementCursor, elementBase, ctx, false, what); !r) return r;
      }
      return {};
    }

    // --- write engine ---------------------------------------------------------

    /** Write @a n raw bytes into @a image at @a imagePos + @a cursor, advancing
        the cursor. The image is pre-sized to the entity footprint, so this write
        stays within bounds.
        @param image    the pre-sized image buffer.
        @param imagePos the base offset of the record being written within @a image.
        @param cursor   the position within the record; advanced by @a n.
        @param bytes    the source bytes.
        @param n        the byte count. */
    static void _putBytes(FileBuffer& image,
                           const std::size_t imagePos,
                           std::size_t& cursor,
                           const void* bytes,
                           const std::size_t n) {
      std::memcpy(image.data() + imagePos + cursor, bytes, n);
      cursor += n;
    }

    /** Reserve @a bytes at the (16-byte aligned, zero-filled) end of @a target
        and return the start index. Blizzard aligns every data block to 16 bytes;
        an index (not a pointer) is returned because the buffer reallocates as it
        grows.
        @param target the buffer to grow.
        @param bytes  the block size to reserve.
        @return the byte index of the reserved block. */
    static std::size_t
    _allocBlock(FileBuffer& target, const std::size_t bytes) {
      target.resize((target.size() + 15) / 16 * 16);
      const std::size_t at = target.size();
      target.resize(target.size() + bytes);
      return at;
    }

    /** Serialize @a self in canonical layout — the image first (pre-sized to
        imageSize()), then data blocks depth-first via _writeMembers — and append
        the result to @a out. Offsets are recorded relative to the image start, so
        the image is built into a fresh buffer before appending.
        @param self the entity to serialize.
        @param out  the destination buffer (appended, not cleared).
        @param ctx  per-sequence sink resolution.
        @return nothing, or the first error. */
    static Result<void> _writeImage(const Derived& self, FileBuffer& out, const OffsetWriteContext& ctx) {
      FileBuffer buffer;
      buffer.resize(self.imageSize());
      std::size_t cursor = 0;
      if (auto r = _writeMembers(self, buffer, 0, cursor, buffer, ctx); !r) return r;
      out.insert(out.end(), buffer.begin(), buffer.end());
      return {};
    }

    /** Write every version-active member of record/entity @a src into @a image
        at @a imagePos + cursor, in layout order; data blocks append to
        @a blocks. Mirrors _readMembers exactly.
        @tparam T the record/entity type.
        @param src      the source record.
        @param image    the pre-sized image buffer.
        @param imagePos the base offset of @a src's image within @a image.
        @param cursor   the position within the record; advanced past the members.
        @param blocks   the buffer receiving array/string data blocks.
        @param ctx      per-sequence sink resolution.
        @return nothing, or the first error. */
    template <typename T>
    static Result<void> _writeMembers(const T& src,
                                       FileBuffer& image,
                                       const std::size_t imagePos,
                                       std::size_t& cursor,
                                       FileBuffer& blocks,
                                       const OffsetWriteContext& ctx) {
      static constexpr auto Members = formats::detail::membersOf<T>();
      static constexpr auto Order = _memberOrder<T>();
      std::optional<Error> failed;
      template for (constexpr std::size_t idx : Order) {
        constexpr auto m = Members[idx];
        if constexpr (formats::detail::versionActive<Derived::Version, m>()) {
          constexpr std::string_view name = std::meta::identifier_of(m);
          constexpr bool isSeq = formats::detail::annotation<formats::detail::SequenceDataSpec, m>().has_value();
          if (!failed && _memberPresent<m>(src))
            if (auto r = _writeMember(src.[:m:], image, imagePos, cursor, blocks, ctx,
                                       _resolvesExternally<isSeq>(src), name); !r) failed = r.error();
        }
      }
      if (failed) return std::unexpected{*failed};
      return {};
    }

    /** Write one member @a src — an array/string reference, an inline sub-record,
        or an inline scalar — advancing @a cursor over its in-image bytes.
        @tparam M the member type; the concept it satisfies picks the branch.
        @param src      the source member.
        @param image    the pre-sized image buffer.
        @param imagePos the base offset of the containing record within @a image.
        @param cursor   the position within the record; advanced.
        @param blocks   the buffer receiving data blocks.
        @param ctx      per-sequence sink resolution.
        @param external set only for a `SequenceData` array routing through @a ctx.
        @param what     the member name, for diagnostics.
        @return nothing, or the first error. */
    template <typename M>
    static Result<void> _writeMember(const M& src,
                                      FileBuffer& image,
                                      const std::size_t imagePos,
                                      std::size_t& cursor,
                                      FileBuffer& blocks,
                                      const OffsetWriteContext& ctx,
                                      const bool external,
                                      const std::string_view what) {
      if constexpr (OffsetArrayMember<M>)
        return _writeArrayMember(src, image, imagePos, cursor, blocks, ctx, external, what);
      else if constexpr (InlineRecordMember<M>) return _writeMembers(src, image, imagePos, cursor, blocks, ctx);
      else {
        static_assert(InlineScalarMember<M>, "offset members are arrays, records, or scalars");
        _putBytes(image, imagePos, cursor, &src, sizeof(M));
        return {};
      }
    }

    /** Write an M2Array-referenced member: allocate its data block, fill it, and
        emit the {count, offset} slot.
        @tparam M the member type (std::string or std::vector<U>).
        @param src      the source member.
        @param image    the pre-sized image buffer.
        @param imagePos the base offset of the containing record within @a image.
        @param cursor   the position within the record; advanced past the slot.
        @param blocks   the buffer receiving the data block.
        @param ctx      per-sequence sink resolution.
        @param external set for an externally-resolved `SequenceData` array.
        @param what     the member name, for diagnostics.
        @return nothing, or the first error. */
    template <typename M>
    static Result<void> _writeArrayMember(const M& src,
                                            FileBuffer& image,
                                            const std::size_t imagePos,
                                            std::size_t& cursor,
                                            FileBuffer& blocks,
                                            const OffsetWriteContext& ctx,
                                            const bool external,
                                            const std::string_view what) {
      if constexpr (OffsetStringMember<M>) return _writeStringBlock(src, image, imagePos, cursor, blocks);
      else
        return _writeVectorBlock(src, image, imagePos, cursor, blocks, ctx, external, what);
    }

    /** Emit @a src as an M2Array<char> whose count includes the NUL terminator —
        the client reads these as buffer sizes and requires the terminator inside,
        so even the empty string writes {1, block}, never {0, 0}.
        @param src      the source string.
        @param image    the pre-sized image buffer.
        @param imagePos the base offset of the containing record within @a image.
        @param cursor   the position within the record; advanced past the slot.
        @param blocks   the buffer receiving the character block.
        @return nothing (never fails). */
    static Result<void> _writeStringBlock(const std::string& src,
                                            FileBuffer& image,
                                            const std::size_t imagePos,
                                            std::size_t& cursor,
                                            FileBuffer& blocks) {
      const std::size_t at = _allocBlock(blocks, src.size() + 1);
      std::memcpy(blocks.data() + at, src.data(), src.size());
      blocks[at + src.size()] = std::byte{0};
      const M2ArrayRef ref{static_cast<std::uint32_t>(src.size() + 1), static_cast<std::uint32_t>(at)};
      _putBytes(image, imagePos, cursor, &ref, sizeof ref);
      return {};
    }

    /** Emit @a src as an M2Array: an empty vector writes {0, 0}; otherwise a
        `count * layoutSize<U>` element block is reserved, filled (bulk memcpy for
        trivially-copyable elements, else _writeArrayElements), and the slot
        emitted.
        @tparam M the vector type.
        @param src      the source vector.
        @param image    the pre-sized image buffer.
        @param imagePos the base offset of the containing record within @a image.
        @param cursor   the position within the record; advanced past the slot.
        @param blocks   the buffer receiving the element block.
        @param ctx      per-sequence sink resolution.
        @param external set for an externally-resolved `SequenceData` array.
        @param what     the member name, for diagnostics.
        @return nothing, or the first error. */
    template <typename M>
    static Result<void> _writeVectorBlock(const M& src,
                                            FileBuffer& image,
                                            const std::size_t imagePos,
                                            std::size_t& cursor,
                                            FileBuffer& blocks,
                                            const OffsetWriteContext& ctx,
                                            const bool external,
                                            const std::string_view what) {
      using U = typename M::value_type;
      if (src.empty()) {
        const M2ArrayRef empty{0, 0};
        _putBytes(image, imagePos, cursor, &empty, sizeof empty);
        return {};
      }
      constexpr std::size_t elem = layoutSize<U, Derived::Version>();
      const std::size_t at = _allocBlock(blocks, src.size() * elem);
      const M2ArrayRef ref{static_cast<std::uint32_t>(src.size()), static_cast<std::uint32_t>(at)};
      _putBytes(image, imagePos, cursor, &ref, sizeof ref);
      if constexpr (std::is_trivially_copyable_v<U>) {
        std::memcpy(blocks.data() + at, src.data(), src.size() * elem);
        return {};
      }
      else return _writeArrayElements(src, at, blocks, ctx, external, what);
    }

    /** Write the non-trivial elements of @a src into the block already reserved
        at @a blockAt in @a blocks. Each element's inline image lands in that
        block; its own nested data blocks append to the sequence sink
        (ctx.sequenceSink(i), the .anim buffer) when this is an externally
        resolved `SequenceData` array, else to @a blocks.
        @tparam M the vector type.
        @param src      the source vector.
        @param blockAt the byte index of the reserved element block in @a blocks.
        @param blocks   the buffer holding the element images (and, by default,
                        their nested data).
        @param ctx      per-sequence sink resolution.
        @param external whether this is an externally-resolved sequence array.
        @param what     the member name, for diagnostics.
        @return nothing, or the first error. */
    template <typename M>
    static Result<void> _writeArrayElements(const M& src,
                                              const std::size_t blockAt,
                                              FileBuffer& blocks,
                                              const OffsetWriteContext& ctx,
                                              const bool external,
                                              const std::string_view what) {
      using U = typename M::value_type;
      constexpr std::size_t elem = layoutSize<U, Derived::Version>();
      for (std::size_t i = 0; i < src.size(); ++i) {
        FileBuffer* sink = external && ctx.sequenceSink ? ctx.sequenceSink(i) : nullptr;
        std::size_t elementCursor = 0;
        if (auto r = _writeMember(src[i], blocks, blockAt + i * elem, elementCursor, sink ? *sink : blocks, ctx,
                                   false, what); !r) return r;
      }
      return {};
    }

    // --- diagnostics ----------------------------------------------------------

    /** Build an offset-scoped error naming the offending @a member.
        @param code   the error category (typically OffsetOutOfBounds).
        @param member the entity member being transferred.
        @param what   the failure description.
        @return the error, ready to return from a Result function. */
    static std::unexpected<Error> _offsetError(const ErrorCode code,
                                                const std::string_view member,
                                                const std::string_view what) {
      return makeError(code, std::format("offset member '{}': {}", member, what));
    }
  };

  /** A type the offset engine can read and write: it is an M2 offset block and
      declares the client version it is laid out for. */
  template <typename E> concept OffsetEntity = std::derived_from<E, M2OffsetBase> && requires {
    { E::Version } -> std::convertible_to<ClientVersion>;
  };
}
