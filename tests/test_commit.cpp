/**
 * Task 3.1 — Unit tests for jgit Commit system.
 *
 * Tests cover:
 *   - Commit struct serialisation / deserialisation round-trip
 *   - Repository::commit() creates a commit and updates HEAD
 *   - Repository::log() returns history newest-first
 *   - Repository::checkout() returns the original JSON document
 *   - Branch management: create_branch, switch_branch, current_branch
 *   - Repository persists across separate instances
 */

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <cstdint>

#include <nlohmann/json.hpp>

#include "jgit/commit.h"
#include "jgit/repository.h"

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static fs::path tmp_repo_path(const std::string& suffix) {
    return fs::temp_directory_path() / ("jgit_test_3_" + suffix);
}

struct TempRepo {
    fs::path path;
    explicit TempRepo(const std::string& suffix) : path(tmp_repo_path(suffix)) {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempRepo() {
        fs::remove_all(path);
    }
};

// ---------------------------------------------------------------------------
// Commit struct serialisation round-trip
// ---------------------------------------------------------------------------

TEST_CASE("Task 3.1.1: Commit to_json/from_json round-trip for root commit") {
    jgit::Commit c;
    c.id            = jgit::ObjectId{ "aabbccdd" };
    c.root_snapshot = jgit::ObjectId{ "1122334455667788" };
    c.parent_id     = jgit::ObjectId{ "" };   // root commit
    c.author        = "Alice";
    c.timestamp     = 1700000000LL;
    c.message       = "Initial commit";

    nlohmann::json j = c.to_json();
    jgit::Commit c2 = jgit::Commit::from_json(j, c.id);

    CHECK(c2.id.hex            == c.id.hex);
    CHECK(c2.root_snapshot.hex == c.root_snapshot.hex);
    CHECK(c2.parent_id.hex     == c.parent_id.hex);
    CHECK(c2.author            == c.author);
    CHECK(c2.timestamp         == c.timestamp);
    CHECK(c2.message           == c.message);
    CHECK(c2.is_root());
}

TEST_CASE("Task 3.1.2: Commit to_json/from_json round-trip for child commit") {
    jgit::Commit c;
    c.id            = jgit::ObjectId{ "ccdd" };
    c.root_snapshot = jgit::ObjectId{ "snapshot2" };
    c.parent_id     = jgit::ObjectId{ "aabbccdd" };   // non-empty parent
    c.author        = "Bob";
    c.timestamp     = 1700001000LL;
    c.message       = "Second commit";

    nlohmann::json j = c.to_json();
    jgit::Commit c2 = jgit::Commit::from_json(j, c.id);

    CHECK(c2.parent_id.hex == "aabbccdd");
    CHECK(!c2.is_root());
}

TEST_CASE("Task 3.1.3: Commit to_json does not include the commit id field") {
    jgit::Commit c;
    c.id            = jgit::ObjectId{ "selfhash" };
    c.root_snapshot = jgit::ObjectId{ "snap" };
    c.parent_id     = jgit::ObjectId{ "" };
    c.author        = "Test";
    c.timestamp     = 0;
    c.message       = "msg";

    nlohmann::json j = c.to_json();
    // The serialised form must NOT contain the commit's own ID to avoid
    // the circular hash problem (hash(json_containing_hash_of_json)).
    CHECK_FALSE(j.contains("id"));
}

// ---------------------------------------------------------------------------
// Repository lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("Task 3.1.4: Repository::get_head returns empty ObjectId on fresh repo") {
    TempRepo tmp("fresh");
    auto repo = jgit::Repository::init(tmp.path);

    jgit::ObjectId head = repo.get_head();
    CHECK(head.hex.empty());
}

TEST_CASE("Task 3.1.5: Repository::log returns empty vector on fresh repo") {
    TempRepo tmp("empty_log");
    auto repo = jgit::Repository::init(tmp.path);

    auto history = repo.log();
    CHECK(history.empty());
}

TEST_CASE("Task 3.1.6: Repository::commit stores document and returns valid ObjectId") {
    TempRepo tmp("commit_basic");
    auto repo = jgit::Repository::init(tmp.path);

    nlohmann::json doc = {{"name", "Alice"}, {"age", 30}};
    jgit::ObjectId cid = repo.commit(doc, "Add Alice", "Tester");

    CHECK(!cid.hex.empty());
    CHECK(cid.hex.size() == 64u);  // SHA-256 hex = 64 chars
    CHECK(repo.object_store().exists(cid));
}

TEST_CASE("Task 3.1.7: Repository::commit updates HEAD branch") {
    TempRepo tmp("head_update");
    auto repo = jgit::Repository::init(tmp.path);

    jgit::ObjectId before = repo.get_head();
    CHECK(before.hex.empty());

    nlohmann::json doc = {{"x", 1}};
    jgit::ObjectId cid = repo.commit(doc, "First", "A");

    jgit::ObjectId after = repo.get_head();
    CHECK(after.hex == cid.hex);
}

TEST_CASE("Task 3.1.8: Repository::log returns single commit after one commit") {
    TempRepo tmp("single_log");
    auto repo = jgit::Repository::init(tmp.path);

    nlohmann::json doc = {{"v", 1}};
    jgit::ObjectId cid = repo.commit(doc, "First commit", "Author");

    auto history = repo.log();
    REQUIRE(history.size() == 1u);
    CHECK(history[0].id.hex     == cid.hex);
    CHECK(history[0].message    == "First commit");
    CHECK(history[0].author     == "Author");
    CHECK(history[0].is_root());
}

TEST_CASE("Task 3.1.9: Repository::log returns commits newest-first") {
    TempRepo tmp("log_order");
    auto repo = jgit::Repository::init(tmp.path);

    jgit::ObjectId c1 = repo.commit({{"v", 1}}, "First",  "A");
    jgit::ObjectId c2 = repo.commit({{"v", 2}}, "Second", "A");
    jgit::ObjectId c3 = repo.commit({{"v", 3}}, "Third",  "A");

    auto history = repo.log();
    REQUIRE(history.size() == 3u);
    CHECK(history[0].id.hex == c3.hex);   // newest first
    CHECK(history[1].id.hex == c2.hex);
    CHECK(history[2].id.hex == c1.hex);   // oldest last
    CHECK(history[2].is_root());
    CHECK_FALSE(history[0].is_root());
}

TEST_CASE("Task 3.1.10: Repository::checkout returns original document") {
    TempRepo tmp("checkout");
    auto repo = jgit::Repository::init(tmp.path);

    nlohmann::json original = {{"name", "Bob"}, {"scores", {10, 20, 30}}};
    jgit::ObjectId cid = repo.commit(original, "Add Bob", "Test");

    nlohmann::json restored = repo.checkout(cid);
    CHECK(restored == original);
}

TEST_CASE("Task 3.1.11: Multiple commits preserve all historical snapshots") {
    TempRepo tmp("history_preserved");
    auto repo = jgit::Repository::init(tmp.path);

    nlohmann::json v1 = {{"version", 1}};
    nlohmann::json v2 = {{"version", 2}};
    nlohmann::json v3 = {{"version", 3}};

    jgit::ObjectId c1 = repo.commit(v1, "v1", "A");
    jgit::ObjectId c2 = repo.commit(v2, "v2", "A");
    jgit::ObjectId c3 = repo.commit(v3, "v3", "A");

    // All three historical versions are still accessible.
    CHECK(repo.checkout(c1) == v1);
    CHECK(repo.checkout(c2) == v2);
    CHECK(repo.checkout(c3) == v3);
}

TEST_CASE("Task 3.1.12: Committing identical content yields different commits") {
    TempRepo tmp("same_content");
    auto repo = jgit::Repository::init(tmp.path);

    nlohmann::json doc = {{"k", "v"}};
    jgit::ObjectId c1 = repo.commit(doc, "First",  "A");
    jgit::ObjectId c2 = repo.commit(doc, "Second", "A");

    // The snapshots (document blobs) are the same (content-addressed).
    auto hist = repo.log();
    REQUIRE(hist.size() == 2u);
    CHECK(hist[0].root_snapshot.hex == hist[1].root_snapshot.hex);

    // But the commit ObjectIds are different (different parent_id, message).
    CHECK(c1.hex != c2.hex);
}

// ---------------------------------------------------------------------------
// Branch management
// ---------------------------------------------------------------------------

TEST_CASE("Task 3.1.13: Repository::current_branch defaults to main") {
    TempRepo tmp("default_branch");
    auto repo = jgit::Repository::init(tmp.path);
    CHECK(repo.current_branch() == "main");
}

TEST_CASE("Task 3.1.14: switch_branch changes current_branch") {
    TempRepo tmp("switch_branch");
    auto repo = jgit::Repository::init(tmp.path);

    repo.switch_branch("feature");
    CHECK(repo.current_branch() == "feature");

    repo.switch_branch("main");
    CHECK(repo.current_branch() == "main");
}

TEST_CASE("Task 3.1.15: Commits on different branches are independent") {
    TempRepo tmp("independent_branches");
    auto repo = jgit::Repository::init(tmp.path);

    // Commit on main.
    jgit::ObjectId c_main = repo.commit({{"branch", "main"}}, "main commit", "A");

    // Switch to feature branch and commit.
    repo.switch_branch("feature");
    jgit::ObjectId c_feat = repo.commit({{"branch", "feature"}}, "feature commit", "A");

    // feature branch has only one commit (feature has no prior parent).
    auto feat_log = repo.log();
    REQUIRE(feat_log.size() == 1u);
    CHECK(feat_log[0].id.hex == c_feat.hex);

    // Switch back to main — history contains only main's commit.
    repo.switch_branch("main");
    auto main_log = repo.log();
    REQUIRE(main_log.size() == 1u);
    CHECK(main_log[0].id.hex == c_main.hex);
}

TEST_CASE("Task 3.1.16: Repository persists across separate instances") {
    TempRepo tmp("persist");
    {
        // First instance: create and commit.
        auto repo = jgit::Repository::init(tmp.path);
        nlohmann::json doc = {{"persistent", true}};
        repo.commit(doc, "Persist test", "CI");
    }
    {
        // Second instance: open same path and read history.
        jgit::Repository repo(tmp.path);
        auto history = repo.log();
        REQUIRE(history.size() == 1u);
        CHECK(history[0].message == "Persist test");

        nlohmann::json restored = repo.checkout(history[0].id);
        CHECK(restored == nlohmann::json({{"persistent", true}}));
    }
}
