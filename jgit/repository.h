#pragma once

#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdint>
#include <ctime>

#include <nlohmann/json.hpp>
#include "hash.h"
#include "object_store.h"
#include "commit.h"

namespace jgit {

/**
 * A jgit repository: wraps ObjectStore and adds commit history,
 * branches, and HEAD tracking.
 *
 * On-disk layout (mirroring Git):
 *
 *   <root>/.jgit/
 *       objects/                  — content-addressed blobs (managed by ObjectStore)
 *       refs/
 *           heads/
 *               main              — text file: commit ObjectId hex (or empty)
 *               <branch>          — text file: commit ObjectId hex
 *       HEAD                      — text file: "ref: refs/heads/main\n"
 *
 * Usage:
 *
 *   // Create a new repository
 *   auto repo = Repository::init("/path/to/repo");
 *
 *   // Commit a JSON document
 *   nlohmann::json doc = {{"key", "value"}};
 *   ObjectId cid = repo.commit(doc, "Initial commit", "Alice");
 *
 *   // Walk history
 *   for (const Commit& c : repo.log()) {
 *       std::cout << c.id.hex << " " << c.message << "\n";
 *   }
 *
 *   // Retrieve a version
 *   nlohmann::json restored = repo.checkout(cid);
 */
class Repository {
public:
    // ---- factory / construction ----

    /**
     * Initialise a new empty repository at `path`.
     * Creates the `.jgit/` directory structure and a default `main` branch.
     * Equivalent to `git init`.
     */
    static Repository init(const std::filesystem::path& path) {
        ObjectStore::init(path);   // creates objects/, refs/heads/, HEAD
        return Repository(path);
    }

    /**
     * Open an existing repository at `path`.
     * The `.jgit/` subdirectory must already exist.
     */
    explicit Repository(const std::filesystem::path& path)
        : root_(path)
        , store_(path)
    {
    }

    // ---- core operations ----

    /**
     * Commit `doc` to the current branch.
     *
     * Steps:
     *   1. Serialise `doc` to CBOR and store it in the ObjectStore
     *      → root_snapshot ObjectId.
     *   2. Create a Commit object with the current HEAD as parent.
     *   3. Serialise the Commit to JSON and store it in the ObjectStore
     *      → commit ObjectId.
     *   4. Update the current branch ref to point to the new commit.
     *   5. Return the commit ObjectId.
     *
     * @param doc      The JSON document to commit.
     * @param message  The commit message.
     * @param author   Optional author string; defaults to empty string.
     * @return         The ObjectId of the new commit.
     */
    ObjectId commit(const nlohmann::json& doc,
                    const std::string& message,
                    const std::string& author = "") {
        // Step 1: store the document snapshot.
        ObjectId snapshot_id = store_.put(doc);

        // Step 2: build the Commit.
        Commit c;
        c.root_snapshot = snapshot_id;
        c.parent_id     = get_head();         // "" for root commit
        c.author        = author;
        c.timestamp     = current_time();
        c.message       = message;

        // Step 3: store the Commit JSON.
        nlohmann::json commit_json = c.to_json();
        ObjectId commit_id = store_.put(commit_json);
        c.id = commit_id;

        // Step 4: update branch ref.
        write_ref(current_branch(), commit_id.hex);

        return commit_id;
    }

    /**
     * Return the commit history of the current branch, newest first.
     * The returned vector is empty if the branch has no commits yet.
     */
    std::vector<Commit> log() const {
        std::vector<Commit> history;
        ObjectId cur = get_head();
        while (!cur.hex.empty()) {
            auto opt = store_.get(cur);
            if (!opt) break;
            Commit c = Commit::from_json(*opt, cur);
            history.push_back(c);
            cur = c.parent_id;
        }
        return history;
    }

    /**
     * Load and return the JSON document stored in the given commit.
     * This is a read-only operation — it does not update HEAD.
     * Equivalent to `git show <commit>:<path>`.
     *
     * Throws std::runtime_error if the commit or its snapshot cannot be found.
     */
    nlohmann::json checkout(const ObjectId& commit_id) const {
        auto commit_opt = store_.get(commit_id);
        if (!commit_opt) {
            throw std::runtime_error(
                "Repository::checkout: commit not found: " + commit_id.hex);
        }
        Commit c = Commit::from_json(*commit_opt, commit_id);

        auto doc_opt = store_.get(c.root_snapshot);
        if (!doc_opt) {
            throw std::runtime_error(
                "Repository::checkout: snapshot not found: " + c.root_snapshot.hex);
        }
        return *doc_opt;
    }

    /**
     * Return the ObjectId of the commit that HEAD currently points to.
     * Returns an ObjectId with empty hex if the current branch has no commits.
     */
    ObjectId get_head() const {
        std::string branch = current_branch();
        std::string hex    = read_ref(branch);
        return ObjectId{ hex };
    }

    // ---- branch management ----

    /**
     * Create a new branch pointing to the same commit as the current HEAD.
     * Does nothing if the branch already exists.
     */
    void create_branch(const std::string& name) {
        std::filesystem::path ref_path = branch_path(name);
        if (!std::filesystem::exists(ref_path)) {
            write_ref(name, get_head().hex);
        }
    }

    /**
     * Switch the repository's HEAD to point to the named branch.
     * The branch file is created (empty) if it does not exist.
     * Equivalent to `git checkout <branch>` (without modifying the working tree).
     */
    void switch_branch(const std::string& name) {
        // Ensure the branch ref file exists.
        std::filesystem::path ref_path = branch_path(name);
        if (!std::filesystem::exists(ref_path)) {
            std::ofstream f(ref_path);
        }
        // Update HEAD to reference the new branch.
        write_head(name);
    }

    /**
     * Return the name of the branch that HEAD currently references.
     * Defaults to "main" if the HEAD file is missing or malformed.
     */
    std::string current_branch() const {
        std::filesystem::path head_path = root_ / ".jgit" / "HEAD";
        std::ifstream f(head_path);
        if (!f) return "main";

        std::string line;
        std::getline(f, line);
        // Expected format: "ref: refs/heads/<branch>"
        const std::string prefix = "ref: refs/heads/";
        if (line.rfind(prefix, 0) == 0) {
            return line.substr(prefix.size());
        }
        return "main";
    }

    /** Access the underlying ObjectStore directly. */
    ObjectStore&       object_store()       { return store_; }
    const ObjectStore& object_store() const { return store_; }

private:
    std::filesystem::path root_;
    ObjectStore           store_;

    // ---- helpers ----

    std::filesystem::path branch_path(const std::string& name) const {
        return root_ / ".jgit" / "refs" / "heads" / name;
    }

    /** Read the commit hex stored in a branch ref file. Returns "" if not found. */
    std::string read_ref(const std::string& branch) const {
        std::filesystem::path ref_path = branch_path(branch);
        std::ifstream f(ref_path);
        if (!f) return "";
        std::string line;
        std::getline(f, line);
        // Strip trailing whitespace/newline.
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        return line;
    }

    /** Write a commit hex to a branch ref file. */
    void write_ref(const std::string& branch, const std::string& hex) {
        std::filesystem::path ref_path = branch_path(branch);
        std::filesystem::create_directories(ref_path.parent_path());
        std::ofstream f(ref_path, std::ios::trunc);
        if (!f) {
            throw std::runtime_error("Repository::write_ref: cannot write " + ref_path.string());
        }
        f << hex << "\n";
    }

    /** Update HEAD to reference the named branch. */
    void write_head(const std::string& branch) {
        std::filesystem::path head_path = root_ / ".jgit" / "HEAD";
        std::ofstream f(head_path, std::ios::trunc);
        if (!f) {
            throw std::runtime_error("Repository::write_head: cannot write HEAD");
        }
        f << "ref: refs/heads/" << branch << "\n";
    }

    /** Return the current Unix timestamp in seconds. */
    static int64_t current_time() noexcept {
        return static_cast<int64_t>(std::time(nullptr));
    }
};

} // namespace jgit
