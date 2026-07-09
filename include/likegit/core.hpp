#pragma once

#include <filesystem>

namespace fs = std::filesystem;

bool init_repository(const fs::path& repo_path);
std::string generate_sha1(const std::string& input_string);
std::string compress_data(const std::string& data);
std::string hash_object(const std::string& content, const std::string& type);
void write_object(const std::string& hash, const std::string& data, const fs::path& repo_path);
void update_index(const std::string& filepath, const std::string& hash, const fs::path& repo_path);
