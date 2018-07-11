#pragma once
#include "augs/drawing/drawing.h"
#include "augs/drawing/drawing_input_base.h"
#include "augs/drawing/make_sprite.h"
#include "augs/texture_atlas/atlas_entry.h"
#include "augs/build_settings/platform_defines.h"
#include "augs/drawing/sprite.h"

namespace augs {
	template <class I, class F>
	FORCE_INLINE void for_each_tile(
		const sprite<I>& spr, 
		const vec2 pos,
		F callback, 
		const real32 final_rotation, 
		const augs::atlas_entry& diffuse,
		const camera_cone& cone
	) {
		const auto total_size = spr.get_size();
		const auto tile_size = diffuse.get_original_size();
		const auto times = total_size / tile_size;

		const auto vis = [&]() {
			/* Rotate the tiled sprite so that it becomes axis aligned */
			const auto rotated_pos = vec2(pos).rotate(-final_rotation, cone.eye.transform.pos);
			auto rotated_camera = cone;

			/* Rotate the camera to the same space */
			rotated_camera.eye.transform.rotation -= final_rotation;

			const auto c = ltrbi(rotated_camera.get_visible_world_rect_aabb());
			const auto s = ltrbi(vec2i(rotated_pos) - total_size / 2, total_size);

			/* We start from assumption that the sprite is completely within the camera */
			auto v = ltrbi(vec2i(0, 0), times);

			/* But, right edge of the object might exceed right bound of camera */
			{
				const auto diff = s.r - c.r;

				if (diff > 0) {
					/* If the difference exceeds the tile size, the result of integer division becomes bigger than 0, in which case we already take 1 from times.x */
					v.r -= diff / tile_size.x;
				}
			}

			v.b -= std::max(0, s.b - c.b) / tile_size.y;

			/* But, left edge of the camera might exceed left bound of object */
			{
				const auto diff = c.l - s.l;

				if (diff > 0) {
					/* If the difference exceeds the tile size, the result of integer division becomes bigger than 0, in which case we already put 1 as the beginning point. */
					v.l = diff / tile_size.x;
				}
			}

			v.t = std::max(0, c.t - s.t) / tile_size.y;

			return v;
		}();

		const auto lt_center = -vec2(tile_size * (times - vec2i(1, 1))) / 2;

		for (int y = vis.t; y < vis.b; ++y) {
			for (int x = vis.l; x < vis.r; ++x) {
				auto piece_offset = lt_center + vec2(tile_size * vec2i(x, y));
				piece_offset.rotate(final_rotation);

				callback(pos + piece_offset);
			}
		}
	}

	template <class id_type>
	FORCE_INLINE void detail_draw(
		const sprite<id_type>& spr,
		const sprite_drawing_input in,
		const atlas_entry considered_texture,
		const vec2 target_position,
		float target_rotation,
		const sprite_size_type considered_size,
		rgba target_color
	) {
		if (spr.effect == sprite_special_effect::CONTINUOUS_ROTATION) {
			target_rotation += std::fmod(in.global_time_seconds * spr.effect_speed_multiplier * 360.f, 360.f);
		}

		if (in.colorize != white) {
			target_color *= in.colorize;
		}

		const auto points = make_sprite_points(target_position, considered_size, target_rotation);

		auto triangles = make_sprite_triangles(
			considered_texture,
			points,
			target_color, 
			in.flip 
		);

		if (spr.effect == sprite_special_effect::COLOR_WAVE) {
			auto left_col = rgba(hsv{ std::fmod(in.global_time_seconds * spr.effect_speed_multiplier / 2.f, 1.f), 1.0, 1.0 });
			auto right_col = rgba(hsv{ std::fmod(in.global_time_seconds * spr.effect_speed_multiplier / 2.f / 2.f + 0.3f, 1.f), 1.0, 1.0 });

			left_col.avoid_dark_blue_for_color_wave();
			right_col.avoid_dark_blue_for_color_wave();

			left_col.a = target_color.a;
			right_col.a = target_color.a;

			auto& t1 = triangles[0];
			auto& t2 = triangles[1];

			t1.vertices[0].color = t2.vertices[0].color = left_col;
			t2.vertices[1].color = right_col;
			t1.vertices[1].color = t2.vertices[2].color = right_col;
			t1.vertices[2].color = left_col;
		}

		in.output.push(triangles[0]);
		in.output.push(triangles[1]);
	}

	template <class id_type, class M>
	FORCE_INLINE void draw(
		const sprite<id_type>& spr,
		const M& manager,
		const sprite_drawing_input in
	) {
		static_assert(
			!has_member_find_v<M, id_type>,
			"Here we assume it is always found, or a harmless default returned."
		);

		const auto pos = in.renderable_transform.pos;
		const auto final_rotation = in.renderable_transform.rotation; //+ rotation_offset;
		const auto drawn_size = spr.get_size();

		const auto& entry = manager.at(spr.image_id);
		const auto& diffuse = entry.diffuse;

		const auto original_size = diffuse.get_original_size();

		if (in.use_neon_map) {
			const auto& maybe_neon_map = entry.neon_map;

			if (maybe_neon_map.exists()) {
				const auto original_neon_size = vec2(maybe_neon_map.get_original_size());

				if (spr.tile_excess_size && drawn_size > original_size) {
					for_each_tile(
						spr,
						pos,
						[&](const auto piece_pos) {
							detail_draw(
								spr,
								in,
								maybe_neon_map,
								piece_pos,
								final_rotation,
								original_neon_size,
								spr.neon_color
							);
						},
						final_rotation,
						diffuse,
						in.cone
					);
				}
				else {
					const auto neon_size_mult = vec2(drawn_size) / original_size;

					detail_draw(
						spr,
						in,
						maybe_neon_map,
						pos,
						final_rotation,
						original_neon_size * neon_size_mult,
						spr.neon_color
					);
				}
			}
		}
		else {
			if (spr.tile_excess_size && drawn_size > original_size) {
				for_each_tile(
					spr,
					pos,
					[&](const auto piece_pos ) {
						detail_draw(
							spr,
							in,
							diffuse,
							piece_pos,
							final_rotation,
							original_size,
							spr.color
						);
					},
					final_rotation,
					diffuse,
					in.cone
				);
			}
			else {
				detail_draw(
					spr,
					in,
					diffuse,
					pos,
					final_rotation,
					drawn_size,
					spr.color
				);
			}
		}
	}
}
