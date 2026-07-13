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

    std::string mode;
    if (fs::is_symlink(filepath)) {
        mode = "120000";
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

static fs::path config_path(const fs::path& repo_path) {
    return repo_path / ".likegit" / "config.json";
}

static json read_config(const fs::path& repo_path) {
    json cfg;
    std::ifstream f(config_path(repo_path));
    if (f.is_open()) f >> cfg;
    return cfg;
}

static void write_config(const json& cfg, const fs::path& repo_path) {
    std::ofstream f(config_path(repo_path));
    f << cfg.dump(4);
}

void set_config(const std::string& key, const std::string& value, const fs::path& repo_path) {
    auto dot = key.find('.');
    if (dot == std::string::npos)
        throw std::invalid_argument("Config key must be in 'section.name' format");
    std::string section = key.substr(0, dot);
    std::string name    = key.substr(dot + 1);
    json cfg = read_config(repo_path);
    cfg[section][name] = value;
    write_config(cfg, repo_path);
}

std::string get_config(const std::string& key, const fs::path& repo_path) {
    auto dot = key.find('.');
    if (dot == std::string::npos)
        throw std::invalid_argument("Config key must be in 'section.name' format");
    std::string section = key.substr(0, dot);
    std::string name    = key.substr(dot + 1);
    json cfg = read_config(repo_path);
    if (!cfg.contains(section) || !cfg[section].contains(name))
        return "";
    return cfg[section][name].get<std::string>();
}

std::vector<std::string> list_config(const fs::path& repo_path) {
    std::vector<std::string> lines;
    json cfg = read_config(repo_path);
    for (auto& [section, entries] : cfg.items())
        for (auto& [name, val] : entries.items())
            lines.push_back(section + "." + name + "=" + val.get<std::string>());
    return lines;
}


std::string read_object(const std::string& hash, const fs::path& repo_path) {
    if (hash.size() != 40) return "";
    fs::path obj_path = repo_path / ".likegit" / "objects" / hash.substr(0, 2) / hash.substr(2);
    if (!fs::exists(obj_path)) return "";

    std::ifstream obj_in(obj_path, std::ios::binary);
    std::string compressed((std::istreambuf_iterator<char>(obj_in)), {});

    // Inflate
    std::string decompressed(compressed.size() * 4, '\0'); 
    uLongf dest_len = decompressed.size();
    while (true) {
        int res = uncompress(reinterpret_cast<Bytef*>(decompressed.data()), &dest_len,
                             reinterpret_cast<const Bytef*>(compressed.data()), compressed.size());
        if (res == Z_OK) {
            decompressed.resize(dest_len);
            break;
        } else if (res == Z_BUF_ERROR) {
            decompressed.resize(decompressed.size() * 2);
            dest_len = decompressed.size();
        } else {
            return ""; // Failure
        }
    }

    return decompressed;
}

void log_history(const fs::path& repo_path) {
    fs::path ref_path = repo_path / ".likegit" / "refs" / "heads" / "main";
    std::string current_hash;
    std::ifstream ref_in(ref_path);
    if (ref_in.is_open()) {
        std::getline(ref_in, current_hash);
        ref_in.close();
    }

    if (current_hash.empty()) {
        std::cerr << "fatal: your current branch 'main' does not have any commits yet\n";
        return;
    }

    while (!current_hash.empty()) {
        std::string raw_obj = read_object(current_hash, repo_path);
        if (raw_obj.empty()) {
            std::cerr << "fatal: invalid object " << current_hash << "\n";
            return;
        }

        std::string_view view(raw_obj);
        
        auto null_pos = view.find('\0');
        if (null_pos == std::string_view::npos) return;
        view.remove_prefix(null_pos + 1);

        std::string_view parent_hash;
        std::string_view author_line;
        std::string_view commit_message;

        auto body_start = view.find("\n\n");
        if (body_start != std::string_view::npos) {
            commit_message = view.substr(body_start + 2);
            std::string_view headers = view.substr(0, body_start);

            size_t pos = 0;
            while (pos < headers.size()) {
                auto end_line = headers.find('\n', pos);
                if (end_line == std::string_view::npos) end_line = headers.size();
                std::string_view line = headers.substr(pos, end_line - pos);

                if (line.substr(0, 7) == "parent ") {
                    parent_hash = line.substr(7);
                } else if (line.substr(0, 7) == "author ") {
                    author_line = line.substr(7);
                }
                
                pos = end_line + 1;
            }
        }

        std::cout << "\033[33mcommit " << current_hash << "\033[0m\n";
        
        if (!author_line.empty()) {
            // Split "Swaroop Jape <swaroop@example.com> 1690020000"
            auto time_pos = author_line.find_last_of("> ");
            if (time_pos != std::string_view::npos) {
                std::string_view name_email = author_line.substr(0, time_pos);
                std::string_view timestamp_str = author_line.substr(time_pos + 1);
                
                try {
                    time_t ts = std::stoll(std::string(timestamp_str));
                    char time_buf[64];
                    std::strftime(time_buf, sizeof(time_buf), "%a %b %d %H:%M:%S %Y %z", std::localtime(&ts));
                    std::cout << "Author: " << name_email << "\n";
                    std::cout << "Date:   " << time_buf << "\n";
                } catch (...) {
                    std::cout << "Author: " << author_line << "\n";
                }
            } else {
                std::cout << "Author: " << author_line << "\n";
            }
        }
        
        std::cout << "\n    " << commit_message << "\n\n";

        current_hash = std::string(parent_hash);
    }
}