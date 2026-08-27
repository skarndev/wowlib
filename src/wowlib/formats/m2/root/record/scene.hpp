#pragma once

/** @file
    M2 scene records (namespace wowlib::formats::m2::root::record): attachments,
    events, lights and cameras — the non-geometry model furniture. */

#include <cstdint>

#include <welder/vocabulary.hpp>

#include <wowlib/core/lang.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/m2/boundaries.hpp>
#include <wowlib/formats/m2/root/record/track.hpp>

namespace wowlib::formats::m2::root::record {
  /** The version-agnostic base of every M2Attachment<V> (welded as "M2Attachment").
      Bindings-only, like every *Base: it gives the per-version classes a
      common welded supertype, so the family surface hoists their shared
      members and containers of them carry a base-typed live view. */
  struct [[
      =welder::weld,
      =welder::weld_as("M2Attachment"),
  WOWLIB_CS_FAMILY_SURFACE
      =welder::doc(R"(
        One attachment point. Abstract over the client version; construct a concrete
        version with M2Attachment.ForVersion / for_version.)")
    ]] M2AttachmentBase {
    // The concretes default operator== — the base must be comparable too.
    bool operator==(const M2AttachmentBase&) const = default;
  };

  /** The version-agnostic base of every M2Event<V> (welded as "M2Event").
      Bindings-only, like every *Base: it gives the per-version classes a
      common welded supertype, so the family surface hoists their shared
      members and containers of them carry a base-typed live view. */
  struct [[
      =welder::weld,
      =welder::weld_as("M2Event"),
  WOWLIB_CS_FAMILY_SURFACE
      =welder::doc(R"(
        One timed event. Abstract over the client version; construct a concrete
        version with M2Event.ForVersion / for_version.)")
    ]] M2EventBase {
    // The concretes default operator== — the base must be comparable too.
    bool operator==(const M2EventBase&) const = default;
  };

  /** The version-agnostic base of every M2Light<V> (welded as "M2Light").
      Bindings-only, like every *Base: it gives the per-version classes a
      common welded supertype, so the family surface hoists their shared
      members and containers of them carry a base-typed live view. */
  struct [[
      =welder::weld,
      =welder::weld_as("M2Light"),
  WOWLIB_CS_FAMILY_SURFACE
      =welder::doc(R"(
        One placed light. Abstract over the client version; construct a concrete
        version with M2Light.ForVersion / for_version.)")
    ]] M2LightBase {
    // The concretes default operator== — the base must be comparable too.
    bool operator==(const M2LightBase&) const = default;
  };

  /** The version-agnostic base of every M2Camera<V> (welded as "M2Camera").
      Bindings-only, like every *Base: it gives the per-version classes a
      common welded supertype, so the family surface hoists their shared
      members and containers of them carry a base-typed live view. */
  struct [[
      =welder::weld,
      =welder::weld_as("M2Camera"),
  WOWLIB_CS_FAMILY_SURFACE
      =welder::doc(R"(
        One model camera. Abstract over the client version; construct a concrete
        version with M2Camera.ForVersion / for_version.)")
    ]] M2CameraBase {
    // The concretes default operator== — the base must be comparable too.
    bool operator==(const M2CameraBase&) const = default;
  };

  namespace detail {
    // The annotated era layouts; instantiate through the canonicalizing
    // aliases below, never directly.
    template <ClientVersion V>
    struct [[
        =welder::weld,
        =welder::doc(
          "An attachment point (weapons, effects, name plates), relative to a "
          "bone.")
      ]] M2Attachment : M2AttachmentBase {
      [[=welder::doc("Attachment slot (see wowdev's attachment id table).")]]
      std::uint32_t id = 0;
      [[=welder::doc("The bone the point follows.")]]
      std::uint16_t bone = 0;
      [[=welder::doc("Unknown; almost always 0 (vanilla's BogBeast.m2 carries "
        "values here).")]]
      std::uint16_t unknown = 0;
      [[=welder::doc("Relative to the bone, typically its pivot.")]]
      C3Vector position{};
      [[=welder::doc("Bool track: animate the attached model.")]]
      record::M2Track<std::uint8_t, V> animateAttached{};

      bool operator==(const M2Attachment&) const = default;
    };

    template <ClientVersion V>
    struct [[
        =welder::weld,
        =welder::doc(
          "A timed event ($DTH death thud, footsteps, sounds); every "
          "enabled-track key fires.")
      ]] M2Event : M2EventBase {
      [[=welder::doc("Usually a '$xxx' four-char tag stored raw.")]]
      std::uint32_t identifier = 0;
      [[=welder::doc("Payload passed on fire (sound entry id, ...).")]]
      std::uint32_t data = 0;
      [[=welder::doc("The bone the event hangs off.")]]
      std::uint32_t bone = 0;
      [[=welder::doc("Relative to the bone.")]]
      C3Vector position{};
      [[=welder::doc("Timestamp-only track: each key fires the event.")]]
      record::M2TrackBase<V> enabled{};

      bool operator==(const M2Event&) const = default;
    };

    template <ClientVersion V>
    struct [[
        =welder::weld,
        =welder::doc(
          "A model light: type 0 directional (login screens only), 1 point.")
      ]] M2Light : M2LightBase {
      [[=welder::doc("0 directional, 1 point.")]]
      std::uint16_t type = 1;
      [[=welder::doc("Bone to attach to, -1 for none.")]]
      std::int16_t bone = -1;
      [[=welder::doc("Relative to the bone, if any.")]]
      C3Vector position{};
      [[=welder::doc("RGB ambient color (no alpha).")]]
      record::M2Track<C3Vector, V> ambientColor{};
      [[=welder::doc("Multiplies the ambient color; defaults to 1.0.")]]
      record::M2Track<float, V> ambientIntensity{};
      [[=welder::doc("RGB diffuse color (no alpha).")]]
      record::M2Track<C3Vector, V> diffuseColor{};
      [[=welder::doc("Multiplies the diffuse color; defaults to 1.0.")]]
      record::M2Track<float, V> diffuseIntensity{};
      [[=welder::doc("Distance where attenuation begins.")]]
      record::M2Track<float, V> attenuationStart{};
      [[=welder::doc("Distance where the light fades out entirely.")]]
      record::M2Track<float, V> attenuationEnd{};
      [[=welder::doc("Bool track: the light is enabled.")]]
      record::M2Track<std::uint8_t, V> visibility{};

      bool operator==(const M2Light&) const = default;
    };

    template <ClientVersion V>
    struct M2Camera;

    template <ClientVersion V> requires (V < M2MultitexParticles)
    struct [[
        =welder::weld,
        =welder::doc("A camera, pre-Cata layout: a static diagonal FOV plus "
          "position/target/roll spline tracks.")
      ]] M2Camera<V> : M2CameraBase {
      [[=welder::doc("0 portrait, 1 character info, -1 flyby.")]]
      std::uint32_t type = 0;
      [[=welder::doc("Diagonal field of view, radians.")]]
      float fov = 0;
      [[=welder::doc("Far clip distance.")]]
      float farClip = 0;
      [[=welder::doc("Near clip distance.")]]
      float nearClip = 0;
      [[=welder::doc("Spline track moving the camera, one spline per segment.")]
      ]
      record::M2Track<M2SplineKey<C3Vector>, V> positions{};
      [[=welder::doc("Pivot point the position splines are relative to.")]]
      C3Vector positionBase{};
      [[=welder::doc("Spline track moving the look-at target, one spline per "
        "segment.")]]
      record::M2Track<M2SplineKey<C3Vector>, V> targetPosition{};
      [[=welder::doc("Pivot point the target splines are relative to.")]]
      C3Vector targetPositionBase{};
      [[=welder::doc("0 .. 2*pi.")]]
      record::M2Track<M2SplineKey<float>, V> roll{};

      bool operator==(const M2Camera&) const = default;
    };

    template <ClientVersion V>
      requires (V >= M2MultitexParticles)
    struct [[
        =welder::weld,
        =welder::doc(
          "A camera (Cata+): the FOV becomes a spline track at the record tail.")
      ]] M2Camera<V> : M2CameraBase {
      [[=welder::doc("0 portrait, 1 character info, -1 flyby.")]]
      std::uint32_t type = 0;
      [[=welder::doc("Far clip distance.")]]
      float farClip = 0;
      [[=welder::doc("Near clip distance.")]]
      float nearClip = 0;
      [[=welder::doc("Spline track moving the camera, one spline per segment.")]
      ]
      record::M2Track<M2SplineKey<C3Vector>, V> positions{};
      [[=welder::doc("Pivot point the position splines are relative to.")]]
      C3Vector positionBase{};
      [[=welder::doc("Spline track moving the look-at target, one spline per "
        "segment.")]]
      record::M2Track<M2SplineKey<C3Vector>, V> targetPosition{};
      [[=welder::doc("Pivot point the target splines are relative to.")]]
      C3Vector targetPositionBase{};
      [[=welder::doc("0 .. 2*pi.")]]
      record::M2Track<M2SplineKey<float>, V> roll{};
      [[=welder::doc("Diagonal field of view, radians.")]]
      record::M2Track<M2SplineKey<float>, V> fov{};

      bool operator==(const M2Camera&) const = default;
    };
  }

  // The canonicalizing faces. Attachments, events and lights vary only with
  // the embedded track era (M2TrackPivots); the camera also trades its
  // static FoV for a spline track at Cata (M2CameraPivots).
  template <ClientVersion V>
  using M2Attachment =
  detail::M2Attachment<canonicalVersion(V, M2TrackPivots, M2Versions)>;
  template <ClientVersion V>
  using M2Event = detail::M2Event<canonicalVersion(
    V, M2TrackPivots, M2Versions)>;
  template <ClientVersion V>
  using M2Light = detail::M2Light<canonicalVersion(
    V, M2TrackPivots, M2Versions)>;
  template <ClientVersion V>
  using M2Camera =
  detail::M2Camera<canonicalVersion(V, M2CameraPivots, M2Versions)>;
}
