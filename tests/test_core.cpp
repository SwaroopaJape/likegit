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


TEST(LogTests, ReadObjectDecompresses) {
    fs::path test_prj = fs::temp_directory_path() / "likegit_log_test";
    fs::remove_all(test_prj);
    init_repository(test_prj);

    std::string content = "Testing read_object decompression\n";
    std::string blob_hash = hash_object(content, "blob");
    std::string blob_header = std::string("blob ") + std::to_string(content.size()) + '\0' + content;
    
    write_object(blob_hash, blob_header, test_prj);

    std::string decompressed = read_object(blob_hash, test_prj);

    EXPECT_EQ(decompressed, blob_header);

    fs::remove_all(test_prj);
}

// ── Diff Tests ───────────────────────────────────────────────────────────────

TEST(DiffTests, IdenticalFiles) {
    std::vector<std::string_view> a = {"A", "B", "C"};
    std::vector<std::string_view> b = {"A", "B", "C"};
    auto result = compute_diff(a, b);
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0].type, EditType::EQUAL);  EXPECT_EQ(result[0].text, "A");
    EXPECT_EQ(result[1].type, EditType::EQUAL);  EXPECT_EQ(result[1].text, "B");
    EXPECT_EQ(result[2].type, EditType::EQUAL);  EXPECT_EQ(result[2].text, "C");
}

TEST(DiffTests, CompletelyDifferentFiles) {
    std::vector<std::string_view> a = {"A", "B"};
    std::vector<std::string_view> b = {"X", "Y"};
    auto result = compute_diff(a, b);
    ASSERT_EQ(result.size(), 4u);
    EXPECT_EQ(result[0].type, EditType::DELETE); EXPECT_EQ(result[0].text, "A");
    EXPECT_EQ(result[1].type, EditType::DELETE); EXPECT_EQ(result[1].text, "B");
    EXPECT_EQ(result[2].type, EditType::INSERT); EXPECT_EQ(result[2].text, "X");
    EXPECT_EQ(result[3].type, EditType::INSERT); EXPECT_EQ(result[3].text, "Y");
}

// Textbook example from Chapter 8
TEST(DiffTests, SingleInsertion) {
    std::vector<std::string_view> a = {"A", "B", "C"};
    std::vector<std::string_view> b = {"A", "B", "B", "C"};
    auto result = compute_diff(a, b);
    ASSERT_EQ(result.size(), 4u);
    EXPECT_EQ(result[0].type, EditType::EQUAL);  EXPECT_EQ(result[0].text, "A");
    EXPECT_EQ(result[1].type, EditType::EQUAL);  EXPECT_EQ(result[1].text, "B");
    EXPECT_EQ(result[2].type, EditType::INSERT); EXPECT_EQ(result[2].text, "B");
    EXPECT_EQ(result[3].type, EditType::EQUAL);  EXPECT_EQ(result[3].text, "C");
}

TEST(DiffTests, SingleDeletion) {
    std::vector<std::string_view> a = {"A", "B", "C"};
    std::vector<std::string_view> b = {"A", "C"};
    auto result = compute_diff(a, b);
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0].type, EditType::EQUAL);  EXPECT_EQ(result[0].text, "A");
    EXPECT_EQ(result[1].type, EditType::DELETE); EXPECT_EQ(result[1].text, "B");
    EXPECT_EQ(result[2].type, EditType::EQUAL);  EXPECT_EQ(result[2].text, "C");
}

TEST(DiffTests, EmptyOldFile) {
    std::vector<std::string_view> a = {};
    std::vector<std::string_view> b = {"X", "Y"};
    auto result = compute_diff(a, b);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].type, EditType::INSERT); EXPECT_EQ(result[0].text, "X");
    EXPECT_EQ(result[1].type, EditType::INSERT); EXPECT_EQ(result[1].text, "Y");
}

TEST(DiffTests, EmptyNewFile) {
    std::vector<std::string_view> a = {"X", "Y"};
    std::vector<std::string_view> b = {};
    auto result = compute_diff(a, b);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].type, EditType::DELETE); EXPECT_EQ(result[0].text, "X");
    EXPECT_EQ(result[1].type, EditType::DELETE); EXPECT_EQ(result[1].text, "Y");
}

TEST(DiffTests, BothFilesEmpty) {
    auto result = compute_diff({}, {});
    EXPECT_TRUE(result.empty());
}

// ── Branch Tests ─────────────────────────────────────────────────────────────

// Helper: set up a repo with one commit so HEAD resolves correctly
static std::string make_one_commit(const fs::path& repo) {
    init_repository(repo);
    set_config("user.name",  "Test User",         repo);
    set_config("user.email", "test@example.com",  repo);

    fs::path f = repo / "file.txt";
    { std::ofstream o(f); o << "hello\n"; }
    std::string content = "hello\n";
    std::string h = hash_object(content, "blob");
    write_object(h, "blob " + std::to_string(content.size()) + '\0' + content, repo);
    update_index(f.string(), h, repo);
    return create_commit("Initial commit", "Test User", "test@example.com", "1690020000", repo);
}

TEST(BranchTests, CreatesBranchFile) {
    fs::path test_prj = fs::temp_directory_path() / "likegit_branch_create";
    fs::remove_all(test_prj);
    make_one_commit(test_prj);

    bool ok = create_branch("feature-x", test_prj);
    EXPECT_TRUE(ok);

    fs::path branch_file = test_prj / ".likegit" / "refs" / "heads" / "feature-x";
    EXPECT_TRUE(fs::exists(branch_file));

    fs::remove_all(test_prj);
}

TEST(BranchTests, BranchContainsCurrentHash) {
    fs::path test_prj = fs::temp_directory_path() / "likegit_branch_hash";
    fs::remove_all(test_prj);
    std::string commit_hash = make_one_commit(test_prj);

    create_branch("feature-x", test_prj);

    fs::path branch_file = test_prj / ".likegit" / "refs" / "heads" / "feature-x";
    std::ifstream in(branch_file);
    std::string stored;
    std::getline(in, stored);
    EXPECT_EQ(stored, commit_hash);

    fs::remove_all(test_prj);
}

TEST(BranchTests, DuplicateBranchFails) {
    fs::path test_prj = fs::temp_directory_path() / "likegit_branch_dup";
    fs::remove_all(test_prj);
    make_one_commit(test_prj);

    create_branch("feature-x", test_prj);
    bool second = create_branch("feature-x", test_prj); // should fail
    EXPECT_FALSE(second);

    fs::remove_all(test_prj);
}

// ── Checkout Tests ───────────────────────────────────────────────────────────

TEST(CheckoutTests, SafetyCheckDetectsDirtyFile) {
    fs::path test_prj = fs::temp_directory_path() / "likegit_checkout_dirty";
    fs::remove_all(test_prj);
    make_one_commit(test_prj);

    // Modify the tracked file without committing
    fs::path f = test_prj / "file.txt";
    { std::ofstream o(f); o << "modified content\n"; }

    EXPECT_TRUE(has_uncommitted_changes(test_prj));

    fs::remove_all(test_prj);
}

TEST(CheckoutTests, CleanRepoHasNoChanges) {
    fs::path test_prj = fs::temp_directory_path() / "likegit_checkout_clean";
    fs::remove_all(test_prj);
    make_one_commit(test_prj);

    EXPECT_FALSE(has_uncommitted_changes(test_prj));

    fs::remove_all(test_prj);
}

TEST(CheckoutTests, SwitchBranchRestoresFiles) {
    fs::path test_prj = fs::temp_directory_path() / "likegit_checkout_switch";
    fs::remove_all(test_prj);
    make_one_commit(test_prj);     // main: file.txt = "hello\n"

    // Create feature branch and add a new file to it
    create_branch("feature-x", test_prj);
    checkout_branch("feature-x", test_prj);

    fs::path f2 = test_prj / "feature.txt";
    { std::ofstream o(f2); o << "feature\n"; }
    std::string c2 = "feature\n";
    std::string h2 = hash_object(c2, "blob");
    write_object(h2, "blob " + std::to_string(c2.size()) + '\0' + c2, test_prj);
    update_index(f2.string(), h2, test_prj);
    create_commit("Feature commit", "Test User", "test@example.com", "1690020001", test_prj);

    // Switch back to main — feature.txt should be gone
    checkout_branch("main", test_prj);
    EXPECT_FALSE(fs::exists(test_prj / "feature.txt"));
    EXPECT_TRUE(fs::exists(test_prj / "file.txt"));

    fs::remove_all(test_prj);
}

TEST(CheckoutTests, HeadUpdatedAfterCheckout) {
    fs::path test_prj = fs::temp_directory_path() / "likegit_checkout_head";
    fs::remove_all(test_prj);
    make_one_commit(test_prj);

    create_branch("feature-x", test_prj);
    checkout_branch("feature-x", test_prj);

    std::ifstream head_in(test_prj / ".likegit" / "HEAD");
    std::string head_content;
    std::getline(head_in, head_content);
    EXPECT_EQ(head_content, "ref: refs/heads/feature-x");

    fs::remove_all(test_prj);
}

TEST(CheckoutTests, RefusesCheckoutWhenDirty) {
    fs::path test_prj = fs::temp_directory_path() / "likegit_checkout_refuse";
    fs::remove_all(test_prj);
    make_one_commit(test_prj);

    create_branch("feature-x", test_prj);

    // Dirty the working directory
    { std::ofstream o(test_prj / "file.txt"); o << "unsaved changes!\n"; }

    bool result = checkout_branch("feature-x", test_prj);
    EXPECT_FALSE(result);

    fs::remove_all(test_prj);
}

// ── Merge Tests ──────────────────────────────────────────────────────────────

TEST(MergeTests, FastForwardMerge) {
    fs::path test_prj = fs::temp_directory_path() / "likegit_merge_ff";
    fs::remove_all(test_prj);
    make_one_commit(test_prj);
    
    create_branch("feature", test_prj);
    checkout_branch("feature", test_prj);
    
    fs::path f2 = test_prj / "file2.txt";
    { std::ofstream o(f2); o << "new file\n"; }
    std::string c2 = "new file\n";
    std::string h2 = hash_object(c2, "blob");
    write_object(h2, "blob " + std::to_string(c2.size()) + '\0' + c2, test_prj);
    update_index(f2.string(), h2, test_prj);
    create_commit("Add file2", "Test User", "test@example.com", "1690020001", test_prj);
    
    checkout_branch("main", test_prj);
    
    bool ok = merge_branch("feature", test_prj);
    EXPECT_TRUE(ok);
    
    EXPECT_TRUE(fs::exists(test_prj / "file2.txt"));
    
    fs::remove_all(test_prj);
}

TEST(MergeTests, MergeConflict) {
    fs::path test_prj = fs::temp_directory_path() / "likegit_merge_conflict";
    fs::remove_all(test_prj);
    make_one_commit(test_prj); // LCA: file.txt = "hello\n"
    
    create_branch("feature", test_prj);
    
    // Modify on main
    { std::ofstream o(test_prj / "file.txt"); o << "hello main\n"; }
    std::string cm = "hello main\n";
    std::string hm = hash_object(cm, "blob");
    write_object(hm, "blob " + std::to_string(cm.size()) + '\0' + cm, test_prj);
    update_index((test_prj / "file.txt").string(), hm, test_prj);
    create_commit("Main mod", "Test User", "test@example.com", "1690020002", test_prj);
    
    // Modify on feature
    checkout_branch("feature", test_prj);
    { std::ofstream o(test_prj / "file.txt"); o << "hello feature\n"; }
    std::string cf = "hello feature\n";
    std::string hf = hash_object(cf, "blob");
    write_object(hf, "blob " + std::to_string(cf.size()) + '\0' + cf, test_prj);
    update_index((test_prj / "file.txt").string(), hf, test_prj);
    create_commit("Feature mod", "Test User", "test@example.com", "1690020003", test_prj);
    
    // Checkout main and merge feature
    checkout_branch("main", test_prj);
    bool ok = merge_branch("feature", test_prj);
    
    EXPECT_FALSE(ok); // Merge should fail due to conflict
    
    std::ifstream in(test_prj / "file.txt");
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    
    EXPECT_NE(content.find("<<<<<<< HEAD"), std::string::npos);
    EXPECT_NE(content.find("======="), std::string::npos);
    EXPECT_NE(content.find(">>>>>>> feature"), std::string::npos);
    
    fs::remove_all(test_prj);
}