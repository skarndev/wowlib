#pragma once

/** @file
    The offset-format storage vocabulary and serializer engine in one header.

    Vocabulary: the OffsetFile mixin that gives an offset-based entity (M2 and
    its satellite files) its read()/write() serialization methods, and the I/O
    contexts that route per-sequence data to/from external buffers (.anim
    files). An offset entity is the in-memory face of a wire layout made of
    inline scalars and `M2Array{count, offset}` references — the M2 family's
    format, as opposed to the fourcc+size chunk streams ChunkedFile covers.
    Members store `std::vector<T>`/`std::string`; the wire M2Array never
    surfaces as a user type. Unlike the chunk framework there is NO byte-perfect
    round-trip guarantee: writes always produce wowlib's canonical relayout
    (user decision 2026-07-24, see .claude/context/m2-architecture.md), and the
    tested guarantee is semantic — an entity written and re-read compares equal.

    Engine: detail::read_offset_entity / detail::write_offset_entity walk an
    offset entity's members (`template for` over the reflected member list)
    and map them onto the M2-family wire layout: inline scalars plus
    `M2Array{count, offset}` references whose data blocks live elsewhere in
    the buffer. Member kinds are type-driven, mirroring the chunk serializer:
    - trivially copyable T           -> inline wire bytes (exact sizeof);
    - std::vector<T>                 -> an M2Array slot + a data block of
                                        wire images (recursive when T is
                                        itself a vector/string/record);
    - std::string                    -> an M2Array<char> counting the NUL
                                        (the client reads these as buffers,
                                        terminator included);
    - any other class type           -> an inline record: its members recurse
                                        at the current cursor (M2Track).

    Wire walk order is declaration/flatten order, overridable by an
    authoritative `static constexpr wire_order` array of member names — needed
    once version-gated members live in conditionally-inherited trait bases,
    whose flatten order is by-trait, not the interleaved wire order. Writes
    always produce the canonical relayout (image first, then data blocks
    depth-first in wire order, 16-byte aligned). */

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

namespace wowlib::formats
{
  /** The non-template marker base of every offset entity (what the OffsetEntity
      concept detects). Carries no state — offset entities have no round-trip
      bookkeeping to store. */
  struct OffsetBase
  {
    // excluded: the parameter type is this unwelded base, not the entity
    [[=welder::mark::exclude]]
    bool operator==(const OffsetBase&) const = default;
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

  /** The serialization face of an offset entity, mixed in CRTP-style: an
      entity `struct E : OffsetFile<E>` gains the read()/write() methods.
      Method definitions sit at the bottom of this header, past the
      serializer engine they drive.

      The `read(span)` / `write(FileBuffer&)` / `empty()` trio deliberately
      matches the chunk framework's SelfSerializing concept, so an offset
      entity can sit directly behind a chunk member (the Legion+ MD21 chunk
      carries the whole MD20 image as its payload).
      @tparam Derived the entity itself (the CRTP pattern). */
  template <typename Derived>
  struct OffsetFile : OffsetBase
  {
    [[=welder::doc("Deserialize file bytes into this entity, replacing its "
                   "contents. Offsets resolve against the given buffer; "
                   "sequence-gated data is read inline.")]]
    Result<void> read(std::span<const std::byte> data
                      [[=welder::doc("the file (or containing-chunk payload) bytes")]]);

    /** Deserialize @a data with external sequence data resolved through
        @a ctx (the M2 .anim baking path).
        @param data the file (or containing-chunk payload) bytes.
        @param ctx  per-sequence base resolution for `sequence_data` members.
        @return nothing, or the first structural error. */
    [[=welder::mark::exclude]]
    Result<void> read(std::span<const std::byte> data, const OffsetReadContext& ctx);

    [[nodiscard]]
    [[=welder::doc("Serialize this entity in wowlib's canonical layout (an "
                   "offset format has no byte-perfect round-trip guarantee; "
                   "a written entity re-reads equal instead)."),
      =welder::returns("the file bytes")]]
    Result<FileBuffer> write() const;

    /** Serialize with external sequence data routed through @a ctx (the M2
        .anim splitting path).
        @param ctx per-sequence sink resolution for `sequence_data` members.
        @return the file bytes, or the first error. */
    [[=welder::mark::exclude]]
    Result<FileBuffer> write(const OffsetWriteContext& ctx) const;

    /** Append this entity's serialized image to @a out (the chunk
        serializer's SelfSerializing write hook — offsets are relative to the
        start of the appended image).
        @param out the destination buffer (appended, not cleared).
        @return nothing, or the first error. */
    [[=welder::mark::exclude]]
    Result<void> write(FileBuffer& out) const;

    /** Never empty: an offset entity always has a header image worth writing
        (the SelfSerializing engagement hook — an MD21-style carrier chunk is
        always emitted). */
    [[=welder::mark::exclude]]
    bool empty() const
    {
      return false;
    }

    [[=welder::mark::exclude]]
    bool operator==(const OffsetFile&) const = default;
  };

  /** A type the offset serializer can read and write: carries the OffsetBase
      marker and knows which client version it is laid out for. */
  template <typename E>
  concept OffsetEntity = std::derived_from<E, OffsetBase> && requires {
    { E::version } -> std::convertible_to<ClientVersion>;
  };

  namespace detail
  {
    /** The on-disk shape of an offset-array reference: element count and the
        byte offset of the data block, relative to the entity's base. */
    struct M2ArrayWire
    {
      std::uint32_t count = 0;
      std::uint32_t offset = 0;
    };
    static_assert(sizeof(M2ArrayWire) == 8 && std::is_trivially_copyable_v<M2ArrayWire>);

    /** Trait: is @a T a std::string (an offset-string member)? */
    template <typename T>
    inline constexpr bool is_wire_string_v = std::is_same_v<T, std::string>;

    /** Whether @a T serializes as an inline record — a class the walker
        recurses into member-by-member at the current cursor (M2Track and
        friends). Everything trivially copyable is a raw wire value instead. */
    template <typename T>
    inline constexpr bool is_offset_record_v =
      std::is_class_v<T> && !std::is_trivially_copyable_v<T> && !is_vector_v<T>
      && !is_wire_string_v<T>;

    /** The member indices (into members_of<T>) in wire-walk order. When @a T
        declares `static constexpr wire_order` (an array of member-name
        string_views), members follow it; names that match no member are
        skipped (they belong to other versions' trait slots), but every member
        @a T does have must be listed exactly once (asserted). Without a
        wire_order, declaration/flatten order applies. */
    template <typename T>
    consteval auto offset_order()
    {
      constexpr auto members = members_of<T>();
      std::vector<std::size_t> order;
      if constexpr (requires { T::wire_order; })
      {
        std::vector<bool> used(members.size(), false);
        for (std::string_view name : T::wire_order)
          for (std::size_t i = 0; i < members.size(); ++i)
            if (!used[i] && std::meta::has_identifier(members[i])
                && std::meta::identifier_of(members[i]) == name)
            {
              order.push_back(i);
              used[i] = true;
              break;
            }
        if (order.size() != members.size())
          throw "wire_order must list every data member exactly once";
      }
      else
        for (std::size_t i = 0; i < members.size(); ++i)
          order.push_back(i);
      return std::define_static_array(order);
    }

    /** The wire image size of @a T for entity version @a V: sizeof for raw
        values, one M2Array slot for vectors/strings, the version-active member
        sum for records (client records have no implicit padding — the sum IS
        the layout). gated_by members are rejected inside records: their size
        is runtime state, only supported on the entity top level where
        entity_image_size accounts for them. */
    template <typename T, ClientVersion V>
    constexpr std::size_t wire_size()
    {
      if constexpr (is_vector_v<T> || is_wire_string_v<T>)
        return sizeof(M2ArrayWire);
      else if constexpr (std::is_trivially_copyable_v<T>)
        return sizeof(T);
      else
      {
        static_assert(is_offset_record_v<T>);
        std::size_t total = 0;
        static constexpr auto members = members_of<T>();
        template for (constexpr auto m : members)
        {
          static_assert(!annotation<gated_by_spec, m>().has_value(),
                        "gated_by members are only supported on the entity top level");
          if constexpr (version_active<V, m>())
          {
            using MT = [:std::meta::type_of(m):];
            total += wire_size<MT, V>();
          }
        }
        return total;
      }
    }

    /** Build an offset-scoped error value.
        @param code   the error category.
        @param member the entity member being transferred.
        @param what   the failure description.
        @return the error, ready to return from a Result function. */
    inline std::unexpected<Error> offset_error(ErrorCode code, std::string_view member,
                                               std::string_view what)
    {
      return make_error(code, std::format("offset member '{}': {}", member, what));
    }

    /** Append-only block allocation for the canonical relayout: blocks are
        16-byte aligned (zero gap fill, Blizzard's preferred alignment) and
        addressed by index, never by pointer — buffers reallocate as they
        grow. */
    inline std::size_t alloc_block(FileBuffer& target, std::size_t bytes)
    {
      target.resize((target.size() + 15) / 16 * 16);
      const std::size_t at = target.size();
      target.resize(target.size() + bytes);
      return at;
    }

    template <ClientVersion V, typename M>
    Result<void> read_wire(M& dst, std::span<const std::byte> image, std::size_t& cursor,
                           std::span<const std::byte> base, const OffsetReadContext& ctx,
                           bool sequence_gated, std::string_view what);

    template <ClientVersion V, typename M>
    Result<void> write_wire(const M& src, FileBuffer& image_buf, std::size_t image_pos,
                            std::size_t& cursor, FileBuffer& base_buf,
                            const OffsetWriteContext& ctx, bool sequence_gated,
                            std::string_view what);

    /** Read record/entity @a T's members from @a image at @a cursor, data
        blocks resolving against @a base. The gated_by check reads
        `dst.global_flags`, valid because wire order puts the flags first. */
    template <ClientVersion V, typename T>
    Result<void> read_members(T& dst, std::span<const std::byte> image, std::size_t& cursor,
                              std::span<const std::byte> base, const OffsetReadContext& ctx)
    {
      static constexpr auto members = members_of<T>();
      static constexpr auto order = offset_order<T>();
      std::optional<Error> failed;
      template for (constexpr std::size_t idx : order)
      {
        constexpr auto m = members[idx];
        if constexpr (version_active<V, m>())
        {
          constexpr auto name = std::meta::identifier_of(m);
          constexpr bool seq = annotation<sequence_data_spec, m>().has_value();
          // A record bound to a global sequence keeps its single timeline
          // inline — per-sequence resolution only applies to sequence tracks.
          bool seq_external = seq;
          if constexpr (seq && requires { dst.global_sequence; })
            seq_external = seq && dst.global_sequence == 0xFFFF;
          bool present = true;
          if constexpr (constexpr auto gate = annotation<gated_by_spec, m>(); gate.has_value())
          {
            static_assert(requires { dst.global_flags; },
                          "gated_by members need a global_flags member to gate on");
            present = (static_cast<std::uint32_t>(dst.global_flags) & gate->mask) != 0;
          }
          if (!failed && present)
            if (auto r = read_wire<V>(dst.[:m:], image, cursor, base, ctx, seq_external, name); !r)
              failed = r.error();
        }
      }
      if (failed)
        return std::unexpected{*failed};
      return {};
    }

    /** Write record/entity @a T's members into @a image_buf at
        @a image_pos + cursor, data blocks appended to @a base_buf. */
    template <ClientVersion V, typename T>
    Result<void> write_members(const T& src, FileBuffer& image_buf, std::size_t image_pos,
                               std::size_t& cursor, FileBuffer& base_buf,
                               const OffsetWriteContext& ctx)
    {
      static constexpr auto members = members_of<T>();
      static constexpr auto order = offset_order<T>();
      std::optional<Error> failed;
      template for (constexpr std::size_t idx : order)
      {
        constexpr auto m = members[idx];
        if constexpr (version_active<V, m>())
        {
          constexpr auto name = std::meta::identifier_of(m);
          constexpr bool seq = annotation<sequence_data_spec, m>().has_value();
          // Mirror the read side: global-sequence-bound records stay inline.
          bool seq_external = seq;
          if constexpr (seq && requires { src.global_sequence; })
            seq_external = seq && src.global_sequence == 0xFFFF;
          bool present = true;
          if constexpr (constexpr auto gate = annotation<gated_by_spec, m>(); gate.has_value())
          {
            static_assert(requires { src.global_flags; },
                          "gated_by members need a global_flags member to gate on");
            present = (static_cast<std::uint32_t>(src.global_flags) & gate->mask) != 0;
          }
          if (!failed && present)
            if (auto r = write_wire<V>(src.[:m:], image_buf, image_pos, cursor, base_buf, ctx,
                                       seq_external, name);
                !r)
              failed = r.error();
        }
      }
      if (failed)
        return std::unexpected{*failed};
      return {};
    }

    template <ClientVersion V, typename M>
    Result<void> read_wire(M& dst, std::span<const std::byte> image, std::size_t& cursor,
                           std::span<const std::byte> base, const OffsetReadContext& ctx,
                           bool sequence_gated, std::string_view what)
    {
      if constexpr (is_vector_v<M> || is_wire_string_v<M>)
      {
        if (cursor > image.size() || image.size() - cursor < sizeof(M2ArrayWire))
          return offset_error(ErrorCode::OffsetOutOfBounds, what, "M2Array slot overruns the image");
        M2ArrayWire arr;
        std::memcpy(&arr, image.data() + cursor, sizeof arr);
        cursor += sizeof arr;

        if constexpr (is_wire_string_v<M>)
        {
          dst.clear();
          if (arr.count == 0)
            return {};
          if (std::uint64_t{arr.offset} + arr.count > base.size())
            return offset_error(ErrorCode::OffsetOutOfBounds, what,
                                std::format("string [{} at {:#x}] overruns the {}-byte buffer",
                                            arr.count, arr.offset, base.size()));
          const auto* chars = reinterpret_cast<const char*>(base.data() + arr.offset);
          const auto* nul = static_cast<const char*>(std::memchr(chars, 0, arr.count));
          dst.assign(chars, nul ? static_cast<std::size_t>(nul - chars) : arr.count);
          return {};
        }
        else
        {
          using U = typename M::value_type;
          dst.clear();
          if (arr.count == 0)
            return {};
          // a sequence element whose context base is empty: data unavailable
          // (missing .anim file) — leave the member empty rather than fail.
          if (base.empty())
            return {};
          constexpr std::size_t elem = wire_size<U, V>();
          if (std::uint64_t{arr.offset} + std::uint64_t{arr.count} * elem > base.size())
            return offset_error(
              ErrorCode::OffsetOutOfBounds, what,
              std::format("array [{} x {} at {:#x}] overruns the {}-byte buffer", arr.count, elem,
                          arr.offset, base.size()));
          if constexpr (std::is_trivially_copyable_v<U>)
          {
            dst.resize(arr.count);
            std::memcpy(dst.data(), base.data() + arr.offset, std::size_t{arr.count} * elem);
            return {};
          }
          else
          {
            dst.resize(arr.count);
            for (std::size_t i = 0; i < dst.size(); ++i)
            {
              const auto elem_image = base.subspan(arr.offset + i * elem, elem);
              // a sequence_data member's element i resolves its own data
              // against the context-provided base (the .anim file bytes).
              const std::span<const std::byte> elem_base =
                sequence_gated && ctx.sequence_base ? ctx.sequence_base(i) : base;
              std::size_t elem_cursor = 0;
              if (auto r = read_wire<V, U>(dst[i], elem_image, elem_cursor, elem_base, ctx,
                                           false, what);
                  !r)
                return r;
            }
            return {};
          }
        }
      }
      else if constexpr (is_offset_record_v<M>)
      {
        return read_members<V>(dst, image, cursor, base, ctx);
      }
      else
      {
        static_assert(std::is_trivially_copyable_v<M>, "offset members are wire values");
        if (image.size() < sizeof(M) || image.size() - sizeof(M) < cursor)
          return offset_error(ErrorCode::OffsetOutOfBounds, what,
                              std::format("{}-byte value at image offset {:#x} overruns the image",
                                          sizeof(M), cursor));
        std::memcpy(&dst, image.data() + cursor, sizeof(M));
        cursor += sizeof(M);
        return {};
      }
    }

    template <ClientVersion V, typename M>
    Result<void> write_wire(const M& src, FileBuffer& image_buf, std::size_t image_pos,
                            std::size_t& cursor, FileBuffer& base_buf,
                            const OffsetWriteContext& ctx, bool sequence_gated,
                            std::string_view what)
    {
      const auto put = [&](const void* bytes, std::size_t n) {
        std::memcpy(image_buf.data() + image_pos + cursor, bytes, n);
        cursor += n;
      };
      if constexpr (is_vector_v<M> || is_wire_string_v<M>)
      {
        if constexpr (is_wire_string_v<M>)
        {
          // count includes the NUL terminator — the client reads these as
          // buffer sizes and requires the terminator inside (even for the
          // empty string, hence never {0, 0}).
          const std::size_t block = alloc_block(base_buf, src.size() + 1);
          std::memcpy(base_buf.data() + block, src.data(), src.size());
          base_buf[block + src.size()] = std::byte{0};
          const M2ArrayWire arr{static_cast<std::uint32_t>(src.size() + 1),
                                static_cast<std::uint32_t>(block)};
          put(&arr, sizeof arr);
          return {};
        }
        else
        {
          using U = typename M::value_type;
          if (src.empty())
          {
            const M2ArrayWire arr{0, 0};
            put(&arr, sizeof arr);
            return {};
          }
          constexpr std::size_t elem = wire_size<U, V>();
          const std::size_t block = alloc_block(base_buf, src.size() * elem);
          const M2ArrayWire arr{static_cast<std::uint32_t>(src.size()),
                                static_cast<std::uint32_t>(block)};
          put(&arr, sizeof arr);
          if constexpr (std::is_trivially_copyable_v<U>)
          {
            std::memcpy(base_buf.data() + block, src.data(), src.size() * elem);
            return {};
          }
          else
          {
            for (std::size_t i = 0; i < src.size(); ++i)
            {
              // a sequence_data member's element i appends its own data to the
              // context-provided sink (the .anim buffer being assembled).
              FileBuffer* sink = nullptr;
              if (sequence_gated && ctx.sequence_sink)
                sink = ctx.sequence_sink(i);
              std::size_t elem_cursor = 0;
              if (auto r = write_wire<V, U>(src[i], base_buf, block + i * elem, elem_cursor,
                                            sink ? *sink : base_buf, ctx, false, what);
                  !r)
                return r;
            }
            return {};
          }
        }
      }
      else if constexpr (is_offset_record_v<M>)
      {
        return write_members<V>(src, image_buf, image_pos, cursor, base_buf, ctx);
      }
      else
      {
        static_assert(std::is_trivially_copyable_v<M>, "offset members are wire values");
        put(&src, sizeof(M));
        return {};
      }
    }

    /** The runtime image size of entity @a e: the static version-active
        member sum plus any engaged gated members. */
    template <OffsetEntity E>
    std::size_t entity_image_size(const E& e)
    {
      static constexpr auto members = members_of<E>();
      std::size_t total = 0;
      template for (constexpr auto m : members)
      {
        if constexpr (version_active<E::version, m>())
        {
          using M = [:std::meta::type_of(m):];
          if constexpr (constexpr auto gate = annotation<gated_by_spec, m>(); gate.has_value())
          {
            if ((static_cast<std::uint32_t>(e.global_flags) & gate->mask) != 0)
              total += wire_size<M, E::version>();
          }
          else
            total += wire_size<M, E::version>();
        }
      }
      return total;
    }

    /** Deserialize @a data into @a entity — the engine behind
        OffsetFile::read(); see that method for the contract.
        @param entity the destination entity; overwritten member-by-member.
        @param data   the file (or containing-chunk payload) bytes; doubles as
                      the offset base.
        @param ctx    sequence-data base resolution.
        @return nothing, or the first structural error. */
    template <OffsetEntity E>
    Result<void> read_offset_entity(E& entity, std::span<const std::byte> data,
                                    const OffsetReadContext& ctx)
    {
      std::size_t cursor = 0;
      return read_members<E::version>(entity, data, cursor, data, ctx);
    }

    /** Serialize @a entity, appending its image + data blocks to @a out — the
        engine behind OffsetFile::write(); see that method for the contract.
        Builds into a fresh buffer first: offsets are relative to the image
        start, not to @a out.
        @param entity the entity to serialize.
        @param out    the destination buffer (appended, not cleared).
        @param ctx    sequence-data sink resolution.
        @return nothing, or the first error. */
    template <OffsetEntity E>
    Result<void> write_offset_entity(const E& entity, FileBuffer& out,
                                     const OffsetWriteContext& ctx)
    {
      FileBuffer local;
      local.resize(entity_image_size(entity));
      std::size_t cursor = 0;
      if (auto r = write_members<E::version>(entity, local, 0, cursor, local, ctx); !r)
        return r;
      out.insert(out.end(), local.begin(), local.end());
      return {};
    }
  }

  /** The static wire offset of entity member @a name for @a E's version: the
      version-active member sizes summed in wire order up to (not including)
      the member. Lets a caller stamp a derived wire field into an
      already-serialized image (the M2 assembly stamping num_skin_profiles
      from its skins vector) without mutating entity state. Only valid while
      no runtime-gated (gated_by) member precedes @a name — the offset would
      be runtime state then; both that and the member's existence are
      enforced at compile time.
      @tparam E the offset entity type.
      @param name the member identifier.
      @return the byte offset inside the entity's wire image. */
  template <OffsetEntity E>
  consteval std::size_t wire_offset_of(std::string_view name)
  {
    // static: `template for` needs a constant address for the range
    static constexpr auto members = detail::members_of<E>();
    static constexpr auto order = detail::offset_order<E>();
    std::size_t off = 0;
    bool found = false;
    template for (constexpr std::size_t idx : order)
    {
      constexpr auto m = members[idx];
      if constexpr (detail::version_active<E::version, m>())
      {
        if (!found && std::meta::identifier_of(m) == name)
          found = true;
        if (!found)
        {
          if (detail::annotation<detail::gated_by_spec, m>().has_value())
            throw "wire_offset_of: a runtime-gated (gated_by) member precedes the target";
          using MT = [:std::meta::type_of(m):];
          off += detail::wire_size<MT, E::version>();
        }
      }
    }
    if (!found)
      throw "wire_offset_of: the entity has no such wire member";
    return off;
  }

  // --- OffsetFile method definitions (declared above) ----------------

  template <typename Derived>
  Result<void> OffsetFile<Derived>::read(std::span<const std::byte> data)
  {
    return detail::read_offset_entity(static_cast<Derived&>(*this), data, {});
  }

  template <typename Derived>
  Result<void> OffsetFile<Derived>::read(std::span<const std::byte> data,
                                         const OffsetReadContext& ctx)
  {
    return detail::read_offset_entity(static_cast<Derived&>(*this), data, ctx);
  }

  template <typename Derived>
  Result<FileBuffer> OffsetFile<Derived>::write() const
  {
    return write(OffsetWriteContext{});
  }

  template <typename Derived>
  Result<FileBuffer> OffsetFile<Derived>::write(const OffsetWriteContext& ctx) const
  {
    FileBuffer out;
    if (auto r = detail::write_offset_entity(static_cast<const Derived&>(*this), out, ctx); !r)
      return std::unexpected{r.error()};
    return out;
  }

  template <typename Derived>
  Result<void> OffsetFile<Derived>::write(FileBuffer& out) const
  {
    return detail::write_offset_entity(static_cast<const Derived&>(*this), out,
                                       OffsetWriteContext{});
  }
}
