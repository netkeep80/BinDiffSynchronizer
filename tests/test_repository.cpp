#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include "jgit/repository.h"

namespace fs = std::filesystem;
using nlohmann::json;

// Helper: unique temporary directory per test
static fs::path make_temp_dir(const std::string& suffix) {
    fs::path tmp = fs::temp_directory_path() / ("jgit_repo_test_" + suffix);
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    return tmp;
}

// -----------------------------------------------------------------------
// Tests for jgit::Repository (jgit/repository.h)
// -----------------------------------------------------------------------

TEST_CASE("Repository: init creates valid .jgit structure", "[repository]")
{
    auto tmp  = make_temp_dir("init");
    auto repo = jgit::Repository::init(tmp);

    REQUIRE(fs::exists(tmp / ".jgit" / "objects"));
    REQUIRE(fs::exists(tmp / ".jgit" / "refs" / "heads"));
    REQUIRE(fs::exists(tmp / ".jgit" / "HEAD"));

    fs::remove_all(tmp);
}

TEST_CASE("Repository: head is nullopt before first commit", "[repository]")
{
    auto tmp  = make_temp_dir("empty");
    auto repo = jgit::Repository::init(tmp);

    REQUIRE_FALSE(repo.head().has_value());
    REQUIRE_FALSE(repo.head_commit().has_value());

    fs::remove_all(tmp);
}

TEST_CASE("Repository: single commit sets HEAD", "[repository]")
{
    auto tmp  = make_temp_dir("first_commit");
    auto repo = jgit::Repository::init(tmp);

    json doc = {{"greeting", "hello"}};
    jgit::ObjectId cid = repo.commit(doc, "First commit", "tester");

    REQUIRE(repo.head().has_value());
    REQUIRE(repo.head()->hex == cid.hex);

    fs::remove_all(tmp);
}

TEST_CASE("Repository: commit stores and retrieves data", "[repository]")
{
    auto tmp  = make_temp_dir("data_roundtrip");
    auto repo = jgit::Repository::init(tmp);

    json doc = {{"name", "Alice"}, {"age", 30}, {"tags", {"a", "b"}}};
    jgit::ObjectId cid = repo.commit(doc, "Store Alice", "alice");

    auto retrieved = repo.get_data(cid);
    REQUIRE(retrieved.has_value());
    REQUIRE(*retrieved == doc);

    fs::remove_all(tmp);
}

TEST_CASE("Repository: consecutive commits form a chain", "[repository]")
{
    auto tmp  = make_temp_dir("chain");
    auto repo = jgit::Repository::init(tmp);

    json doc1 = {{"v", 1}};
    json doc2 = {{"v", 2}};

    jgit::ObjectId cid1 = repo.commit(doc1, "v1", "author");
    jgit::ObjectId cid2 = repo.commit(doc2, "v2", "author");

    auto c2 = repo.get_commit(cid2);
    REQUIRE(c2.has_value());
    REQUIRE(c2->parent.has_value());
    REQUIRE(c2->parent->hex == cid1.hex);

    auto c1 = repo.get_commit(cid1);
    REQUIRE(c1.has_value());
    REQUIRE_FALSE(c1->parent.has_value());

    fs::remove_all(tmp);
}

TEST_CASE("Repository: log returns all commits in reverse order", "[repository]")
{
    auto tmp  = make_temp_dir("log");
    auto repo = jgit::Repository::init(tmp);

    std::vector<jgit::ObjectId> ids;
    for (int i = 1; i <= 4; ++i) {
        ids.push_back(repo.commit({{"i", i}}, "commit " + std::to_string(i), "author"));
    }

    auto history = repo.log();
    REQUIRE(history.size() == 4);

    // log() returns newest first
    REQUIRE(history[0].first.hex == ids[3].hex);
    REQUIRE(history[1].first.hex == ids[2].hex);
    REQUIRE(history[2].first.hex == ids[1].hex);
    REQUIRE(history[3].first.hex == ids[0].hex);

    fs::remove_all(tmp);
}

TEST_CASE("Repository: log respects max_count", "[repository]")
{
    auto tmp  = make_temp_dir("log_max");
    auto repo = jgit::Repository::init(tmp);

    for (int i = 0; i < 5; ++i) {
        repo.commit({{"i", i}}, "commit", "author");
    }

    auto history = repo.log(3);
    REQUIRE(history.size() == 3);

    fs::remove_all(tmp);
}

TEST_CASE("Repository: checkout returns data and detaches HEAD", "[repository]")
{
    auto tmp  = make_temp_dir("checkout");
    auto repo = jgit::Repository::init(tmp);

    json old_doc = {{"version", 1}};
    json new_doc = {{"version", 2}};

    jgit::ObjectId old_id = repo.commit(old_doc, "v1", "author");
    repo.commit(new_doc, "v2", "author");

    // Checkout the first commit
    auto result = repo.checkout(old_id);
    REQUIRE(result.has_value());
    REQUIRE(*result == old_doc);

    // HEAD is now detached
    REQUIRE_FALSE(repo.current_branch().has_value());
    REQUIRE(repo.head()->hex == old_id.hex);

    fs::remove_all(tmp);
}

TEST_CASE("Repository: checkout non-existent commit returns nullopt", "[repository]")
{
    auto tmp  = make_temp_dir("checkout_missing");
    auto repo = jgit::Repository::init(tmp);

    jgit::ObjectId fake{"0000000000000000000000000000000000000000000000000000000000000000"};
    auto result = repo.checkout(fake);
    REQUIRE_FALSE(result.has_value());

    fs::remove_all(tmp);
}

TEST_CASE("Repository: create_branch and list_branches", "[repository]")
{
    auto tmp  = make_temp_dir("branches");
    auto repo = jgit::Repository::init(tmp);

    jgit::ObjectId cid = repo.commit({{"x", 1}}, "init", "author");

    repo.create_branch("feature", cid);

    auto branches = repo.list_branches();
    REQUIRE(branches.size() == 2); // main + feature
    REQUIRE(std::find(branches.begin(), branches.end(), "main")    != branches.end());
    REQUIRE(std::find(branches.begin(), branches.end(), "feature") != branches.end());

    fs::remove_all(tmp);
}

TEST_CASE("Repository: switch_branch changes current branch", "[repository]")
{
    auto tmp  = make_temp_dir("switch");
    auto repo = jgit::Repository::init(tmp);

    jgit::ObjectId cid = repo.commit({{"y", 2}}, "init", "author");
    repo.create_branch("dev", cid);

    repo.switch_branch("dev");
    REQUIRE(repo.current_branch().has_value());
    REQUIRE(*repo.current_branch() == "dev");

    fs::remove_all(tmp);
}

TEST_CASE("Repository: switch_branch throws for non-existent branch", "[repository]")
{
    auto tmp  = make_temp_dir("switch_missing");
    auto repo = jgit::Repository::init(tmp);

    REQUIRE_THROWS_AS(repo.switch_branch("nonexistent"), std::runtime_error);

    fs::remove_all(tmp);
}

TEST_CASE("Repository: delete_branch removes it", "[repository]")
{
    auto tmp  = make_temp_dir("delete_branch");
    auto repo = jgit::Repository::init(tmp);

    jgit::ObjectId cid = repo.commit({{"z", 3}}, "init", "author");
    repo.create_branch("temp", cid);

    // Switch to main so we can delete 'temp'
    repo.switch_branch("main");
    repo.delete_branch("temp");

    auto branches = repo.list_branches();
    REQUIRE(std::find(branches.begin(), branches.end(), "temp") == branches.end());

    fs::remove_all(tmp);
}

TEST_CASE("Repository: delete current branch throws", "[repository]")
{
    auto tmp  = make_temp_dir("delete_current");
    auto repo = jgit::Repository::init(tmp);

    // HEAD is on 'main' by default
    REQUIRE_THROWS_AS(repo.delete_branch("main"), std::runtime_error);

    fs::remove_all(tmp);
}

TEST_CASE("Repository: create_tag, resolve_tag, list_tags, delete_tag", "[repository]")
{
    auto tmp  = make_temp_dir("tags");
    auto repo = jgit::Repository::init(tmp);

    jgit::ObjectId cid = repo.commit({{"release", 1}}, "first release", "author");
    repo.create_tag("v1.0.0", cid);

    auto target = repo.resolve_tag("v1.0.0");
    REQUIRE(target.has_value());
    REQUIRE(target->hex == cid.hex);

    auto tags = repo.list_tags();
    REQUIRE(tags.size() == 1);
    REQUIRE(tags[0] == "v1.0.0");

    repo.delete_tag("v1.0.0");
    REQUIRE_FALSE(repo.resolve_tag("v1.0.0").has_value());

    fs::remove_all(tmp);
}

TEST_CASE("Repository: persists across instances", "[repository]")
{
    auto tmp = make_temp_dir("persist");

    json doc = {{"persistent", true}};
    jgit::ObjectId cid;

    {
        auto repo = jgit::Repository::init(tmp);
        cid = repo.commit(doc, "persist test", "author");
    }

    {
        jgit::Repository repo2(tmp);
        auto retrieved = repo2.get_data(cid);
        REQUIRE(retrieved.has_value());
        REQUIRE(*retrieved == doc);

        auto h = repo2.head();
        REQUIRE(h.has_value());
        REQUIRE(h->hex == cid.hex);
    }

    fs::remove_all(tmp);
}

TEST_CASE("Repository: commit metadata is stored correctly", "[repository]")
{
    auto tmp  = make_temp_dir("metadata");
    auto repo = jgit::Repository::init(tmp);

    json doc = {{"meta", "test"}};
    jgit::ObjectId cid = repo.commit(doc, "My message", "my_author");

    auto c = repo.get_commit(cid);
    REQUIRE(c.has_value());
    REQUIRE(c->author  == "my_author");
    REQUIRE(c->message == "My message");
    REQUIRE(c->timestamp > 0);

    fs::remove_all(tmp);
}
