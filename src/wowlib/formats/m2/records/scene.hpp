#pragma once

/** @file
    M2 scene records (namespace wowlib::formats::m2::records): attachments,
    events, lights and cameras — the non-geometry model furniture. */

#include <cstdint>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/m2/boundaries.hpp>
#include <wowlib/formats/m2/records/track.hpp>

namespace wowlib::formats::m2::records
{
  /** An attachment point (weapons, effects, name plates), relative to a
      bone. */
  template <ClientVersion V>
  struct M2Attachment
  {
    std::uint32_t id = 0;      /**< Attachment slot (see wowdev's attachment id table). */
    std::uint16_t bone = 0;    /**< The bone the point follows. */
    std::uint16_t unknown = 0;
    C3Vector position{};       /**< Relative to the bone, typically its pivot. */
    M2Track<std::uint8_t, V> animate_attached{}; /**< Bool track: animate the attached model. */

    bool operator==(const M2Attachment&) const = default;
  };

  /** A timed event ($DTH death thud, footsteps, sounds); the enabled track
      is timestamp-only — every key fires. */
  template <ClientVersion V>
  struct M2Event
  {
    std::uint32_t identifier = 0; /**< Usually a '$xxx' four-char tag stored raw. */
    std::uint32_t data = 0;       /**< Payload passed on fire (sound entry id, ...). */
    std::uint32_t bone = 0;       /**< The bone the event hangs off. */
    C3Vector position{};          /**< Relative to the bone. */
    M2TrackBase<V> enabled{};     /**< Timestamp-only track: each key fires the event. */

    bool operator==(const M2Event&) const = default;
  };

  /** A model light (login screens, wands, doodads). Type 0 directional
      (login screens only), 1 point. */
  template <ClientVersion V>
  struct M2Light
  {
    std::uint16_t type = 1;  /**< 0 directional, 1 point. */
    std::int16_t bone = -1;  /**< Bone to attach to, -1 for none. */
    C3Vector position{};     /**< Relative to the bone, if any. */
    M2Track<C3Vector, V> ambient_color{};
    M2Track<float, V> ambient_intensity{};
    M2Track<C3Vector, V> diffuse_color{};
    M2Track<float, V> diffuse_intensity{};
    M2Track<float, V> attenuation_start{};
    M2Track<float, V> attenuation_end{};
    M2Track<std::uint8_t, V> visibility{};

    bool operator==(const M2Light&) const = default;
  };

  template <ClientVersion V>
  struct M2Camera;

  /** Pre-Cata: a static diagonal FOV (radians). */
  template <ClientVersion V>
    requires (V < m2_multitex_particles)
  struct M2Camera<V>
  {
    std::uint32_t type = 0;  /**< 0 portrait, 1 character info, -1 flyby. */
    float fov = 0;           /**< Diagonal field of view, radians. */
    float far_clip = 0;
    float near_clip = 0;
    M2Track<M2SplineKey<C3Vector>, V> positions{};
    C3Vector position_base{};
    M2Track<M2SplineKey<C3Vector>, V> target_position{};
    C3Vector target_position_base{};
    M2Track<M2SplineKey<float>, V> roll{};  /**< 0 .. 2*pi. */

    bool operator==(const M2Camera&) const = default;
  };

  /** Cata+: the FOV becomes a spline track at the record tail. */
  template <ClientVersion V>
    requires (V >= m2_multitex_particles)
  struct M2Camera<V>
  {
    std::uint32_t type = 0;  /**< 0 portrait, 1 character info, -1 flyby. */
    float far_clip = 0;
    float near_clip = 0;
    M2Track<M2SplineKey<C3Vector>, V> positions{};
    C3Vector position_base{};
    M2Track<M2SplineKey<C3Vector>, V> target_position{};
    C3Vector target_position_base{};
    M2Track<M2SplineKey<float>, V> roll{}; /**< 0 .. 2*pi. */
    M2Track<M2SplineKey<float>, V> fov{};  /**< Diagonal field of view, radians. */

    bool operator==(const M2Camera&) const = default;
  };
}
