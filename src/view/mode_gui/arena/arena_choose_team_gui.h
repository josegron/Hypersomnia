#pragma once
#include <vector>
#include <optional>
#include "game/enums/faction_type.h"
#include "game/modes/mode_commands/team_choice.h"
#include "augs/misc/enum/enum_array.h"
#include "game/assets/ids/asset_ids.h"

class images_in_atlas_map;
struct app_ingame_intent_input;

struct arena_choose_team_gui {
	struct input {
		struct faction_info {
			const faction_type f;
			const rgba color;
			const std::size_t num_players;
			const std::size_t max_players;
		};

		const per_faction_t<assets::image_id>& button_logos;
		const std::vector<faction_info>& available_factions;
		const images_in_atlas_map& images_in_atlas;
		const faction_type current_faction;
	};

	// GEN INTROSPECTOR struct arena_choose_team_gui
	bool show = false;
	// END GEN INTROSPECTOR

	/* Always initialize as hidden */

	bool control(app_ingame_intent_input);
	std::optional<mode_commands::team_choice> perform_imgui(input);
};
