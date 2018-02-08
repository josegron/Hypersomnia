#pragma once
#include <type_traits>
#include "augs/ensure.h"
#include "augs/misc/pool/pooled_object_id.h"
#include "augs/build_settings/platform_defines.h"

#include "game/transcendental/entity_id_declaration.h"
#include "game/transcendental/cosmic_types.h"

#include "game/organization/all_entity_types.h"

struct entity_guid {
	using guid_value_type = unsigned;
	// GEN INTROSPECTOR struct entity_guid
	guid_value_type value = 0u;
	// END GEN INTROSPECTOR

	entity_guid(const guid_value_type b = 0u) : value(b) {}
	
	entity_guid& operator=(const guid_value_type b) {
		value = b;
		return *this;
	}

	operator guid_value_type() const {
		return value;
	}
};

struct unversioned_entity_id : unversioned_entity_id_base {
	using base = unversioned_entity_id_base;
	// GEN INTROSPECTOR struct unversioned_entity_id
	// INTROSPECT BASE unversioned_entity_id_base
	entity_type_id type_id;
	// END GEN INTROSPECTOR

	unversioned_entity_id(
		const base id = base(),
		entity_type_id type_id = entity_type_id()
	) : 
		base(id)
		type_id(type_id)
	{}
};

template <class E>
struct typed_entity_id : entity_id_base {
	using base = entity_id_base;
	using base::base;

	operator entity_id_base() const {
		return *static_cast<base*>(this);
	}

	operator entity_id() const {
		return { *this, entity_type_id::of<E> };
	}
}; 

struct entity_id : entity_id_base {
	using base = entity_id_base;
	// GEN INTROSPECTOR struct entity_id
	// INTROSPECT BASE entity_id_base
	entity_type_id type_id;
	// END GEN INTROSPECTOR

	entity_id(
		const base id = base(),
		entity_type_id type_id = entity_type_id()
	) : 
		base(id)
		type_id(type_id)
	{}
}; 

struct child_entity_id : entity_id {
	// GEN INTROSPECTOR struct child_entity_id
	// INTROSPECT BASE entity_id
	// END GEN INTROSPECTOR

	using base = entity_id;
	child_entity_id(const entity_id id = entity_id()) : entity_id(id) {}
	using base::operator unversioned_entity_id;
};

namespace std {
	template <>
	struct hash<entity_guid> {
		std::size_t operator()(const entity_guid v) const {
			return hash<entity_guid::guid_value_type>()(v.value);
		}
	};

	template <>
	struct hash<entity_id> {
		std::size_t operator()(const entity_id v) const {
			return augs::simple_two_hash(static_cast<entity_id::base&>(v), v.type_id);
		}
	};

	template <>
	struct hash<unversioned_entity_id> {
		std::size_t operator()(const unversioned_entity_id v) const {
			return augs::simple_two_hash(static_cast<unversioned_entity_id::base&>(v), v.type_id);
		}
	};
}
