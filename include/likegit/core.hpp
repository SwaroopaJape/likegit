#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <string_view>
#include "likegit/diff.hpp"

namespace fs = std::filesystem;

bool init_repository(const fs::path& repo_path);
std::string generate_sha1(const std::string& input_string);
std::string compress_data(const std::string& data);
std::string hash_object(const std::string& content, const std::string& type);
std::string read_object(const std::string& hash, const fs::path& repo_path);
void write_object(const std::string& hash, const std::string& data, const fs::path& repo_path);
void update_index(const std::string& filepath, const std::string& hash, const fs::path& repo_path);
std::string generate_tree(const fs::path& repo_path);
std::string create_commit(const std::string& message, const std::string& author_name, const std::string& author_email, const std::string& timestamp, const fs::path& repo_path);
void set_config(const std::string& key, const std::string& value, const fs::path& repo_path);
std::string get_config(const std::string& key, const fs::path& repo_path);
std::vector<std::string> list_config(const fs::path& repo_path);
void log_history(const fs::path& repo_path);
bool create_branch(const std::string& name, const fs::path& repo_path);
bool has_uncommitted_changes(const fs::path& repo_path);
bool checkout_branch(const std::string& target, const fs::path& repo_path);
bool merge_branch(const std::string& target_branch, const fs::path& repo_path);

bool is_ignored(const fs::path& filepath, const fs::path& repo_path);
void cmd_status(const fs::path& repo_path);
void cmd_diff(const fs::path& repo_path);
