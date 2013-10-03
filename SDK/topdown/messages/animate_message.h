#pragma once
#include "message.h"
#include "utility/sorted_vector.h"

namespace resources {
	struct animation;
}

namespace messages {
	struct animate_message : public message {
		enum animation {
			MOVE,
			SHOT
		} animation_type;

		enum type {
			CONTINUE,
			START,
			PAUSE,
			STOP
		} message_type;

		bool preserve_state_if_animation_changes;
		bool change_animation;
		bool change_speed;
		float speed_factor;
		int animation_priority;

		/* if nullptr, will take animation_type as key for animation_subscribtion map in animate_component */
		resources::animation* set_animation;

		animate_message() : set_animation(nullptr), change_speed(false), speed_factor(1.f), change_animation(false), preserve_state_if_animation_changes(false), animation_priority(0) {}
		animate_message(animation animation_type, type message_type, bool override_speed, float speed) 
			: set_animation(nullptr), animation_type(animation_type), message_type(message_type), change_speed(change_speed), speed_factor(speed_factor), animation_priority(0), preserve_state_if_animation_changes(false)
		{}
	};
}
