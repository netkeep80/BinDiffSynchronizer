#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <algorithm>
#include "jgit/refs.h"

namespace fs = std::filesystem;

// Helper: create a fresh temporary directory with the .jgit structure
static fs::path make_jgit_temp(const std::string& suffix) {
    fs::path tmp = fs::temp_directory_path() / ("jgit_refs_test_" + suffix);
    fs::remove_all(tmp);
    // Create minimal .jgit structure
    fs::create_directories(tmp / ".jgit" / "refs" / "heads");
    fs::create_directories(tmp / ".jgit" / "refs" / "tags");
    // Write initial HEAD
    std::ofstream(tmp / ".jgit" / "HEAD") << "ref: refs/heads/main\n";
    return tmp;
}

// -----------------------------------------------------------------------
// Tests for jgit::Refs (jgit/refs.h)
// -----------------------------------------------------------------------

TEST_CASE("Refs: current_branch returns 'main' from default HEAD", "[refs]")
{
    auto tmp  = make_jgit_temp("branch_default");
    jgit::Refs refs(tmp / ".jgit");

    auto branch = refs.current_branch();
    REQUIRE(branch.has_value());
    REQUIRE(*branch == "main");

    fs::remove_all(tmp);
}

TEST_CASE("Refs: resolve_head returns nullopt when branch has no commits", "[refs]")
{
    auto tmp = make_jgit_temp("no_commits");
    jgit::Refs refs(tmp / ".jgit");

    REQUIRE_FALSE(refs.resolve_head().has_value());

    fs::remove_all(tmp);
}

TEST_CASE("Refs: set_branch and branch_tip", "[refs]")
{
    auto tmp = make_jgit_temp("set_branch");
    jgit::Refs refs(tmp / ".jgit");

    jgit::ObjectId id{"1111111111111111111111111111111111111111111111111111111111111111"};
    refs.set_branch("main", id);

    auto tip = refs.branch_tip("main");
    REQUIRE(tip.has_value());
    REQUIRE(tip->hex == id.hex);

    fs::remove_all(tmp);
}

TEST_CASE("Refs: resolve_head after writing branch", "[refs]")
{
    auto tmp = make_jgit_temp("resolve_head");
    jgit::Refs refs(tmp / ".jgit");

    jgit::ObjectId id{"2222222222222222222222222222222222222222222222222222222222222222"};
    refs.set_branch("main", id);

    auto resolved = refs.resolve_head();
    REQUIRE(resolved.has_value());
    REQUIRE(resolved->hex == id.hex);

    fs::remove_all(tmp);
}

TEST_CASE("Refs: set_head_to_branch updates current_branch", "[refs]")
{
    auto tmp = make_jgit_temp("switch_branch");
    jgit::Refs refs(tmp / ".jgit");

    // Create a 'dev' branch
    jgit::ObjectId id{"3333333333333333333333333333333333333333333333333333333333333333"};
    refs.set_branch("dev", id);

    refs.set_head_to_branch("dev");

    auto branch = refs.current_branch();
    REQUIRE(branch.has_value());
    REQUIRE(*branch == "dev");

    // HEAD now resolves to dev's tip
    auto resolved = refs.resolve_head();
    REQUIRE(resolved.has_value());
    REQUIRE(resolved->hex == id.hex);

    fs::remove_all(tmp);
}

TEST_CASE("Refs: set_head_detached causes nullopt current_branch", "[refs]")
{
    auto tmp = make_jgit_temp("detached");
    jgit::Refs refs(tmp / ".jgit");

    jgit::ObjectId id{"4444444444444444444444444444444444444444444444444444444444444444"};
    refs.set_head_detached(id);

    REQUIRE_FALSE(refs.current_branch().has_value());

    auto resolved = refs.resolve_head();
    REQUIRE(resolved.has_value());
    REQUIRE(resolved->hex == id.hex);

    fs::remove_all(tmp);
}

TEST_CASE("Refs: list_branches returns sorted names", "[refs]")
{
    auto tmp = make_jgit_temp("list_branches");
    jgit::Refs refs(tmp / ".jgit");

    jgit::ObjectId id{"5555555555555555555555555555555555555555555555555555555555555555"};
    refs.set_branch("zebra", id);
    refs.set_branch("alpha", id);
    refs.set_branch("main",  id);

    auto names = refs.list_branches();
    REQUIRE(names.size() == 3);
    REQUIRE(std::is_sorted(names.begin(), names.end()));
    REQUIRE(names[0] == "alpha");
    REQUIRE(names[1] == "main");
    REQUIRE(names[2] == "zebra");

    fs::remove_all(tmp);
}

TEST_CASE("Refs: delete_branch removes the branch", "[refs]")
{
    auto tmp = make_jgit_temp("delete_branch");
    jgit::Refs refs(tmp / ".jgit");

    jgit::ObjectId id{"6666666666666666666666666666666666666666666666666666666666666666"};
    refs.set_branch("temp", id);
    REQUIRE(refs.branch_tip("temp").has_value());

    refs.delete_branch("temp");
    REQUIRE_FALSE(refs.branch_tip("temp").has_value());

    fs::remove_all(tmp);
}

TEST_CASE("Refs: branch_tip returns nullopt for non-existent branch", "[refs]")
{
    auto tmp = make_jgit_temp("no_branch");
    jgit::Refs refs(tmp / ".jgit");

    REQUIRE_FALSE(refs.branch_tip("nonexistent").has_value());

    fs::remove_all(tmp);
}

TEST_CASE("Refs: tags — set, get, list, delete", "[refs]")
{
    auto tmp = make_jgit_temp("tags");
    jgit::Refs refs(tmp / ".jgit");

    jgit::ObjectId id{"7777777777777777777777777777777777777777777777777777777777777777"};
    refs.set_tag("v1.0.0", id);

    auto target = refs.tag_target("v1.0.0");
    REQUIRE(target.has_value());
    REQUIRE(target->hex == id.hex);

    auto tags = refs.list_tags();
    REQUIRE(tags.size() == 1);
    REQUIRE(tags[0] == "v1.0.0");

    refs.delete_tag("v1.0.0");
    REQUIRE_FALSE(refs.tag_target("v1.0.0").has_value());
    REQUIRE(refs.list_tags().empty());

    fs::remove_all(tmp);
}
