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