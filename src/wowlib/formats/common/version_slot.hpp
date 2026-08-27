#pragma once

/** @file
    The conditional-base mechanism that gives a versioned chunked entity exactly
    the fields its client version defines. Version-gated chunks are grouped by
    availability range into small unwelded trait structs; the entity inherits each
    trait through `Slot<V, Since, Trait[, Until]>` — the trait when
    Since <= V < Until, else a distinct empty `absent<Trait>`. welder flattens an
    active trait's members onto the entity's binding (annotations survive, read off
    the declaring class); an inactive trait contributes nothing, so the field simply
    does not exist for that version (setting it is a compile error). */

#include <type_traits>

#include <wowlib/core/client_version.hpp>

namespace wowlib::formats {
  /** A distinct empty base per @a Trait, so an entity inheriting several *inactive*
      version slots never inherits the same empty type twice (ill-formed). Empty, so
      it is elided (EBO). */
  template <class Trait>
  struct Absent {
    // excluded from bindings: the parameter type is this unwelded base
    [[=welder::mark::exclude]]
    bool operator==(const Absent&) const = default;
  };

  /** Above any supported client build — the default `Until` (never removed). */
  inline constexpr ClientVersion VersionNeverRemoved{255, 0, 0, 0};

  /** A version-gated base: the entity inherits @a Trait (flattening its chunk
      members in) iff @a Since <= @a V < @a Until, else the empty absent<Trait>.
      Group the chunks that share an availability range into one Trait; a chunk
      removed at some version goes in a trait with that version as @a Until.

      @a V is compared on the retail timeline (ClientVersion::formatLineage),
      so a Classic version gets the chunk set its ENGINE defines rather than
      the one its legacy version number suggests. Entities reached through the
      canonicalizing family aliases are already instantiated at a retail grid
      version, so this only matters to code naming a detail:: template itself. */
  template <ClientVersion V, ClientVersion Since, class Trait, ClientVersion Until = VersionNeverRemoved>
  using Slot = std::conditional_t<(V.formatLineage() >= Since && V.formatLineage() < Until), Trait, Absent<Trait>>;
}
