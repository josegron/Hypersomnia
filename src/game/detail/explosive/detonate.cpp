#include "game/components/explosive_component.h"
#include "game/detail/explosive/detonate.h"
#include "game/cosmos/logic_step.h"
#include "game/messages/queue_deletion.h"
#include "game/cosmos/data_living_one_step.h"
#include "game/cosmos/cosmos.h"
#include "game/cosmos/entity_handle.h"
#include "game/cosmos/create_entity.hpp"

template <class T, std::size_t I, class F>
auto vectorize_array(const std::array<T, I>& arr, F&& predicate) {
	std::vector<T> output;
	output.reserve(I);

	for (const auto& f : arr) {
		if (predicate(f)) { 
			output.emplace_back(f);
		}
	}

	return output;
}

void detonate(const detonate_input in) {
	const auto& e = in.explosive;
	const auto& step = in.step;
	auto& cosm = step.get_cosmos();

	e.explosion.instantiate_no_subject(step, in.location);
	step.post_message(messages::queue_deletion(in.subject));

	const auto cascade_inputs = vectorize_array(e.cascade, [](const auto& f) { return f.flavour_id.is_set(); });

	for (const auto& c_in : cascade_inputs) {
		const auto n = c_in.num_spawned;
		auto rng = cosm.get_rng_for(in.subject);

		const auto angle_dt = c_in.spawn_spread / n;

		const auto t = in.location;
		const auto left = t.rotation - c_in.spawn_spread / 2;

		for (unsigned i = 0; i < n; ++i) {
			const auto target_speed = rng.randval(c_in.initial_speed);
			const auto target_angle_offset = [&]() {
				const auto variation = angle_dt * c_in.spawn_angle_variation;
				const auto h = variation / 2;
				return rng.randval_h(h);
			}();

			const auto vel_angle = target_angle_offset + (n == 1 ? t.rotation : left + angle_dt * i);

			cosmic::create_entity(
				cosm,
				c_in.flavour_id,
				[&](const auto typed_handle) {
					{
						const auto target_transform = transformr(t.pos, vel_angle);
						typed_handle.set_logic_transform(target_transform);
					}

					{
						const auto& body = typed_handle.template get<components::rigid_body>();
						body.set_velocity(vec2::from_degrees(vel_angle) * target_speed);
					}

					{
						const auto& cascade_def = typed_handle.template get<invariants::cascade_explosion>();

						auto& cascade = typed_handle.template get<components::cascade_explosion>();

						cascade.explosions_left = rng.randval(c_in.num_explosions);
						
						const auto next_explosion_in_ms = rng.randval(0.f, cascade_def.explosion_interval_ms.value);

						LOG_NVPS(next_explosion_in_ms);
						const auto now = cosm.get_timestamp();
						const auto dt = cosm.get_fixed_delta();

						cascade.when_next_explosion = now;
						cascade.when_next_explosion.step += next_explosion_in_ms / dt.in_milliseconds();
					}
				},
				[&](auto&) {}
			);
		}
	}
}

void detonate_if(const entity_id& id, const transformr& where, const logic_step& step) {
	step.get_cosmos()[id].dispatch_on_having_all<invariants::explosive>([&](const auto typed_handle) {
		detonate({
			step, id, typed_handle.template get<invariants::explosive>(), where
		});
	});
}
