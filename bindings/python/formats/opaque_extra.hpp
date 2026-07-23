#pragma once

/** @file
    Manual opaque-container registrations that welder's auto-generator misses.

    The opaque-container generator (opaque_gen.cpp) discovers std::vector uses by
    scanning welded types' members for a *direct* std::vector. std::vector<C2Vector>
    appears only nested inside Repeated<std::vector<C2Vector>, 3> (the MOTV texcoord
    member), and the scan does not descend into that wrapper — so VectorC2Vector is
    never generated, and the Repeated caster (formats/repeated_caster.hpp) falls back
    to nanobind's copy caster, binding texcoords as a by-value list[C2Vector].

    Declaring it opaque here — the same WELDER_OPAQUE(...) + welded alias the
    generator emits for every other vector — makes make_caster<std::vector<C2Vector>>
    resolve to the by-reference, zero-copy wrapper, so texcoords binds as
    list[VectorC2Vector], matching vertex_colors (whose std::vector<CImVector> IS
    discovered directly, via vertex_colors2).

    STOPGAP: delete once welder's opaque generator descends into wrapper templates
    like Repeated<> and generates VectorC2Vector itself.

    Include AFTER the nanobind rod (defines WELDER_OPAQUE) and the generated
    wowlib.opaque.hpp, BEFORE the WELDER_MODULE walk. */

#include <vector>

#include <wowlib/formats/common/types.hpp>

WELDER_OPAQUE(std::vector<wowlib::formats::C2Vector>)

namespace wowlib
{
  using VectorC2Vector [[=welder::weld(welder::lang::py)]] =
    std::vector<wowlib::formats::C2Vector>;
}
