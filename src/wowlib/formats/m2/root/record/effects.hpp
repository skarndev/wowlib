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

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/annotations.hpp>
#include <wowlib/formats/m2/boundaries.hpp>
#include <wowlib/formats/m2/root/record/track.hpp>

namespace wowlib::formats::m2::root::record
{
namespace detail
  {
    // The annotated era layouts; instantiate through the canonicalizing
    // aliases below, never directly.
      template <ClientVersion V>
    struct [[
      =welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("A ribbon (trail) emitter; the priority/color-index tail exists "
                   "WotLK+.")
    ]] M2Ribbon
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
      record::M2Track<C3Vector, V> color{};
      [[=welder::doc("0 transparent .. 0x7FFF opaque.")]]
      record::M2Track<fixed16, V> alpha{};
      record::M2Track<float, V> height_above{};
      record::M2Track<float, V> height_below{};
      [[=welder::doc("Quad emission rate.")]]
      float edges_per_second = 0;
      [[=welder::doc("Seconds a quad stays around.")]]
      float edge_lifetime = 0;
      [[=welder::doc("Sinks/rises the ribbon over time.")]]
      float gravity = 0;
      [[=welder::doc("Flipbook tiling.")]]
      std::uint16_t texture_rows = 0;
      std::uint16_t texture_cols = 0;
      record::M2Track<std::uint16_t, V> tex_slot{};
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
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A 2D vector of 6.9 fixed-point values (raw u16 storage).")
  ]] M2Vec2FP69
  {
    std::uint16_t x = 0;
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
      =welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("A particle emitter, vanilla layout: wide u16 blending/emitter header, "
                   "static color/scale/UV ramps, a single spin value.")
    ]] M2Particle<V>
    {
      [[=welder::doc("Always -1 in known files.")]]
      std::uint32_t particle_id = 0xFFFFFFFF;
      [[=welder::doc("See wowdev's particle flag table.")]]
      std::uint32_t flags = 0;
      [[=welder::doc("Relative to the bone.")]]
      C3Vector position{};
      std::uint16_t bone_id = 0;
      std::uint16_t texture_id = 0;
      [[=welder::doc("Spawns model particles when set.")]]
      std::string geometry_model_filename;
      [[=welder::doc("Child emitters come from this model.")]]
      std::string recursion_model_filename;
      std::uint16_t blending_type = 0;
      [[=welder::doc("1 plane, 2 sphere, 3 spline, 4 bone.")]]
      std::uint16_t emitter_type = 0;
      std::uint8_t particle_type = 0;
      [[=welder::doc("0 head, 1 tail, 2 both.")]]
      std::uint8_t head_or_tail = 0;
      std::int16_t priority_plane = 0;
      [[=welder::doc("Flipbook tiling.")]]
      std::uint16_t rows = 0;
      std::uint16_t columns = 0;
      record::M2Track<float, V> emission_speed{};
      record::M2Track<float, V> speed_variation{};
      record::M2Track<float, V> vertical_range{};
      record::M2Track<float, V> horizontal_range{};
      record::M2Track<float, V> gravity{};
      record::M2Track<float, V> lifespan{};
      record::M2Track<float, V> emission_rate{};
      record::M2Track<float, V> emission_area_width{};
      record::M2Track<float, V> emission_area_length{};
      record::M2Track<float, V> z_source{};
      [[=welder::doc("Parametric middle of the lifespan.")]]
      float mid_point = 0;
      [[=welder::doc("Start/middle/end BGRA multiply.")]]
      std::array<CImVector, 3> color_values{};
      [[=welder::doc("Start/middle/end scale.")]]
      std::array<float, 3> scale_values{};
      std::array<std::uint16_t, 3> lifespan_uv_anim{};
      std::array<std::uint16_t, 3> decay_uv_anim{};
      std::array<std::int16_t, 2> tail_uv_anim{};
      std::array<std::int16_t, 2> tail_decay_uv_anim{};
      float tail_length = 0;
      float twinkle_speed = 0;
      float twinkle_percent = 0;
      CRange twinkle_scale{};
      float inherit_velocity_scale = 0;
      float drag = 0;
      [[=welder::doc("1.0 = one full turn over the lifetime.")]]
      float spin = 0;
      [[=welder::doc("Angular velocity bounds (model particles).")]]
      M2Box tumble{};
      [[=welder::doc("Static wind, unless DynamicWind flag.")]]
      C3Vector wind_vector{};
      float wind_time = 0;
      float follow_speed1 = 0;
      float follow_scale1 = 0;
      float follow_speed2 = 0;
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
      =welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("A particle emitter, TBC layout: byte-packed blending/emitter beside "
                   "the ParticleColor.dbc index; ramps still static.")
    ]] M2Particle<V>
    {
      [[=welder::doc("Always -1 in known files.")]]
      std::uint32_t particle_id = 0xFFFFFFFF;
      [[=welder::doc("See wowdev's particle flag table.")]]
      std::uint32_t flags = 0;
      [[=welder::doc("Relative to the bone.")]]
      C3Vector position{};
      std::uint16_t bone_id = 0;
      std::uint16_t texture_id = 0;
      [[=welder::doc("Spawns model particles when set.")]]
      std::string geometry_model_filename;
      [[=welder::doc("Child emitters come from this model.")]]
      std::string recursion_model_filename;
      std::uint8_t blending_type = 0;
      [[=welder::doc("1 plane, 2 sphere, 3 spline, 4 bone.")]]
      std::uint8_t emitter_type = 0;
      [[=welder::doc("ParticleColor.dbc row selector (0/11/12/13).")]]
      std::uint16_t particle_color_index = 0;
      std::uint8_t particle_type = 0;
      [[=welder::doc("0 head, 1 tail, 2 both.")]]
      std::uint8_t head_or_tail = 0;
      std::int16_t priority_plane = 0;
      [[=welder::doc("Flipbook tiling.")]]
      std::uint16_t rows = 0;
      std::uint16_t columns = 0;
      record::M2Track<float, V> emission_speed{};
      record::M2Track<float, V> speed_variation{};
      record::M2Track<float, V> vertical_range{};
      record::M2Track<float, V> horizontal_range{};
      record::M2Track<float, V> gravity{};
      record::M2Track<float, V> lifespan{};
      record::M2Track<float, V> emission_rate{};
      record::M2Track<float, V> emission_area_width{};
      record::M2Track<float, V> emission_area_length{};
      record::M2Track<float, V> z_source{};
      [[=welder::doc("Parametric middle of the lifespan.")]]
      float mid_point = 0;
      [[=welder::doc("Start/middle/end BGRA multiply.")]]
      std::array<CImVector, 3> color_values{};
      [[=welder::doc("Start/middle/end scale.")]]
      std::array<float, 3> scale_values{};
      std::array<std::uint16_t, 3> lifespan_uv_anim{};
      std::array<std::uint16_t, 3> decay_uv_anim{};
      std::array<std::int16_t, 2> tail_uv_anim{};
      std::array<std::int16_t, 2> tail_decay_uv_anim{};
      float tail_length = 0;
      float twinkle_speed = 0;
      float twinkle_percent = 0;
      CRange twinkle_scale{};
      float inherit_velocity_scale = 0;
      float drag = 0;
      [[=welder::doc("1.0 = one full turn over the lifetime.")]]
      float spin = 0;
      [[=welder::doc("Angular velocity bounds (model particles).")]]
      M2Box tumble{};
      [[=welder::doc("Static wind, unless DynamicWind flag.")]]
      C3Vector wind_vector{};
      float wind_time = 0;
      float follow_speed1 = 0;
      float follow_scale1 = 0;
      float follow_speed2 = 0;
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
      =welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("A particle emitter, WotLK-era layout (476 wire bytes): FBlock ramps, "
                   "lifespan/emission variation, four spin fields.")
    ]] M2Particle<V>
    {
      [[=welder::doc("Always -1 in known files.")]]
      std::uint32_t particle_id = 0xFFFFFFFF;
      [[=welder::doc("See wowdev's particle flag table.")]]
      std::uint32_t flags = 0;
      [[=welder::doc("Relative to the bone.")]]
      C3Vector position{};
      std::uint16_t bone_id = 0;
      std::uint16_t texture_id = 0;
      [[=welder::doc("Spawns model particles when set.")]]
      std::string geometry_model_filename;
      [[=welder::doc("Child emitters come from this model.")]]
      std::string recursion_model_filename;
      std::uint8_t blending_type = 0;
      [[=welder::doc("1 plane, 2 sphere, 3 spline, 4 bone.")]]
      std::uint8_t emitter_type = 0;
      [[=welder::doc("ParticleColor.dbc row selector (0/11/12/13).")]]
      std::uint16_t particle_color_index = 0;
      std::uint8_t particle_type = 0;
      [[=welder::doc("0 head, 1 tail, 2 both.")]]
      std::uint8_t head_or_tail = 0;
      std::int16_t priority_plane = 0;
      [[=welder::doc("Flipbook tiling.")]]
      std::uint16_t rows = 0;
      std::uint16_t columns = 0;
      record::M2Track<float, V> emission_speed{};
      record::M2Track<float, V> speed_variation{};
      record::M2Track<float, V> vertical_range{};
      record::M2Track<float, V> horizontal_range{};
      record::M2Track<float, V> gravity{};
      record::M2Track<float, V> lifespan{};
      [[=welder::doc("+ lifespan_variation * random(-1, 1).")]]
      float lifespan_variation = 0;
      record::M2Track<float, V> emission_rate{};
      float emission_rate_variation = 0;
      record::M2Track<float, V> emission_area_width{};
      record::M2Track<float, V> emission_area_length{};
      record::M2Track<float, V> z_source{};
      [[=welder::doc("Usually 3 keys: start/middle/end.")]]
      FBlock<C3Vector> color_track{};
      FBlock<fixed16> alpha_track{};
      FBlock<C2Vector> scale_track{};
      [[=welder::doc("Random per-particle scale variation.")]]
      C2Vector scale_vary{};
      FBlock<std::uint16_t> head_uv_anim{};
      FBlock<std::uint16_t> tail_uv_anim{};
      float tail_length = 0;
      float twinkle_speed = 0;
      float twinkle_percent = 0;
      CRange twinkle_scale{};
      float inherit_velocity_scale = 0;
      float drag = 0;
      [[=welder::doc("Initial quad rotation.")]]
      float base_spin = 0;
      float base_spin_variation = 0;
      [[=welder::doc("Quad rotation per second.")]]
      float spin_speed = 0;
      float spin_speed_variation = 0;
      [[=welder::doc("Angular velocity bounds (model particles).")]]
      M2Box tumble{};
      [[=welder::doc("Static wind, unless DynamicWind flag.")]]
      C3Vector wind_vector{};
      float wind_time = 0;
      float follow_speed1 = 0;
      float follow_scale1 = 0;
      float follow_speed2 = 0;
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
      =welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("A particle emitter (Cata+, 492 wire bytes): multi-textured — packed "
                   "texture ids, multiTexScale, trailing scroll parameters.")
    ]] M2Particle<V>
    {
      [[=welder::doc("Always -1 in known files.")]]
      std::uint32_t particle_id = 0xFFFFFFFF;
      [[=welder::doc("See wowdev's particle flag table.")]]
      std::uint32_t flags = 0;
      [[=welder::doc("Relative to the bone.")]]
      C3Vector position{};
      std::uint16_t bone_id = 0;
      [[=welder::doc("3x5-bit texture ids under the MultiTexture flag.")]]
      std::uint16_t texture_id = 0;
      [[=welder::doc("Spawns model particles when set.")]]
      std::string geometry_model_filename;
      [[=welder::doc("Child emitters come from this model.")]]
      std::string recursion_model_filename;
      std::uint8_t blending_type = 0;
      [[=welder::doc("1 plane, 2 sphere, 3 spline, 4 bone.")]]
      std::uint8_t emitter_type = 0;
      [[=welder::doc("ParticleColor.dbc row selector (0/11/12/13).")]]
      std::uint16_t particle_color_index = 0;
      [[=welder::doc("2.5 fixed-point per extra layer.")]]
      std::array<std::int8_t, 2> multi_tex_scale{};
      std::int16_t priority_plane = 0;
      [[=welder::doc("Flipbook tiling.")]]
      std::uint16_t rows = 0;
      std::uint16_t columns = 0;
      record::M2Track<float, V> emission_speed{};
      record::M2Track<float, V> speed_variation{};
      record::M2Track<float, V> vertical_range{};
      record::M2Track<float, V> horizontal_range{};
      record::M2Track<float, V> gravity{};
      record::M2Track<float, V> lifespan{};
      [[=welder::doc("+ lifespan_variation * random(-1, 1).")]]
      float lifespan_variation = 0;
      record::M2Track<float, V> emission_rate{};
      float emission_rate_variation = 0;
      record::M2Track<float, V> emission_area_width{};
      record::M2Track<float, V> emission_area_length{};
      record::M2Track<float, V> z_source{};
      [[=welder::doc("Usually 3 keys: start/middle/end.")]]
      FBlock<C3Vector> color_track{};
      FBlock<fixed16> alpha_track{};
      FBlock<C2Vector> scale_track{};
      [[=welder::doc("Random per-particle scale variation.")]]
      C2Vector scale_vary{};
      FBlock<std::uint16_t> head_uv_anim{};
      FBlock<std::uint16_t> tail_uv_anim{};
      float tail_length = 0;
      float twinkle_speed = 0;
      float twinkle_percent = 0;
      CRange twinkle_scale{};
      float inherit_velocity_scale = 0;
      float drag = 0;
      [[=welder::doc("Initial quad rotation.")]]
      float base_spin = 0;
      float base_spin_variation = 0;
      [[=welder::doc("Quad rotation per second.")]]
      float spin_speed = 0;
      float spin_speed_variation = 0;
      [[=welder::doc("Angular velocity bounds (model particles).")]]
      M2Box tumble{};
      [[=welder::doc("Static wind, unless DynamicWind flag.")]]
      C3Vector wind_vector{};
      float wind_time = 0;
      float follow_speed1 = 0;
      float follow_scale1 = 0;
      float follow_speed2 = 0;
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
