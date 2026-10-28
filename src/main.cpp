#include <iostream>

#include "app_info.hpp"

int main() {
    std::cout << rrs::app_name() << " v" << rrs::app_version() << std::endl;
    std::cout << "Application skeleton is running." << std::endl;
    return 0;
}
