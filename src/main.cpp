#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include "../include/likegit/core.hpp"

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: likegit <command> [args...]\n";
        return 1;
    }

    std::string command = argv[1];
    fs::path repo_path = fs::current_path();

    if (command == "init") {
        fs::path target = (argc >= 3) ? fs::path(argv[2]) : repo_path;
        if (init_repository(target)) {
            std::cout << "Initialized empty LikeGit repository in " << target << "\n";
            return 0;
        }
        std::cerr << "Failed to initialize repository.\n";
        return 1;

    } else if (command == "config") {
        if (argc < 3) {
            std::cerr << "Usage: likegit config <key> [value]\n";
            std::cerr << "       likegit config --get <key>\n";
            std::cerr << "       likegit config --list\n";
            return 1;
        }

        std::string sub = argv[2];

        if (sub == "--list") {
            for (auto& line : list_config(repo_path))
                std::cout << line << "\n";
            return 0;
        }

        if (sub == "--get") {
            if (argc < 4) { std::cerr << "Usage: likegit config --get <key>\n"; return 1; }
            std::string val = get_config(argv[3], repo_path);
            if (val.empty()) { std::cerr << "Key not found.\n"; return 1; }
            std::cout << val << "\n";
            return 0;
        }

        if (argc < 4) { std::cerr << "Usage: likegit config <key> <value>\n"; return 1; }
        set_config(sub, argv[3], repo_path);
        return 0;

    } else if (command == "add") {
        if (argc < 3) { std::cerr << "Usage: likegit add <file>\n"; return 1; }

        fs::path file_path = argv[2];
        std::ifstream in(file_path, std::ios::binary);
        if (!in) { std::cerr << "Error: cannot open '" << file_path << "'\n"; return 1; }
        std::string content((std::istreambuf_iterator<char>(in)), {});

        std::string hash = hash_object(content, "blob");
        std::string header = std::string("blob ") + std::to_string(content.size()) + '\0' + content;
        write_object(hash, header, repo_path);
        update_index(file_path.string(), hash, repo_path);

        std::cout << "Added '" << file_path.string() << "' (" << hash << ")\n";
        return 0;

    } else if (command == "commit") {
        if (argc < 4 || std::string(argv[2]) != "-m") {
            std::cerr << "Usage: likegit commit -m \"<message>\"\n";
            return 1;
        }

        std::string name  = get_config("user.name",  repo_path);
        std::string email = get_config("user.email", repo_path);

        if (name.empty() || email.empty()) {
            std::cerr << "Error: author identity unknown.\n";
            std::cerr << "  Run: likegit config user.name \"Your Name\"\n";
            std::cerr << "       likegit config user.email \"you@example.com\"\n";
            return 1;
        }

        auto now = std::chrono::system_clock::now();
        std::string ts = std::to_string(
            std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count());

        std::string hash = create_commit(argv[3], name, email, ts, repo_path);
        std::cout << "[main " << hash.substr(0, 7) << "] " << argv[3] << "\n";
        return 0;

    } else if (command == "log") {
        log_history(repo_path);
        return 0;

    } else {
        std::cerr << "Unknown command: " << command << "\n";
        std::cerr << "Available: init, config, add, commit, log\n";
        return 1;
    }
}