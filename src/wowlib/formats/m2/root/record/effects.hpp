#pragma once

/** @file
    M2 effect-emitter records (namespace wowlib::formats::m2::root::record):
    ribbon emitters and the particle emitters — the format's most
    layout-turbulent record, in four eras (vanilla statics, late-TBC packed
    header, WotLK FBlock ramps + four spin fields, Cata+ multi-texturing). */

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/lang.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/annotations.hpp>
#include <wowlib/formats/m2/boundaries.hpp>
#include <wowlib/formats/m2/root/record/track.hpp>

namespace wowlib::formats::m2::root::record
{
  /** The version-agnostic base of every M2Ribbon<V> (welded as "M2Ribbon").
      Bindings-only, like every *Base: it gives the per-version classes a
      common welded supertype, so the family surface hoists their shared
      members and containers of them carry a base-typed live view. */
  struct [[
    =welder::weld,
    =welder::weld_as("M2Ribbon"),
    WOWLIB_CS_FAMILY_SURFACE
    =welder::doc(R"(
        One ribbon emitter. Abstract over the client version; construct a concrete
        version with M2Ribbon.ForVersion / for_version.)")
  ]] M2RibbonBase
  {
    // The concretes default operator== — the base must be comparable too.
    bool operator==(const M2RibbonBase&) const = default;
  };

  /** The version-agnostic base of every M2Particle<V> (welded as "M2Particle").
      Bindings-only, like every *Base: it gives the per-version classes a
      common welded supertype, so the family surface hoists their shared
      members and containers of them carry a base-typed live view. */
  struct [[
    =welder::weld,
    =welder::weld_as("M2Particle"),
    WOWLIB_CS_FAMILY_SURFACE
    =welder::doc(R"(
        One particle emitter. Abstract over the client version; construct a concrete
        version with M2Particle.ForVersion / for_version.)")
  ]] M2ParticleBase
  {
    // The concretes default operator== — the base must be comparable too.
    bool operator==(const M2ParticleBase&) const = default;
  };

namespace detail
  {
    // The annotated era layouts; instantiate through the canonicalizing
    // aliases below, never directly.
      template <ClientVersion V>
    struct [[
      =welder::weld,
      =welder::doc("A ribbon (trail) emitter; the priority/color-index tail exists "
                   "WotLK+.")
    ]] M2Ribbon : M2RibbonBase
    {
      [[=welder::doc("Always -1 in known files.")]]
      std::uint32_t ribbon_id = 0xFFFFFFFF;
      [[=welder::doc("The bone the ribbon trails from.")]]
      std::uint32_t bone_index = 0;
      [[=welder::doc("Relative to that bone.")]]
      C3Vector position{};
      [[=welder::doc("Into the model's textures.")]]
      std::vector<std::uint16_t> texture_indices;
      [[=welder::doc("Into the model's materials.")]]
      std::vector<std::uint16_t> material_indices;
      [[=welder::doc("RGB multiplier for the material.")]]
      record::M2Track<C3Vector, V> color{};
      [[=welder::doc("0 transparent .. 0x7FFF opaque.")]]
      record::M2Track<fixed16, V> alpha{};
      [[=welder::doc("Ribbon width above the bone origin.")]]
      record::M2Track<float, V> height_above{};
      [[=welder::doc("Ribbon width below the bone origin; do not set equal to "
                     "height_above.")]]
      record::M2Track<float, V> height_below{};
      [[=welder::doc("Quad emission rate.")]]
      float edges_per_second = 0;
      [[=welder::doc("Seconds a quad stays around.")]]
      float edge_lifetime = 0;
      [[=welder::doc("Sinks/rises the ribbon over time.")]]
      float gravity = 0;
      [[=welder::doc("Flipbook tiling.")]]
      std::uint16_t texture_rows = 0;
      [[=welder::doc("Flipbook tiling.")]]
      std::uint16_t texture_cols = 0;
      [[=welder::doc("Animated flipbook cell index.")]]
      record::M2Track<std::uint16_t, V> tex_slot{};
      [[=welder::doc("Bool track: ribbon visible.")]]
      record::M2Track<std::uint8_t, V> visibility{};

      [[
        =since(m2_per_sequence_timelines),
        =welder::doc("Render priority plane (WotLK+).")]]
      std::int16_t priority_plane = 0;

      [[
        =since(m2_per_sequence_timelines),
        =welder::doc("ParticleColor.dbc replacement-color index (WotLK+).")]]
      std::int8_t ribbon_color_index = 0;

      [[
        =since(m2_per_sequence_timelines),
        =welder::doc("Index into the texture-transform combos, applied only "
                     "under global flag 0x20000 (WotLK+).")]]
      std::int8_t texture_transform_lookup_index = 0;

      bool operator==(const M2Ribbon&) const = default;
    };
  }

  /** A ribbon emitter — the canonicalizing face of detail::M2Ribbon (the
      WotLK trailing fields and the embedded track era share one pivot,
      m2_track_pivots). */
  template <ClientVersion V>
  using M2Ribbon = detail::M2Ribbon<canonical_version(V, m2_track_pivots, m2_versions)>;


  struct [[
    =welder::weld,
    =welder::doc("A 2D vector of 6.9 fixed-point values (raw u16 storage).")
  ]] M2Vec2FP69
  {
    [[=welder::doc("Raw 6.9 fixed-point x component.")]]
    std::uint16_t x = 0;
    [[=welder::doc("Raw 6.9 fixed-point y component.")]]
    std::uint16_t y = 0;

    bool operator==(const M2Vec2FP69&) const = default;
  };
  static_assert(sizeof(M2Vec2FP69) == 4);

namespace detail
  {
    // The annotated era layouts; instantiate through the canonicalizing
    // aliases below, never directly.
      template <ClientVersion V>
    struct M2Particle;

    template <ClientVersion V>
      requires (V < m2_compressed_bones)
    struct [[
      =welder::weld,
      =welder::doc("A particle emitter, vanilla layout: wide u16 blending/emitter header, "
                   "static color/scale/UV ramps, a single spin value.")
    ]] M2Particle<V> : M2ParticleBase
    {
      [[=welder::doc("Always -1 in known files.")]]
      std::uint32_t particle_id = 0xFFFFFFFF;
      [[=welder::doc("See wowdev's particle flag table.")]]
      std::uint32_t flags = 0;
      [[=welder::doc("Relative to the bone.")]]
      C3Vector position{};
      [[=welder::doc("The bone the emitter attaches to.")]]
      std::uint16_t bone_id = 0;
      [[=welder::doc("Into the model's textures.")]]
      std::uint16_t texture_id = 0;
      [[=welder::doc("Spawns model particles when set.")]]
      std::string geometry_model_filename;
      [[=welder::doc("Child emitters come from this model.")]]
      std::string recursion_model_filename;
      [[=welder::doc("Blend mode; see wowdev's blending-type table.")]]
      std::uint16_t blending_type = 0;
      [[=welder::doc("1 plane, 2 sphere, 3 spline, 4 bone.")]]
      std::uint16_t emitter_type = 0;
      [[=welder::doc("Render type; in practice implied by flags and model.")]]
      std::uint8_t particle_type = 0;
      [[=welder::doc("0 head, 1 tail, 2 both.")]]
      std::uint8_t head_or_tail = 0;
      [[=welder::doc("Render priority plane.")]]
      std::int16_t priority_plane = 0;
      [[=welder::doc("Flipbook tiling.")]]
      std::uint16_t rows = 0;
      [[=welder::doc("Flipbook tiling.")]]
      std::uint16_t columns = 0;
      [[=welder::doc("Base emission velocity.")]]
      record::M2Track<float, V> emission_speed{};
      [[=welder::doc("Random emission-speed variation (0..1).")]]
      record::M2Track<float, V> speed_variation{};
      [[=welder::doc("Max polar angle (0..pi): of the initial velocity for "
                     "plane, of the position for sphere emitters.")]]
      record::M2Track<float, V> vertical_range{};
      [[=welder::doc("Max azimuth angle (0..2*pi): of the initial velocity "
                     "for plane, of the position for sphere emitters.")]]
      record::M2Track<float, V> horizontal_range{};
      [[=welder::doc("Gravity; a compressed direction vector under the "
                     "CompressedGravity flag.")]]
      record::M2Track<float, V> gravity{};
      [[=welder::doc("Seconds each particle stays alive.")]]
      record::M2Track<float, V> lifespan{};
      [[=welder::doc("Particles emitted per second.")]]
      record::M2Track<float, V> emission_rate{};
      [[=welder::doc("Plane: emission area width; sphere: max radius.")]]
      record::M2Track<float, V> emission_area_width{};
      [[=welder::doc("Plane: emission area length; sphere: min radius.")]]
      record::M2Track<float, V> emission_area_length{};
      [[=welder::doc("If > 0, initial velocity points from (0, 0, z_source) "
                     "to the spawn point.")]]
      record::M2Track<float, V> z_source{};
      [[=welder::doc("Parametric middle of the lifespan.")]]
      float mid_point = 0;
      [[=welder::doc("Start/middle/end BGRA multiply.")]]
      std::array<CImVector, 3> color_values{};
      [[=welder::doc("Start/middle/end scale.")]]
      std::array<float, 3> scale_values{};
      [[=welder::doc("Head flipbook cells, first half of life "
                     "(start/middle/end).")]]
      std::array<std::uint16_t, 3> lifespan_uv_anim{};
      [[=welder::doc("Head flipbook cells, second half of life "
                     "(start/middle/end).")]]
      std::array<std::uint16_t, 3> decay_uv_anim{};
      [[=welder::doc("Tail flipbook cells, first half of life (start/end).")]]
      std::array<std::int16_t, 2> tail_uv_anim{};
      [[=welder::doc("Tail flipbook cells, second half of life (start/end).")]]
      std::array<std::int16_t, 2> tail_decay_uv_anim{};
      [[=welder::doc("Multiplier to the computed tail length.")]]
      float tail_length = 0;
      [[=welder::doc("Blinking speed.")]]
      float twinkle_speed = 0;
      [[=welder::doc("Fraction of the time visible (1.0 = always).")]]
      float twinkle_percent = 0;
      [[=welder::doc("Min/max random scale variation.")]]
      CRange twinkle_scale{};
      [[=welder::doc("Scales velocity inherited from the parent particle.")]]
      float inherit_velocity_scale = 0;
      [[=welder::doc("Speed is multiplied by exp(-drag * t).")]]
      float drag = 0;
      [[=welder::doc("1.0 = one full turn over the lifetime.")]]
      float spin = 0;
      [[=welder::doc("Angular velocity bounds (model particles).")]]
      M2Box tumble{};
      [[=welder::doc("Static wind, unless DynamicWind flag.")]]
      C3Vector wind_vector{};
      [[=welder::doc("Undocumented; paired with the static wind vector.")]]
      float wind_time = 0;
      [[=welder::doc("Emitter-follow ramp: at this emitter speed particles "
                     "follow by follow_scale1.")]]
      float follow_speed1 = 0;
      [[=welder::doc("Fraction of emitter motion applied at follow_speed1.")]]
      float follow_scale1 = 0;
      [[=welder::doc("Second point of the emitter-follow ramp.")]]
      float follow_speed2 = 0;
      [[=welder::doc("Fraction of emitter motion applied at follow_speed2.")]]
      float follow_scale2 = 0;
      [[=welder::doc("Spline emitter path.")]]
      std::vector<C3Vector> spline_points;
      [[=welder::doc("Bool track: emitter active.")]]
      record::M2Track<std::uint8_t, V> enabled_in{};

      bool operator==(const M2Particle&) const = default;
    };

    template <ClientVersion V>
      requires (V >= m2_compressed_bones && V < m2_per_sequence_timelines)
    struct [[
      =welder::weld,
      =welder::doc("A particle emitter, TBC layout: byte-packed blending/emitter beside "
                   "the ParticleColor.dbc index; ramps still static.")
    ]] M2Particle<V> : M2ParticleBase
    {
      [[=welder::doc("Always -1 in known files.")]]
      std::uint32_t particle_id = 0xFFFFFFFF;
      [[=welder::doc("See wowdev's particle flag table.")]]
      std::uint32_t flags = 0;
      [[=welder::doc("Relative to the bone.")]]
      C3Vector position{};
      [[=welder::doc("The bone the emitter attaches to.")]]
      std::uint16_t bone_id = 0;
      [[=welder::doc("Into the model's textures.")]]
      std::uint16_t texture_id = 0;
      [[=welder::doc("Spawns model particles when set.")]]
      std::string geometry_model_filename;
      [[=welder::doc("Child emitters come from this model.")]]
      std::string recursion_model_filename;
      [[=welder::doc("Blend mode; see wowdev's blending-type table.")]]
      std::uint8_t blending_type = 0;
      [[=welder::doc("1 plane, 2 sphere, 3 spline, 4 bone.")]]
      std::uint8_t emitter_type = 0;
      [[=welder::doc("ParticleColor.dbc row selector (0/11/12/13).")]]
      std::uint16_t particle_color_index = 0;
      [[=welder::doc("Render type; in practice implied by flags and model.")]]
      std::uint8_t particle_type = 0;
      [[=welder::doc("0 head, 1 tail, 2 both.")]]
      std::uint8_t head_or_tail = 0;
      [[=welder::doc("Render priority plane.")]]
      std::int16_t priority_plane = 0;
      [[=welder::doc("Flipbook tiling.")]]
      std::uint16_t rows = 0;
      [[=welder::doc("Flipbook tiling.")]]
      std::uint16_t columns = 0;
      [[=welder::doc("Base emission velocity.")]]
      record::M2Track<float, V> emission_speed{};
      [[=welder::doc("Random emission-speed variation (0..1).")]]
      record::M2Track<float, V> speed_variation{};
      [[=welder::doc("Max polar angle (0..pi): of the initial velocity for "
                     "plane, of the position for sphere emitters.")]]
      record::M2Track<float, V> vertical_range{};
      [[=welder::doc("Max azimuth angle (0..2*pi): of the initial velocity "
                     "for plane, of the position for sphere emitters.")]]
      record::M2Track<float, V> horizontal_range{};
      [[=welder::doc("Gravity; a compressed direction vector under the "
                     "CompressedGravity flag.")]]
      record::M2Track<float, V> gravity{};
      [[=welder::doc("Seconds each particle stays alive.")]]
      record::M2Track<float, V> lifespan{};
      [[=welder::doc("Particles emitted per second.")]]
      record::M2Track<float, V> emission_rate{};
      [[=welder::doc("Plane: emission area width; sphere: max radius.")]]
      record::M2Track<float, V> emission_area_width{};
      [[=welder::doc("Plane: emission area length; sphere: min radius.")]]
      record::M2Track<float, V> emission_area_length{};
      [[=welder::doc("If > 0, initial velocity points from (0, 0, z_source) "
                     "to the spawn point.")]]
      record::M2Track<float, V> z_source{};
      [[=welder::doc("Parametric middle of the lifespan.")]]
      float mid_point = 0;
      [[=welder::doc("Start/middle/end BGRA multiply.")]]
      std::array<CImVector, 3> color_values{};
      [[=welder::doc("Start/middle/end scale.")]]
      std::array<float, 3> scale_values{};
      [[=welder::doc("Head flipbook cells, first half of life "
                     "(start/middle/end).")]]
      std::array<std::uint16_t, 3> lifespan_uv_anim{};
      [[=welder::doc("Head flipbook cells, second half of life "
                     "(start/middle/end).")]]
      std::array<std::uint16_t, 3> decay_uv_anim{};
      [[=welder::doc("Tail flipbook cells, first half of life (start/end).")]]
      std::array<std::int16_t, 2> tail_uv_anim{};
      [[=welder::doc("Tail flipbook cells, second half of life (start/end).")]]
      std::array<std::int16_t, 2> tail_decay_uv_anim{};
      [[=welder::doc("Multiplier to the computed tail length.")]]
      float tail_length = 0;
      [[=welder::doc("Blinking speed.")]]
      float twinkle_speed = 0;
      [[=welder::doc("Fraction of the time visible (1.0 = always).")]]
      float twinkle_percent = 0;
      [[=welder::doc("Min/max random scale variation.")]]
      CRange twinkle_scale{};
      [[=welder::doc("Scales velocity inherited from the parent particle.")]]
      float inherit_velocity_scale = 0;
      [[=welder::doc("Speed is multiplied by exp(-drag * t).")]]
      float drag = 0;
      [[=welder::doc("1.0 = one full turn over the lifetime.")]]
      float spin = 0;
      [[=welder::doc("Angular velocity bounds (model particles).")]]
      M2Box tumble{};
      [[=welder::doc("Static wind, unless DynamicWind flag.")]]
      C3Vector wind_vector{};
      [[=welder::doc("Undocumented; paired with the static wind vector.")]]
      float wind_time = 0;
      [[=welder::doc("Emitter-follow ramp: at this emitter speed particles "
                     "follow by follow_scale1.")]]
      float follow_speed1 = 0;
      [[=welder::doc("Fraction of emitter motion applied at follow_speed1.")]]
      float follow_scale1 = 0;
      [[=welder::doc("Second point of the emitter-follow ramp.")]]
      float follow_speed2 = 0;
      [[=welder::doc("Fraction of emitter motion applied at follow_speed2.")]]
      float follow_scale2 = 0;
      [[=welder::doc("Spline emitter path.")]]
      std::vector<C3Vector> spline_points;
      [[=welder::doc("Bool track: emitter active.")]]
      record::M2Track<std::uint8_t, V> enabled_in{};

      bool operator==(const M2Particle&) const = default;
    };

    template <ClientVersion V>
      requires (V >= m2_per_sequence_timelines && V < m2_multitex_particles)
    struct [[
      =welder::weld,
      =welder::doc("A particle emitter, WotLK-era layout (476 bytes): FBlock ramps, "
                   "lifespan/emission variation, four spin fields.")
    ]] M2Particle<V> : M2ParticleBase
    {
      [[=welder::doc("Always -1 in known files.")]]
      std::uint32_t particle_id = 0xFFFFFFFF;
      [[=welder::doc("See wowdev's particle flag table.")]]
      std::uint32_t flags = 0;
      [[=welder::doc("Relative to the bone.")]]
      C3Vector position{};
      [[=welder::doc("The bone the emitter attaches to.")]]
      std::uint16_t bone_id = 0;
      [[=welder::doc("Into the model's textures.")]]
      std::uint16_t texture_id = 0;
      [[=welder::doc("Spawns model particles when set.")]]
      std::string geometry_model_filename;
      [[=welder::doc("Child emitters come from this model.")]]
      std::string recursion_model_filename;
      [[=welder::doc("Blend mode; see wowdev's blending-type table.")]]
      std::uint8_t blending_type = 0;
      [[=welder::doc("1 plane, 2 sphere, 3 spline, 4 bone.")]]
      std::uint8_t emitter_type = 0;
      [[=welder::doc("ParticleColor.dbc row selector (0/11/12/13).")]]
      std::uint16_t particle_color_index = 0;
      [[=welder::doc("Render type; in practice implied by flags and model.")]]
      std::uint8_t particle_type = 0;
      [[=welder::doc("0 head, 1 tail, 2 both.")]]
      std::uint8_t head_or_tail = 0;
      [[=welder::doc("Render priority plane.")]]
      std::int16_t priority_plane = 0;
      [[=welder::doc("Flipbook tiling.")]]
      std::uint16_t rows = 0;
      [[=welder::doc("Flipbook tiling.")]]
      std::uint16_t columns = 0;
      [[=welder::doc("Base emission velocity.")]]
      record::M2Track<float, V> emission_speed{};
      [[=welder::doc("Random emission-speed variation (0..1).")]]
      record::M2Track<float, V> speed_variation{};
      [[=welder::doc("Max polar angle (0..pi): of the initial velocity for "
                     "plane, of the position for sphere emitters.")]]
      record::M2Track<float, V> vertical_range{};
      [[=welder::doc("Max azimuth angle (0..2*pi): of the initial velocity "
                     "for plane, of the position for sphere emitters.")]]
      record::M2Track<float, V> horizontal_range{};
      [[=welder::doc("Gravity; a compressed direction vector under the "
                     "CompressedGravity flag.")]]
      record::M2Track<float, V> gravity{};
      [[=welder::doc("Seconds each particle stays alive.")]]
      record::M2Track<float, V> lifespan{};
      [[=welder::doc("+ lifespan_variation * random(-1, 1).")]]
      float lifespan_variation = 0;
      [[=welder::doc("Particles emitted per second.")]]
      record::M2Track<float, V> emission_rate{};
      [[=welder::doc("+ emission_rate_variation * random(-1, 1), rerolled per "
                     "update.")]]
      float emission_rate_variation = 0;
      [[=welder::doc("Plane: emission area width; sphere: max radius.")]]
      record::M2Track<float, V> emission_area_width{};
      [[=welder::doc("Plane: emission area length; sphere: min radius.")]]
      record::M2Track<float, V> emission_area_length{};
      [[=welder::doc("If > 0, initial velocity points from (0, 0, z_source) "
                     "to the spawn point.")]]
      record::M2Track<float, V> z_source{};
      [[=welder::doc("Usually 3 keys: start/middle/end.")]]
      FBlock<C3Vector> color_track{};
      [[=welder::doc("Opacity ramp: 0 transparent .. 0x7FFF opaque.")]]
      FBlock<fixed16> alpha_track{};
      [[=welder::doc("Particle size ramp.")]]
      FBlock<C2Vector> scale_track{};
      [[=welder::doc("Random per-particle scale variation.")]]
      C2Vector scale_vary{};
      [[=welder::doc("Head flipbook cell ramp.")]]
      FBlock<std::uint16_t> head_uv_anim{};
      [[=welder::doc("Tail flipbook cell ramp.")]]
      FBlock<std::uint16_t> tail_uv_anim{};
      [[=welder::doc("Multiplier to the computed tail length.")]]
      float tail_length = 0;
      [[=welder::doc("Blinking speed.")]]
      float twinkle_speed = 0;
      [[=welder::doc("Fraction of the time visible (1.0 = always).")]]
      float twinkle_percent = 0;
      [[=welder::doc("Min/max random scale variation.")]]
      CRange twinkle_scale{};
      [[=welder::doc("Scales velocity inherited from the parent particle.")]]
      float inherit_velocity_scale = 0;
      [[=welder::doc("Speed is multiplied by exp(-drag * t).")]]
      float drag = 0;
      [[=welder::doc("Initial quad rotation.")]]
      float base_spin = 0;
      [[=welder::doc("Random variation of base_spin.")]]
      float base_spin_variation = 0;
      [[=welder::doc("Quad rotation per second.")]]
      float spin_speed = 0;
      [[=welder::doc("Random variation of spin_speed.")]]
      float spin_speed_variation = 0;
      [[=welder::doc("Angular velocity bounds (model particles).")]]
      M2Box tumble{};
      [[=welder::doc("Static wind, unless DynamicWind flag.")]]
      C3Vector wind_vector{};
      [[=welder::doc("Undocumented; paired with the static wind vector.")]]
      float wind_time = 0;
      [[=welder::doc("Emitter-follow ramp: at this emitter speed particles "
                     "follow by follow_scale1.")]]
      float follow_speed1 = 0;
      [[=welder::doc("Fraction of emitter motion applied at follow_speed1.")]]
      float follow_scale1 = 0;
      [[=welder::doc("Second point of the emitter-follow ramp.")]]
      float follow_speed2 = 0;
      [[=welder::doc("Fraction of emitter motion applied at follow_speed2.")]]
      float follow_scale2 = 0;
      [[=welder::doc("Spline emitter path.")]]
      std::vector<C3Vector> spline_points;
      [[=welder::doc("Bool track: emitter active.")]]
      record::M2Track<std::uint8_t, V> enabled_in{};

      bool operator==(const M2Particle&) const = default;
    };

    template <ClientVersion V>
      requires (V >= m2_multitex_particles)
    struct [[
      =welder::weld,
      =welder::doc("A particle emitter (Cata+, 492 bytes): multi-textured — packed "
                   "texture ids, multiTexScale, trailing scroll parameters.")
    ]] M2Particle<V> : M2ParticleBase
    {
      [[=welder::doc("Always -1 in known files.")]]
      std::uint32_t particle_id = 0xFFFFFFFF;
      [[=welder::doc("See wowdev's particle flag table.")]]
      std::uint32_t flags = 0;
      [[=welder::doc("Relative to the bone.")]]
      C3Vector position{};
      [[=welder::doc("The bone the emitter attaches to.")]]
      std::uint16_t bone_id = 0;
      [[=welder::doc("3x5-bit texture ids under the MultiTexture flag.")]]
      std::uint16_t texture_id = 0;
      [[=welder::doc("Spawns model particles when set.")]]
      std::string geometry_model_filename;
      [[=welder::doc("Child emitters come from this model.")]]
      std::string recursion_model_filename;
      [[=welder::doc("Blend mode; see wowdev's blending-type table.")]]
      std::uint8_t blending_type = 0;
      [[=welder::doc("1 plane, 2 sphere, 3 spline, 4 bone.")]]
      std::uint8_t emitter_type = 0;
      [[=welder::doc("ParticleColor.dbc row selector (0/11/12/13).")]]
      std::uint16_t particle_color_index = 0;
      [[=welder::doc("2.5 fixed-point per extra layer.")]]
      std::array<std::int8_t, 2> multi_tex_scale{};
      [[=welder::doc("Render priority plane.")]]
      std::int16_t priority_plane = 0;
      [[=welder::doc("Flipbook tiling.")]]
      std::uint16_t rows = 0;
      [[=welder::doc("Flipbook tiling.")]]
      std::uint16_t columns = 0;
      [[=welder::doc("Base emission velocity.")]]
      record::M2Track<float, V> emission_speed{};
      [[=welder::doc("Random emission-speed variation (0..1).")]]
      record::M2Track<float, V> speed_variation{};
      [[=welder::doc("Max polar angle (0..pi): of the initial velocity for "
                     "plane, of the position for sphere emitters.")]]
      record::M2Track<float, V> vertical_range{};
      [[=welder::doc("Max azimuth angle (0..2*pi): of the initial velocity "
                     "for plane, of the position for sphere emitters.")]]
      record::M2Track<float, V> horizontal_range{};
      [[=welder::doc("Gravity; a compressed direction vector under the "
                     "CompressedGravity flag.")]]
      record::M2Track<float, V> gravity{};
      [[=welder::doc("Seconds each particle stays alive.")]]
      record::M2Track<float, V> lifespan{};
      [[=welder::doc("+ lifespan_variation * random(-1, 1).")]]
      float lifespan_variation = 0;
      [[=welder::doc("Particles emitted per second.")]]
      record::M2Track<float, V> emission_rate{};
      [[=welder::doc("+ emission_rate_variation * random(-1, 1), rerolled per "
                     "update.")]]
      float emission_rate_variation = 0;
      [[=welder::doc("Plane: emission area width; sphere: max radius.")]]
      record::M2Track<float, V> emission_area_width{};
      [[=welder::doc("Plane: emission area length; sphere: min radius.")]]
      record::M2Track<float, V> emission_area_length{};
      [[=welder::doc("If > 0, initial velocity points from (0, 0, z_source) "
                     "to the spawn point.")]]
      record::M2Track<float, V> z_source{};
      [[=welder::doc("Usually 3 keys: start/middle/end.")]]
      FBlock<C3Vector> color_track{};
      [[=welder::doc("Opacity ramp: 0 transparent .. 0x7FFF opaque.")]]
      FBlock<fixed16> alpha_track{};
      [[=welder::doc("Particle size ramp.")]]
      FBlock<C2Vector> scale_track{};
      [[=welder::doc("Random per-particle scale variation.")]]
      C2Vector scale_vary{};
      [[=welder::doc("Head flipbook cell ramp.")]]
      FBlock<std::uint16_t> head_uv_anim{};
      [[=welder::doc("Tail flipbook cell ramp.")]]
      FBlock<std::uint16_t> tail_uv_anim{};
      [[=welder::doc("Multiplier to the computed tail length.")]]
      float tail_length = 0;
      [[=welder::doc("Blinking speed.")]]
      float twinkle_speed = 0;
      [[=welder::doc("Fraction of the time visible (1.0 = always).")]]
      float twinkle_percent = 0;
      [[=welder::doc("Min/max random scale variation.")]]
      CRange twinkle_scale{};
      [[=welder::doc("Scales velocity inherited from the parent particle.")]]
      float inherit_velocity_scale = 0;
      [[=welder::doc("Speed is multiplied by exp(-drag * t).")]]
      float drag = 0;
      [[=welder::doc("Initial quad rotation.")]]
      float base_spin = 0;
      [[=welder::doc("Random variation of base_spin.")]]
      float base_spin_variation = 0;
      [[=welder::doc("Quad rotation per second.")]]
      float spin_speed = 0;
      [[=welder::doc("Random variation of spin_speed.")]]
      float spin_speed_variation = 0;
      [[=welder::doc("Angular velocity bounds (model particles).")]]
      M2Box tumble{};
      [[=welder::doc("Static wind, unless DynamicWind flag.")]]
      C3Vector wind_vector{};
      [[=welder::doc("Undocumented; paired with the static wind vector.")]]
      float wind_time = 0;
      [[=welder::doc("Emitter-follow ramp: at this emitter speed particles "
                     "follow by follow_scale1.")]]
      float follow_speed1 = 0;
      [[=welder::doc("Fraction of emitter motion applied at follow_speed1.")]]
      float follow_scale1 = 0;
      [[=welder::doc("Second point of the emitter-follow ramp.")]]
      float follow_speed2 = 0;
      [[=welder::doc("Fraction of emitter motion applied at follow_speed2.")]]
      float follow_scale2 = 0;
      [[=welder::doc("Spline emitter path.")]]
      std::vector<C3Vector> spline_points;
      [[=welder::doc("Bool track: emitter active.")]]
      record::M2Track<std::uint8_t, V> enabled_in{};
      [[=welder::doc("Per extra layer.")]]
      std::array<M2Vec2FP69, 2> multi_tex_scroll_mid{};
      [[=welder::doc("Per extra layer.")]]
      std::array<M2Vec2FP69, 2> multi_tex_scroll_range{};

      bool operator==(const M2Particle&) const = default;
    };
  }

  /** A particle emitter — the canonicalizing face of detail::M2Particle
      (m2_particle_pivots: TBC's byte-packed types, WotLK's ramps and spins,
      Cata's multi-texture layout). */
  template <ClientVersion V>
  using M2Particle =
    detail::M2Particle<canonical_version(V, m2_particle_pivots, m2_versions)>;

}
