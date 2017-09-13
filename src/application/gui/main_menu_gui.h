#pragma once
#include "augs/misc/machine_entropy.h"
#include "augs/gui/formatted_string.h"

#include "view/necessary_resources.h"

#include "application/gui/main_menu_button_type.h"
#include "application/gui/menu/menu_context.h"

#include "generated/introspectors.h"

using main_menu_context = menu_context<false, main_menu_button_type>;
using const_main_menu_context = menu_context<true, main_menu_button_type>;
using viewing_main_menu_context = viewing_menu_context<main_menu_button_type>;

struct main_menu_gui {
	bool show = false;

	main_menu_context::tree_type tree;
	main_menu_context::root_type root;
	main_menu_context::rect_world_type world;

	auto create_context(
		const vec2i screen_size,
		const augs::event::state input_state,
		const menu_context_dependencies deps
	) {
		return main_menu_context {
			{ world, tree, screen_size, input_state },
			root,
			deps
		};
	}

	template <class B>
	auto control(
		const main_menu_context context,
		const augs::event::change change,
		B button_callback
	) {
		const auto gui_entropies = 
			world.consume_raw_input_and_generate_gui_events(
				context, 
				change
			)
		;

		world.respond_to_events(context, gui_entropies);

		augs::for_each_enum_except_bounds<main_menu_button_type>([&](const main_menu_button_type t) {
			if (root.buttons[t].click_callback_required) {
				root.buttons[t].click_callback_required = false;

				button_callback(t);
			}
		});
	}

	void advance(
		const main_menu_context context,
		const augs::delta vdt
	) {
		const auto& gui_font = context.get_gui_font();

		root.set_menu_buttons_colors(cyan);
		root.set_menu_buttons_sizes(context.get_necessary_images(), gui_font, { 1000, 1000 });

		world.advance_elements(context, vdt);
		world.rebuild_layouts(context);
		world.build_tree_data_into(context);
	}

	auto draw(const viewing_main_menu_context context) const {
		if (!show) {
			return assets::necessary_image_id::INVALID;
		}

		const auto output = context.get_output();
		const auto screen_size = context.get_screen_size();

		output.color_overlay(screen_size, rgba{ 0, 0, 0, 140 });

		root.draw_background_behind_buttons(context);
		world.draw(context);

		auto determined_cursor = assets::necessary_image_id::GUI_CURSOR;

		if (context.alive(world.rect_hovered)) {
			determined_cursor = assets::necessary_image_id::GUI_CURSOR_HOVER;
		}

		return determined_cursor;
	}
};
