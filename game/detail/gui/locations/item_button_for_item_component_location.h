#pragma once
#include "game/transcendental/entity_id.h"

class item_button_for_item_component_location {
public:
	entity_id item_id;

	bool operator==(const item_button_for_item_component_location& b) const {
		return item_id == b.item_id;
	}

	template <class C>
	bool alive(C context) const {
		return context.get_step().get_cosmos()[item_id].alive();
	}

	template <class C, class L>
	decltype(auto) get_object_at_location_and_call(C context, L generic_call) const {
		return generic_call(context.get_step().get_cosmos()[item_id].get<components::item>().button);
	}
};

namespace std {
	template <>
	struct hash<item_button_for_item_component_location> {
		std::size_t operator()(const item_button_for_item_component_location& k) const {
			return std::hash<entity_id>()(k.item_id);
		}
	};
}