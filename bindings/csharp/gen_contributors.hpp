#pragma once

/** @file
    @brief The per-format generator contributors (multi-TU generation).

    One function per versioned format, each defined in its own gen_<fmt>.cpp
    TU that includes ONLY that format's per-range alias table — the
    version-matrix reflection compiles in parallel, peak generator memory is
    max(TU), and the single-TU generator's serial ~11 minutes becomes the
    width of the slowest format. Welding goes through weld_type<Alias>
    (explicit, no namespace walk), so nothing collides with the main TU's
    walk of the chunk-level classes; cross-TU type references resolve at
    render through the document's placeholder registries. */

namespace welder::inline v0::rods::csharp
{
  struct document;
}

namespace wowlib_cs
{
  void contributeWmo(::welder::rods::csharp::document& doc);
  void contributeM2(::welder::rods::csharp::document& doc);
  void contributeAdt(::welder::rods::csharp::document& doc);
  void contributeWdt(::welder::rods::csharp::document& doc);
  void contributeWdl(::welder::rods::csharp::document& doc);
}
