#include "android_dialog.hpp"
#include <cassert>
#include <iostream>

using namespace storm::android;

int main() {
    for (const auto malformed : {"", "1", ":1", "1:", "-1:0", "1:-1", "1:+2", "1:0x", "1 :0",
                                "18446744073709551616:1", "1:4294967296"})
        assert(!parseDialogueInput(malformed));
    assert(!parseDialogueInput("1:2")->text);
    assert(parseDialogueInput("1:2:")->text->empty());
    assert(*parseDialogueInput("1:2:  x:y\\\"  ")->text == "  x:y\\\"  ");
    assert(dialogueEditError("").empty());
    assert(dialogueEditError("  Проба: \\\"; Event(42) 😀  ").empty());
    for (const auto &invalid : {std::string("a\0b", 3), std::string("\n"), std::string("\r"),
                              std::string("\t"), std::string("\x7f"), std::string("\xc2\x85"),
                              std::string("\xe2\x80\xa8"), std::string("\xe2\x80\xa9"),
                              std::string("\xc0\x80"), std::string("\xe0\x80\x80"),
                              std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80"),
                              std::string("\xf0\x80\x80\x80"), std::string("\xf0\x9f"),
                              std::string("\x80")})
        assert(dialogueEditError(invalid) == "invalid_text");
    assert(dialogueEditError(std::string(256, 'a')).empty());
    assert(dialogueEditError(std::string(257, 'a')) == "too_long");
    std::string emoji;
    for (int i = 0; i < 256; ++i) emoji += "😀";
    assert(emoji.size() == dialogueEditMaxBytes && dialogueEditError(emoji).empty());
    assert(dialogueEditError(emoji + "a") == "too_long");

    ATTRIBUTES attrs, links;
    attrs.links = &links;
    attrs.values["Text"] = "Fixture question";
    attrs.values["CurrentNode"] = "start";
    auto link = std::make_unique<ATTRIBUTES>();
    link->value = "initial";
    link->values["go"] = "next";
    link->values["edit"] = "2";
    auto *choice = link.get();
    links.children.push_back(std::move(link));
    VDATA variable;
    core.variable = &variable;
    const void *owner = &attrs;
    const auto fits = [](std::string_view) { return true; };
    assert(publishDialogue(owner, &attrs, "Fixture speaker"));
    assert(publications.back().find("\"editable\":true,\"maxCodePoints\":256") != std::string::npos);
    auto submit = [&](std::string text, auto width) {
        dialogueActions.push_back(std::to_string(dialogueGeneration) + ":0:" + text);
        return takeDialogueChoice(owner, &attrs, width);
    };
    dialogueActions.push_back(std::to_string(dialogueGeneration) + ":0");
    assert(!takeDialogueChoice(owner, &attrs, fits)); // No accidental empty edit.
    dialogueActions.push_back(std::to_string(dialogueGeneration - 1) + ":0:stale");
    assert(!takeDialogueChoice(owner, &attrs, fits));
    dialogueActions.push_back(std::to_string(dialogueGeneration) + ":0:wrong owner");
    assert(!takeDialogueChoice(&links, &attrs, fits));
    assert(variable.slots.empty() && !attrs.GetAttribute("value"));

    const auto generation = dialogueGeneration;
    assert(!submit("abc", [](std::string_view) { return false; }));
    assert(publications.back().find("\"editError\":\"too_long\"") != std::string::npos);
    auto count = publications.size();
    assert(!submit("abc", [](std::string_view) { return false; }));
    assert(publications.size() == count + 1 && dialogueGeneration == generation);
    assert(!submit("bad\ntext", fits));
    assert(publications.back().find("\"editError\":\"invalid_text\"") != std::string::npos);
    variable.writable = false;
    assert(!submit("ok", fits));
    assert(publications.back().find("\"editError\":\"unavailable\"") != std::string::npos);
    assert(variable.slots.empty() && !attrs.GetAttribute("value"));
    core.variable = nullptr;
    assert(!submit("ok", fits));
    assert(publications.back().find("\"editError\":\"unavailable\"") != std::string::npos);
    core.variable = &variable;
    variable.writable = true;
    const std::string text = "  Проба: \\\"; Event(42) 😀  ";
    dialogueActions.push_back(std::to_string(dialogueGeneration) + ":0:" + text);
    dialogueActions.push_back(std::to_string(dialogueGeneration) + ":0:duplicate");
    assert(takeDialogueChoice(owner, &attrs, fits) == choice);
    assert(variable.slots.at(2) == text && attrs.values.at("value") == text);
    assert(dialogueActions.empty());

    assert(publishDialogue(owner, &attrs, "Fixture speaker"));
    assert(dialogueGeneration == generation + 1);
    choice->values["go"] = "same_caption_other_target";
    const auto old = dialogueGeneration;
    dialogueActions.push_back(std::to_string(old) + ":0:stale route");
    assert(publishDialogue(owner, &attrs, "Fixture speaker"));
    assert(dialogueGeneration == old + 1 && dialogueActions.empty());
    attrs.values["CurrentNode"] = "other_node";
    assert(publishDialogue(owner, &attrs, "Fixture speaker"));
    assert(dialogueGeneration == old + 2);
    choice->values["edit"] = "StRiNg";
    assert(publishDialogue(owner, &attrs, "Fixture speaker"));
    assert(dialogueGeneration == old + 3);
    core.variable = nullptr;
    assert(submit("", fits) == choice && attrs.values.at("value").empty());
    assert(variable.slots.at(2) == text);

    choice->values["edit"] = "10";
    assert(publishDialogue(owner, &attrs, "Fixture speaker"));
    assert(submit("outside array", fits) == choice);
    assert(attrs.values.at("value") == "outside array" && variable.slots.size() == 1);

    auto last = std::make_unique<ATTRIBUTES>();
    last->values["go"] = "last_target";
    last->values["edit"] = "string";
    links.children.push_back(std::move(last));
    assert(publishDialogue(owner, &attrs, "Fixture speaker"));
    assert(dialogueEditIndex(&links) == 1);
    assert(!submit("inactive edit", fits));
    dialogueActions.push_back(std::to_string(dialogueGeneration) + ":0");
    assert(takeDialogueChoice(owner, &attrs, fits) == choice);
    links.children.pop_back();

    choice->values.erase("edit");
    assert(publishDialogue(owner, &attrs, "Fixture speaker"));
    assert(!submit("unexpected edit", fits));
    dialogueActions.push_back(std::to_string(dialogueGeneration) + ":0");
    assert(takeDialogueChoice(owner, &attrs, fits) == choice);
    closeDialogue(owner);
    assert(dialogueOwner == nullptr && publications.back() == "{\"active\":false}");
    std::cout << "PASS UTF-8/input bounds, actual bridge metadata, stale generations, errors, atomic edits and ordinary choices\n";
}
