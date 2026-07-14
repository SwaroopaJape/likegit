#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <queue>
#include <unordered_set>
#include <map>
#include <set>
#include <stdexcept>
#include <zlib.h>
#include "likegit/sha1.hpp"
#include "likegit/diff.hpp"
#include "likegit/core.hpp"
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

    // Resolve HEAD → find the current branch ref path
    fs::path head_path = repo_path / ".likegit" / "HEAD";
    std::ifstream head_in(head_path);
    std::string head_content;
    std::getline(head_in, head_content);
    head_in.close();

    const std::string ref_prefix = "ref: ";
    fs::path ref_path;
    if (head_content.substr(0, ref_prefix.size()) == ref_prefix) {
        // Attached HEAD → e.g. "ref: refs/heads/feature-x"
        ref_path = repo_path / ".likegit" / head_content.substr(ref_prefix.size());
    } else {
        // Detached HEAD — we write nothing (the hash in HEAD is the commit itself)
        ref_path = "";
    }

    std::string parent_hash;
    if (!ref_path.empty()) {
        std::ifstream ref_in(ref_path);
        if (ref_in.is_open()) std::getline(ref_in, parent_hash);
    }

    std::string author_line = author_name + " <" + author_email + "> " + timestamp;

    std::string commit_content;
    commit_content += "tree " + tree_hash + "\n";
    if (!parent_hash.empty()) commit_content += "parent " + parent_hash + "\n";
    commit_content += "author "    + author_line + "\n";
    commit_content += "committer " + author_line + "\n";
    commit_content += "\n";
    commit_content += message + "\n";

    std::string header = "commit " + std::to_string(commit_content.size());
    header += '\0';
    header += commit_content;

    std::string commit_hash = generate_sha1(header);
    write_object(commit_hash, header, repo_path);

    // Update the current branch ref (or HEAD directly if detached)
    if (!ref_path.empty()) {
        fs::create_directories(ref_path.parent_path());
        std::ofstream ref_out(ref_path);
        ref_out << commit_hash;
    } else {
        std::ofstream head_out(head_path);
        head_out << commit_hash << "\n";
    }

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

// ── Helpers shared by branch/checkout ────────────────────────────────────────

// Resolve HEAD to a commit hash. Returns the hash and sets branch_name if
// HEAD points to a branch (empty branch_name → detached HEAD).
static std::string resolve_head(const fs::path& repo_path,
                                std::string* branch_name = nullptr) {
    fs::path head_path = repo_path / ".likegit" / "HEAD";
    std::ifstream head_in(head_path);
    if (!head_in.is_open()) return "";
    std::string head_content;
    std::getline(head_in, head_content);

    const std::string prefix = "ref: refs/heads/";
    if (head_content.substr(0, prefix.size()) == prefix) {
        std::string branch = head_content.substr(prefix.size());
        if (branch_name) *branch_name = branch;
        fs::path ref_path = repo_path / ".likegit" / "refs" / "heads" / branch;
        std::ifstream ref_in(ref_path);
        if (!ref_in.is_open()) return "";
        std::string hash;
        std::getline(ref_in, hash);
        return hash;
    }

    // Detached HEAD — content is the hash itself
    if (branch_name) *branch_name = "";
    return head_content;
}

// ── Branch creation ───────────────────────────────────────────────────────────

bool create_branch(const std::string& name, const fs::path& repo_path) {
    std::string current_hash = resolve_head(repo_path);
    if (current_hash.empty()) {
        std::cerr << "fatal: not a valid object name: 'HEAD'\n";
        return false;
    }

    fs::path branch_path = repo_path / ".likegit" / "refs" / "heads" / name;
    if (fs::exists(branch_path)) {
        std::cerr << "fatal: a branch named '" << name << "' already exists\n";
        return false;
    }

    std::ofstream out(branch_path);
    if (!out.is_open()) return false;
    out << current_hash;
    return true;
}

// ── Merge branch ──────────────────────────────────────────────────────────────

struct CommitInfo {
    std::string tree_hash;
    std::vector<std::string> parents;
};

static CommitInfo parse_commit(const std::string& commit_hash, const fs::path& repo_path) {
    CommitInfo info;
    std::string raw = read_object(commit_hash, repo_path);
    if (raw.empty()) return info;
    std::string_view view(raw);
    auto null_pos = view.find('\0');
    if (null_pos != std::string_view::npos) view.remove_prefix(null_pos + 1);

    auto body_start = view.find("\n\n");
    std::string_view headers = view.substr(0, body_start);
    size_t pos = 0;
    while (pos < headers.size()) {
        auto end_line = headers.find('\n', pos);
        if (end_line == std::string_view::npos) end_line = headers.size();
        std::string_view line = headers.substr(pos, end_line - pos);
        if (line.substr(0, 5) == "tree ") info.tree_hash = std::string(line.substr(5));
        else if (line.substr(0, 7) == "parent ") info.parents.push_back(std::string(line.substr(7)));
        pos = end_line + 1;
    }
    return info;
}

static std::string find_lca(const std::string& commit1, const std::string& commit2, const fs::path& repo_path) {
    if (commit1 == commit2) return commit1;
    
    std::queue<std::string> q1, q2;
    std::unordered_set<std::string> visited1, visited2;
    
    q1.push(commit1);
    q2.push(commit2);
    
    while (!q1.empty() || !q2.empty()) {
        if (!q1.empty()) {
            std::string curr = q1.front();
            q1.pop();
            visited1.insert(curr);
            if (visited2.count(curr)) return curr;
            
            CommitInfo info = parse_commit(curr, repo_path);
            for (const auto& p : info.parents) q1.push(p);
        }
        
        if (!q2.empty()) {
            std::string curr = q2.front();
            q2.pop();
            visited2.insert(curr);
            if (visited1.count(curr)) return curr;
            
            CommitInfo info = parse_commit(curr, repo_path);
            for (const auto& p : info.parents) q2.push(p);
        }
    }
    return ""; // No common ancestor found
}

static std::map<std::string, std::string> get_tree_files(const std::string& tree_hash, const fs::path& repo_path) {
    std::map<std::string, std::string> files;
    if (tree_hash.empty()) return files;
    std::string raw_tree = read_object(tree_hash, repo_path);
    if (raw_tree.empty()) return files;
    
    std::string_view tree_view(raw_tree);
    auto tree_null = tree_view.find('\0');
    if (tree_null != std::string_view::npos) tree_view.remove_prefix(tree_null + 1);
    
    while (!tree_view.empty()) {
        auto nl = tree_view.find('\n');
        std::string_view tree_line = tree_view.substr(0, nl);
        if (!tree_line.empty()) {
            auto s1 = tree_line.find(' ');
            auto s2 = tree_line.find(' ', s1 + 1);
            auto s3 = tree_line.find(' ', s2 + 1);
            if (s1 != std::string_view::npos && s3 != std::string_view::npos) {
                std::string blob_hash = std::string(tree_line.substr(s2 + 1, s3 - s2 - 1));
                std::string file_path = std::string(tree_line.substr(s3 + 1));
                files[file_path] = blob_hash;
            }
        }
        if (nl == std::string_view::npos) break;
        tree_view.remove_prefix(nl + 1);
    }
    return files;
}

static void remove_from_index(const std::string& filepath, const fs::path& repo_path) {
    fs::path index_path = repo_path / ".likegit" / "index.json";
    std::ifstream file_in(index_path);
    if (!file_in.is_open()) return;
    json index_data;
    file_in >> index_data;
    file_in.close();
    
    json new_entries = json::array();
    for (auto& entry : index_data["entries"]) {
        if (entry["path"] != filepath) {
            new_entries.push_back(entry);
        }
    }
    index_data["entries"] = new_entries;
    std::ofstream file_out(index_path);
    file_out << index_data.dump(4);
}

bool merge_branch(const std::string& target_branch, const fs::path& repo_path) {
    if (has_uncommitted_changes(repo_path)) {
        std::cerr << "error: Your local changes would be overwritten by merge.\n";
        std::cerr << "Please commit your changes before merging.\n";
        return false;
    }

    std::string head_branch;
    std::string head_commit = resolve_head(repo_path, &head_branch);
    if (head_commit.empty()) {
        std::cerr << "error: HEAD is empty\n";
        return false;
    }
    
    fs::path branch_path = repo_path / ".likegit" / "refs" / "heads" / target_branch;
    if (!fs::exists(branch_path)) {
        std::cerr << "error: branch '" << target_branch << "' not found\n";
        return false;
    }
    std::ifstream ref_in(branch_path);
    std::string target_commit;
    std::getline(ref_in, target_commit);
    
    if (head_commit == target_commit) {
        std::cout << "Already up to date.\n";
        return true;
    }
    
    std::string lca_commit = find_lca(head_commit, target_commit, repo_path);
    if (lca_commit.empty()) {
        std::cerr << "fatal: refusing to merge unrelated histories\n";
        return false;
    }
    
    CommitInfo head_info = parse_commit(head_commit, repo_path);
    CommitInfo target_info = parse_commit(target_commit, repo_path);
    CommitInfo lca_info = parse_commit(lca_commit, repo_path);
    
    auto head_files = get_tree_files(head_info.tree_hash, repo_path);
    auto target_files = get_tree_files(target_info.tree_hash, repo_path);
    auto base_files = get_tree_files(lca_info.tree_hash, repo_path);
    
    std::set<std::string> all_files;
    for (const auto& [p, h] : head_files) all_files.insert(p);
    for (const auto& [p, h] : target_files) all_files.insert(p);
    for (const auto& [p, h] : base_files) all_files.insert(p);
    
    std::vector<std::string> conflicts;
    
    for (const auto& path : all_files) {
        std::string h_base = base_files.count(path) ? base_files[path] : "";
        std::string h_x = head_files.count(path) ? head_files[path] : "";
        std::string h_y = target_files.count(path) ? target_files[path] : "";
        
        if (h_x == h_y) {
            // Keep h_x (already in HEAD)
        } else if (h_base == h_x && h_base != h_y) {
            // Keep h_y
            if (h_y.empty()) {
                fs::remove(repo_path / path);
                remove_from_index(path, repo_path);
            } else {
                std::string raw_blob = read_object(h_y, repo_path);
                auto blob_null = raw_blob.find('\0');
                std::string content = raw_blob.substr(blob_null + 1);
                fs::create_directories((repo_path / path).parent_path());
                std::ofstream out(repo_path / path, std::ios::binary);
                out << content;
                out.close();
                update_index(path, h_y, repo_path);
            }
        } else if (h_base == h_y && h_base != h_x) {
            // Keep h_x (already in HEAD)
        } else {
            // CONFLICT
            conflicts.push_back(path);
            
            std::string content_x;
            if (!h_x.empty()) {
                std::string raw_x = read_object(h_x, repo_path);
                content_x = raw_x.substr(raw_x.find('\0') + 1);
            }
            
            std::string content_y;
            if (!h_y.empty()) {
                std::string raw_y = read_object(h_y, repo_path);
                content_y = raw_y.substr(raw_y.find('\0') + 1);
            }
            
            fs::create_directories((repo_path / path).parent_path());
            std::ofstream out(repo_path / path, std::ios::binary);
            out << "<<<<<<< HEAD\n" << content_x;
            if (!content_x.empty() && content_x.back() != '\n') out << "\n";
            out << "=======\n" << content_y;
            if (!content_y.empty() && content_y.back() != '\n') out << "\n";
            out << ">>>>>>> " << target_branch << "\n";
            out.close();
        }
    }
    
    if (!conflicts.empty()) {
        std::cout << "Merge conflicts in:\n";
        for (const auto& c : conflicts) {
            std::cout << "  " << c << "\n";
        }
        std::cout << "Automatic merge failed; fix conflicts and then commit the result.\n";
        return false;
    }
    
    std::cout << "Merge successful. Files updated in working directory. Please commit to finalize.\n";
    return true;
}

// ── Safety check ──────────────────────────────────────────────────────────────

bool has_uncommitted_changes(const fs::path& repo_path) {
    fs::path index_path = repo_path / ".likegit" / "index.json";
    std::ifstream f(index_path);
    if (!f.is_open()) return false;

    json index_data;
    f >> index_data;

    for (auto& entry : index_data["entries"]) {
        std::string path  = entry["path"];
        std::string stored_hash = entry["hash"];

        // Read the actual file from the working directory
        std::ifstream file_in(path, std::ios::binary);
        if (!file_in.is_open()) return true; // file deleted but tracked → dirty

        std::string content((std::istreambuf_iterator<char>(file_in)), {});
        std::string current_hash = hash_object(content, "blob");

        if (current_hash != stored_hash) return true;
    }

    return false;
}

// ── Checkout ──────────────────────────────────────────────────────────────────

bool checkout_branch(const std::string& target, const fs::path& repo_path) {
    // 1. Safety check — refuse if the working directory is dirty
    if (has_uncommitted_changes(repo_path)) {
        std::cerr << "error: Your local changes would be overwritten by checkout.\n";
        std::cerr << "Please commit your changes before switching branches.\n";
        return false;
    }

    // 2. Resolve target: is it a branch name or a raw commit hash?
    fs::path branch_path = repo_path / ".likegit" / "refs" / "heads" / target;
    std::string commit_hash;
    bool is_branch = fs::exists(branch_path);

    if (is_branch) {
        std::ifstream ref_in(branch_path);
        std::getline(ref_in, commit_hash);
    } else {
        // Treat target as a raw commit hash (Detached HEAD)
        commit_hash = target;
    }

    if (commit_hash.empty() || commit_hash.size() != 40) {
        std::cerr << "error: pathspec '" << target << "' did not match any branch or commit\n";
        return false;
    }

    // 3. Read and parse the commit object to find its tree hash
    std::string raw_commit = read_object(commit_hash, repo_path);
    if (raw_commit.empty()) {
        std::cerr << "error: object " << commit_hash << " not found\n";
        return false;
    }

    std::string_view view(raw_commit);
    auto null_pos = view.find('\0');
    if (null_pos == std::string_view::npos) return false;
    view.remove_prefix(null_pos + 1);

    std::string tree_hash;
    auto body_start = view.find("\n\n");
    std::string_view headers = view.substr(0, body_start);
    size_t pos = 0;
    while (pos < headers.size()) {
        auto end_line = headers.find('\n', pos);
        if (end_line == std::string_view::npos) end_line = headers.size();
        std::string_view line = headers.substr(pos, end_line - pos);
        if (line.substr(0, 5) == "tree ") { tree_hash = std::string(line.substr(5)); break; }
        pos = end_line + 1;
    }

    if (tree_hash.empty()) { std::cerr << "error: malformed commit object\n"; return false; }

    // 4. Wipe tracked files from the working directory
    fs::path index_path = repo_path / ".likegit" / "index.json";
    {
        std::ifstream f(index_path);
        if (f.is_open()) {
            json index_data;
            f >> index_data;
            for (auto& entry : index_data["entries"]) {
                fs::path file_path = repo_path / entry["path"].get<std::string>();
                if (fs::exists(file_path)) fs::remove(file_path);
            }
        }
    }

    // 5. Restore files from the target tree
    std::string raw_tree = read_object(tree_hash, repo_path);
    std::string_view tree_view(raw_tree);
    auto tree_null = tree_view.find('\0');
    if (tree_null == std::string_view::npos) return false;
    tree_view.remove_prefix(tree_null + 1);

    json new_index;
    new_index["entries"] = json::array();

    // Tree payload: "<mode> blob <hash> <path>\n" per line
    while (!tree_view.empty()) {
        auto nl = tree_view.find('\n');
        std::string_view tree_line = tree_view.substr(0, nl);
        if (!tree_line.empty()) {
            // Parse: "<mode> blob <hash> <path>"
            auto s1 = tree_line.find(' ');
            auto s2 = tree_line.find(' ', s1 + 1);
            auto s3 = tree_line.find(' ', s2 + 1);
            if (s1 != std::string_view::npos && s3 != std::string_view::npos) {
                std::string mode = std::string(tree_line.substr(0, s1));
                std::string blob_hash = std::string(tree_line.substr(s2 + 1, s3 - s2 - 1));
                std::string file_path = std::string(tree_line.substr(s3 + 1));

                std::string raw_blob = read_object(blob_hash, repo_path);
                // Skip the "blob <size>\0" header
                auto blob_null = raw_blob.find('\0');
                std::string file_content = raw_blob.substr(blob_null + 1);

                // Create parent dirs if needed
                fs::path dest = repo_path / file_path;
                fs::create_directories(dest.parent_path());
                std::ofstream out(dest, std::ios::binary);
                out << file_content;

                auto mtime = fs::last_write_time(dest).time_since_epoch().count();
                new_index["entries"].push_back({{"path", file_path}, {"hash", blob_hash},
                                                {"mode", mode}, {"mtime", mtime}});
            }
        }
        if (nl == std::string_view::npos) break;
        tree_view.remove_prefix(nl + 1);
    }

    // 6. Write new index
    std::ofstream idx_out(index_path);
    idx_out << new_index.dump(4);

    // 7. Update HEAD
    fs::path head_path = repo_path / ".likegit" / "HEAD";
    std::ofstream head_out(head_path);
    if (is_branch)
        head_out << "ref: refs/heads/" << target << "\n";
    else
        head_out << commit_hash << "\n"; // Detached HEAD

    return true;
}