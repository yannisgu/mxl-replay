#pragma once

#include "util/json.hpp"

#include <string>
#include <vector>

// The shared tag list (docs/concepts/clip-tags.md): tags prepared before a show and tags made while tagging, the
// same on every screen. A clip keeps its tags as the comma list it always had (`ClipRef::tags`).
namespace replay
{
// The list a replay starts with until someone stores one.
[[nodiscard]] std::vector<std::string> defaultTagList();
// A tag is 1–40 characters without commas (trimmed). Empty when it is not one.
[[nodiscard]] std::string cleanTag(std::string const& text);
// From a JSON array of strings: trimmed, each a tag, none twice (case-insensitive). False with `problem` otherwise.
bool readTagList(Json const& tags, std::vector<std::string>& out, std::string& problem);
// A clip's comma list as tags (trimmed, empty ones left out), and back.
[[nodiscard]] std::vector<std::string> splitTags(std::string const& tags);
[[nodiscard]] std::string joinTags(std::vector<std::string> const& tags);
// `tags` with every `from` (case-insensitive) as `to`, once; unchanged when it has no `from`.
[[nodiscard]] std::string renameTag(std::string const& tags, std::string const& from, std::string const& to);
[[nodiscard]] bool sameTag(std::string const& a, std::string const& b);
} // namespace replay
