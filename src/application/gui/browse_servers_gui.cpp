#include <future>

#include "application/gui/browse_servers_gui.h"
#include "augs/network/netcode_utils.h"
#include "augs/misc/imgui/imgui_control_wrappers.h"
#include "3rdparty/cpp-httplib/httplib.h"
#include "augs/templates/thread_templates.h"
#include "augs/readwrite/memory_stream.h"
#include "augs/readwrite/pointer_to_buffer.h"
#include "augs/readwrite/byte_readwrite.h"
#include "application/setups/client/client_start_input.h"
#include "augs/log.h"
#include "application/setups/editor/detail/maybe_different_colors.h"
#include "augs/misc/time_utils.h"
#include "augs/network/netcode_sockets.h"
#include "application/masterserver/nat_puncher_client.h"

constexpr auto nat_request_interval = 0.5;
constexpr auto ping_retry_interval = 1;
constexpr auto ping_nat_keepalive_interval = 10;
constexpr auto server_entry_timeout = 5;
constexpr auto max_packets_per_frame_v = 64;

#define LOG_BROWSER !NDEBUG

template <class... Args>
void BRW_LOG(Args&&... args) {
#if LOG_BROWSER
	LOG(std::forward<Args>(args)...);
#else
	((void)args, ...);
#endif
}

#if LOG_MASTERSERVER
#define BRW_LOG_NVPS LOG_NVPS
#else
#define BRW_LOG_NVPS BRW_LOG
#endif

bool server_list_entry::is_set() const {
	return data.server_name.size() > 0;
}

browse_servers_gui_state::~browse_servers_gui_state() = default;

struct browse_servers_gui_internal {
	std::optional<httplib::Client> http;
	std::future<std::shared_ptr<httplib::Response>> future_response;
	netcode_socket_t socket;

	nat_puncher_client nat;

	bool refresh_op_in_progress() const {
		return future_response.valid() || nat.is_resolving_host();
	}
};


browse_servers_gui_state::browse_servers_gui_state(const std::string& title) 
	: base(title), data(std::make_unique<browse_servers_gui_internal>()) 
{

}

#if 0
static std::string get_internal_ip() {
	const char* google_dns_server = "8.8.8.8";
	int dns_port = 53;

	struct sockaddr_in serv;
	int sock = socket(AF_INET, SOCK_DGRAM, 0);

	if (sock < 0)
	{
		return "";
	}

	memset(&serv, 0, sizeof(serv));
	serv.sin_family = AF_INET;
	serv.sin_addr.s_addr = inet_addr(google_dns_server);
	serv.sin_port = htons(dns_port);

	int err = connect(sock, (const struct sockaddr*)&serv, sizeof(serv));

	if (err < 0)
	{
		return "";
	}

	struct sockaddr_in name;
	socklen_t namelen = sizeof(name);
	err = getsockname(sock, (struct sockaddr*)&name, &namelen);

	char buffer[80];
	const char* p = inet_ntop(AF_INET, &name.sin_addr, buffer, 80);

	if (p != NULL)
	{
		return buffer;
	}

	close(sock);
	return "";
}
#endif

double yojimbo_time();

void browse_servers_gui_state::refresh_server_list(const browse_servers_input in) {
	using namespace httplib;

	if (data->refresh_op_in_progress()) {
		return;
	}

	official_server_addresses.clear();
	error_message.clear();
	server_list.clear();
	selected_server = {};

	auto& http_opt = data->http;

	data->nat.resolve_relay_host(in.nat_punch_provider);

	data->future_response = launch_async(
		[&http_opt, host_url = in.server_list_provider]() {
			const auto timeout = 5;
			http_opt.emplace(host_url.c_str(), 8080, timeout);
			LOG("Refreshing server list from %x.", host_url);

			auto progress = [](uint64_t len, uint64_t total) {
				(void)len;
				(void)total;

				return true;
			};

			auto& http = *http_opt;
			http.follow_location(true);

			return http.Get("/server_list_binary", progress);
		}
	);

	when_last_downloaded_server_list = yojimbo_time();
}

void browse_servers_gui_state::refresh_server_pings() {
	for (auto& s : server_list) {
		s.progress = {};
	}
}

bool server_list_entry::is_behind_nat() const {
	return address != data.internal_network_address;
}

server_list_entry* browse_servers_gui_state::find(const netcode_address_t& address) {
	for (auto& s : server_list) {
		if (s.address == address) {
			return &s;
		}
	}

	return nullptr;
}

void browse_servers_gui_state::advance_ping_and_nat_logic(const browse_servers_input in) {
	if (in.nat_puncher_socket == nullptr) {
		return;
	}

	auto udp_socket = *in.nat_puncher_socket;
	auto& nat = data->nat;

	nat.advance_relay_host_resolution();

	const auto current_time = yojimbo_time();

	{

		const auto num_dots = uint64_t(current_time * 3) % 3 + 1;
		loading_dots = std::string(num_dots, '.');
	}

	{
		struct netcode_address_t from;
		uint8_t packet_buffer[NETCODE_MAX_PACKET_BYTES];

		while (true) {
			const auto packet_bytes = netcode_socket_receive_packet(&udp_socket, &from, packet_buffer, NETCODE_MAX_PACKET_BYTES);

			if (packet_bytes == 0) {
				break;
			}

			BRW_LOG("Packet arrived!");

			if (packet_bytes != 1 + sizeof(uint64_t)) {
				BRW_LOG("Wrong num of bytes: %x!", packet_bytes);
				continue;
			}

			const auto command = packet_buffer[0];

			if (command == NETCODE_PING_RESPONSE_PACKET) {
				BRW_LOG("Ping response!");
				if (const auto entry = find(from)) {
					uint64_t sequence;
					std::memcpy(&sequence, packet_buffer + 1, sizeof(uint64_t));

					auto& progress = entry->progress;

					BRW_LOG("Has entry! Sequence: %x; entry sequence: %x", sequence, progress.ping_sequence);

					if (sequence == uint64_t(-1)) {
						progress.state = server_entry_state::PUNCHED;
					}
					else if (sequence == progress.ping_sequence) {
						BRW_LOG("Sequence matches!");
						progress.ping = static_cast<int>(current_time - progress.when_sent_last_ping);
						progress.state = server_entry_state::PING_MEASURED;
					}
				}
			}
		}
	}

	int packets_left = max_packets_per_frame_v;

	if (server_list.empty()) {
		return;
	}

	for (auto& s : server_list) {
		if (packets_left <= 0) {
			break;
		}

		auto& p = s.progress;

		using S = server_entry_state;

		auto request_ping = [&]() {
			p.when_sent_last_ping = current_time;
			p.ping_sequence = ping_sequence_counter++;

			send_ping_request(udp_socket, p.ping_sequence, s.address);
			--packets_left;
		};

		if (p.state == S::AWAITING_RESPONSE) {
			if (s.is_behind_nat()) {
				if (nat.relay_host_resolved()) {
					auto& when_first = p.when_first_nat_request;
					auto& when_last = p.when_last_nat_request;

					if (when_last == 0 || current_time - when_last >= nat_request_interval) {
						when_last = current_time;

						if (when_first == 0) {
							when_first = current_time;
						}

						nat.punched_server_addr = s.address;
						nat.request_punch(udp_socket);

						--packets_left;
					}

					if (when_first != 0 && current_time - when_first >= server_entry_timeout) {
						p.state = server_entry_state::GIVEN_UP;
						continue;
					}
				}
			}
		}

		if (p.state == S::AWAITING_RESPONSE || p.state == S::PUNCHED) {
			BRW_LOG("State needs pinging");
			auto& when_first = p.when_sent_first_ping;
			auto& when_last = p.when_sent_last_ping;

			if (when_last == 0 || current_time - when_last >= ping_retry_interval) {
				if (when_first == 0) {
					when_first = current_time;
				}

				BRW_LOG("Requesting ping");
				request_ping();
			}

			if (when_first != 0 && current_time - when_first >= server_entry_timeout) {
				p.state = server_entry_state::GIVEN_UP;
				continue;
			}
		}

		if (p.state == S::PING_MEASURED) {
			auto& when_last = p.when_sent_last_ping;

			if (current_time - when_last >= ping_nat_keepalive_interval) {
				request_ping();
			}
		}
	}
}

std::optional<netcode_address_t> browse_servers_gui_state::show_server_list() {
	using namespace augs::imgui;

	auto requested_connection = std::optional<netcode_address_t>();

	const auto& style = ImGui::GetStyle();

	for (const auto& sp : filtered_server_list) {
		auto id = scoped_id(index_in(filtered_server_list, sp));
		const auto& s = *sp;
		const auto& progress = s.progress;
		const auto& d = s.data;

		const bool given_up = progress.state == server_entry_state::GIVEN_UP;
		const auto color = rgba(given_up ? style.Colors[ImGuiCol_TextDisabled] : style.Colors[ImGuiCol_Text]);

		auto do_text = [&](const auto& t) {
			text_color(t, color);
		};

		const bool is_selected = selected_server.address == s.address;

		{
			const auto ping = progress.ping;

			if (ping != -1) {
				if (ping > 999) {
					do_text("999>");
				}
				else {
					do_text(typesafe_sprintf("%x", ping));
				}
			}
			else {
				if (given_up) {
					do_text("?");
				}
				else {
					do_text(loading_dots);
				}
			}
		}

		ImGui::NextColumn();

		{
			auto col_scope = scoped_style_color(ImGuiCol_Text, color);

			if (ImGui::Selectable(d.server_name.c_str(), is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
				selected_server = s;
			}
		}

		if (ImGui::IsItemClicked()) {
			if (ImGui::IsMouseDoubleClicked(0)) {
				LOG("Double-clicked server list entry: %x (%x). Connecting.", ToString(s.address), d.server_name);

				requested_connection = s.address;
			}
		}

		if (ImGui::IsItemClicked(1)) {
			selected_server = s;
			server_details.open();
		}

		ImGui::NextColumn();

		if (d.game_mode.is_set()) {
			const auto game_mode_name = d.game_mode.dispatch([](auto d) {
				using D = decltype(d);
				return format_field_name(get_type_name<D>());
			});

			do_text(game_mode_name);
		}
		else {
			do_text("<unknown>");
		}

		ImGui::NextColumn();

		const auto max_players = std::min(d.max_fighting, d.max_online);
		do_text(typesafe_sprintf("%x/%x", d.num_fighting, max_players));

		ImGui::NextColumn();

		const auto num_spectators = d.num_online - d.num_fighting;
		const auto max_spectators = d.max_online - d.num_fighting;

		do_text(typesafe_sprintf("%x/%x", num_spectators, max_spectators));

		ImGui::NextColumn();
		do_text(d.current_arena);

		ImGui::NextColumn();

		const auto secs_ago = augs::date_time::secs_since_epoch() - s.appeared_when;
		text_disabled(augs::date_time::format_how_long_ago(true, secs_ago));

		ImGui::NextColumn();
	}

	return requested_connection;
}

bool successful(const int http_status_code);

bool browse_servers_gui_state::perform(const browse_servers_input in) {
	if (!show) {
		return false;
	}

	using namespace augs::imgui;

	centered_size_mult = vec2(0.75f, 0.6f);

	auto imgui_window = make_scoped_window();

	if (!imgui_window) {
		return false;
	}

	const auto couldnt_download = std::string("Couldn't download the server list.\n");

	auto handle_response = [&](auto response) {
		if (response == nullptr) {
			error_message = "Couldn't connect to the server list host.";
			return;
		}

		const auto status = response->status;

		LOG("Server list response status: %x", status);

		if (!successful(status)) {
			error_message = couldnt_download + "HTTP response: " + std::to_string(status);
			return;
		}

		const auto& bytes = response->body;

		LOG("Server list response bytes: %x", bytes.size());

		const auto buffer_ptr = augs::cpointer_to_buffer{ reinterpret_cast<const std::byte*>(bytes.data()), bytes.size() };

		auto ss = augs::cptr_memory_stream{ buffer_ptr };

		try {
			while (ss.get_unread_bytes() > 0) {
				server_list_entry entry;

				augs::read_bytes(ss, entry.address);
				augs::read_bytes(ss, entry.appeared_when);
				augs::read_bytes(ss, entry.data);

				server_list.emplace_back(std::move(entry));
			}
		}
		catch (const augs::stream_read_error& err) {
			error_message = "There was a problem deserializing the server list:\n" + std::string(err.what()) + "\n\nTry restarting the game and updating your client!";
			server_list.clear();
		}
	};

	if (valid_and_is_ready(data->future_response)) {
		handle_response(data->future_response.get());
	}

	thread_local ImGuiTextFilter filter;

	filter.Draw();
	filtered_server_list.clear();

	for (auto& s : server_list) {
		if (only_responding) {
			if (s.progress.ping == -1) {
				continue;
			}
		}

		if (at_least_players.is_enabled) {
			if (s.data.num_online < at_least_players.value) {
				continue;
			}
		}

		const auto& name = s.data.server_name;
		const auto& arena = s.data.current_arena;
		const auto effective_name = std::string(name) + " " + std::string(arena);

		if (filter.PassFilter(effective_name.c_str())) {
			filtered_server_list.push_back(&s);
		}
	}

	ImGui::Separator();

	if (when_last_downloaded_server_list == 0) {
		refresh_server_list(in);
	}

	auto requested_connection = std::optional<netcode_address_t>();

	auto do_list_view = [&](bool disable_content_view) {
		auto child = scoped_child("list view", ImVec2(0, 2 * -(ImGui::GetFrameHeightWithSpacing() + 4)));


		const auto num_filtered_results = filtered_server_list.size();

		const auto official_servers_label = typesafe_sprintf("Official servers (%x)", num_filtered_results);

		if (disable_content_view) {
			return;
		}

		int current_col = 0;

		auto do_column = [&](std::string label, std::optional<rgba> col = std::nullopt) {
			const bool is_current = current_col == sort_by_column;

			if (is_current) {
				label += ascending ? "▲" : "▼";
			}

			const auto& style = ImGui::GetStyle();

			rgba final_color = is_current ? style.Colors[ImGuiCol_Text] : style.Colors[ImGuiCol_TextDisabled];

			if (col != std::nullopt) {
				final_color = *col;
			}

			auto col_scope = scoped_style_color(ImGuiCol_Text, final_color);

			if (ImGui::Selectable(label.c_str())) {
				if (is_current) {
					ascending = !ascending;
				}
				else 
				{
					sort_by_column = current_col;
					ascending = true;
				}
			}

			++current_col;
			ImGui::NextColumn();
		};

		ImGui::Columns(7);
		ImGui::SetColumnWidth(0, ImGui::CalcTextSize("9999999").x);
		ImGui::SetColumnWidth(1, ImGui::CalcTextSize("9").x * 80);
		ImGui::SetColumnWidth(2, ImGui::CalcTextSize("9").x * 30);
		ImGui::SetColumnWidth(3, ImGui::CalcTextSize("Players  9").x);
		ImGui::SetColumnWidth(4, ImGui::CalcTextSize("Spectators  9").x);
		ImGui::SetColumnWidth(5, ImGui::CalcTextSize("9").x * (max_arena_name_length_v + 1));
		ImGui::SetColumnWidth(6, ImGui::CalcTextSize("9").x * 25);

		do_column("Ping");
		do_column(official_servers_label, yellow);
		do_column("Game mode");
		do_column("Players");
		do_column("Spectators");
		do_column("Arena");
		do_column("First appeared");

		ImGui::Separator();

		requested_connection = show_server_list();

		text_disabled("\n\n");

		const auto community_servers_label = typesafe_sprintf("Community servers");
		ImGui::Separator();
		ImGui::NextColumn();
		text_color(community_servers_label, orange);

		next_columns(7);
		ImGui::Separator();
	};


	{
		bool disable_content_view = false;

		if (error_message.size() > 0) {
			text_color(error_message, red);
			disable_content_view = true;
		}

#if 0
		if (num_filtered_results == 0 && server_list.size() > 0) {
			text_disabled("No servers match applied filters.");
		}

		if (server_list.empty()) {
			if (data->future_response.valid()) {
				text_disabled("Downloading the server list...");
			}
		}
		else {
		}
#endif
		if (data->future_response.valid()) {
			text_disabled("Downloading the server list...");
			disable_content_view = true;
		}

		do_list_view(disable_content_view);
	}

	{
		auto child = scoped_child("buttons view");

		ImGui::Separator();

		checkbox("Only responding", only_responding);
		ImGui::SameLine();

		checkbox("At least", at_least_players.is_enabled);

		{
			auto& val = at_least_players.value;
			auto scoped = maybe_disabled_cols({}, !at_least_players.is_enabled);
			auto width = scoped_item_width(ImGui::CalcTextSize("9").x * 4);

			ImGui::SameLine();
			auto input = std::to_string(val);
			input_text(val == 1 ? "player###numbox" : "players###numbox", input);
			val = std::clamp(std::atoi(input.c_str()), 1, int(max_incoming_connections_v));
		}

		{
			auto scope = maybe_disabled_cols({}, !selected_server.is_set());

			if (ImGui::Button("Connect")) {
				requested_connection = selected_server.address;
			}

			ImGui::SameLine();

			if (ImGui::Button("Server details")) {
				server_details.open();
			}

			ImGui::SameLine();
		}

		{
			auto scope = maybe_disabled_cols({}, data->refresh_op_in_progress());

			if (ImGui::Button("Refresh ping")) {
				refresh_server_pings();
			}

			ImGui::SameLine();

			if (ImGui::Button("Refresh list")) {
				refresh_server_list(in);
			}
		}
	}
	
	server_details.perform(selected_server);

	if (requested_connection != std::nullopt) {
		in.client_start.set_custom(::ToString(*requested_connection));

		return true;
	}

	return false;
}

void server_details_gui_state::perform(const server_list_entry& entry) {
	using namespace augs::imgui;

	auto window = make_scoped_window(ImGuiWindowFlags_AlwaysAutoResize);

	if (!window) {
		return;
	}

	if (!entry.is_set()) {
		text_disabled("No server selected.");
		return;
	}

	auto data = entry.data;
	auto address = ::ToString(entry.address);

	acquire_keyboard_once();
	input_text("IP address", address, ImGuiInputTextFlags_ReadOnly);
	input_text("Server name", data.server_name, ImGuiInputTextFlags_ReadOnly);

	input_text("Arena", data.current_arena, ImGuiInputTextFlags_ReadOnly);
}

void browse_servers_gui_state::request_nat_reopen() {
	for (auto& s : server_list) {
		if (s.is_behind_nat()) {
			s.progress = {};
		}
	}
}
