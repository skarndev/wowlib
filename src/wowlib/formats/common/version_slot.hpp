#pragma once

/** @file
    The conditional-base mechanism that gives a versioned chunked entity exactly
    the fields its client version defines. Version-gated chunks are grouped by
    availability range into small unwelded trait structs; the entity inherits each
    trait through `slot<V, Since, Trait[, Until]>` — the trait when
    Since <= V < Until, else a distinct empty `absent<Trait>`. welder flattens an
    active trait's members onto the entity's binding (annotations survive, read off
    the declaring class); an inactive trait contributes nothing, so the field simply
    does not exist for that version (setting it is a compile error). */

#include <type_traits>

#include <wowlib/core/client_version.hpp>

namespace wowlib::formats
{
  /** A distinct empty base per @a Trait, so an entity inheriting several *inactive*
      version slots never inherits the same empty type twice (ill-formed). Empty, so
      it is elided (EBO). */
  template <class Trait>
  struct absent
  {
    // excluded from bindings: the parameter type is this unwelded base
    [[=welder::mark::exclude]]
    bool operator==(const absent&) const = default;
  };

  /** Above any supported client build — the default `Until` (never removed). */
  inline constexpr ClientVersion version_never_removed{255, 0, 0, 0};

  /** A version-gated base: the entity inherits @a Trait (flattening its chunk
      members in) iff @a Since <= @a V < @a Until, else the empty absent<Trait>.
      Group the chunks that share an availability range into one Trait; a chunk
      removed at some version goes in a trait with that version as @a Until. */
  template <ClientVersion V, ClientVersion Since, class Trait,
            ClientVersion Until = version_never_removed>
  using slot = std::conditional_t<(V >= Since && V < Until), Trait, absent<Trait>>;
}
