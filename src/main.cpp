#include <cstdlib>
#include <chrono>
#include <ctime>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "app_info.hpp"
#include "audit_log.hpp"
#include "auth_flow.hpp"
#include "console_ui.hpp"

namespace {

std::string current_utc_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    const auto* utc = std::gmtime(&time);
    if (utc == nullptr) {
        throw std::runtime_error("cannot format audit timestamp");
    }
    std::ostringstream formatted;
    formatted << std::put_time(utc, "%Y-%m-%dT%H:%M:%SZ");
    return formatted.str();
}

}  // namespace

int main() {
    std::cout << rrs::app_name() << " v" << rrs::app_version() << std::endl;

    const char* data_directory = std::getenv("RRS_DATA_DIR");
    try {
        const std::filesystem::path data_path =
            data_directory != nullptr ? data_directory : "data";
        const rrs::UserStore users{
            rrs::PersistenceStore{data_path}};
        const rrs::AuditLog audit_log{data_path / "audit.log"};
        const rrs::AuditTimestampProvider timestamp_provider = [] {
            return current_utc_timestamp();
        };

        // RRS-F-004: nothing else is reachable until the first administrator exists.
        if (!rrs::run_bootstrap(std::cin, std::cout, std::cerr, users, audit_log,
                                timestamp_provider)) {
            std::cout << "Setup not completed.\n";
            return 0;
        }

        const rrs::ActionHandler not_yet = [](rrs::MenuAction) {
            return rrs::Outcome::refusal("this feature is not available yet");
        };
        const rrs::InteractiveSessionActionHandler session_handler =
            [&users, &audit_log, &timestamp_provider, &not_yet](rrs::MenuAction action,
                                           const rrs::Session& session,
                                           const rrs::SessionActivityCheck& session_activity) {
                if (action == rrs::MenuAction::ManageTrains) {
                    return rrs::manage_trains_from_console(std::cin, std::cout, std::cerr, users,
                                                           audit_log, session, timestamp_provider,
                                                           session_activity);
                }
                if (action == rrs::MenuAction::ManageClasses) {
                    return rrs::manage_classes_from_console(std::cin, std::cout, std::cerr, users,
                                                            audit_log, session, timestamp_provider,
                                                            session_activity);
                }
                return not_yet(action);
            };
        rrs::run_menu(std::cin, std::cout, std::cerr, rrs::MenuTree::Public,
                      [&users, &audit_log, &timestamp_provider,
                       &not_yet, &session_handler](rrs::MenuAction action) {
                          if (action == rrs::MenuAction::Register) {
                              return rrs::register_from_console(std::cin, std::cout, users, audit_log,
                                                                timestamp_provider);
                          }
                          if (action == rrs::MenuAction::Login) {
                              return rrs::login_and_run_session(std::cin, std::cout, std::cerr,
                                                                users, audit_log,
                                                                timestamp_provider,
                                                                session_handler);
                          }
                          return not_yet(action);
                      });
    } catch (const std::exception& error) {
        std::cerr << "SYSTEM FAILURE: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
