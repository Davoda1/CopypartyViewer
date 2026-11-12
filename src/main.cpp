// SPDX-License-Identifier: MIT
// Copyright (C) 2026 davoda1
#include "core.hpp"
#include "ui.hpp"
#include <iostream>
#include <cstring>
#include <exception>
int main(int argc, char** argv) {
    if (argc > 1) {
        if (!std::strcmp(argv[1], "--version")) {
            std::cout << "Copyparty Viewer " << cpv::version << "\n"; return 0;
        }
        if (!std::strcmp(argv[1], "--help")) {
            std::cout << "Copyparty Viewer " << cpv::version
                      << "\nRead-only Copyparty desktop browser.\n"
                      << "Settings: " << cpv::settings_path().u8string() << "\n"
                      << "Use --version to print the application version.\n";
            return 0;
        }
    }
    try {
        cpv::network_init();
        int result = cpv::run_application(argc, argv);
        cpv::network_cleanup();
        return result;
    } catch (const std::exception& e) {
        std::cerr << "Copyparty Viewer could not start: " << e.what() << '\n';
        return 1;
    }
}
