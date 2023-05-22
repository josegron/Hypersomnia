#pragma once

enum class render_layer {
	// GEN INTROSPECTOR enum class render_layer
	INVALID,

	DISABLED,

	GROUND,

	PLANTED_ITEMS,
	SOLID_OBSTACLES,
	REMNANTS,
	ITEMS_ON_GROUND,
	SENTIENCES,
	FOREGROUND,
	FOREGROUND_GLOWS,

	DIM_WANDERING_PIXELS,
	CONTINUOUS_SOUNDS,
	CONTINUOUS_PARTICLES,
	ILLUMINATING_WANDERING_PIXELS,
	LIGHTS,
	AREA_MARKERS,
	POINT_MARKERS,
	AREA_SENSORS,
	CALLOUT_MARKERS,

	COUNT
	// END GEN INTROSPECTOR
};