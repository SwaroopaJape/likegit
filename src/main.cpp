#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include "../include/likegit/core.hpp"

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: likegit <repo_path> <command> [args...]\n";
        return 1;
    }

    fs::path repo_path = argv[1];
    std::string command = argv[2];

    if (command == "init") {
        if (init_repository(repo_path)) {
            std::cout << "Initialized empty LikeGit repository in " << repo_path << "\n";
            return 0;
        }
        std::cerr << "Failed to initialize repository.\n";
        return 1;

    } else if (command == "config") {
        if (argc < 4) {
            std::cerr << "Usage: likegit <repo_path> config <key> [value]\n";
            std::cerr << "       likegit <repo_path> config --get <key>\n";
            std::cerr << "       likegit <repo_path> config --list\n";
            return 1;
        }

        std::string sub = argv[3];

        if (sub == "--list") {
            for (auto& line : list_config(repo_path))
                std::cout << line << "\n";
            return 0;
        }

        if (sub == "--get") {
            if (argc < 5) { std::cerr << "Usage: likegit <repo_path> config --get <key>\n"; return 1; }
            std::string val = get_config(argv[4], repo_path);
            if (val.empty()) { std::cerr << "Key not found.\n"; return 1; }
            std::cout << val << "\n";
            return 0;
        }

        if (argc < 5) { std::cerr << "Usage: likegit <repo_path> config <key> <value>\n"; return 1; }
        set_config(sub, argv[4], repo_path);
        return 0;

    } else if (command == "add") {
        if (argc < 4) { std::cerr << "Usage: likegit <repo_path> add <file|.>\n"; return 1; }

        std::string target_file = argv[3];
        if (target_file == ".") {
            for (auto& entry : fs::recursive_directory_iterator(repo_path)) {
                if (entry.is_regular_file()) {
                    std::string rel_path = fs::relative(entry.path(), repo_path).string();
                    if (rel_path.find(".likegit") == 0) continue;
                    if (is_ignored(entry.path(), repo_path)) continue;
                    
                    std::ifstream in(entry.path(), std::ios::binary);
                    std::string content((std::istreambuf_iterator<char>(in)), {});
                    std::string hash = hash_object(content, "blob");
                    std::string header = std::string("blob ") + std::to_string(content.size()) + '\0' + content;
                    write_object(hash, header, repo_path);
                    update_index(rel_path, hash, repo_path);
                }
            }
            std::cout << "Added all files\n";
            return 0;
        } else {
            fs::path file_path = repo_path / target_file;
            std::ifstream in(file_path, std::ios::binary);
            if (!in) { std::cerr << "Error: cannot open '" << file_path << "'\n"; return 1; }
            std::string content((std::istreambuf_iterator<char>(in)), {});

            std::string hash = hash_object(content, "blob");
            std::string header = std::string("blob ") + std::to_string(content.size()) + '\0' + content;
            write_object(hash, header, repo_path);
            update_index(target_file, hash, repo_path);

            std::cout << "Added '" << target_file << "' (" << hash << ")\n";
            return 0;
        }

    } else if (command == "commit") {
        if (argc < 5 || std::string(argv[3]) != "-m") {
            std::cerr << "Usage: likegit <repo_path> commit -m \"<message>\"\n";
            return 1;
        }

        std::string name  = get_config("user.name",  repo_path);
        std::string email = get_config("user.email", repo_path);

        if (name.empty() || email.empty()) {
            std::cerr << "Error: author identity unknown.\n";
            std::cerr << "  Run: likegit <repo_path> config user.name \"Your Name\"\n";
            std::cerr << "       likegit <repo_path> config user.email \"you@example.com\"\n";
            return 1;
        }

        auto now = std::chrono::system_clock::now();
        std::string ts = std::to_string(
            std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count());

        std::string hash = create_commit(argv[4], name, email, ts, repo_path);
        std::cout << "[main " << hash.substr(0, 7) << "] " << argv[4] << "\n";
        return 0;

    } else if (command == "log") {
        log_history(repo_path);
        return 0;

    } else if (command == "status") {
        cmd_status(repo_path);
        return 0;

    } else if (command == "diff") {
        cmd_diff(repo_path);
        return 0;

    } else if (command == "branch") {
        if (argc < 4) { std::cerr << "Usage: likegit <repo_path> branch <name>\n"; return 1; }
        if (create_branch(argv[3], repo_path)) {
            std::cout << "Created branch '" << argv[3] << "'\n";
            return 0;
        }
        return 1;

    } else if (command == "checkout") {
        if (argc < 4) {
            std::cerr << "Usage: likegit <repo_path> checkout <branch|hash>\n";
            std::cerr << "       likegit <repo_path> checkout -b <new-branch>\n";
            return 1;
        }

        std::string sub = argv[3];

        // checkout -b <name>  → create + checkout in one step
        if (sub == "-b") {
            if (argc < 5) { std::cerr << "Usage: likegit <repo_path> checkout -b <name>\n"; return 1; }
            std::string new_branch = argv[4];
            if (!create_branch(new_branch, repo_path)) return 1;
            if (!checkout_branch(new_branch, repo_path)) return 1;
            std::cout << "Switched to a new branch '" << new_branch << "'\n";
            return 0;
        }

        // Plain checkout
        if (!checkout_branch(sub, repo_path)) return 1;
        // Detect detached HEAD (target is a 40-char hash, not a branch name)
        fs::path branch_path = repo_path / ".likegit" / "refs" / "heads" / sub;
        if (fs::exists(branch_path))
            std::cout << "Switched to branch '" << sub << "'\n";
        else
            std::cout << "HEAD is now at " << sub.substr(0, 7) << " (detached HEAD)\n";
        return 0;

    } else if (command == "merge") {
        if (argc < 4) { std::cerr << "Usage: likegit <repo_path> merge <branch>\n"; return 1; }
        if (!merge_branch(argv[3], repo_path)) return 1;
        return 0;

    } else {
        std::cerr << "Unknown command: " << command << "\n";
        std::cerr << "Available: init, config, add, commit, log, branch, checkout, merge, status, diff\n";
        return 1;
    }
}