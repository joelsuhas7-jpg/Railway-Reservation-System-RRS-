#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>

#include "admin_flow.hpp"
#include "app_info.hpp"
#include "audit_log.hpp"
#include "auth_flow.hpp"
#include "console_ui.hpp"
#include "inventory_flow.hpp"
#include "search_flow.hpp"
#include "session_context.hpp"

int main() {
    std::cout << rrs::app_name() << " v" << rrs::app_version() << std::endl;

    const char* data_directory = std::getenv("RRS_DATA_DIR");
    try {
        const std::filesystem::path data_path =
            data_directory != nullptr ? data_directory : "data";
        const rrs::UserStore users{rrs::PersistenceStore{data_path}};
        const rrs::AuditLog audit_log{data_path / "audit.log"};
        const rrs::AppContext context{std::cin, std::cout, std::cerr, users, audit_log,
                                      rrs::utc_timestamp_now, {}};

        // RRS-F-004: nothing else is reachable until the first administrator exists.
        if (!rrs::run_bootstrap(context.in, context.out, context.err, users, audit_log,
                                context.timestamp)) {
            std::cout << "Setup not completed.\n";
            return 0;
        }

        // Each feature registers its own role-menu actions; unregistered ones are not available yet.
        rrs::MenuRouter router;
        rrs::register_account_flows(router);
        rrs::register_admin_flows(router);
        rrs::register_inventory_flows(router);
        rrs::register_search_flows(router);

        rrs::run_menu(context.in, context.out, context.err, rrs::MenuTree::Public,
                      [&context, &router](rrs::MenuAction action) {
                          if (action == rrs::MenuAction::Register) {
                              return rrs::register_from_console(context.in, context.out,
                                                                context.users, context.audit_log,
                                                                context.timestamp);
                          }
                          if (action == rrs::MenuAction::Login) {
                              return rrs::login_and_run_session(context, router);
                          }
                          return rrs::Outcome::refusal("this feature is not available yet");
                      });
    } catch (const std::exception& error) {
        std::cerr << "SYSTEM FAILURE: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
