#pragma once

/** @file
    M2 effect-emitter records (namespace wowlib::formats::m2::records):
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
#include <wowlib/formats/m2/records/track.hpp>

namespace wowlib::formats::m2::records
{
  /** A ribbon (trail) emitter. The priority/color-index tail arrived with
      WotLK — the members simply do not exist on earlier versions' wire. */
  template <ClientVersion V>
  struct M2Ribbon
  {
    std::uint32_t ribbon_id = 0xFFFFFFFF; /**< Always -1 in known files. */
    std::uint32_t bone_index = 0;         /**< The bone the ribbon trails from. */
    C3Vector position{};                  /**< Relative to that bone. */
    std::vector<std::uint16_t> texture_indices;  /**< Into the model's textures. */
    std::vector<std::uint16_t> material_indices; /**< Into the model's materials. */
    M2Track<C3Vector, V> color{};
    M2Track<fixed16, V> alpha{};          /**< 0 transparent .. 0x7FFF opaque. */
    M2Track<float, V> height_above{};
    M2Track<float, V> height_below{};
    float edges_per_second = 0;           /**< Quad emission rate. */
    float edge_lifetime = 0;              /**< Seconds a quad stays around. */
    float gravity = 0;                    /**< Sinks/rises the ribbon over time. */
    std::uint16_t texture_rows = 0;       /**< Flipbook tiling. */
    std::uint16_t texture_cols = 0;
    M2Track<std::uint16_t, V> tex_slot{};
    M2Track<std::uint8_t, V> visibility{};

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

  /** One 6.9 fixed-point 2D vector (Cata+ particle multi-texture scroll
      parameters), stored raw. */
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

  template <ClientVersion V>
  struct M2Particle;

  /** Vanilla (v256/257): wide u16 blending/emitter header, static
      color/scale/UV ramps, a single spin value. */
  template <ClientVersion V>
    requires (V < m2_compressed_bones)
  struct M2Particle<V>
  {
    std::uint32_t particle_id = 0xFFFFFFFF; /**< Always -1 in known files. */
    std::uint32_t flags = 0;                /**< See wowdev's particle flag table. */
    C3Vector position{};                    /**< Relative to the bone. */
    std::uint16_t bone_id = 0;
    std::uint16_t texture_id = 0;
    std::string geometry_model_filename;    /**< Spawns model particles when set. */
    std::string recursion_model_filename;   /**< Child emitters come from this model. */
    std::uint16_t blending_type = 0;
    std::uint16_t emitter_type = 0;         /**< 1 plane, 2 sphere, 3 spline, 4 bone. */
    std::uint8_t particle_type = 0;
    std::uint8_t head_or_tail = 0;          /**< 0 head, 1 tail, 2 both. */
    std::int16_t priority_plane = 0;
    std::uint16_t rows = 0;                 /**< Flipbook tiling. */
    std::uint16_t columns = 0;
    M2Track<float, V> emission_speed{};
    M2Track<float, V> speed_variation{};
    M2Track<float, V> vertical_range{};
    M2Track<float, V> horizontal_range{};
    M2Track<float, V> gravity{};
    M2Track<float, V> lifespan{};
    M2Track<float, V> emission_rate{};
    M2Track<float, V> emission_area_width{};
    M2Track<float, V> emission_area_length{};
    M2Track<float, V> z_source{};
    float mid_point = 0;                    /**< Parametric middle of the lifespan. */
    std::array<CImVector, 3> color_values{};      /**< Start/middle/end BGRA multiply. */
    std::array<float, 3> scale_values{};          /**< Start/middle/end scale. */
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
    float spin = 0;                         /**< 1.0 = one full turn over the lifetime. */
    M2Box tumble{};                         /**< Angular velocity bounds (model particles). */
    C3Vector wind_vector{};                 /**< Static wind, unless DynamicWind flag. */
    float wind_time = 0;
    float follow_speed1 = 0;
    float follow_scale1 = 0;
    float follow_speed2 = 0;
    float follow_scale2 = 0;
    std::vector<C3Vector> spline_points;    /**< Spline emitter path. */
    M2Track<std::uint8_t, V> enabled_in{};  /**< Bool track: emitter active. */

    bool operator==(const M2Particle&) const = default;
  };

  /** TBC (v262/263): the blending/emitter pair packs into bytes beside the
      new ParticleColor.dbc index; ramps still static, tracks still on the
      global timeline. */
  template <ClientVersion V>
    requires (V >= m2_compressed_bones && V < m2_per_sequence_timelines)
  struct M2Particle<V>
  {
    std::uint32_t particle_id = 0xFFFFFFFF; /**< Always -1 in known files. */
    std::uint32_t flags = 0;                /**< See wowdev's particle flag table. */
    C3Vector position{};                    /**< Relative to the bone. */
    std::uint16_t bone_id = 0;
    std::uint16_t texture_id = 0;
    std::string geometry_model_filename;    /**< Spawns model particles when set. */
    std::string recursion_model_filename;   /**< Child emitters come from this model. */
    std::uint8_t blending_type = 0;
    std::uint8_t emitter_type = 0;          /**< 1 plane, 2 sphere, 3 spline, 4 bone. */
    std::uint16_t particle_color_index = 0; /**< ParticleColor.dbc row selector (0/11/12/13). */
    std::uint8_t particle_type = 0;
    std::uint8_t head_or_tail = 0;          /**< 0 head, 1 tail, 2 both. */
    std::int16_t priority_plane = 0;
    std::uint16_t rows = 0;                 /**< Flipbook tiling. */
    std::uint16_t columns = 0;
    M2Track<float, V> emission_speed{};
    M2Track<float, V> speed_variation{};
    M2Track<float, V> vertical_range{};
    M2Track<float, V> horizontal_range{};
    M2Track<float, V> gravity{};
    M2Track<float, V> lifespan{};
    M2Track<float, V> emission_rate{};
    M2Track<float, V> emission_area_width{};
    M2Track<float, V> emission_area_length{};
    M2Track<float, V> z_source{};
    float mid_point = 0;                    /**< Parametric middle of the lifespan. */
    std::array<CImVector, 3> color_values{};      /**< Start/middle/end BGRA multiply. */
    std::array<float, 3> scale_values{};          /**< Start/middle/end scale. */
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
    float spin = 0;                         /**< 1.0 = one full turn over the lifetime. */
    M2Box tumble{};                         /**< Angular velocity bounds (model particles). */
    C3Vector wind_vector{};                 /**< Static wind, unless DynamicWind flag. */
    float wind_time = 0;
    float follow_speed1 = 0;
    float follow_scale1 = 0;
    float follow_speed2 = 0;
    float follow_scale2 = 0;
    std::vector<C3Vector> spline_points;    /**< Spline emitter path. */
    M2Track<std::uint8_t, V> enabled_in{};  /**< Bool track: emitter active. */

    bool operator==(const M2Particle&) const = default;
  };

  /** WotLK through MoP/WoD pre-multitex (v264..271 layouts; our grid: wotlk
      only): FBlock color/alpha/scale/UV ramps, lifespan/emission variation,
      four spin fields. 476 wire bytes. */
  template <ClientVersion V>
    requires (V >= m2_per_sequence_timelines && V < m2_multitex_particles)
  struct M2Particle<V>
  {
    std::uint32_t particle_id = 0xFFFFFFFF; /**< Always -1 in known files. */
    std::uint32_t flags = 0;                /**< See wowdev's particle flag table. */
    C3Vector position{};                    /**< Relative to the bone. */
    std::uint16_t bone_id = 0;
    std::uint16_t texture_id = 0;
    std::string geometry_model_filename;    /**< Spawns model particles when set. */
    std::string recursion_model_filename;   /**< Child emitters come from this model. */
    std::uint8_t blending_type = 0;
    std::uint8_t emitter_type = 0;          /**< 1 plane, 2 sphere, 3 spline, 4 bone. */
    std::uint16_t particle_color_index = 0; /**< ParticleColor.dbc row selector (0/11/12/13). */
    std::uint8_t particle_type = 0;
    std::uint8_t head_or_tail = 0;          /**< 0 head, 1 tail, 2 both. */
    std::int16_t priority_plane = 0;
    std::uint16_t rows = 0;                 /**< Flipbook tiling. */
    std::uint16_t columns = 0;
    M2Track<float, V> emission_speed{};
    M2Track<float, V> speed_variation{};
    M2Track<float, V> vertical_range{};
    M2Track<float, V> horizontal_range{};
    M2Track<float, V> gravity{};
    M2Track<float, V> lifespan{};
    float lifespan_variation = 0;           /**< + lifespan_variation * random(-1, 1). */
    M2Track<float, V> emission_rate{};
    float emission_rate_variation = 0;
    M2Track<float, V> emission_area_width{};
    M2Track<float, V> emission_area_length{};
    M2Track<float, V> z_source{};
    FBlock<C3Vector> color_track{};         /**< Usually 3 keys: start/middle/end. */
    FBlock<fixed16> alpha_track{};
    FBlock<C2Vector> scale_track{};
    C2Vector scale_vary{};                  /**< Random per-particle scale variation. */
    FBlock<std::uint16_t> head_uv_anim{};
    FBlock<std::uint16_t> tail_uv_anim{};
    float tail_length = 0;
    float twinkle_speed = 0;
    float twinkle_percent = 0;
    CRange twinkle_scale{};
    float inherit_velocity_scale = 0;
    float drag = 0;
    float base_spin = 0;                    /**< Initial quad rotation. */
    float base_spin_variation = 0;
    float spin_speed = 0;                   /**< Quad rotation per second. */
    float spin_speed_variation = 0;
    M2Box tumble{};                         /**< Angular velocity bounds (model particles). */
    C3Vector wind_vector{};                 /**< Static wind, unless DynamicWind flag. */
    float wind_time = 0;
    float follow_speed1 = 0;
    float follow_scale1 = 0;
    float follow_speed2 = 0;
    float follow_scale2 = 0;
    std::vector<C3Vector> spline_points;    /**< Spline emitter path. */
    M2Track<std::uint8_t, V> enabled_in{};  /**< Bool track: emitter active. */

    bool operator==(const M2Particle&) const = default;
  };

  /** Cata+ (v272+): multi-texture particles — the texture id becomes a
      3x5-bit selector under flag 0x10000000, multiTexScale replaces
      particleType/headOrTail, and the scroll parameters trail the record.
      492 wire bytes. */
  template <ClientVersion V>
    requires (V >= m2_multitex_particles)
  struct M2Particle<V>
  {
    std::uint32_t particle_id = 0xFFFFFFFF; /**< Always -1 in known files. */
    std::uint32_t flags = 0;                /**< See wowdev's particle flag table. */
    C3Vector position{};                    /**< Relative to the bone. */
    std::uint16_t bone_id = 0;
    std::uint16_t texture_id = 0;           /**< 3x5-bit texture ids under the MultiTexture flag. */
    std::string geometry_model_filename;    /**< Spawns model particles when set. */
    std::string recursion_model_filename;   /**< Child emitters come from this model. */
    std::uint8_t blending_type = 0;
    std::uint8_t emitter_type = 0;          /**< 1 plane, 2 sphere, 3 spline, 4 bone. */
    std::uint16_t particle_color_index = 0; /**< ParticleColor.dbc row selector (0/11/12/13). */
    std::array<std::int8_t, 2> multi_tex_scale{}; /**< 2.5 fixed-point per extra layer. */
    std::int16_t priority_plane = 0;
    std::uint16_t rows = 0;                 /**< Flipbook tiling. */
    std::uint16_t columns = 0;
    M2Track<float, V> emission_speed{};
    M2Track<float, V> speed_variation{};
    M2Track<float, V> vertical_range{};
    M2Track<float, V> horizontal_range{};
    M2Track<float, V> gravity{};
    M2Track<float, V> lifespan{};
    float lifespan_variation = 0;           /**< + lifespan_variation * random(-1, 1). */
    M2Track<float, V> emission_rate{};
    float emission_rate_variation = 0;
    M2Track<float, V> emission_area_width{};
    M2Track<float, V> emission_area_length{};
    M2Track<float, V> z_source{};
    FBlock<C3Vector> color_track{};         /**< Usually 3 keys: start/middle/end. */
    FBlock<fixed16> alpha_track{};
    FBlock<C2Vector> scale_track{};
    C2Vector scale_vary{};                  /**< Random per-particle scale variation. */
    FBlock<std::uint16_t> head_uv_anim{};
    FBlock<std::uint16_t> tail_uv_anim{};
    float tail_length = 0;
    float twinkle_speed = 0;
    float twinkle_percent = 0;
    CRange twinkle_scale{};
    float inherit_velocity_scale = 0;
    float drag = 0;
    float base_spin = 0;                    /**< Initial quad rotation. */
    float base_spin_variation = 0;
    float spin_speed = 0;                   /**< Quad rotation per second. */
    float spin_speed_variation = 0;
    M2Box tumble{};                         /**< Angular velocity bounds (model particles). */
    C3Vector wind_vector{};                 /**< Static wind, unless DynamicWind flag. */
    float wind_time = 0;
    float follow_speed1 = 0;
    float follow_scale1 = 0;
    float follow_speed2 = 0;
    float follow_scale2 = 0;
    std::vector<C3Vector> spline_points;    /**< Spline emitter path. */
    M2Track<std::uint8_t, V> enabled_in{};  /**< Bool track: emitter active. */
    std::array<M2Vec2FP69, 2> multi_tex_scroll_mid{};   /**< Per extra layer. */
    std::array<M2Vec2FP69, 2> multi_tex_scroll_range{}; /**< Per extra layer. */

    bool operator==(const M2Particle&) const = default;
  };
}
