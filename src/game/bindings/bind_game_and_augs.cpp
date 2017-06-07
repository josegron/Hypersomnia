#include "augs/log.h"

#include <sol.hpp>

#include "augs/math/vec2.h"
#include "augs/graphics/pixel.h"

#include "application/config_lua_table.h"

void bind_game_and_augs(sol::state& wrapper) {
	wrapper.new_usertype<config_lua_table>(
		"config_lua_table",
		sol::constructors<config_lua_table, config_lua_table()>()
	);

	wrapper.new_usertype<vec2>(
		"vec2",
		sol::constructors<vec2, vec2(float, float)>()
	);

	wrapper.new_usertype<rgba>(
		"rgba",
		sol::constructors<rgba, rgba(rgba_channel, rgba_channel, rgba_channel, rgba_channel)>()
	);
}