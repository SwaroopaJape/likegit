# include <gtest/gtest.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include "likegit/core.hpp"

namespace fs = std::filesystem;

TEST(InitTests, CreateObjDirectory) {
    fs::path test_prj = fs::temp_directory_path() / "likegit_test_prj";
    fs::remove_all(test_prj);

    bool result = init_repository(test_prj);

    ASSERT_TRUE(result);
    EXPECT_TRUE(fs::exists(test_prj / ".likegit" / "objects"));
    EXPECT_TRUE(fs::exists(test_prj / ".likegit" / "refs" / "heads"));
    EXPECT_TRUE(fs::exists(test_prj / ".likegit" / "HEAD"));
    
    fs::remove_all(test_prj);
}

TEST(InitTests, HeadFileContentCheck) {
    fs::path test_prj = fs::temp_directory_path() / "likegit_head_test";
    fs::remove_all(test_prj);

    init_repository(test_prj);

    std::ifstream head_file(test_prj / ".likegit" / "HEAD");
    
    std::string content;
    std::getline(head_file, content);

    ASSERT_EQ(content, "ref: refs/heads/main");

    fs::remove_all(test_prj);
}

TEST(HashTests, CheckHashing) {
    std::string input = std::string("blob 12") + '\0' + "Hello World!";
    std::string hash = hash_object("Hello World!", "blob");
    EXPECT_EQ(generate_sha1(input), "c57eff55ebc0c54973903af5f72bac72762cf4f4");
    EXPECT_EQ(hash, "c57eff55ebc0c54973903af5f72bac72762cf4f4");
}

TEST(HashTests, WritesObjectToDisk) {
    fs::path test_prj = fs::temp_directory_path() / "likegit_write_test";
    fs::remove_all(test_prj);
    init_repository(test_prj);

    std::string content = "Hello World!";
    std::string type = "blob";
    std::string header = type + " " + std::to_string(content.size());
    header += '\0';
    header += content;

    std::string hash = generate_sha1(header);
    write_object(hash, header, test_prj);

    fs::path expected = test_prj / ".likegit" / "objects" / hash.substr(0,2) / hash.substr(2);
    EXPECT_TRUE(fs::exists(expected));

    fs::remove_all(test_prj);
}

TEST(IndexTests, AddsEntryToIndex) {
    fs::path test_prj = fs::temp_directory_path() / "likegit_index_test";
    fs::remove_all(test_prj);
    init_repository(test_prj);

    update_index("README.md", "abc123", test_prj);

    fs::path index_path = test_prj / ".likegit" / "index.json";
    ASSERT_TRUE(fs::exists(index_path));

    std::ifstream f(index_path);
    nlohmann::json data;
    f >> data;

    ASSERT_EQ(data["entries"].size(), 1u);
    EXPECT_EQ(data["entries"][0]["path"], "README.md");
    EXPECT_EQ(data["entries"][0]["hash"], "abc123");

    fs::remove_all(test_prj);
}

TEST(IndexTests, NoDuplicatesOnDoubleAdd) {
    fs::path test_prj = fs::temp_directory_path() / "likegit_dedup_test";
    fs::remove_all(test_prj);
    init_repository(test_prj);

    // Add the same path twice — second call simulates a file edit with a new hash
    update_index("README.md", "oldhash111", test_prj);
    update_index("README.md", "newhash222", test_prj);

    fs::path index_path = test_prj / ".likegit" / "index.json";
    std::ifstream f(index_path);
    nlohmann::json data;
    f >> data;

    // Must still be exactly ONE entry, not two
    ASSERT_EQ(data["entries"].size(), 1u);
    // And it must reflect the latest hash
    EXPECT_EQ(data["entries"][0]["hash"], "newhash222");

    fs::remove_all(test_prj);
}