#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
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

    } else if (command == "add") {
        if (argc < 3) {
            std::cerr << "Usage: likegit add <file>\n";
            return 1;
        }

        fs::path file_path = argv[2];
        fs::path repo_path = fs::current_path();

        std::ifstream in_file(file_path, std::ios::binary);
        if (!in_file.is_open()) {
            std::cerr << "Error: cannot open file '" << file_path << "'\n";
            return 1;
        }
        std::ostringstream buf;
        buf << in_file.rdbuf();
        std::string content = buf.str();
        in_file.close();

        std::string hash = hash_object(content, "blob");
        std::string header = "blob " + std::to_string(content.size()) + '\0' + content;
        write_object(hash, header, repo_path);

        update_index(file_path.string(), hash, repo_path);

        std::cout << "Added '" << file_path.string() << "' (" << hash << ")\n";
        return 0;

    } else {
        std::cerr << "Unknown command: " << command << '\n';
        std::cerr << "Available commands: init, add\n";
        return 1;
    }
}