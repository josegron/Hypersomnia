#pragma once
#include "game/detail/state_for_drawing_camera.h"
#include "game/transcendental/entity_id.h"
class viewing_step;

struct aabb_highlighter {
	float timer = 0.f;
	float cycle_duration_ms = 400.f;

	float base_gap = 2.f;
	float smallest_length = 8.f;
	float biggest_length = 16.f;
	float scale_down_when_aabb_no_bigger_than = 40.f;

	void update(const float delta_ms);
	void draw(viewing_step&, const const_entity_handle& subject) const;
};