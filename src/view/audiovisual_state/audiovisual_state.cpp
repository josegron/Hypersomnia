#include "augs/string/string_templates.h"
#include "augs/drawing/drawing_input_base.h"
#include "augs/drawing/sprite.h"
#include "game/components/interpolation_component.h"
#include "game/components/flags_component.h"
#include "game/components/fixtures_component.h"
#include "game/cosmos/cosmos.h"
#include "game/cosmos/logic_step.h"
#include "game/cosmos/data_living_one_step.h"
#include "game/detail/visible_entities.h"
#include "game/cosmos/cosmos.h"

#include "game/messages/health_event.h"
#include "game/messages/performed_transfer_message.h"
#include "game/messages/interpolation_correction_request.h"
#include "game/messages/thunder_effect.h"
#include "game/messages/exploding_ring_effect.h"

#include "view/audiovisual_state/audiovisual_state.h"
#include "view/audiovisual_state/systems/exploding_ring_system.hpp"
#include "view/audiovisual_state/systems/interpolation_system.h"
#include "view/audiovisual_state/special_effects_settings.h"

#include "view/character_camera.h"
#include "augs/templates/thread_pool.h"
#include "view/viewables/images_in_atlas_map.h"
#include "augs/graphics/dedicated_buffers.h"
#include "view/rendering_scripts/draw_wandering_pixels_as_sprites.h"
#include "view/audiovisual_state/systems/wandering_pixels_system.hpp"
#include "augs/audio/audio_command_buffers.h"
#include "game/detail/visible_entities.hpp"
#include "game/detail/sentience/sentience_getters.h"
#include "view/damage_indication_settings.h"

template <class E>
void interpolation_system::set_updated_interpolated_transform(
	const E& subject,
	const transformr updated_value
) {
	auto& info = get_corresponding<components::interpolation>(subject);
	info.interpolated_transform = updated_value;
}

void audiovisual_state::clear() {
	systems.for_each([](auto& sys) {
		sys.clear();
	});
}

void audiovisual_state::reserve_caches_for_entities(const std::size_t n) {
	systems.for_each([n](auto& sys) {
		using T = remove_cref<decltype(sys)>;

		if constexpr(can_reserve_caches_v<T>) {
			sys.reserve_caches_for_entities(n);
		}
	});
}

void audiovisual_state::advance(const audiovisual_advance_input input) {
	using D = augs::dedicated_buffer;

	auto scope = measure_scope(performance.advance);

	const auto viewed_character = input.camera.viewed_character;
	const auto input_camera = input.camera;
	const auto queried_cone = input.queried_cone;
	const auto& cosm = viewed_character.get_cosmos();
	
	const auto frame_dt = input.frame_delta;

	const auto state_dt = input.new_state_delta;
	auto scaled_state_dt = state_dt;

	auto dt = frame_dt;
	dt *= input.speed_multiplier;
	
	if (scaled_state_dt) {
		*scaled_state_dt *= input.speed_multiplier;
	}

	const auto& anims = input.plain_animations;
	const auto audio_renderer = input.audio_renderer;

	auto& rng = get_rng();

	auto& sounds = get<sound_system>();
	auto& thunders = get<thunder_system>();
	auto& exploding_rings = get<exploding_ring_system>();
	auto& damage_indication = get<damage_indication_system>();
	auto& highlights = get<pure_color_highlight_system>();
	auto& interp = get<interpolation_system>();
	auto& particles = get<particles_simulation_system>();
	auto& wandering_pixels = get<wandering_pixels_system>();
	auto& all_visible = input.all_visible;
	auto& game_images = input.game_images;

	interp.id_to_integerize = viewed_character;

	auto advance_exploding_rings = [&]() {
		auto cone_for_explosion_particles = queried_cone;
		cone_for_explosion_particles.eye.zoom *= 0.9f;

		exploding_rings.advance(
			rng, 
			cone_for_explosion_particles, 
			cosm.get_common_assets(), 
			input.particle_effects, 
			dt, 
			input.performance.special_effects.explosions,
			particles
		);
	};

	auto advance_attenuation_variations = [&]() {
		get<light_system>().advance_attenuation_variations(rng, cosm, dt);
	};

	auto advance_world_hover_highlighter = [&]() {
		world_hover_highlighter.cycle_duration_ms = 400;
		world_hover_highlighter.update(input.frame_delta);
	};

	auto advance_thunders = [&]() {
		thunders.advance(rng, cosm, queried_cone, input.particle_effects, dt, particles);
	};

	auto advance_damage_indication = [&]() {
		damage_indication.advance(input.damage_indication, dt);
	};

	auto advance_highlights = [&]() {
		highlights.advance(dt);
	};

	auto advance_visible_particle_streams = [&]() {
		auto scope = measure_scope(performance.advance_particle_streams);

		particles.advance_visible_streams(
			rng,
			queried_cone,
			input.performance.special_effects,
			cosm,
			input.particle_effects,
			anims,
			dt,
			interp
		);
	};

	auto synchronous_facade = [&]() {
		advance_visible_particle_streams();
		advance_world_hover_highlighter();
		advance_highlights();
		advance_attenuation_variations();

		advance_thunders();
		advance_exploding_rings();

		particles.remove_dead_particles(cosm);
		particles.preallocate_particle_buffers(input.particles_output);

		advance_damage_indication();
	};

	auto launch_particle_jobs = [&]() {
		auto scope = measure_scope(performance.integrate_particles);

		particles.integrate_and_draw_all_particles({
			cosm,
			dt,
			interp,
			input.game_images,
			anims,
			input.performance.max_particles_in_single_job,
			input.particles_output,
			input.pool
		});

		performance.num_particles.measure(particles.count_all_particles());
	};

	auto launch_wandering_pixels_jobs = [&]() {
		auto& dedicated = input.dedicated;

		{
			const auto total = [&]() {
				int total = 0;

				all_visible.for_each<render_layer::ILLUMINATING_WANDERING_PIXELS>(cosm, [&total](const auto& e) {
					total += e.template get<components::wandering_pixels>().particles_count;
				});

				return total;
			}();

			auto& triangles = dedicated[D::ILLUMINATING_WANDERING_PIXELS].triangles;
			triangles.resize(total * 2);

			int current_index = 0;

			all_visible.for_each<render_layer::ILLUMINATING_WANDERING_PIXELS>(cosm, [&](const auto& e) {
				e.template dispatch_on_having_all<invariants::wandering_pixels>(
					[&](const auto& typed_wandering_pixels) {
						auto job = [&triangles, current_index, &wandering_pixels, &game_images, typed_wandering_pixels, dt]() {
							wandering_pixels.advance_for(typed_wandering_pixels, dt);
							draw_wandering_pixels_as_sprites(triangles, current_index, wandering_pixels, typed_wandering_pixels, game_images);
						};

						const auto current_count = typed_wandering_pixels.template get<components::wandering_pixels>().particles_count;;
						current_index += current_count;

						input.pool.enqueue(job);
					}
				);
			});
		}

		{
			const auto total = [&]() {
				int total = 0;

				all_visible.for_each<render_layer::DIM_WANDERING_PIXELS>(cosm, [&total](const auto& e) {
					total += e.template get<components::wandering_pixels>().particles_count;
				});

				return total;
			}();

			auto& triangles = dedicated[D::DIM_WANDERING_PIXELS].triangles;
			triangles.resize(total * 2);

			int current_index = 0;

			all_visible.for_each<render_layer::DIM_WANDERING_PIXELS>(cosm, [&](const auto& e) {
				e.template dispatch_on_having_all<invariants::wandering_pixels>(
					[&](const auto& typed_wandering_pixels) {
						auto job = [&triangles, current_index, &wandering_pixels, &game_images, typed_wandering_pixels, dt]() {
							wandering_pixels.advance_for(typed_wandering_pixels, dt);
							draw_wandering_pixels_as_sprites(triangles, current_index, wandering_pixels, typed_wandering_pixels, game_images);
						};

						const auto current_count = typed_wandering_pixels.template get<components::wandering_pixels>().particles_count;;
						current_index += current_count;

						input.pool.enqueue(job);
					}
				);
			});
		}
	};

	const auto& sound_freq = input.sound_settings.processing_frequency;
	const bool sound_every_step = sound_freq == sound_processing_frequency::EVERY_SIMULATION_STEP;

	auto update_sound_properties = [audio_renderer, sound_every_step, scaled_state_dt, viewed_character, dt, input, &sounds, &interp, input_camera]() {
		if (viewed_character.dead()) {
			return;
		}

		auto ear = input_camera;
		ear.cone.eye.transform = viewed_character.get_viewing_transform(interp);
		
		sounds.update_sound_properties(
			{
				*audio_renderer,
				sounds,
				input.audio_volume,
				input.sound_settings,
				input.sounds,
				interp,
				ear,
				input_camera.cone,
				sound_every_step ? *scaled_state_dt : dt,
				input.speed_multiplier,
				input.inv_tickrate
			}
		);
	};

	auto fade_sound_sources = [audio_renderer, sound_every_step, &sounds, frame_dt, state_dt]() {
		sounds.fade_sources(*audio_renderer, sound_every_step ? *state_dt : frame_dt);
	};

	auto& command_buffers = input.command_buffers;

	auto audio_job = [this, &command_buffers, update_sound_properties, fade_sound_sources]() {
		auto scope = measure_scope(performance.sound_logic);

		update_sound_properties();
		fade_sound_sources();

		command_buffers.submit_write_buffer();
	};

	synchronous_facade();

	launch_particle_jobs();
	launch_wandering_pixels_jobs();

	const bool should_update_audio = [&]() {
		if (audio_renderer == nullptr) {
			return false;
		}

		if (sound_freq == sound_processing_frequency::EVERY_SINGLE_FRAME) {
			return true;
		}

		if (sound_every_step) {
			return input.new_state_delta != std::nullopt;
		}

		return false;
	}();

	if (should_update_audio) {
		input.pool.enqueue(audio_job);
	}
}

void audiovisual_state::spread_past_infection(const const_logic_step step) {
	const auto& cosm = step.get_cosmos();

	const auto& events = step.get_queue<messages::collision_message>();

	for (const auto& it : events) {
		const const_entity_handle subject_owner_body = cosm[it.subject].get_owner_of_colliders();
		const const_entity_handle collider_owner_body = cosm[it.collider].get_owner_of_colliders();

		auto& past_system = get<past_infection_system>();

		if (past_system.is_infected(subject_owner_body) && !collider_owner_body.get_flag(entity_flag::IS_IMMUNE_TO_PAST)) {
			past_system.infect(collider_owner_body);
		}
	}
}

void audiovisual_state::standard_post_solve(
	const const_logic_step step, 
	const audiovisual_post_solve_input input
) {
	auto scope = measure_scope(performance.post_solve);

	const auto& cosm = step.get_cosmos();
	//reserve_caches_for_entities(cosm.get_solvable().get_entity_pool().capacity());

	auto& interp = get<interpolation_system>();

	const auto& settings = input.settings;

	const auto correct_interpolations = always_predictable_v;
	const auto acquire_damage_indication = never_predictable_v;
	const auto acquire_highlights = always_predictable_v;

	if (correct_interpolations.should_play(settings.prediction)) {
		const auto& new_interpolation_corrections = step.get_queue<messages::interpolation_correction_request>();

		for (const auto& c : new_interpolation_corrections) {
			const auto from = cosm[c.set_previous_transform_from];
			const auto subject = cosm[c.subject];

			if (subject.dead()) {
				continue;
			}

			subject.dispatch_on_having_all<invariants::interpolation>(
				[&](const auto& typed_subject) {
					if (from) {
						from.dispatch_on_having_all<invariants::interpolation>([&](const auto& typed_from) {
							const auto target_transform = interp.get_interpolated(typed_from);

							interp.set_updated_interpolated_transform(
								typed_subject,
								target_transform
							);
						});
					}
					else {
						const auto target_transform = c.set_previous_transform_value;

						interp.set_updated_interpolated_transform(
							typed_subject,
							target_transform
						);
					}
				}
			);
		}
	}

	{
		auto& rng = get_rng();

		{
			const auto& new_thunders = step.get_queue<messages::thunder_effect>();
			auto& thunders = get<thunder_system>();

			for (const auto& t : new_thunders) {
				const auto thunder_mult = input.performance.special_effects.explosions.thunder_amount;

				auto new_t = t;

				auto& spawned = new_t.payload.max_all_spawned_branches;
				spawned = static_cast<float>(spawned) * thunder_mult;

				if (spawned > 0) {
					thunders.add(rng, new_t);
				}
			}
		}
		
		{
			const auto& new_rings = step.get_queue<messages::exploding_ring_effect>();
			auto& exploding_rings = get<exploding_ring_system>();
			exploding_rings.acquire_new_rings(new_rings);
		}

		{
			auto& particles = get<particles_simulation_system>();
			particles.update_effects_from_messages(rng, step, input.particle_effects, interp, input.performance.special_effects);
		}

		const auto audio_renderer = input.audio_renderer;

		if (audio_renderer) {
			auto& sounds = get<sound_system>();

			auto ear = input.camera;

			if (const auto viewed_character = ear.viewed_character) {
				if (const auto transform = viewed_character.find_viewing_transform(interp)) {
					ear.cone.eye.transform = *transform;
				}
			}

			sounds.update_effects_from_messages(
				step, 
				{
					*audio_renderer,
					sounds,
					input.audio_volume,
					input.sound_settings,
					input.sounds, 
					interp, 
					ear,
					input.camera.cone,
					augs::delta::zero,
					1.0,
					0.0
				}
			);
		}
	}


	const auto& healths = step.get_queue<messages::health_event>();
	
	struct color_info {
		rgba number = white;
		rgba highlight = white;

		color_info(const messages::health_event& h) {
			if (h.target == messages::health_event::target_type::HEALTH) {
				if (h.damage.effective > 0) {
					number = red;
					highlight = white;
				}
				else {
					number = green;
					highlight = green;
				}
			}
			else if (h.target == messages::health_event::target_type::PERSONAL_ELECTRICITY) {
				if (h.damage.effective > 0) {
					number = turquoise;
					highlight = turquoise;
				}
				else {
					number = cyan;
					highlight = cyan;
				}
			}
			else if (h.target == messages::health_event::target_type::CONSCIOUSNESS) {
				if (h.damage.effective > 0) {
					number = orange;
					highlight = orange;
				}
				else {
					number = yellow;
					highlight = yellow;
				}
			}
		}
	};

	if (acquire_damage_indication.should_play(settings.prediction)) {
		auto& damage_indication = get<damage_indication_system>();

		for (const auto& h : healths) {
			if (h.damage.effective < 0.0) {
				// TODO: Support healing events
				continue;
			}

			using damage_event = damage_indication_system::damage_event;

			auto de = damage_event::input();

			de.pos = h.point_of_impact;

			float original_ratio = 0.0f;

			if (h.target == messages::health_event::target_type::HEALTH) {
				de.type = damage_event::event_type::HEALTH;
				de.amount = h.damage.get_total();

				original_ratio = ::get_health_ratio(cosm[h.subject]) + h.damage.ratio_effective_to_maximum;
			}
			else if (h.target == messages::health_event::target_type::PERSONAL_ELECTRICITY) {
				if (h.source_adversity == adverse_element_type::PED) {
					de.type = damage_event::event_type::SHIELD_DRAIN;
				}
				else {
					de.type = damage_event::event_type::SHIELD;
				}

				de.amount = h.damage.effective;

				original_ratio = ::get_shield_ratio(cosm[h.subject]) + h.damage.ratio_effective_to_maximum;
			}
			else {
				continue;
			}

			if (h.headshot) {
				de.critical = true;
			}

			damage_indication.add(h.subject, de);
			damage_indication.add_white_highlight(h.subject, h.target, original_ratio);
		}
	}

	if (acquire_highlights.should_play(settings.prediction)) {
		auto& highlights = get<pure_color_highlight_system>();

		for (const auto& h : healths) {
			if (augs::is_nonzero(h.damage.effective)) {
				const auto cols = color_info(h);

				pure_color_highlight_system::highlight::input new_highlight;

				new_highlight.starting_alpha_ratio = 1.f;// std::min(1.f, h.damage.ratio_effective_to_maximum * 5);
				new_highlight.maximum_duration_seconds = input.damage_indication.character_silhouette_damage_highlight_secs;
				new_highlight.color = cols.highlight;

				highlights.add(h.subject, new_highlight);
			}
		}
	}
}