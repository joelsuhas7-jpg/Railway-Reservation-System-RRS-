#include <cstdlib>
#include <exception>
#include <iostream>

#include "app_info.hpp"
#include "auth_flow.hpp"
#include "console_ui.hpp"

int main() {
    std::cout << rrs::app_name() << " v" << rrs::app_version() << std::endl;

    const char* data_directory = std::getenv("RRS_DATA_DIR");
    try {
        const rrs::UserStore users{
            rrs::PersistenceStore{data_directory != nullptr ? data_directory : "data"}};

        // RRS-F-004: nothing else is reachable until the first administrator exists.
        if (!rrs::run_bootstrap(std::cin, std::cout, std::cerr, users)) {
            std::cout << "Setup not completed.\n";
            return 0;
        }

        // Login is wired in by RRS-F-002.
        rrs::run_menu(std::cin, std::cout, std::cerr, rrs::MenuTree::Public,
                      [&users](rrs::MenuAction action) {
                          if (action == rrs::MenuAction::Register) {
                              return rrs::register_from_console(std::cin, std::cout, users);
                          }
                          return rrs::Outcome::refusal("this feature is not available yet");
                      });
    } catch (const std::exception& error) {
        std::cerr << "SYSTEM FAILURE: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
