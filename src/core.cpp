#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <stdexcept>
#include <zlib.h>
#include "likegit/sha1.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace fs = std::filesystem;

bool init_repository(const fs::path& repo_path) {
    fs::path likegit_dir = repo_path / ".likegit";
    try {
        fs::create_directories(likegit_dir / "objects");
        fs::create_directories(likegit_dir / "refs" / "heads");

        std::ofstream head_file(likegit_dir / "HEAD");
        if (!head_file.is_open()) return false;

        head_file << "ref: refs/heads/main\n";
        head_file.close();

        return true;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error: " << e.what() << '\n';
        return false;
    }
}

std::string generate_sha1(const std::string& input_string) {
    return manual_sha1(input_string);
}

std::string compress_data(const std::string& data) {
    z_stream strm = {};
    if (deflateInit(&strm, Z_BEST_COMPRESSION) != Z_OK) {
        throw std::runtime_error("Failed to initialize zlib");
    }
    strm.next_in = (Bytef*)data.data();
    strm.avail_in = data.size();
    std::vector<char> out_buffer(data.size() + 1024);
    strm.next_out = (Bytef*)out_buffer.data();
    strm.avail_out = out_buffer.size();
    deflate(&strm, Z_FINISH);
    deflateEnd(&strm);
    return std::string(out_buffer.data(), strm.total_out);
}

std::string hash_object(const std::string& content, const std::string& type) {
    std::string header = type + " " + std::to_string(content.size());
    header += '\0';
    header += content;
    return generate_sha1(header);
}

void write_object(const std::string& hash, const std::string& data, const fs::path& repo_path) {
    fs::path object_path = repo_path / ".likegit" / "objects";
    std::string dir_name  = hash.substr(0, 2);
    std::string file_name = hash.substr(2);
    fs::path dir_path =object_path / dir_name;
    fs::create_directories(dir_path);
    fs::path file_path = dir_path / file_name;
    std::string compressed_data = compress_data(data);
    std::ofstream object_file(file_path);
    object_file << compressed_data;
    object_file.close();
    return;

}



void update_index(const std::string& filepath, const std::string& hash, const fs::path& repo_path) {
    fs::path index_path = repo_path / ".likegit" / "index.json";
    json index_data;

    std::ifstream file_in(index_path);
    if (file_in.is_open()) {
        file_in >> index_data;
        file_in.close();
    } else {
        index_data["entries"] = json::array();
    }

    // Detect the file mode from actual filesystem permissions
    std::string mode;
    if (fs::is_symlink(filepath)) {
        mode = "120000";  // symbolic link
    } else {
        auto perms = fs::status(filepath).permissions();
        bool executable = (perms & fs::perms::owner_exec) != fs::perms::none;
        mode = executable ? "100755" : "100644";
    }

    auto mtime = fs::last_write_time(filepath).time_since_epoch().count();

    json new_entry = {
        {"path", filepath},
        {"hash", hash},
        {"mode", mode},
        {"mtime", mtime}
    };

    bool found = false;
    for (auto& entry : index_data["entries"]) {
        if (entry["path"] == filepath) {
            entry = new_entry;
            found = true;
            break;
        }
    }
    if (!found) {
        index_data["entries"].push_back(new_entry);
    }

    std::ofstream file_out(index_path);
    file_out << index_data.dump(4);
    file_out.close();
}

std::string generate_tree(const fs::path& repo_path) {
    fs::path index_path = repo_path / ".likegit" / "index.json";
    std::ifstream f(index_path);
    json index_data;
    f >> index_data;

    std::string payload;
    for (auto& entry : index_data["entries"]) {
        std::string path = entry["path"];
        std::string hash = entry["hash"];
        std::string mode = entry.value("mode", "100644");
        payload += mode + " blob " + hash + " " + path + "\n";
    }

    std::string header = "tree " + std::to_string(payload.size());
    header += '\0';
    header += payload;
    std::string tree_hash = generate_sha1(header);
    write_object(tree_hash, header, repo_path);
    return tree_hash;
}

std::string create_commit(const std::string& message, const std::string& author_name, const std::string& author_email, const std::string& timestamp, const fs::path& repo_path) {
    std::string tree_hash = generate_tree(repo_path);
    fs::path ref_path = repo_path / ".likegit" / "refs" / "heads" / "main";
    std::string parent_hash;
    std::ifstream ref_in(ref_path);
    if (ref_in.is_open()) {
        std::getline(ref_in, parent_hash);
        ref_in.close();
    }

    std::string author_line = author_name + " <" + author_email + "> " + timestamp;

    std::string commit_content;
    commit_content += "tree " + tree_hash + "\n";
    if (!parent_hash.empty()) {
        commit_content += "parent " + parent_hash + "\n";
    }
    commit_content += "author " + author_line + "\n";
    commit_content += "committer " + author_line + "\n";
    commit_content += "\n";
    commit_content += message + "\n";

    std::string header = "commit " + std::to_string(commit_content.size());
    header += '\0';
    header += commit_content;

    std::string commit_hash = generate_sha1(header);
    write_object(commit_hash, header, repo_path);

    std::ofstream ref_out(ref_path);
    ref_out << commit_hash;
    ref_out.close();

    return commit_hash;
}