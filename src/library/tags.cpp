#include "library/tags.hpp"

#include "util/logging.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace replay
{
namespace
{
std::string trim(std::string const& text)
{
    auto const begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
    {
        return {};
    }
    return text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
}

std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

} // namespace

std::vector<std::string> defaultTagList()
{
    return {"Goal", "Chance", "Save", "Foul", "Card", "Offside", "Corner", "Free kick", "Home", "Away", "Crowd", "Coach"};
}

std::string cleanTag(std::string const& text)
{
    auto tag = trim(text);
    return tag.empty() || tag.size() > 40 || tag.find(',') != std::string::npos ? std::string{} : tag;
}

bool sameTag(std::string const& a, std::string const& b)
{
    return lower(a) == lower(b);
}

bool readTagList(Json const& tags, std::vector<std::string>& out, std::string& problem)
{
    if (tags.kind != Json::Kind::Array)
    {
        problem = "tags must be an array";
        return false;
    }
    std::vector<std::string> result;
    std::set<std::string> seen;
    for (auto const& item : tags.items)
    {
        auto const value = jsonText(item);
        auto tag = value ? cleanTag(*value) : std::string{};
        if (tag.empty())
        {
            problem = "a tag is 1–40 characters without commas";
            return false;
        }
        if (!seen.insert(lower(tag)).second)
        {
            problem = "tag " + tag + " is there twice";
            return false;
        }
        result.push_back(std::move(tag));
    }
    out = std::move(result);
    return true;
}

std::vector<std::string> splitTags(std::string const& tags)
{
    std::vector<std::string> out;
    std::stringstream in(tags);
    std::string part;
    while (std::getline(in, part, ','))
    {
        if (auto tag = trim(part); !tag.empty())
        {
            out.push_back(std::move(tag));
        }
    }
    return out;
}

std::string joinTags(std::vector<std::string> const& tags)
{
    std::string out;
    for (auto const& tag : tags)
    {
        out += (out.empty() ? "" : ", ") + tag;
    }
    return out;
}

std::string renameTag(std::string const& tags, std::string const& from, std::string const& to)
{
    auto list = splitTags(tags);
    auto const key = lower(from);
    bool found = false;
    std::vector<std::string> out;
    for (auto const& tag : list)
    {
        if (lower(tag) == key || lower(tag) == lower(to))
        {
            if (lower(tag) == key)
            {
                found = true;
            }
            if (std::none_of(out.begin(), out.end(), [&](std::string const& t) { return lower(t) == lower(to); }))
            {
                out.push_back(lower(tag) == key ? to : tag);
            }
            continue;
        }
        out.push_back(tag);
    }
    return found ? joinTags(out) : tags;
}
} // namespace replay
