#pragma once

#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include "hash.h"

namespace jgit {

/**
 * Manages the refs/ namespace of a jgit repository.
 *
 * Layout under <repo>/.jgit/:
 *
 *   HEAD                    — "ref: refs/heads/<branch>" or bare ObjectId
 *   refs/
 *     heads/
 *       main                — ObjectId of the tip commit on branch "main"
 *       feature-x           — ObjectId of the tip commit on branch "feature-x"
 *     tags/
 *       v1.0.0              — ObjectId of a tagged commit
 *
 * All ref files contain a single 64-character hex ObjectId followed by '\n'.
 */
class Refs {
public:
    explicit Refs(const std::filesystem::path& jgit_dir)
        : jgit_(jgit_dir)
    {
    }

    // -----------------------------------------------------------------------
    // HEAD
    // -----------------------------------------------------------------------

    /**
     * Read HEAD.
     * Returns the name of the current branch (e.g. "main") if HEAD is a
     * symbolic ref, or nullopt if HEAD points directly to a commit (detached).
     */
    std::optional<std::string> current_branch() const {
        std::string content = read_file(jgit_ / "HEAD");
        if (content.rfind("ref: refs/heads/", 0) == 0) {
            // Strip "ref: refs/heads/" prefix and trailing whitespace
            std::string branch = content.substr(std::string("ref: refs/heads/").size());
            while (!branch.empty() && (branch.back() == '\n' || branch.back() == '\r' || branch.back() == ' '))
                branch.pop_back();
            return branch;
        }
        return std::nullopt; // detached HEAD
    }

    /**
     * Set HEAD to point to a branch (symbolic ref).
     */
    void set_head_to_branch(const std::string& branch_name) {
        write_file(jgit_ / "HEAD", "ref: refs/heads/" + branch_name + "\n");
    }

    /**
     * Set HEAD to point directly to a commit (detached HEAD state).
     */
    void set_head_detached(const ObjectId& commit_id) {
        write_file(jgit_ / "HEAD", commit_id.hex + "\n");
    }

    /**
     * Resolve HEAD to the current commit ObjectId.
     * Returns nullopt if the repository has no commits yet.
     */
    std::optional<ObjectId> resolve_head() const {
        std::string content = read_file(jgit_ / "HEAD");
        if (content.rfind("ref: ", 0) == 0) {
            // Symbolic ref — dereference it
            std::string ref_path = content.substr(5);
            while (!ref_path.empty() && (ref_path.back() == '\n' || ref_path.back() == '\r' || ref_path.back() == ' '))
                ref_path.pop_back();
            return read_ref(jgit_ / ref_path);
        }
        // Bare ObjectId in HEAD (detached)
        std::string hex = content;
        while (!hex.empty() && (hex.back() == '\n' || hex.back() == '\r' || hex.back() == ' '))
            hex.pop_back();
        if (hex.size() == 64) {
            return ObjectId{ hex };
        }
        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // Branches (refs/heads/)
    // -----------------------------------------------------------------------

    /**
     * Read the tip commit of a branch.
     * Returns nullopt if the branch does not exist or has no commits.
     */
    std::optional<ObjectId> branch_tip(const std::string& name) const {
        return read_ref(jgit_ / "refs" / "heads" / name);
    }

    /**
     * Write (create or update) a branch ref.
     */
    void set_branch(const std::string& name, const ObjectId& commit_id) {
        std::filesystem::path p = jgit_ / "refs" / "heads" / name;
        std::filesystem::create_directories(p.parent_path());
        write_file(p, commit_id.hex + "\n");
    }

    /**
     * Delete a branch ref.
     * No-op if the branch does not exist.
     */
    void delete_branch(const std::string& name) {
        std::filesystem::path p = jgit_ / "refs" / "heads" / name;
        std::filesystem::remove(p);
    }

    /**
     * List all branch names.
     */
    std::vector<std::string> list_branches() const {
        return list_refs(jgit_ / "refs" / "heads");
    }

    // -----------------------------------------------------------------------
    // Tags (refs/tags/)
    // -----------------------------------------------------------------------

    /**
     * Read the ObjectId of a tag.
     * Returns nullopt if the tag does not exist.
     */
    std::optional<ObjectId> tag_target(const std::string& name) const {
        return read_ref(jgit_ / "refs" / "tags" / name);
    }

    /**
     * Create or update a tag.
     */
    void set_tag(const std::string& name, const ObjectId& commit_id) {
        std::filesystem::path p = jgit_ / "refs" / "tags" / name;
        std::filesystem::create_directories(p.parent_path());
        write_file(p, commit_id.hex + "\n");
    }

    /**
     * Delete a tag.
     * No-op if the tag does not exist.
     */
    void delete_tag(const std::string& name) {
        std::filesystem::path p = jgit_ / "refs" / "tags" / name;
        std::filesystem::remove(p);
    }

    /**
     * List all tag names.
     */
    std::vector<std::string> list_tags() const {
        return list_refs(jgit_ / "refs" / "tags");
    }

private:
    std::filesystem::path jgit_;

    // Read a ref file (heads or tags).  Returns nullopt if missing.
    std::optional<ObjectId> read_ref(const std::filesystem::path& p) const {
        if (!std::filesystem::exists(p)) return std::nullopt;
        std::string content = read_file(p);
        while (!content.empty() && (content.back() == '\n' || content.back() == '\r' || content.back() == ' '))
            content.pop_back();
        if (content.size() == 64) return ObjectId{ content };
        return std::nullopt;
    }

    // Read entire file as string.
    static std::string read_file(const std::filesystem::path& p) {
        std::ifstream f(p);
        if (!f) return {};
        return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
    }

    // Write string to file, overwriting if it exists.
    static void write_file(const std::filesystem::path& p, const std::string& content) {
        std::ofstream f(p, std::ios::trunc);
        if (!f) throw std::runtime_error("Refs: cannot write file: " + p.string());
        f << content;
    }

    // List the names of all files (non-recursive, single level) in `dir`.
    static std::vector<std::string> list_refs(const std::filesystem::path& dir) {
        std::vector<std::string> names;
        if (!std::filesystem::exists(dir)) return names;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                names.push_back(entry.path().filename().string());
            }
        }
        std::sort(names.begin(), names.end());
        return names;
    }
};

} // namespace jgit
