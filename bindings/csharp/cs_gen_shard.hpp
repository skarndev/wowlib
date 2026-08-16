#pragma once

/** @file
    @brief Prologue of every generated C# GENERATOR shard TU
    (cs_gen_shard_<N>.cpp, dbdgen output).

    Each shard welds its slice of the client-database tables into the shared
    welder-csharp document at runtime (see welder-csharp's multi-TU generation:
    `rod::at` + `weld_type`). The reflection cost of ~13 tables per TU is what
    keeps the generator's peak memory at max(shard) instead of the whole
    surface at once — the single-TU generator approached 16 GB and was
    OOM-killed on CI runners. */

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/version_range.hpp>

#include <welder/rods/csharp/naming.hpp>
#include <welder/rods/csharp/rod.hpp>
