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

    auto mtime = fs::last_write_time(filepath).time_since_epoch().count();

    json new_entry = {
        {"path", filepath},
        {"hash", hash},
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