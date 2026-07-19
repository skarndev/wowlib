#pragma once

/** @file
    The reflection-driven chunk serializer: the engine behind
    ChunkedFile::read()/write(). detail::read_entity / detail::write_entity
    walk a chunked entity's annotated members (`template for` over the
    reflected member list) and map them onto the fourcc+size chunk stream.

    Reading is chunk-order-independent — chunks dispatch by fourcc into the
    matching member — while every encounter is journaled so writing can replay
    the original order and reproduce the file byte for byte, unknown chunks and
    trailing bytes included. Fresh entities (empty journal) write their members
    in declaration order, which each entity declares in canonical client order. */

#include <meta>

#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/formats/common/annotations.hpp>
#include <wowlib/formats/common/chunk.hpp>
#include <wowlib/formats/common/fourcc.hpp>
#include <wowlib/formats/common/string_block.hpp>

namespace wowlib::formats
{
  /** A type the chunk serializer can read and write: carries the round-trip
      bookkeeping and knows which client version it is laid out for. */
  template <typename E>
  concept ChunkedEntity = std::derived_from<E, ChunkExtras> && requires {
    { E::version } -> std::convertible_to<ClientVersion>;
  };

  /** A member type that owns its chunk-payload encoding: the serializer hands
      it the raw payload on read and the output buffer on write (StringBlock,
      ChunkBlob). */
  template <typename T>
  concept SelfSerializing =
    requires(T& value, const T& constant, std::span<const std::byte> payload, FileBuffer& out) {
      { value.read(payload) } -> std::same_as<Result<void>>;
      { constant.write(out) } -> std::same_as<Result<void>>;
      { constant.empty() } -> std::convertible_to<bool>;
    };

  namespace detail
  {
    /** The first annotation of type @a Spec on reflected member @a M, if any.
        @tparam Spec the annotation payload type (a `*_spec` struct).
        @tparam M    the reflected member.
        @return the payload, or nullopt when the member is unannotated. */
    template <typename Spec, std::meta::info M>
    consteval std::optional<Spec> annotation()
    {
      auto anns = std::meta::annotations_of_with_type(M, ^^Spec);
      if (anns.empty())
        return std::nullopt;
      return std::meta::extract<Spec>(anns[0]);
    }

    /** Whether member @a M participates for entity version @a V, per its
        since/until annotations (absent bounds mean unbounded).
        @tparam V the entity's client version.
        @tparam M the reflected member. */
    template <ClientVersion V, std::meta::info M>
    consteval bool version_active()
    {
      if (auto s = annotation<since_spec, M>(); s && V < s->v)
        return false;
      if (auto u = annotation<until_spec, M>(); u && V >= u->v)
        return false;
      return true;
    }

    /** Trait: is @a T a std::vector (an array-chunk member)? */
    template <typename T>
    inline constexpr bool is_vector_v = false;
    template <typename U, typename A>
    inline constexpr bool is_vector_v<std::vector<U, A>> = true;

    /** Trait: is @a T a Repeated<U, N> (a repeats-annotated member)? Exposes
        the element type and capacity when it is. */
    template <typename T>
    struct repeated_traits
    {
      static constexpr bool value = false;
    };
    template <typename U, std::size_t N>
    struct repeated_traits<Repeated<U, N>>
    {
      static constexpr bool value = true;
      using element = U;
      static constexpr std::size_t capacity = N;
    };

    /** The reflected own-member list of @a E (base-class members — i.e. the
        ChunkExtras bookkeeping — are not enumerated).
        @tparam E the entity type.
        @return a static array of the reflected non-static data members. */
    template <typename E>
    consteval auto members_of()
    {
      return std::define_static_array(
        std::meta::nonstatic_data_members_of(^^E, std::meta::access_context::current()));
    }

    /** Build a chunk-scoped error value.
        @param code   the error category.
        @param fourcc the chunk id the error occurred in.
        @param offset the chunk's byte offset in the buffer being read.
        @param what   the failure description.
        @return the error, ready to return from a Result function. */
    inline std::unexpected<Error> chunk_error(ErrorCode code, std::uint32_t fourcc,
                                              std::size_t offset, std::string_view what)
    {
      return make_error(code, std::format("chunk {} at offset {:#x}: {}",
                                          fourcc_to_string(fourcc), offset, what));
    }

    // --- single-value (chunk payload <-> member) transfer ---------------------

    /** Read one chunk payload into member @a dst, dispatching on the member
        kind (nested entity, self-serializing, array, raw wire struct).
        @param dst     the destination member.
        @param payload the chunk payload bytes.
        @param fourcc  the chunk id (for diagnostics).
        @param offset  the chunk's byte offset (for diagnostics).
        @return nothing, or the structural error. */
    template <typename M>
    Result<void> read_value(M& dst, std::span<const std::byte> payload, std::uint32_t fourcc,
                            std::size_t offset);

    /** Append member @a src's chunk payload to @a out (payload only — the
        caller emits the chunk header).
        @param src the member to serialize.
        @param out the destination buffer.
        @return nothing, or the first nested error. */
    template <typename M>
    Result<void> write_value(const M& src, FileBuffer& out);

    template <ChunkedEntity E>
    bool entity_engaged(const E& entity);

    /** Is the member's chunk written when it never appeared in the journal?
        Required chunks always write (clients ship size-0 required chunks);
        optional ones only when they hold data. A plain data-chunk member's
        engagement is not observable — it always writes, so optional plain
        data chunks must not be declared (single-record optional chunks like
        MDAL are modeled as vectors instead).
        @param value the member value.
        @return whether the member holds observable data. */
    template <typename M>
    bool engaged(const M& value)
    {
      if constexpr (is_vector_v<M> || repeated_traits<M>::value || SelfSerializing<M>)
        return !value.empty();
      else if constexpr (ChunkedEntity<M>)
        return entity_engaged(value);
      else
        return true;
    }

    /** Append-only chunk emission with size backpatching. */
    class ChunkWriter
    {
    public:
      /** @param out the buffer every emission appends to. */
      explicit ChunkWriter(FileBuffer& out) : out_{out} {}

      /** Emit a chunk header with a placeholder size.
          @param fourcc the chunk id, in disk layout.
          @return the placeholder's buffer position, for end_chunk(). */
      std::size_t begin_chunk(std::uint32_t fourcc)
      {
        append(&fourcc, sizeof fourcc);
        const std::size_t size_at = out_.size();
        const std::uint32_t placeholder = 0;
        append(&placeholder, sizeof placeholder);
        return size_at;
      }

      /** Backpatch the size of the chunk opened at @a size_at with the byte
          count appended since.
          @param size_at the value begin_chunk() returned. */
      void end_chunk(std::size_t size_at)
      {
        const auto size = static_cast<std::uint32_t>(out_.size() - size_at - sizeof(std::uint32_t));
        std::memcpy(out_.data() + size_at, &size, sizeof size);
      }

      /** Append @a n raw bytes from @a bytes.
          @param bytes the source.
          @param n     the byte count. */
      void append(const void* bytes, std::size_t n)
      {
        const auto* p = static_cast<const std::byte*>(bytes);
        out_.insert(out_.end(), p, p + n);
      }

    private:
      FileBuffer& out_;  /**< The destination buffer. */
    };

    /** Deserialize @a data into @a entity — the engine behind
        ChunkedFile::read(); see that method for the full contract.
        @param entity the destination entity; overwritten member-by-member.
        @param data   the file (or container-chunk payload) bytes.
        @return nothing, or the first structural error (ChunkTruncated,
                ChunkSizeMismatch, ChunkMissing). */
    template <ChunkedEntity E>
    Result<void> read_entity(E& entity, std::span<const std::byte> data)
    {
      static constexpr auto members = detail::members_of<E>();
      std::size_t pos = 0;

      // phase 1: container-header prelude members, in declaration order
      template for (constexpr auto m : members)
      {
        if constexpr (detail::annotation<detail::header_spec, m>().has_value())
        {
          using M = [:std::meta::type_of(m):];
          static_assert(std::is_trivially_copyable_v<M>, "header members are raw wire structs");
          if (data.size() - pos < sizeof(M))
            return make_error(ErrorCode::ChunkTruncated,
                              std::format("header prelude at offset {:#x}: {} bytes needed, {} left",
                                          pos, sizeof(M), data.size() - pos));
          std::memcpy(&entity.[:m:], data.data() + pos, sizeof(M));
          pos += sizeof(M);
        }
      }

      // phase 2: order-independent chunk scan
      std::array<std::uint32_t, members.size()> occurrences{};
      while (data.size() - pos >= 2 * sizeof(std::uint32_t))
      {
        std::uint32_t fourcc = 0;
        std::uint32_t size = 0;
        std::memcpy(&fourcc, data.data() + pos, sizeof fourcc);
        std::memcpy(&size, data.data() + pos + sizeof fourcc, sizeof size);
        if (size > data.size() - pos - 2 * sizeof(std::uint32_t))
          return detail::chunk_error(ErrorCode::ChunkTruncated, fourcc, pos,
                                     std::format("size {} overruns the buffer", size));
        const auto payload = data.subspan(pos + 2 * sizeof(std::uint32_t), size);

        bool matched = false;
        std::int32_t index = -1;
        template for (constexpr auto m : members)
        {
          ++index;
          if constexpr (constexpr auto spec = detail::annotation<detail::chunk_spec, m>();
                        spec.has_value() && detail::version_active<E::version, m>())
          {
            using M = [:std::meta::type_of(m):];
            if (!matched && fourcc == spec->magic)
            {
              if constexpr (detail::repeated_traits<M>::value)
              {
                static_assert(detail::annotation<detail::repeats_spec, m>().has_value(),
                              "Repeated<> members must carry a repeats() annotation");
                if (auto* slot = entity.[:m:].push())
                {
                  auto r = detail::read_value(*slot, payload, fourcc, pos);
                  if (!r)
                    return std::unexpected{r.error()};
                  matched = true;
                  entity.journal.push_back({fourcc, index, occurrences[index]++});
                }
                // all slots taken: falls through to the unknown route below
              }
              else if (occurrences[index] == 0)
              {
                auto r = detail::read_value(entity.[:m:], payload, fourcc, pos);
                if (!r)
                  return std::unexpected{r.error()};
                matched = true;
                entity.journal.push_back({fourcc, index, occurrences[index]++});
              }
              // duplicate of a non-repeated chunk: preserved as unknown below
            }
          }
        }
        if (!matched)
        {
          entity.journal.push_back(
            {fourcc, -1, static_cast<std::uint32_t>(entity.unknown.size())});
          entity.unknown.push_back({fourcc, {payload.begin(), payload.end()}});
        }
        pos += 2 * sizeof(std::uint32_t) + size;
      }
      if (pos != data.size())
        entity.trailing.assign(data.begin() + static_cast<std::ptrdiff_t>(pos), data.end());

      // phase 3: required-presence check
      std::int32_t index = -1;
      std::optional<Error> missing;
      template for (constexpr auto m : members)
      {
        ++index;
        if constexpr (constexpr auto spec = detail::annotation<detail::chunk_spec, m>();
                      spec.has_value() && detail::version_active<E::version, m>()
                        && !detail::annotation<detail::optional_spec, m>().has_value())
        {
          if (!missing && occurrences[index] == 0)
            missing = Error{ErrorCode::ChunkMissing,
                            std::format("required chunk {} is absent",
                                        fourcc_to_string(spec->magic, spec->endian))};
        }
      }
      if (missing)
        return std::unexpected{*missing};
      return {};
    }

    /** Serialize @a entity, appending to @a out — the engine behind
        ChunkedFile::write(); see that method for the full contract.
        @param entity the entity to serialize.
        @param out    the destination buffer (appended, not cleared).
        @return nothing, or the first error. */
    template <ChunkedEntity E>
    Result<void> write_entity(const E& entity, FileBuffer& out)
    {
      static constexpr auto members = detail::members_of<E>();
      detail::ChunkWriter writer{out};

      // header prelude members first, mirroring read_entity's phase 1
      template for (constexpr auto m : members)
      {
        if constexpr (detail::annotation<detail::header_spec, m>().has_value())
          writer.append(&entity.[:m:], sizeof(entity.[:m:]));
      }

      std::array<std::uint32_t, members.size()> written{};
      const auto emit = [&](std::int32_t target, std::uint32_t occurrence) -> Result<void> {
        std::int32_t index = -1;
        template for (constexpr auto m : members)
        {
          ++index;
          if constexpr (constexpr auto spec = detail::annotation<detail::chunk_spec, m>();
                        spec.has_value() && detail::version_active<E::version, m>())
          {
            using M = [:std::meta::type_of(m):];
            if (index == target)
            {
              const std::size_t size_at = writer.begin_chunk(spec->magic);
              if constexpr (detail::repeated_traits<M>::value)
              {
                if (occurrence >= entity.[:m:].size())
                  return make_error(ErrorCode::ChunkSizeMismatch,
                                    std::format("journal references occurrence {} of chunk {}, "
                                                "only {} present",
                                                occurrence,
                                                fourcc_to_string(spec->magic, spec->endian),
                                                entity.[:m:].size()));
                if (auto r = detail::write_value(entity.[:m:][occurrence], out); !r)
                  return r;
              }
              else if (auto r = detail::write_value(entity.[:m:], out); !r)
                return r;
              writer.end_chunk(size_at);
              ++written[static_cast<std::size_t>(index)];
            }
          }
        }
        return {};
      };

      for (const JournalEntry& entry : entity.journal)
      {
        if (entry.member < 0)
        {
          if (entry.occurrence >= entity.unknown.size())
            return make_error(ErrorCode::ChunkSizeMismatch,
                              std::format("journal references unknown chunk {}, only {} preserved",
                                          entry.occurrence, entity.unknown.size()));
          const UnknownChunk& u = entity.unknown[entry.occurrence];
          const std::size_t size_at = writer.begin_chunk(u.fourcc);
          writer.append(u.bytes.data(), u.bytes.size());
          writer.end_chunk(size_at);
        }
        else if (auto r = emit(entry.member, entry.occurrence); !r)
          return r;
      }

      // members engaged but not journaled: fresh entities (whole declaration
      // order) and post-read additions alike
      std::int32_t index = -1;
      std::optional<Error> failed;
      template for (constexpr auto m : members)
      {
        ++index;
        if constexpr (constexpr auto spec = detail::annotation<detail::chunk_spec, m>();
                      spec.has_value() && detail::version_active<E::version, m>())
        {
          using M = [:std::meta::type_of(m):];
          if constexpr (detail::repeated_traits<M>::value)
          {
            // journaled occurrences already replayed; append the slots added since
            for (std::uint32_t i = written[static_cast<std::size_t>(index)];
                 !failed && i < entity.[:m:].size(); ++i)
              if (auto r = emit(index, i); !r)
                failed = r.error();
          }
          else if (!failed && written[static_cast<std::size_t>(index)] == 0)
          {
            constexpr bool required = !detail::annotation<detail::optional_spec, m>().has_value();
            if (required || detail::engaged(entity.[:m:]))
              if (auto r = emit(index, 0); !r)
                failed = r.error();
          }
        }
      }
      if (failed)
        return std::unexpected{*failed};

      writer.append(entity.trailing.data(), entity.trailing.size());
      return {};
    }

    /** Does a never-journaled nested entity hold anything worth a chunk? An
        entity that came from a file has a journal; a fresh one engages once any
        of its container-shaped members (or nested entities) hold data. Plain
        data-chunk members do not engage a container by themselves — their
        content is not distinguishable from the default.
        @param entity the nested entity.
        @return whether writing it would emit observable content. */
    template <ChunkedEntity E>
    bool entity_engaged(const E& entity)
    {
      if (!entity.journal.empty() || !entity.unknown.empty() || !entity.trailing.empty())
        return true;
      static constexpr auto members = members_of<E>();
      bool any = false;
      template for (constexpr auto m : members)
      {
        if constexpr (detail::annotation<detail::chunk_spec, m>().has_value()
                      && detail::version_active<E::version, m>())
        {
          using M = [:std::meta::type_of(m):];
          if constexpr (is_vector_v<M> || repeated_traits<M>::value || SelfSerializing<M>)
            any = any || !entity.[:m:].empty();
          else if constexpr (ChunkedEntity<M>)
            any = any || entity_engaged(entity.[:m:]);
        }
      }
      return any;
    }

    template <typename M>
    Result<void> read_value(M& dst, std::span<const std::byte> payload, std::uint32_t fourcc,
                            std::size_t offset)
    {
      if constexpr (ChunkedEntity<M>)
      {
        // a container chunk: its payload is a chunk stream of its own
        return read_entity(dst, payload);
      }
      else if constexpr (SelfSerializing<M>)
      {
        return dst.read(payload);
      }
      else if constexpr (is_vector_v<M>)
      {
        using T = typename M::value_type;
        static_assert(std::is_trivially_copyable_v<T>, "array chunks hold raw wire structs");
        if (payload.size() % sizeof(T) != 0)
          return chunk_error(ErrorCode::ChunkSizeMismatch, fourcc, offset,
                             std::format("size {} is not a multiple of the {}-byte element",
                                         payload.size(), sizeof(T)));
        dst.resize(payload.size() / sizeof(T));
        std::memcpy(dst.data(), payload.data(), payload.size());
        return {};
      }
      else
      {
        static_assert(std::is_trivially_copyable_v<M>, "data chunks hold raw wire structs");
        if (payload.size() != sizeof(M))
          return chunk_error(ErrorCode::ChunkSizeMismatch, fourcc, offset,
                             std::format("size {} != expected {}", payload.size(), sizeof(M)));
        std::memcpy(&dst, payload.data(), sizeof(M));
        return {};
      }
    }

    template <typename M>
    Result<void> write_value(const M& src, FileBuffer& out)
    {
      if constexpr (ChunkedEntity<M>)
        return write_entity(src, out);  // a container chunk's payload: nested entity
      else if constexpr (SelfSerializing<M>)
        return src.write(out);
      else
      {
        ChunkWriter writer{out};
        if constexpr (is_vector_v<M>)
          writer.append(src.data(), src.size() * sizeof(typename M::value_type));
        else
          writer.append(&src, sizeof(M));
        return {};
      }
    }
  }

  // --- ChunkedFile method definitions (declared in chunk.hpp) -----------------

  template <typename Derived>
  Result<void> ChunkedFile<Derived>::read(std::span<const std::byte> data)
  {
    return detail::read_entity(static_cast<Derived&>(*this), data);
  }

  template <typename Derived>
  Result<FileBuffer> ChunkedFile<Derived>::write() const
  {
    FileBuffer out;
    if (auto r = detail::write_entity(static_cast<const Derived&>(*this), out); !r)
      return std::unexpected{r.error()};
    return out;
  }
}
