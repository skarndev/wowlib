#pragma once

/** @file
    The reflection helpers every format engine shares: reading a member's
    annotations, gating it by the entity's client version, and enumerating an
    entity's members with its public bases flattened in (how conditionally
    inherited version-trait bases stay invisible to the engines).

    These live apart from any one engine because all three read them — the
    chunk serializer (chunked_file.hpp), the M2 offset serializer
    (m2/offset_block.hpp) and the validation walker (validation.hpp). They
    keep the `wowlib::formats::detail` namespace they have always had, so call
    sites are unaffected by where they are declared. */

#include <meta>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/annotations.hpp>

namespace wowlib::formats::detail {
  /** The first annotation of type @a Spec on reflected member @a M, if any.
      @tparam Spec the annotation payload type (a `*_spec` struct).
      @tparam M    the reflected member.
      @return the payload, or nullopt when the member is unannotated. */
  template <typename Spec, std::meta::info M>
  consteval std::optional<Spec> annotation() {
    auto anns = std::meta::annotations_of_with_type(M, ^^Spec);
    if (anns.empty()) return std::nullopt;
    return std::meta::extract<Spec>(anns[0]);
  }

  /** Whether member @a M participates for entity version @a V, per its
      since/until annotations (absent bounds mean unbounded).
      @tparam V the entity's client version.
      @tparam M the reflected member. */
  template <ClientVersion V, std::meta::info M>
  consteval bool versionActive() {
    if (auto s = annotation<SinceSpec, M>(); s && V < s->v) return false;
    if (auto u = annotation<UntilSpec, M>(); u && V >= u->v) return false;
    return true;
  }

  /** Trait: is @a T a std::vector (an array-chunk member)? */
  template <typename T>
  inline constexpr bool IsVectorV = false;
  template <typename U, typename A>
  inline constexpr bool IsVectorV<std::vector<U, A>> = true;

  /** Collect @a type's non-static data members with its public bases flattened
      in FIRST (recursively, declaration order) — mirroring how welder flattens a
      non-welded base's members into the derived binding. Lets a versioned entity
      carry its version-gated chunk members in conditionally-inherited trait bases
      (present only for the versions they belong to) while the serializer still
      sees them. Base bookkeeping members (ChunkExtras: journal/unknown/trailing)
      come along but carry no chunk() annotation, so every chunk loop skips them.
      @param type the reflected class to walk.
      @param out  the member list being built. */
  consteval void collectMembers(std::meta::info type, std::vector<std::meta::info>& out) {
    for (auto b : std::meta::bases_of(type, std::meta::access_context::unchecked()))
      if (std::meta::is_public(b)) collectMembers(std::meta::type_of(b), out);
    for (auto m : std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked())) out.push_back(m);
  }

  /** The reflected member list of @a E, public bases flattened in (see
      collectMembers). Stable order, so the journal's member index is consistent
      between read and write.
      @tparam E the entity type.
      @return a static array of the reflected non-static data members. */
  template <typename E>
  consteval auto membersOf() {
    std::vector<std::meta::info> out;
    collectMembers(^^E, out);
    return std::define_static_array(out);
  }

  /** The reflected member of @a E named @a name (public bases flattened, like
      membersOf), or the null reflection when no member carries the name — how
      the sibling-naming annotations (countMatches, indexes, offsetAfter)
      resolve their target at compile time, so a typo is a static_assert at the
      use site rather than a silent no-op.
      @tparam E    the entity type.
      @param  name the member identifier to find.
      @return the reflected member, or `std::meta::info{}`. */
  template <typename E>
  consteval std::meta::info memberNamed(std::string_view name) {
    for (auto m : membersOf<E>())
      if (std::meta::has_identifier(m) && std::meta::identifier_of(m) == name) return m;
    return {};
  }

  /** Whether @a r is declared inside namespace `std` (at any nesting depth —
      libstdc++ hides types in inline namespaces like `std::__cxx11`). The
      validation walker stops there: standard-library internals are not our
      records, and reflecting into them would walk implementation details.
      @param r the reflected entity (typically a type).
      @return whether `std` encloses it. */
  consteval bool nestedInStd(std::meta::info r) {
    while (r != std::meta::info{} && r != ^^::) {
      if (r == ^^std) return true;
      if (!std::meta::has_parent(r)) return false;
      r = std::meta::parent_of(r);
    }
    return false;
  }

  /** Whether @a T is a standard-library type (see nestedInStd).
      @tparam T the type to classify. */
  template <typename T>
  consteval bool isStdType() {
    return nestedInStd(^^T);
  }
}
