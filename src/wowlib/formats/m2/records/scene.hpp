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
  template <ClientVersion V>
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("An attachment point (weapons, effects, name plates), relative to a "
                 "bone.")
  ]] M2Attachment
  {
    [[=welder::doc("Attachment slot (see wowdev's attachment id table).")]]
    std::uint32_t id = 0;
    [[=welder::doc("The bone the point follows.")]]
    std::uint16_t bone = 0;
    std::uint16_t unknown = 0;
    [[=welder::doc("Relative to the bone, typically its pivot.")]]
    C3Vector position{};
    [[=welder::doc("Bool track: animate the attached model.")]]
    M2Track<std::uint8_t, V> animate_attached{};

    bool operator==(const M2Attachment&) const = default;
  };

  template <ClientVersion V>
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A timed event ($DTH death thud, footsteps, sounds); every "
                 "enabled-track key fires.")
  ]] M2Event
  {
    [[=welder::doc("Usually a '$xxx' four-char tag stored raw.")]]
    std::uint32_t identifier = 0;
    [[=welder::doc("Payload passed on fire (sound entry id, ...).")]]
    std::uint32_t data = 0;
    [[=welder::doc("The bone the event hangs off.")]]
    std::uint32_t bone = 0;
    [[=welder::doc("Relative to the bone.")]]
    C3Vector position{};
    [[=welder::doc("Timestamp-only track: each key fires the event.")]]
    M2TrackBase<V> enabled{};

    bool operator==(const M2Event&) const = default;
  };

  template <ClientVersion V>
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A model light: type 0 directional (login screens only), 1 point.")
  ]] M2Light
  {
    [[=welder::doc("0 directional, 1 point.")]]
    std::uint16_t type = 1;
    [[=welder::doc("Bone to attach to, -1 for none.")]]
    std::int16_t bone = -1;
    [[=welder::doc("Relative to the bone, if any.")]]
    C3Vector position{};
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

  template <ClientVersion V>
    requires (V < m2_multitex_particles)
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A camera, pre-Cata layout: a static diagonal FOV plus "
                 "position/target/roll spline tracks.")
  ]] M2Camera<V>
  {
    [[=welder::doc("0 portrait, 1 character info, -1 flyby.")]]
    std::uint32_t type = 0;
    [[=welder::doc("Diagonal field of view, radians.")]]
    float fov = 0;
    float far_clip = 0;
    float near_clip = 0;
    M2Track<M2SplineKey<C3Vector>, V> positions{};
    C3Vector position_base{};
    M2Track<M2SplineKey<C3Vector>, V> target_position{};
    C3Vector target_position_base{};
    [[=welder::doc("0 .. 2*pi.")]]
    M2Track<M2SplineKey<float>, V> roll{};

    bool operator==(const M2Camera&) const = default;
  };

  template <ClientVersion V>
    requires (V >= m2_multitex_particles)
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A camera (Cata+): the FOV becomes a spline track at the record tail.")
  ]] M2Camera<V>
  {
    [[=welder::doc("0 portrait, 1 character info, -1 flyby.")]]
    std::uint32_t type = 0;
    float far_clip = 0;
    float near_clip = 0;
    M2Track<M2SplineKey<C3Vector>, V> positions{};
    C3Vector position_base{};
    M2Track<M2SplineKey<C3Vector>, V> target_position{};
    C3Vector target_position_base{};
    [[=welder::doc("0 .. 2*pi.")]]
    M2Track<M2SplineKey<float>, V> roll{};
    [[=welder::doc("Diagonal field of view, radians.")]]
    M2Track<M2SplineKey<float>, V> fov{};

    bool operator==(const M2Camera&) const = default;
  };
}
