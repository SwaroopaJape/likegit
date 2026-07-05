#include <iostream>
#include <filesystem>
#include "../include/likegit/core.hpp"

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: likegit <command> [args...]\n";
        return 1;
    }

    std::string command = argv[1];

    if (command == "init") {
        if (argc < 3) {
            std::cerr << "Usage: likegit init <path>\n";
            return 1;
        }

        fs::path repo_path = argv[2];
        if (init_repository(repo_path)) {
            std::cout << "Initialized empty LikeGit repository in " << repo_path << "\n";
            return 0;
        } else {
            std::cerr << "Failed to initialize repository.\n";
            return 1;
        }
    } else {
        std::cerr << "Unknown command: " << command << '\n';
        std::cerr << "See 'likegit --help' for more information.\n";
        return 1;
    }
}