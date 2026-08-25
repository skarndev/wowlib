#pragma once

/** @file
    The chunk framework, vocabulary and engine in one header.

    Vocabulary: the round-trip bookkeeping every chunked entity carries
    (ChunkExtras), the ChunkedFile mixin that gives an entity its
    read()/write() serialization methods, and the member types the serializer
    dispatches on beyond plain structs and vectors — ChunkBlob and Repeated
    (StringBlock lives in its own header).

    Engine: the reflection-driven chunk serializer behind
    ChunkedFile::read()/write(). detail::read_entity / detail::write_entity
    walk a chunked entity's annotated members (`template for` over the
    reflected member list) and map them onto the fourcc+size chunk stream.
    Reading is chunk-order-independent — chunks dispatch by fourcc into the
    matching member — while every encounter is journaled so writing can replay
    the original order and reproduce the file byte for byte, unknown chunks and
    trailing bytes included. Fresh entities (empty journal) write their members
    in declaration order, which each entity declares in canonical client order. */

#include <meta>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/formats/common/annotations.hpp>
#include <wowlib/formats/common/entity_reflect.hpp>
#include <wowlib/formats/common/fourcc.hpp>
#include <wowlib/formats/common/string_block.hpp>
#include <wowlib/formats/common/validation.hpp>

namespace wowlib::formats {
  /** A chunk the entity does not model, preserved verbatim for round-trip. */
  struct UnknownChunk {
    std::uint32_t fourcc = 0; /**< The id as scanned (memcpy'd host u32). */
    std::vector<std::byte> bytes; /**< The payload, verbatim. */

    bool operator==(const UnknownChunk&) const = default;
  };

  /** One chunk encounter in file order — the write path replays the journal to
      reproduce the original byte layout exactly. */
  struct JournalEntry {
    std::uint32_t fourcc = 0; /**< The id as scanned. */

    /** Declaration index of the member the chunk was read into, or -1 for an
        unknown chunk. */
    std::int32_t member = -1;

    /** Which occurrence of the member this was (repeated chunks), or the index
        into unknown for member == -1. */
    std::uint32_t occurrence = 0;

    bool operator==(const JournalEntry&) const = default;
  };

  /** Round-trip bookkeeping common to every chunked entity: the encounter
      journal, unmodeled chunks, and stray trailing bytes. All preserved so a
      read-then-write reproduces the original file byte for byte. */
  struct ChunkExtras {
    [[=welder::mark::exclude]] std::vector<JournalEntry> journal;
    [[=welder::mark::exclude]] std::vector<UnknownChunk> unknown;
    [[=welder::mark::exclude]] std::vector<std::byte> trailing;

    // excluded: the parameter type is this unwelded base, not the entity
    [[=welder::mark::exclude]]
    bool operator==(const ChunkExtras&) const = default;
  };

  /** The serialization face of a chunked entity, mixed in CRTP-style: an
      entity `struct E : ChunkedFile<E>` carries the ChunkExtras bookkeeping
      and gains the read()/write() methods every file representation shares.
      Method definitions sit at the bottom of this header, past the
      serializer engine they drive.
      @tparam Derived the entity itself (the CRTP pattern). */
  template <typename Derived>
  struct ChunkedFile : ChunkExtras {
    [[=welder::doc("Deserialize file bytes into this entity, replacing its "
      "contents. Unmodeled chunks are preserved so an unmodified "
      "entity rewrites byte-for-byte.")]]
    Result<void> read(std::span<const std::byte> data [[=welder::doc("the file bytes")]]);

    [[nodiscard]]
    [[=welder::doc("Serialize this entity."),
      =welder::returns("the file bytes")]]
    Result<FileBuffer> write() const;

    [[nodiscard]]
    [[=welder::doc(R"(
        Check the logical integrity contracts this file must satisfy to LOAD in
        the client — companion-chunk counts, index ranges, flag/presence
        coherence — which write() deliberately never enforces. Call it before
        writing when you want to know the result will load. A file read from a
        client and left unmodified reports no errors; warnings mark states real
        client files ship.)"),
      =welder::returns("every violated contract, in member order")]]
    ValidationReport validate() const;

    [[nodiscard]]
    [[=welder::doc("Validate and raise on the first error instead of returning "
        "a report — the assert-style face of validate()."),
      =welder::returns("nothing; raises when validate() finds any error")]]
    Result<void> ensure_valid() const;

    [[=welder::mark::exclude]]
    bool operator==(const ChunkedFile&) const = default;
  };

  struct [[
      =welder::weld,
      =welder::doc(
        "An unparsed chunk payload, preserved verbatim for round-trip. "
        "Backs chunks wowlib keeps opaque — offset-based (MOTA, MDDL) "
        "or undocumented (MPVD, MOMX).")
    ]] ChunkBlob {
    [[=welder::mark::exclude]] std::vector<std::byte> bytes;

    /** Replace the payload with @a payload (the serializer's read hook).
        @param payload the chunk payload bytes.
        @return nothing; storing raw bytes cannot fail. */
    [[=welder::mark::exclude]]
    Result<void> read(std::span<const std::byte> payload) {
      bytes.assign(payload.begin(), payload.end());
      return {};
    }

    /** Append the payload to @a out (the serializer's write hook).
        @param out the destination buffer (appended, not cleared).
        @return nothing; emitting raw bytes cannot fail. */
    [[=welder::mark::exclude]]
    Result<void> write(FileBuffer& out) const {
      out.insert(out.end(), bytes.begin(), bytes.end());
      return {};
    }

    [[nodiscard]]
    [[=welder::getter, =welder::doc("Whether the payload holds any bytes.")]]
    bool empty() const {
      return bytes.empty();
    }

    [[=welder::getter, =welder::doc("The payload size in bytes.")]]
    std::size_t size() const {
      return bytes.size();
    }

    bool operator==(const ChunkBlob&) const = default;
  };

  /** Storage for a chunk that may appear up to N times in one entity (MOTV
      texcoord sets, MOCV vertex-color layers). Slots fill in encounter order.
      @tparam T the per-occurrence payload type.
      @tparam N the maximum occurrence count (matches the member's `repeats(N)`
                annotation). */
  template <typename T, std::size_t N>
  class Repeated {
  public:
    /** The per-occurrence payload type — spelled like a standard container's
        so the generic walkers (validation) treat slots as a sequence. */
    using value_type = T;

    /** Each slot is a complete occurrence of the chunk, not one element of a
        larger array: a count contract holds per filled slot (see the
        validation walker's SlotSequence). */
    static constexpr bool validation_slots = true;

    /** Claim the next slot (the serializer's per-encounter hook).
        @return the slot to read into, or nullptr if all N are taken. */
    T* push() {
      if (count_ == N) return nullptr;
      return &slots_[count_++];
    }

    /** @return the number of filled slots. */
    [[nodiscard]] std::size_t size() const { return count_; }

    /** @return whether no slot is filled. */
    [[nodiscard]] bool empty() const { return count_ == 0; }

    /** @return the slot capacity N. */
    [[nodiscard]] static constexpr std::size_t capacity() { return N; }

    /** @param i a filled-slot index, < size().
        @return the i-th filled slot. */
    [[nodiscard]] T& operator[](std::size_t i) { return slots_[i]; }

    /** @copydoc operator[](std::size_t) */
    [[nodiscard]] const T& operator[](std::size_t i) const { return slots_[i]; }

    /** @return the start of the filled slots (range-for support). */
    [[nodiscard]] T* begin() { return slots_.data(); }

    /** @return past-the-end of the filled slots. */
    [[nodiscard]] T* end() { return slots_.data() + count_; }

    /** @copydoc begin() */
    [[nodiscard]] const T* begin() const { return slots_.data(); }

    /** @copydoc end() */
    [[nodiscard]] const T* end() const { return slots_.data() + count_; }

  private:
    std::array<T, N> slots_{}; /**< The slot storage; the first count_ are live. */
    std::size_t count_ = 0; /**< Number of filled slots. */
  };

  /** A type the chunk serializer can read and write: carries the round-trip
      bookkeeping and knows which client version it is laid out for. */
  template <typename E> concept ChunkedEntity = std::derived_from<E, ChunkExtras> && requires {
    { E::version } -> std::convertible_to<ClientVersion>;
  };

  /** A member type that owns its chunk-payload encoding: the serializer hands
      it the raw payload on read and the output buffer on write (StringBlock,
      ChunkBlob). */
  template <typename T> concept SelfSerializing = requires(T& value,
                                                           const T& constant,
                                                           std::span<const std::byte> payload,
                                                           FileBuffer& out) {
    { value.read(payload) } -> std::same_as<Result<void>>; { constant.write(out) } -> std::same_as<Result<void>>; {
      constant.empty()
    } -> std::convertible_to<bool>;
  };

  namespace detail {
    /** Trait: is @a T a Repeated<U, N> (a repeats-annotated member)? Exposes
        the element type and capacity when it is. */
    template <typename T>
    struct repeated_traits {
      static constexpr bool value = false;
    };

    template <typename U, std::size_t N>
    struct repeated_traits<Repeated<U, N>> {
      static constexpr bool value = true;
      using element = U;
      static constexpr std::size_t capacity = N;
    };

    /** The chunk() fourcc of member @a member, or 0 if it carries none. */
    consteval std::uint32_t chunk_magic_of(std::meta::info member) {
      auto anns = std::meta::annotations_of_with_type(member, ^^chunk_spec);
      return anns.empty() ? 0u : std::meta::extract<chunk_spec>(anns[0]).magic;
    }

    /** The member indices (into members_of<E>) in the order fresh entities emit
        their chunks. When @a E declares a canonical `chunk_order` (a static array of
        fourccs — needed once an entity's chunk members are scattered across
        conditionally-inherited trait bases, whose flatten order is by-trait, not
        canonical), the chunk members follow it; otherwise members keep their
        declaration/flatten order. A `chunk_order` must list every chunk member
        exactly once (asserted). */
    template <typename E>
    consteval auto write_order() {
      constexpr auto members = members_of<E>();
      std::vector<std::size_t> order;
      if constexpr (requires { E::chunk_order; }) {
        for (std::uint32_t magic : E::chunk_order)
          for (std::size_t i = 0; i < members.size(); ++i)
            if (chunk_magic_of(members[i]) == magic) {
              order.push_back(i);
              break;
            }
        std::size_t chunk_members = 0;
        for (std::size_t i = 0; i < members.size(); ++i) chunk_members += (chunk_magic_of(members[i]) != 0);
        if (order.size() != chunk_members) throw "chunk_order must list every chunk member exactly once";
      }
      else
        for (std::size_t i = 0; i < members.size(); ++i) order.push_back(i);
      return std::define_static_array(order);
    }

    /** Build a chunk-scoped error value.
        @param code   the error category.
        @param fourcc the chunk id the error occurred in.
        @param offset the chunk's byte offset in the buffer being read.
        @param what   the failure description.
        @param endian how the id is laid out on disk (matched members pass
                      their declared layout; unknown-chunk paths default to
                      reversed, the common case).
        @return the error, ready to return from a Result function. */
    inline std::unexpected<Error> chunk_error(ErrorCode code,
                                              std::uint32_t fourcc,
                                              std::size_t offset,
                                              std::string_view what,
                                              FourCCEndian endian = FourCCEndian::reversed) {
      return make_error(code, std::format("chunk {} at offset {:#x}: {}", fourcc_to_string(fourcc, endian), offset,
                                          what));
    }

    /** The id layout to display for chunks the entity has no spec for
        (garbage magics, unknown chunks): the entity's declared
        `unknown_fourcc_endian` when present (the forward-magic M2 file
        family), else the reversed common case.
        @tparam E the chunked entity. */
    template <typename E>
    consteval FourCCEndian unknown_fourcc_endian() {
      if constexpr (requires { E::unknown_fourcc_endian; }) return E::unknown_fourcc_endian;
      else return FourCCEndian::reversed;
    }

    // --- single-value (chunk payload <-> member) transfer ---------------------

    /** Read one chunk payload into member @a dst, dispatching on the member
        kind (nested entity, self-serializing, array, raw binary struct).
        @param dst     the destination member.
        @param payload the chunk payload bytes.
        @param fourcc  the chunk id (for diagnostics).
        @param offset  the chunk's byte offset (for diagnostics).
        @param endian  the chunk id's disk layout (for diagnostics).
        @return nothing, or the structural error. */
    template <typename M>
    Result<void> read_value(M& dst,
                            std::span<const std::byte> payload,
                            std::uint32_t fourcc,
                            std::size_t offset,
                            FourCCEndian endian);

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
    bool engaged(const M& value) {
      if constexpr (is_vector_v<M> || repeated_traits<M>::value || SelfSerializing<M>) return !value.empty();
      else if constexpr (ChunkedEntity<M>) return entity_engaged(value);
      else return true;
    }

    /** Append-only chunk emission with size backpatching. */
    class ChunkWriter {
    public:
      /** @param out the buffer every emission appends to. */
      explicit ChunkWriter(FileBuffer& out) : out_{out} {}

      /** Emit a chunk header with a placeholder size.
          @param fourcc the chunk id, in disk layout.
          @return the placeholder's buffer position, for end_chunk(). */
      std::size_t begin_chunk(std::uint32_t fourcc) {
        append(&fourcc, sizeof fourcc);
        const std::size_t size_at = out_.size();
        const std::uint32_t placeholder = 0;
        append(&placeholder, sizeof placeholder);
        return size_at;
      }

      /** Backpatch the size of the chunk opened at @a size_at with the byte
          count appended since.
          @param size_at the value begin_chunk() returned. */
      void end_chunk(std::size_t size_at) {
        const auto size = static_cast<std::uint32_t>(out_.size() - size_at - sizeof(std::uint32_t));
        std::memcpy(out_.data() + size_at, &size, sizeof size);
      }

      /** Append @a n raw bytes from @a bytes.
          @param bytes the source.
          @param n     the byte count. */
      void append(const void* bytes, std::size_t n) {
        const auto* p = static_cast<const std::byte*>(bytes);
        out_.insert(out_.end(), p, p + n);
      }

    private:
      FileBuffer& out_; /**< The destination buffer. */
    };

    /** Deserialize @a data into @a entity — the engine behind
        ChunkedFile::read(); see that method for the full contract.
        @param entity the destination entity; overwritten member-by-member.
        @param data   the file (or container-chunk payload) bytes.
        @return nothing, or the first structural error (ChunkTruncated,
                ChunkSizeMismatch, ChunkMissing). */
    template <ChunkedEntity E>
    Result<void> read_entity(E& entity, std::span<const std::byte> data) {
      static constexpr auto members = detail::members_of<E>();
      std::size_t pos = 0;

      // phase 1: container-header prelude members, in declaration order
      template for (constexpr auto m : members) {
        if constexpr (detail::annotation<detail::header_spec, m>().has_value()) {
          using M = [:std::meta::type_of(m):];
          static_assert(std::is_trivially_copyable_v<M>, "header members are raw binary structs");
          if (data.size() - pos < sizeof(M))
            return make_error(ErrorCode::ChunkTruncated,
                              std::format("header prelude at offset {:#x}: {} bytes needed, {} left", pos, sizeof(M),
                                          data.size() - pos));
          std::memcpy(&entity.[:m:], data.data() + pos, sizeof(M));
          pos += sizeof(M);
        }
      }

      // phase 2: order-independent chunk scan
      std::array<std::uint32_t, members.size()> occurrences{};
      while (data.size() - pos >= 2 * sizeof(std::uint32_t)) {
        std::uint32_t fourcc = 0;
        std::uint32_t size = 0;
        std::memcpy(&fourcc, data.data() + pos, sizeof fourcc);
        std::memcpy(&size, data.data() + pos + sizeof fourcc, sizeof size);
        if (size > data.size() - pos - 2 * sizeof(std::uint32_t))
          return detail::chunk_error(ErrorCode::ChunkTruncated, fourcc, pos,
                                     std::format("size {} overruns the buffer", size),
                                     detail::unknown_fourcc_endian<E>());
        const auto payload = data.subspan(pos + 2 * sizeof(std::uint32_t), size);

        bool matched = false;
        std::int32_t index = -1;
        template for (constexpr auto m : members) {
          ++index;
          if constexpr (constexpr auto spec = detail::annotation<detail::chunk_spec, m>(); spec.has_value() &&
            detail::version_active<E::version, m>()) {
            using M = [:std::meta::type_of(m):];
            if (!matched && fourcc == spec->magic) {
              const auto member_slot = static_cast<std::size_t>(index);
              if constexpr (detail::repeated_traits<M>::value) {
                static_assert(detail::annotation<detail::repeats_spec, m>().has_value(),
                              "Repeated<> members must carry a repeats() annotation");
                if (auto* slot = entity.[:m:].push()) {
                  auto r = detail::read_value(*slot, payload, fourcc, pos, spec->endian);
                  if (!r) return std::unexpected{r.error()};
                  matched = true;
                  entity.journal.push_back({fourcc, index, occurrences[member_slot]++});
                }
                // all slots taken: falls through to the unknown route below
              }
              else if constexpr (detail::annotation<detail::repeating_spec, m>().has_value()) {
                // one element per encounter, unbounded (see the repeating
                // annotation): the payload reads into a fresh back element
                static_assert(detail::is_vector_v<M>, "repeating members must be std::vector<Element>");
                auto r = detail::read_value(entity.[:m:].emplace_back(), payload, fourcc, pos, spec->endian);
                if (!r) return std::unexpected{r.error()};
                matched = true;
                entity.journal.push_back({fourcc, index, occurrences[member_slot]++});
              }
              else if (occurrences[member_slot] == 0) {
                auto r = detail::read_value(entity.[:m:], payload, fourcc, pos, spec->endian);
                if (!r) return std::unexpected{r.error()};
                matched = true;
                entity.journal.push_back({fourcc, index, occurrences[member_slot]++});
              }
              // duplicate of a non-repeated chunk: preserved as unknown below
            }
          }
        }
        if (!matched) {
          entity.journal.push_back({fourcc, -1, static_cast<std::uint32_t>(entity.unknown.size())});
          entity.unknown.push_back({fourcc, {payload.begin(), payload.end()}});
        }
        pos += 2 * sizeof(std::uint32_t) + size;
      }
      if (pos != data.size())
        entity.trailing.assign(data.begin() + static_cast<std::ptrdiff_t>(pos), data.end());

      // phase 3: required-presence check
      std::int32_t index = -1;
      std::optional<Error> missing;
      template for (constexpr auto m : members) {
        ++index;
        if constexpr (constexpr auto spec = detail::annotation<detail::chunk_spec, m>(); spec.has_value() &&
          detail::version_active<E::version, m>() && !detail::annotation<detail::optional_spec, m>().has_value()) {
          if (!missing && occurrences[static_cast<std::size_t>(index)] == 0)
            missing = Error{
              ErrorCode::ChunkMissing,
              std::format("required chunk {} is absent", fourcc_to_string(spec->magic, spec->endian))
            };
        }
      }
      if (missing) return std::unexpected{*missing};
      return {};
    }

    /** Serialize @a entity, appending to @a out — the engine behind
        ChunkedFile::write(); see that method for the full contract.
        @param entity the entity to serialize.
        @param out    the destination buffer (appended, not cleared).
        @return nothing, or the first error. */
    template <ChunkedEntity E>
    Result<void> write_entity(const E& entity, FileBuffer& out) {
      static constexpr auto members = detail::members_of<E>();
      detail::ChunkWriter writer{out};
      const std::size_t image_start = out.size();

      // Journal resequencing: an entity may declare
      //   Result<std::optional<std::vector<JournalEntry>>> resequenced_journal() const
      // to replace the replay order for THIS write — nullopt keeps the stored
      // journal. WDL uses it to interleave its per-tile repeating chunks
      // (MARE/MAHO) once tiles were added or removed, where the stored journal
      // no longer matches the entity's content.
      std::vector<JournalEntry> resequenced;
      std::span<const JournalEntry> journal{entity.journal};
      if constexpr (requires { entity.resequenced_journal(); }) {
        auto r = entity.resequenced_journal();
        if (!r) return std::unexpected{r.error()};
        if (*r) {
          resequenced = std::move(**r);
          journal = resequenced;
        }
      }

      // header prelude members first, mirroring read_entity's phase 1
      template for (constexpr auto m : members) {
        if constexpr (detail::annotation<detail::header_spec, m>().has_value()) writer.append(
          &entity.[:m:], sizeof(entity.[:m:]));
      }

      std::array<std::uint32_t, members.size()> written{};
      const auto emit = [&](std::int32_t target, std::uint32_t occurrence) -> Result<void> {
        std::int32_t index = -1;
        template for (constexpr auto m : members) {
          ++index;
          if constexpr (constexpr auto spec = detail::annotation<detail::chunk_spec, m>(); spec.has_value() &&
            detail::version_active<E::version, m>()) {
            using M = [:std::meta::type_of(m):];
            if (index == target) {
              const std::size_t size_at = writer.begin_chunk(spec->magic);
              if constexpr (detail::repeated_traits<M>::value || detail::annotation<detail::repeating_spec, m>().
                has_value()) {
                if (occurrence >= entity.[:m:].size())
                  return make_error(ErrorCode::ChunkSizeMismatch,
                                    std::format("journal references occurrence {} of chunk {}, " "only {} present",
                                                occurrence, fourcc_to_string(spec->magic, spec->endian),
                                                entity.[:m:].size()));
                if (auto r = detail::write_value(entity.[:m:][occurrence], out); !r) return r;
              }
              else if (auto r = detail::write_value(entity.[:m:], out); !r) return r;
              writer.end_chunk(size_at);
              // Derived-field stamping: an entity may declare
              //   void patch_chunk(std::uint32_t fourcc, std::span<std::byte>) const
              // to overwrite binary fields whose source of truth is other
              // members (e.g. WMORoot stamping the MOHD counts from its
              // vectors). The hook sees the finished payload in place, so
              // write() stays const and entity state is untouched.
              if constexpr (requires {
                entity.patch_chunk(std::uint32_t{}, std::span<std::byte>{});
              })
                entity.patch_chunk(spec->magic, std::span<std::byte>{
                                     out.data() + size_at + sizeof(std::uint32_t),
                                     out.size() - size_at - sizeof(std::uint32_t)
                                   });
              ++written[static_cast<std::size_t>(index)];
            }
          }
        }
        return {};
      };

      for (const JournalEntry& entry : journal) {
        if (entry.member < 0) {
          if (entry.occurrence >= entity.unknown.size())
            return make_error(ErrorCode::ChunkSizeMismatch,
                              std::format("journal references unknown chunk {}, only {} preserved", entry.occurrence,
                                          entity.unknown.size()));
          const UnknownChunk& u = entity.unknown[entry.occurrence];
          const std::size_t size_at = writer.begin_chunk(u.fourcc);
          writer.append(u.bytes.data(), u.bytes.size());
          writer.end_chunk(size_at);
        }
        else if (auto r = emit(entry.member, entry.occurrence); !r) return r;
      }

      // members engaged but not journaled: fresh entities (in canonical chunk
      // order — see write_order) and post-read additions alike
      static constexpr auto order = detail::write_order<E>();
      std::optional<Error> failed;
      template for (constexpr std::size_t idx : order) {
        constexpr auto m = members[idx];
        constexpr auto index = static_cast<std::int32_t>(idx);
        if constexpr (constexpr auto spec = detail::annotation<detail::chunk_spec, m>(); spec.has_value() &&
          detail::version_active<E::version, m>()) {
          using M = [:std::meta::type_of(m):];
          if constexpr (detail::repeated_traits<M>::value || detail::annotation<detail::repeating_spec, m>().
            has_value()) {
            // journaled occurrences already replayed; append the slots added since
            for (std::uint32_t i = written[idx]; !failed && i < entity.[:m:].size(); ++i)
              if (auto r = emit(index, i); !r) failed = r.error();
          }
          else if (!failed && written[idx] == 0) {
            constexpr bool required = !detail::annotation<detail::optional_spec, m>().has_value();
            if (required || detail::engaged(entity.[:m:]))
              if (auto r = emit(index, 0); !r) failed = r.error();
          }
        }
      }
      if (failed) return std::unexpected{*failed};

      writer.append(entity.trailing.data(), entity.trailing.size());

      // Whole-image stamping: an entity may declare
      //   Result<void> patch_file(std::span<std::byte>) const
      // to overwrite binary fields whose source of truth is the finished LAYOUT —
      // fields patch_chunk cannot serve because they reference bytes written
      // after their own chunk (WDL's MAOF table of absolute MARE offsets; the
      // ADT header offsets later). The hook sees this entity's complete
      // serialized image in place.
      if constexpr (requires(std::span<std::byte> image) {
        { entity.patch_file(image) } -> std::same_as<Result<void>>;
      })
        if (auto r = entity.patch_file(std::span<std::byte>{out.data() + image_start, out.size() - image_start}); !r)
          return r;
      return {};
    }

    /** The flattened member index (a JournalEntry::member value) of the chunk
        member carrying @a magic, or -1 when no member does. For entity code
        that builds journal entries by fourcc (resequenced_journal hooks).
        @tparam E     the entity type.
        @param  magic the chunk id (see four_cc()). */
    template <typename E>
    consteval std::int32_t chunk_member_index(std::uint32_t magic) {
      constexpr auto members = members_of<E>();
      for (std::size_t i = 0; i < members.size(); ++i)
        if (chunk_magic_of(members[i]) == magic) return static_cast<std::int32_t>(i);
      return -1;
    }

    /** The journal a FRESH write of @a entity would produce: one entry per
        engaged (or required) chunk member in canonical write order, repeated/
        repeating members expanded to one entry per element, no unknown-chunk
        or trailing entries. The starting point for a resequenced_journal hook,
        which reorders it (and re-appends the preserved unknown chunks) before
        handing it to write_entity.
        @param entity the entity to enumerate.
        @return the canonical-order journal. */
    template <ChunkedEntity E>
    std::vector<JournalEntry> fresh_journal(const E& entity) {
      static constexpr auto members = detail::members_of<E>();
      static constexpr auto order = detail::write_order<E>();
      std::vector<JournalEntry> out;
      template for (constexpr std::size_t idx : order) {
        constexpr auto m = members[idx];
        constexpr auto index = static_cast<std::int32_t>(idx);
        if constexpr (constexpr auto spec = detail::annotation<detail::chunk_spec, m>(); spec.has_value() &&
          detail::version_active<E::version, m>()) {
          using M = [:std::meta::type_of(m):];
          if constexpr (detail::repeated_traits<M>::value || detail::annotation<detail::repeating_spec, m>().
            has_value()) {
            for (std::uint32_t i = 0; i < entity.[:m:].size(); ++i) out.push_back({spec->magic, index, i});
          }
          else {
            constexpr bool required = !detail::annotation<detail::optional_spec, m>().has_value();
            if (required || detail::engaged(entity.[:m:])) out.push_back({spec->magic, index, 0});
          }
        }
      }
      return out;
    }

    /** Does a never-journaled nested entity hold anything worth a chunk? An
        entity that came from a file has a journal; a fresh one engages once any
        of its container-shaped members (or nested entities) hold data. Plain
        data-chunk members do not engage a container by themselves — their
        content is not distinguishable from the default.
        @param entity the nested entity.
        @return whether writing it would emit observable content. */
    template <ChunkedEntity E>
    bool entity_engaged(const E& entity) {
      if (!entity.journal.empty() || !entity.unknown.empty() || !entity.trailing.empty()) return true;
      static constexpr auto members = members_of<E>();
      bool any = false;
      template for (constexpr auto m : members) {
        if constexpr (detail::annotation<detail::chunk_spec, m>().has_value() && detail::version_active<
          E::version, m>()) {
          using M = [:std::meta::type_of(m):];
          if constexpr (is_vector_v<M> || repeated_traits<M>::value || SelfSerializing<M>) any = any || !entity.[:m:].
            empty();
          else if constexpr (ChunkedEntity<M>) any = any || entity_engaged(entity.[:m:]);
        }
      }
      return any;
    }

    template <typename M>
    Result<void> read_value(M& dst,
                            std::span<const std::byte> payload,
                            std::uint32_t fourcc,
                            std::size_t offset,
                            FourCCEndian endian) {
      if constexpr (ChunkedEntity<M>) {
        // a container chunk: its payload is a chunk stream of its own
        return read_entity(dst, payload);
      }
      else if constexpr (SelfSerializing<M>) {
        return dst.read(payload);
      }
      else if constexpr (is_vector_v<M>) {
        using T = typename M::value_type;
        static_assert(std::is_trivially_copyable_v<T>, "array chunks hold raw binary structs");
        if (payload.size() % sizeof(T) != 0)
          return chunk_error(ErrorCode::ChunkSizeMismatch, fourcc, offset,
                             std::format("size {} is not a multiple of the {}-byte element", payload.size(), sizeof(T)),
                             endian);
        dst.resize(payload.size() / sizeof(T));
        std::memcpy(dst.data(), payload.data(), payload.size());
        return {};
      }
      else {
        static_assert(std::is_trivially_copyable_v<M>, "data chunks hold raw binary structs");
        if (payload.size() != sizeof(M))
          return chunk_error(ErrorCode::ChunkSizeMismatch, fourcc, offset,
                             std::format("size {} != expected {}", payload.size(), sizeof(M)), endian);
        std::memcpy(&dst, payload.data(), sizeof(M));
        return {};
      }
    }

    template <typename M>
    Result<void> write_value(const M& src, FileBuffer& out) {
      if constexpr (ChunkedEntity<M>) return write_entity(src, out);
        // a container chunk's payload: nested entity
      else if constexpr (SelfSerializing<M>) return src.write(out);
      else {
        ChunkWriter writer{out};
        if constexpr (is_vector_v<M>)
          writer.append(src.data(), src.size() * sizeof(typename M::value_type));
        else writer.append(&src, sizeof(M));
        return {};
      }
    }
  }

  // --- ChunkedFile method definitions (declared above) -----------------

  template <typename Derived>
  Result<void> ChunkedFile<Derived>::read(std::span<const std::byte> data) {
    return detail::read_entity(static_cast<Derived&>(*this), data);
  }

  template <typename Derived>
  Result<FileBuffer> ChunkedFile<Derived>::write() const {
    FileBuffer out;
    if (auto r = detail::write_entity(static_cast<const Derived&>(*this), out); !r) return std::unexpected{r.error()};
    return out;
  }

  template <typename Derived>
  ValidationReport ChunkedFile<Derived>::validate() const {
    ValidationReport report;
    detail::validate_entity(static_cast<const Derived&>(*this), report);
    return report;
  }

  template <typename Derived>
  Result<void> ChunkedFile<Derived>::ensure_valid() const {
    return validate().to_result();
  }
}
