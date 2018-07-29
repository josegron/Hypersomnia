#include "augs/ensure.h"
#include "augs/drawing/drawing.h"
#include "augs/gui/text/printer.h"
#include "augs/string/string_templates.h"

#include "game/cosmos/cosmos.h"
#include "game/cosmos/entity_handle.h"

#include "game/enums/item_category.h"
#include "game/detail/inventory/inventory_slot.h"
#include "game/detail/entity_handle_mixins/for_each_slot_and_item.hpp"
#include "game/detail/inventory/perform_transfer.h"
#include "game/components/sprite_component.h"
#include "game/components/item_component.h"
#include "game/components/fixtures_component.h"
#include "game/stateless_systems/input_system.h"

#include "view/viewables/all_viewables_declaration.h"
#include "view/viewables/image_in_atlas.h"
#include "view/viewables/image_meta.h"

#include "view/game_gui/game_gui_context.h"
#include "view/game_gui/game_gui_system.h"
#include "view/game_gui/elements/item_button.h"
#include "view/game_gui/elements/pixel_line_connector.h"
#include "view/game_gui/elements/gui_grid.h"
#include "view/game_gui/elements/drag_and_drop.h"
#include "view/game_gui/elements/game_gui_root.h"
#include "view/game_gui/elements/character_gui.h"
#include "view/game_gui/elements/slot_button.h"

#include "view/audiovisual_state/systems/interpolation_system.h"
#include "view/audiovisual_state/systems/randomizing_system.h"

#include "view/rendering_scripts/draw_entity.h"
#include "view/viewables/images_in_atlas_map.h"

using namespace augs::gui::text;

bool item_button::is_being_wholely_dragged_or_pending_finish(
	const const_game_gui_context context, 
	const const_this_in_item this_id
) {
	const auto& rect_world = context.get_rect_world();
	const auto& element = context.get_character_gui();
	const auto& cosmos = context.get_cosmos();

	if (rect_world.is_currently_dragging(this_id)) {
		const bool is_drag_partial = element.dragged_charges < cosmos[this_id.get_location().item_id].get<components::item>().get_charges();
		return !is_drag_partial;
	}
	else {
		for (const auto& r : context.get_game_gui_system().pending_transfers) {
			if (r.item == this_id.get_location().item_id) {
				return true;
			}
		}
	}

	return false;
}

item_button::item_button(xywh rc) : base(rc) {
	unset_flag(augs::gui::flag::CLIP);
	unset_flag(augs::gui::flag::SCROLLABLE);
	unset_flag(augs::gui::flag::FOCUSABLE);
}

void item_button::draw_dragged_ghost_inside(
	const viewing_game_gui_context context, 
	const const_this_in_item this_id, 
	const vec2 absolute_xy_offset
) {
	drawing_settings f;
	f.draw_background = true;
	f.draw_item = true;
	f.draw_border = false;
	f.draw_connector = false;
	f.decrease_alpha = true;
	f.decrease_border_alpha = false;
	f.draw_container_opened_mark = false;
	f.draw_charges = false;
	f.absolute_xy_offset = absolute_xy_offset;

	draw_proc(context, this_id, f);
}

void item_button::draw_complete_with_children(
	const viewing_game_gui_context context, 
	const const_this_in_item this_id
) {
	drawing_settings f;
	f.draw_background = true;
	f.draw_item = true;
	f.draw_border = true;
	f.draw_connector = true;
	f.decrease_alpha = false;
	f.decrease_border_alpha = false;
	f.draw_container_opened_mark = true;
	f.draw_charges = true;

	draw_children(context, this_id);
	draw_proc(context, this_id, f);
}

void item_button::draw_grid_border_ghost(
	const viewing_game_gui_context context, 
	const const_this_in_item this_id, 
	const vec2 absolute_xy_offset
) {
	drawing_settings f;
	f.draw_background = false;
	f.draw_item = false;
	f.draw_border = true;
	f.draw_connector = false;
	f.decrease_alpha = true;
	f.decrease_border_alpha = true;
	f.draw_container_opened_mark = false;
	f.draw_charges = true;
	f.absolute_xy_offset = griddify(absolute_xy_offset);

	draw_proc(context, this_id, f);
}

void item_button::draw_complete_dragged_ghost(
	const viewing_game_gui_context context, 
	const const_this_in_item this_id, 
	const vec2 absolute_xy_offset
) {
	draw_dragged_ghost_inside(context, this_id, absolute_xy_offset);
}

ltrb item_button::calc_button_layout(
	const const_entity_handle component_owner,
	const image_definitions_map& defs,
	const bool include_attachments
) {
	(void)defs;

	auto aabb = component_owner.get_aabb(transformr());
	
	if (include_attachments) {
		aabb.contain(component_owner.calc_attachments_aabb([](){ return torso_offsets(); }));
	}

	return aabb;
}

vec2 item_button::griddify_size(const vec2 size, const vec2 expander) {
	vec2i rounded_size = size;
	rounded_size += expander;

	rounded_size += 22;
	rounded_size /= 11;
	rounded_size *= 11;

	return rounded_size;
}

void item_button::draw_proc(
	const viewing_game_gui_context context, 
	const const_this_in_item this_id, 
	const drawing_settings& f
) {
	if (is_inventory_root(context, this_id)) {
		return;
	}

	const auto& cosmos = context.get_cosmos();
	const auto& item = cosmos[this_id.get_location().item_id];
	const auto& detector = this_id->detector;
	const auto& rect_world = context.get_rect_world();
	const auto& element = context.get_character_gui();
	const auto output = context.get_output();
	const auto& necessarys = context.get_necessary_images();
	const auto& drawing_in = context.make_draw_renderable_input();

	auto this_tree_entry = context.get_tree_entry(this_id);
	
	this_tree_entry.set_absolute_pos(
		this_tree_entry.get_absolute_pos() 
		+ f.absolute_xy_offset
	);
	
	const auto this_absolute_rect = this_tree_entry.get_absolute_rect();

	auto parent_slot = cosmos[item.get<components::item>().get_current_slot()];

	rgba inside_col = cyan;
	rgba border_col = cyan;

	if (parent_slot->category_allowed != item_category::GENERAL) {
		border_col = pink;
		inside_col = violet;
	}

	inside_col.a = 20;
	border_col.a = 190;

	if (detector.is_hovered) {
		inside_col.a = 30;
		border_col.a = 220;
	}

	if (detector.current_appearance == augs::gui::appearance_detector::appearance::pushed) {
		inside_col.a = 60;
		border_col.a = 255;
	}

	if (f.decrease_alpha) {
		inside_col.a = 15;
	}

	if (f.decrease_border_alpha) {
		border_col = slightly_visible_white;
	}

	if (f.draw_background) {
		output.aabb(this_absolute_rect, inside_col);
	}

	if (f.draw_item) {
		{
			const bool draw_attachments = !this_id->is_container_open || f.draw_attachments_even_if_open;
			const auto layout = calc_button_layout(item, context.get_image_metas(), draw_attachments);
			
			vec2 expansion_offset;

			if (f.expand_size_to_grid) {
				const auto gui_def = context.get_image_metas().at(item.get<invariants::sprite>().image_id).meta.usage_as_button;
				const auto size = vec2i(layout.get_size());
				const auto rounded_size = griddify_size(size, gui_def.bbox_expander);

				expansion_offset = (rounded_size - size) / 2;
			}

			const auto rc_pos = this_absolute_rect.get_position();

			const auto viewing_pos = vec2i(-layout.left_top()) + rc_pos + expansion_offset;
			const auto viewing_transform = transformr(viewing_pos, 0);

			auto render_visitor = [](const auto& renderable, auto&&... args) {
				augs::draw(renderable, std::forward<decltype(args)>(args)...);
			};

			auto drawer = [&](
				const const_entity_handle attachment_handle, 
				const transformr where
			) {
				attachment_handle.dispatch_on_having_all<invariants::item>([&](const auto typed_attachment_handle) {
					detail_specific_entity_drawer<true>(
						typed_attachment_handle,
						drawing_in,
						render_visitor,
						where
					);
				});
			};

			drawer(item, viewing_transform);

			if (draw_attachments) {
				item.for_each_attachment_recursive(
					[&](
						const auto attachment_entity,
						const auto attachment_offset
					) {
						drawer(attachment_entity, viewing_transform * attachment_offset);
					},
					[]() {
						return torso_offsets();
					},
					attachment_offset_settings::for_logic()
				);
			}
		}

		if (f.draw_charges) {
			const auto item_data = item.get<components::item>();

			int considered_charges = item_data.get_charges();

			if (rect_world.is_currently_dragging(this_id)) {
				considered_charges = item_data.get_charges() - element.dragged_charges;
			}

			double bottom_number_val = -1.0;
			bool printing_charge_count = false;
			bool trim_zero = false;

			auto label_color = border_col;

			if (considered_charges > 1) {
				bottom_number_val = considered_charges;
				printing_charge_count = true;
			}
			else if (element.draw_space_available_inside_container_icons && item[slot_function::ITEM_DEPOSIT].alive()) {
				if (item.get<invariants::item>().categories_for_slot_compatibility.test(item_category::MAGAZINE)) {
					if (!this_id->is_container_open) {
						printing_charge_count = true;
					}
				}

				if (printing_charge_count) {
					bottom_number_val = count_charges_in_deposit(item);
				}
				else {
					bottom_number_val = item[slot_function::ITEM_DEPOSIT].calc_real_space_available() / double(SPACE_ATOMS_PER_UNIT);

					if (bottom_number_val < 1.0 && bottom_number_val > 0.0) {
						trim_zero = true;
					}

					label_color.set_rgb(cyan.rgb());
				}
			}

			if (bottom_number_val > -1.f) {
				std::string label_str;

				if (printing_charge_count) {
					//label_str = 'x';
					label_color.set_rgb(white.rgb());
					label_str += to_string_ex(bottom_number_val);
				}
				else
					label_str = to_string_ex(bottom_number_val, 2);

				if (trim_zero && label_str[0] == '0') {
					label_str.erase(label_str.begin());
				}

				// else label_str = '{' + label_str + '}';

				const auto label_text = formatted_string {
					label_str, { context.get_gui_font(), label_color }
				};

				const auto label_bbox = get_text_bbox(label_text);

				print_stroked(
					output,
					this_tree_entry.get_absolute_rect().right_bottom() - label_bbox,
					label_text
				);
			}
		}
	}

	if (f.draw_border) {
		output.border(this_absolute_rect, border_col);
	}

	if (f.draw_connector && parent_slot.get_container().get_owning_transfer_capability() != parent_slot.get_container()) {
		draw_pixel_line_connector(
			output, 
			this_absolute_rect, 
			context.get_tree_entry(item_button_in_item{ parent_slot.get_container() }).get_absolute_rect(),
			border_col
		);
	}

	if (f.draw_container_opened_mark) {
		if (item.find<invariants::container>()) {
			assets::necessary_image_id container_icon;

			if (this_id->is_container_open) {
				container_icon = assets::necessary_image_id::CONTAINER_OPEN_ICON;
			}
			else {
				container_icon = assets::necessary_image_id::CONTAINER_CLOSED_ICON;
			}

			const auto size = necessarys.at(container_icon).get_original_size();

			output.aabb_lt(necessarys.at(container_icon), vec2(this_absolute_rect.r - size.x + 2, this_absolute_rect.t + 1), border_col);
		}
	}
}

bool item_button::is_inventory_root(const const_game_gui_context context, const const_this_in_item this_id) {
	const bool result = this_id.get_location().item_id == context.get_subject_entity();
	ensure(!result);
	return result;
}

void item_button::rebuild_layouts(const game_gui_context context, const this_in_item this_id) {
	base::rebuild_layouts(context, this_id);

	const auto& cosmos = context.get_cosmos();
	const auto item = cosmos[this_id.get_location().item_id];

	if (is_inventory_root(context, this_id)) {
		this_id->set_flag(augs::gui::flag::ENABLE_DRAWING_OF_CHILDREN);
		this_id->set_flag(augs::gui::flag::DISABLE_HOVERING);
		return;
	}

	this_id->set_flag(augs::gui::flag::ENABLE_DRAWING_OF_CHILDREN, this_id->is_container_open && !is_being_wholely_dragged_or_pending_finish(context, this_id));
	this_id->set_flag(augs::gui::flag::DISABLE_HOVERING, is_being_wholely_dragged_or_pending_finish(context, this_id));

	const auto* const sprite = item.find<invariants::sprite>();
	const auto& manager = context.get_image_metas();

	if (sprite) {
		vec2i rounded_size = calc_button_layout(item, manager, !this_id->is_container_open).get_size();
		rounded_size = griddify_size(rounded_size, manager.at(item.get<invariants::sprite>().image_id).meta.usage_as_button.bbox_expander);
		this_id->rc.set_size(rounded_size);
	}

	auto parent_slot = cosmos[item.get<components::item>().get_current_slot()];

	if (parent_slot->always_allow_exactly_one_item) {
		const_dereferenced_location<slot_button_in_container> parent_button = context.dereference_location(slot_button_in_container{ parent_slot.get_id() });

		this_id->rc.set_position(parent_button->rc.get_position());
	}
	else {
		this_id->rc.set_position(this_id->drag_offset_in_item_deposit);
	}
}

void item_button::respond_to_events(const game_gui_context context, const this_in_item this_id, const gui_entropy& entropies) {
	base::respond_to_events(context, this_id, entropies);

	const auto& cosmos = context.get_cosmos();
	const auto& item = cosmos[this_id.get_location().item_id];
	const auto& rect_world = context.get_rect_world();
	auto& element = context.get_character_gui();

	if (!is_inventory_root(context, this_id)) {
		for (const auto& info : entropies.get_events_for(this_id)) {
			this_id->detector.update_appearance(info);

			if (info == gui_event::lstarteddrag) {
				element.dragged_charges = item.get<components::item>().get_charges();
			}

			if (info == gui_event::rclick) {
				this_id->is_container_open = !this_id->is_container_open;
			}

			if (info == gui_event::lfinisheddrag) {
				this_id->started_drag = false;

				drag_and_drop_callback(context, prepare_drag_and_drop_result(context, this_id, rect_world.rect_hovered), info.total_dragged_amount);
			}
		}
	}
}

void item_button::draw(
	const viewing_game_gui_context context, 
	const const_this_in_item this_id
) {
	if (!this_id->get_flag(augs::gui::flag::ENABLE_DRAWING)) {
		return;
	}

	if (!is_being_wholely_dragged_or_pending_finish(context, this_id)) {
		this_id->draw_complete_with_children(context, this_id);
	}
}