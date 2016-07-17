#pragma once
#include <vector>
#include <string>

namespace augs {
	void create_directory(std::string filename);
	std::vector<std::string> get_all_files_in_directory(std::string dir);
}