#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include "commit.h"
#include "hash.h"
#include "object_store.h"
#include "refs.h"

namespace jgit {

/**
 * High-level jgit repository.
 *
 * A Repository wraps an ObjectStore and a Refs manager and provides
 * user-facing operations analogous to `git commit`, `git checkout`,
 * `git log`, `git branch`, and `git tag`.
 *
 * Typical usage:
 *
 *   // Create a new repository
 *   auto repo = jgit::Repository::init("./my_repo");
 *
 *   // Commit a JSON document
 *   nlohmann::json doc = {{"name", "Alice"}, {"age", 30}};
 *   jgit::ObjectId cid = repo.commit(doc, "Initial commit", "alice");
 *
 *   // Read HEAD commit
 *   auto c = repo.head_commit();
 *
 *   // Browse history
 *   auto history = repo.log();
 *
 *   // Retrieve the data at a given commit
 *   auto data = repo.get_data(cid);
 */
class Repository {
public:
    /**
     * Open an existing jgit repository at `root_path`.
     * The `.jgit/` directory must already exist.
     */
    explicit Repository(const std::filesystem::path& root_path)
        : store_(root_path)
        , refs_(root_path / ".jgit")
        , root_(root_path)
    {
    }

    /**
     * Initialise a new empty jgit repository at `path` and return it.
     */
    static Repository init(const std::filesystem::path& path) {
        ObjectStore::init(path);
        return Repository(path);
    }

    // -----------------------------------------------------------------------
    // Committing
    // -----------------------------------------------------------------------

    /**
     * Store `data` as a new commit on the current branch.
     *
     * @param data     The JSON value to commit.
     * @param message  Human-readable commit message.
     * @param author   Author identifier (name, email, etc.).
     * @returns        The ObjectId of the newly created commit object.
     */
    ObjectId commit(const nlohmann::json& data,
                    const std::string&    message,
                    const std::string&    author = "unknown") {
        // 1. Store the data blob
        ObjectId data_id = store_.put(data);

        // 2. Build commit object
        Commit c;
        c.root      = data_id;
        c.parent    = refs_.resolve_head();
        c.author    = author;
        c.timestamp = current_unix_time();
        c.message   = message;

        // 3. Store commit object
        ObjectId commit_id = store_.put(c.to_json());

        // 4. Advance current branch (or HEAD)
        auto branch = refs_.current_branch();
        if (branch.has_value()) {
            refs_.set_branch(*branch, commit_id);
        } else {
            // Detached HEAD: just move HEAD forward
            refs_.set_head_detached(commit_id);
        }

        return commit_id;
    }

    // -----------------------------------------------------------------------
    // Reading
    // -----------------------------------------------------------------------

    /**
     * Retrieve the data (JSON blob) stored at a specific commit.
     * Returns nullopt if the commit or its data object does not exist.
     */
    std::optional<nlohmann::json> get_data(const ObjectId& commit_id) const {
        auto commit_json = store_.get(commit_id);
        if (!commit_json.has_value()) return std::nullopt;
        Commit c = Commit::from_json(*commit_json);
        return store_.get(c.root);
    }

    /**
     * Retrieve the Commit object at a given commit ObjectId.
     * Returns nullopt if not found.
     */
    std::optional<Commit> get_commit(const ObjectId& commit_id) const {
        auto j = store_.get(commit_id);
        if (!j.has_value()) return std::nullopt;
        return Commit::from_json(*j);
    }

    /**
     * Return the ObjectId of the current HEAD commit.
     * Returns nullopt if no commits have been made yet.
     */
    std::optional<ObjectId> head() const {
        return refs_.resolve_head();
    }

    /**
     * Return the Commit object at HEAD, or nullopt if no commits exist.
     */
    std::optional<Commit> head_commit() const {
        auto h = head();
        if (!h.has_value()) return std::nullopt;
        return get_commit(*h);
    }

    // -----------------------------------------------------------------------
    // History
    // -----------------------------------------------------------------------

    /**
     * Return the commit history reachable from `start_id`, in reverse
     * chronological order (newest first).
     *
     * @param start_id  The commit to start from.
     * @param max_count Maximum number of commits to return (0 = unlimited).
     */
    std::vector<std::pair<ObjectId, Commit>> log(const ObjectId& start_id,
                                                  std::size_t     max_count = 0) const {
        std::vector<std::pair<ObjectId, Commit>> history;
        std::optional<ObjectId> current = start_id;

        while (current.has_value()) {
            if (max_count > 0 && history.size() >= max_count) break;

            auto commit_json = store_.get(*current);
            if (!commit_json.has_value()) break;

            Commit c = Commit::from_json(*commit_json);
            history.emplace_back(*current, c);
            current = c.parent;
        }
        return history;
    }

    /**
     * Return the commit history reachable from HEAD (newest first).
     */
    std::vector<std::pair<ObjectId, Commit>> log(std::size_t max_count = 0) const {
        auto h = head();
        if (!h.has_value()) return {};
        return log(*h, max_count);
    }

    // -----------------------------------------------------------------------
    // Checkout
    // -----------------------------------------------------------------------

    /**
     * Move HEAD to point at `commit_id` (detached HEAD) and return the
     * stored JSON data at that commit.
     *
     * @returns The data (JSON blob) at the checked-out commit,
     *          or nullopt if the commit does not exist.
     */
    std::optional<nlohmann::json> checkout(const ObjectId& commit_id) {
        auto data = get_data(commit_id);
        if (!data.has_value()) return std::nullopt;
        refs_.set_head_detached(commit_id);
        return data;
    }

    // -----------------------------------------------------------------------
    // Branch management
    // -----------------------------------------------------------------------

    /**
     * Create a new branch pointing at `commit_id`.
     * Does not switch HEAD.
     */
    void create_branch(const std::string& name, const ObjectId& commit_id) {
        refs_.set_branch(name, commit_id);
    }

    /**
     * Switch HEAD to an existing branch.
     * @throws std::runtime_error if the branch does not exist.
     */
    void switch_branch(const std::string& name) {
        if (!refs_.branch_tip(name).has_value()) {
            throw std::runtime_error("jgit: branch '" + name + "' does not exist");
        }
        refs_.set_head_to_branch(name);
    }

    /**
     * Delete a branch.  Refuses to delete the currently checked-out branch.
     * @throws std::runtime_error if trying to delete the current branch.
     */
    void delete_branch(const std::string& name) {
        auto cur = refs_.current_branch();
        if (cur.has_value() && *cur == name) {
            throw std::runtime_error("jgit: cannot delete currently checked-out branch '" + name + "'");
        }
        refs_.delete_branch(name);
    }

    /**
     * List all branch names.
     */
    std::vector<std::string> list_branches() const {
        return refs_.list_branches();
    }

    /**
     * Return the name of the current branch, or nullopt if HEAD is detached.
     */
    std::optional<std::string> current_branch() const {
        return refs_.current_branch();
    }

    // -----------------------------------------------------------------------
    // Tag management
    // -----------------------------------------------------------------------

    /**
     * Create (or update) a tag pointing at `commit_id`.
     */
    void create_tag(const std::string& name, const ObjectId& commit_id) {
        refs_.set_tag(name, commit_id);
    }

    /**
     * Delete a tag.
     */
    void delete_tag(const std::string& name) {
        refs_.delete_tag(name);
    }

    /**
     * List all tag names.
     */
    std::vector<std::string> list_tags() const {
        return refs_.list_tags();
    }

    /**
     * Resolve a tag name to its ObjectId.
     * Returns nullopt if the tag does not exist.
     */
    std::optional<ObjectId> resolve_tag(const std::string& name) const {
        return refs_.tag_target(name);
    }

    // -----------------------------------------------------------------------
    // Low-level access
    // -----------------------------------------------------------------------

    /** Direct access to the underlying object store. */
    ObjectStore& object_store() { return store_; }
    const ObjectStore& object_store() const { return store_; }

    /** Direct access to the refs manager. */
    Refs& refs() { return refs_; }
    const Refs& refs() const { return refs_; }

private:
    ObjectStore store_;
    Refs        refs_;
    std::filesystem::path root_;

    static int64_t current_unix_time() {
        using namespace std::chrono;
        return static_cast<int64_t>(
            duration_cast<seconds>(system_clock::now().time_since_epoch()).count()
        );
    }
};

} // namespace jgit
