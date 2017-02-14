#include "crosshair_component.h"
#include "game/transcendental/entity_id.h"
#include "transform_component.h"
#include "game/transcendental/entity_handle.h"
#include "game/transcendental/cosmos.h"

namespace components {
	vec2 crosshair::calculate_aiming_displacement(const_entity_handle subject_crosshair, bool snap_epsilon_base_offset) {
		auto recoil_body = subject_crosshair[sub_entity_name::CROSSHAIR_RECOIL_BODY];

		auto considered_base_offset = subject_crosshair.get<components::crosshair>().base_offset;

		if (snap_epsilon_base_offset && considered_base_offset.is_epsilon(4))
			considered_base_offset.set(4, 0);

		considered_base_offset.rotate(recoil_body.get_logic_transform().rotation, vec2());

		return considered_base_offset;
	}
}