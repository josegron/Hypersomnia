#pragma once
#include "augs/filesystem/path.h"

struct editor_view;
struct editor_project;

struct editor_resource_pools;
struct editor_official_resource_map;

namespace editor_project_readwrite {
	void write_project_json(const augs::path_type& json_path, const editor_project&);

	/*
		If strict is false, invalid entries will be ignored and it will load whatever is possible.
		Otherwise augs::json_deserialization_error will be thrown upon encountering any problem.
	*/

	editor_project read_project_json(
		const augs::path_type& json_path,
		const editor_resource_pools& officials,
		const editor_official_resource_map& officials_map,
		const bool strict
	);

	void write_project_json(
		const augs::path_type& json_path,
		const editor_project& project,
		const editor_resource_pools& officials,
		const editor_official_resource_map& officials_map
	);

	editor_project_about read_only_project_about(const augs::path_type& json_path);
	editor_project_meta read_only_project_meta(const augs::path_type& json_path);

	void write_editor_view(const augs::path_type& json_path, const editor_view&);
	editor_view read_editor_view(const augs::path_type& json_path);
}

