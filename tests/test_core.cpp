# include <gtest/gtest.h>
#include <fstream>
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