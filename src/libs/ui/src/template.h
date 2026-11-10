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

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace storm::ui
{

using Json = nlohmann::json;

std::string RenderTemplate(std::string_view source, const Json &scope);
Json EvaluateExpression(std::string_view expression, const Json &scope);
bool Truthy(const Json &value);
std::string ToText(const Json &value);
std::string EscapeRml(std::string_view text);

} // namespace storm::ui
