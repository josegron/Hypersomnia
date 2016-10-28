#pragma once
#include "game/detail/inventory_slot_id.h"

class slot_button_for_inventory_slot_location {
public:
	inventory_slot_id slot_id;

	bool operator==(const slot_button_for_inventory_slot_location& b) const {
		return slot_id == b.slot_id;
	}

	template <class C>
	bool alive(C context) const {
		return context.get_step().get_cosmos()[slot_id].alive();
	}

	template <class C, class L>
	decltype(auto) get_object_at_location_and_call(C context, L generic_call) const {
		auto& cosm = context.get_step().get_cosmos();
		//typename decltype(cosm.get_handle(slot_id))::slot_button_ref bb = cosm[slot_id].get_button();
		return generic_call(cosm[slot_id].get().button);
	}
};

namespace std {
	template <>
	struct hash<slot_button_for_inventory_slot_location> {
		std::size_t operator()(const slot_button_for_inventory_slot_location& k) const {
			return std::hash<inventory_slot_id>()(k.slot_id);
		}
	};
}