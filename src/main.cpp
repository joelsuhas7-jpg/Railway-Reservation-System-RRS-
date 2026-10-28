#include <iostream>

#include "app_info.hpp"
#include "console_ui.hpp"

int main() {
    std::cout << rrs::app_name() << " v" << rrs::app_version() << std::endl;

    // Register and Login are wired in by RRS-F-001 and RRS-F-002.
    rrs::run_menu(std::cin, std::cout, std::cerr, rrs::MenuTree::Public, [](rrs::MenuAction) {
        return rrs::Outcome::refusal("this feature is not available yet");
    });
    return 0;
}
