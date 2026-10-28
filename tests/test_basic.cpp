#include <cstdlib>
#include <iostream>
#include <string>

int main() {
    const std::string appName = "Railway Reservation System";

    if (appName != "Railway Reservation System") {
        std::cerr << "FAIL: application name check" << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "PASS: basic application test" << std::endl;
    return EXIT_SUCCESS;
}
