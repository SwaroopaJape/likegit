# include <gtest/gtest.h>
#include <fstream>
#include <zlib.h>
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
    fs::path tests_dir = fs::path(__FILE__).parent_path();
    fs::path test_prj  = tests_dir / "likegit_index_test";
    fs::remove_all(test_prj);
    init_repository(test_prj);

    fs::path file_path = test_prj / "README.md";
    std::string file_content = "# My Project\nHello, LikeGit!\n";
    {
        std::ofstream out(file_path);
        out << file_content;
    }

    std::string hash   = hash_object(file_content, "blob");
    std::string header = "blob " + std::to_string(file_content.size()) + '\0' + file_content;
    write_object(hash, header, test_prj);

    update_index(file_path.string(), hash, test_prj);

    fs::path index_path = test_prj / ".likegit" / "index.json";
    ASSERT_TRUE(fs::exists(index_path));

    std::ifstream f(index_path);
    nlohmann::json data;
    f >> data;

    ASSERT_EQ(data["entries"].size(), 1u);
    EXPECT_EQ(data["entries"][0]["path"], file_path.string());
    EXPECT_EQ(data["entries"][0]["hash"], hash);

    fs::remove_all(test_prj);
}

TEST(IndexTests, NoDuplicatesOnDoubleAdd) {
    fs::path tests_dir = fs::path(__FILE__).parent_path();
    fs::path test_prj  = tests_dir / "likegit_dedup_test";
    fs::remove_all(test_prj);
    init_repository(test_prj);

    fs::path file_path    = test_prj / "hello.txt";
    std::string content_v1 = "Hello, World!\n";
    {
        std::ofstream out(file_path);
        out << content_v1;
    }

    std::string hash_v1   = hash_object(content_v1, "blob");
    std::string header_v1 = "blob " + std::to_string(content_v1.size()) + '\0' + content_v1;
    write_object(hash_v1, header_v1, test_prj);
    update_index(file_path.string(), hash_v1, test_prj);

    update_index(file_path.string(), hash_v1, test_prj);

    {
        fs::path index_path = test_prj / ".likegit" / "index.json";
        std::ifstream f(index_path);
        nlohmann::json data;
        f >> data;
        ASSERT_EQ(data["entries"].size(), 1u) << "Duplicate entry was created!";
        EXPECT_EQ(data["entries"][0]["hash"], hash_v1);
    }

    std::string content_v2 = "Hello, LikeGit! (edited)\n";
    {
        std::ofstream out(file_path);   
        out << content_v2;
    }

    std::string hash_v2   = hash_object(content_v2, "blob");
    std::string header_v2 = "blob " + std::to_string(content_v2.size()) + '\0' + content_v2;
    write_object(hash_v2, header_v2, test_prj);
    update_index(file_path.string(), hash_v2, test_prj);

    {
        fs::path index_path = test_prj / ".likegit" / "index.json";
        std::ifstream f(index_path);
        nlohmann::json data;
        f >> data;
        ASSERT_EQ(data["entries"].size(), 1u) << "Entry count changed after file edit!";
        EXPECT_EQ(data["entries"][0]["hash"], hash_v2) << "Hash not updated after file edit!";
    }

    fs::remove_all(test_prj);
}

TEST(CommitTests, TreeObjectCreated) {
    fs::path test_prj = fs::temp_directory_path() / "likegit_tree_test";
    fs::remove_all(test_prj);
    init_repository(test_prj);

    std::string content = "Hello, World!\n";
    fs::path file_path = test_prj / "hello.txt";
    std::ofstream out1(file_path);
    out1 << content;
    out1.close();
    std::string blob_hash = hash_object(content, "blob");
    std::string blob_header = std::string("blob ") + std::to_string(content.size()) + '\0' + content;
    write_object(blob_hash, blob_header, test_prj);
    update_index(file_path.string(), blob_hash, test_prj);

    {
        nlohmann::json data;
        std::ifstream f(test_prj / ".likegit" / "index.json");
        f >> data;
        EXPECT_EQ(data["entries"][0]["mode"], "100644");
    }

    fs::permissions(file_path, fs::perms::owner_exec, fs::perm_options::add);
    update_index(file_path.string(), blob_hash, test_prj);

    {
        nlohmann::json data;
        std::ifstream f(test_prj / ".likegit" / "index.json");
        f >> data;
        EXPECT_EQ(data["entries"][0]["mode"], "100755");
    }

    std::string tree_hash = generate_tree(test_prj);
    ASSERT_EQ(tree_hash.size(), 40u);
    fs::path obj = test_prj / ".likegit" / "objects" / tree_hash.substr(0, 2) / tree_hash.substr(2);
    EXPECT_TRUE(fs::exists(obj));

    fs::remove_all(test_prj);
}

TEST(CommitTests, CommitUpdatesRef) {
    fs::path test_prj = fs::temp_directory_path() / "likegit_commit_test";
    fs::remove_all(test_prj);
    init_repository(test_prj);

    std::string content = "# My Project\n";
    fs::path file_path = test_prj / "README.md";
    std::ofstream out2(file_path);
    out2 << content;
    out2.close();
    std::string blob_hash = hash_object(content, "blob");
    std::string blob_header = std::string("blob ") + std::to_string(content.size()) + '\0' + content;
    write_object(blob_hash, blob_header, test_prj);
    update_index(file_path.string(), blob_hash, test_prj);

    std::string commit_hash = create_commit("Initial commit", "LikeGit User", "user@likegit.com", "1690020000", test_prj);

    ASSERT_EQ(commit_hash.size(), 40u);

    fs::path ref_path = test_prj / ".likegit" / "refs" / "heads" / "main";
    std::ifstream ref_in(ref_path);
    std::string stored_hash;
    std::getline(ref_in, stored_hash);
    EXPECT_EQ(stored_hash, commit_hash);

    fs::path commit_obj = test_prj / ".likegit" / "objects"
                        / commit_hash.substr(0, 2) / commit_hash.substr(2);
    EXPECT_TRUE(fs::exists(commit_obj));

    fs::remove_all(test_prj);
}

TEST(ConfigTests, SetAndGet) {
    fs::path test_prj = fs::temp_directory_path() / "likegit_config_test";
    fs::remove_all(test_prj);
    init_repository(test_prj);

    set_config("user.name",  "eg_user",         test_prj);
    set_config("user.email", "eg_user@example.com",  test_prj);

    EXPECT_EQ(get_config("user.name",  test_prj), "eg_user");
    EXPECT_EQ(get_config("user.email", test_prj), "eg_user@example.com");

    auto lines = list_config(test_prj);
    ASSERT_EQ(lines.size(), 2u);

    fs::remove_all(test_prj);
}

TEST(ConfigTests, CommitEmbedsAuthor) {
    fs::path test_prj = fs::temp_directory_path() / "likegit_author_test";
    fs::remove_all(test_prj);
    init_repository(test_prj);

    set_config("user.name",  "eg_user",        test_prj);
    set_config("user.email", "eg_user@example.com", test_prj);

    std::string content = "hello\n";
    fs::path file_path  = test_prj / "hello.txt";
    { std::ofstream o(file_path); o << content; }
    std::string blob_hash   = hash_object(content, "blob");
    std::string blob_header = std::string("blob ") + std::to_string(content.size()) + '\0' + content;
    write_object(blob_hash, blob_header, test_prj);
    update_index(file_path.string(), blob_hash, test_prj);

    std::string name  = get_config("user.name",  test_prj);
    std::string email = get_config("user.email", test_prj);
    std::string commit_hash = create_commit("Initial commit", name, email, "1690020000", test_prj);
    fs::path obj_path = test_prj / ".likegit" / "objects"
                      / commit_hash.substr(0, 2) / commit_hash.substr(2);
    std::ifstream obj_in(obj_path, std::ios::binary);
    std::string compressed((std::istreambuf_iterator<char>(obj_in)), {});

    std::string decompressed(4096, '\0');
    uLongf dest_len = decompressed.size();
    uncompress(reinterpret_cast<Bytef*>(decompressed.data()), &dest_len,
               reinterpret_cast<const Bytef*>(compressed.data()), compressed.size());
    decompressed.resize(dest_len);

    std::string expected_author = "author eg_user <eg_user@example.com> 1690020000";
    EXPECT_NE(decompressed.find(expected_author), std::string::npos)
        << "Author line not found in commit object.\nCommit content:\n" << decompressed;

    fs::remove_all(test_prj);
}