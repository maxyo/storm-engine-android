// Typed, path-tracking access to nlohmann::json values for the scene_data parsers.
#pragma once

#include "storm/scene_data/scene_data.hpp"

#include <nlohmann/json.hpp>

#include <functional>

namespace storm::scene_data::detail
{

using Json = nlohmann::ordered_json; // keeps file order (effects sections)

class Reader
{
  public:
    explicit Reader(Report &report) : report_(report) {}

    // Parses `text`; on malformed JSON records an error and returns false.
    bool Parse(std::string_view text, Json &out);

    // Calls `field(key, value, path)` for every member of the object `object` (an error if it is not
    // an object). A field that returns false is recorded as an unknown key.
    using Field = std::function<bool(const std::string &key, const Json &value, const std::string &path)>;
    void Object(const Json &object, const std::string &path, const Field &field);

    bool Float(const Json &value, const std::string &path, float &out);
    bool Int(const Json &value, const std::string &path, int &out);
    // true/false, or the integers 0/1 the CL files use for flags
    bool Bool(const Json &value, const std::string &path, bool &out);
    bool String(const Json &value, const std::string &path, std::string &out);
    bool Color(const Json &value, const std::string &path, Rgb &out);
    bool StringList(const Json &value, const std::string &path, std::vector<std::string> &out);

    void Error(const std::string &path, const std::string &what);

  private:
    Report &report_;
};

} // namespace storm::scene_data::detail
