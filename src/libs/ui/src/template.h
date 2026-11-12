// Component templates: RML text with holes, conditions and loops over JSON props.
//
//   {{ expr }}                      escaped text
//   {{{ expr }}}                    raw text (RML built by a helper)
//   <t-if test="expr">…</t-if>      optional <t-else/> inside
//   <t-for each="expr" as="x">…</t-for>   also defines x_i (index) and x_last
//
// Expressions: literals, paths (a.b[0].c), ! - + * / % comparisons && || ?: and calls
// (see template.cpp for the helper list). Missing values are null; null prints nothing.
#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <unordered_map>

namespace storm::ui
{

using Json = nlohmann::json;

// What a component keeps between its renders so that a loop over a prop renders again only
// the items that changed (RenderTemplate): per loop, the output of each item by its value and
// position, valid while the rest of the scope is what it was.
struct TemplateMemo
{
    struct Loop
    {
        std::string outside; // the scope without the looped prop, as it was
        std::unordered_map<std::string, std::string> items; // item key -> its output
        std::unordered_map<std::string, std::string> next;  // filled by the render in progress
    };
    std::unordered_map<std::string, Loop> loops; // by the loop's place in the template
    int rendered = 0, reused = 0;                // items of the last render
};

std::string RenderTemplate(std::string_view source, const Json &scope, TemplateMemo *memo = nullptr);
// The lookup behind the template helper tr('key'): the UI string of the current language
// (pack strings/<lang>.json). The host installs it; without one tr() is a template error.
void SetTemplateStrings(std::function<std::string(const std::string &key)> lookup);
Json EvaluateExpression(std::string_view expression, const Json &scope);
bool Truthy(const Json &value);
std::string ToText(const Json &value);
std::string EscapeRml(std::string_view text);

} // namespace storm::ui
