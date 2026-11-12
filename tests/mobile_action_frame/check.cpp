// The widget actions of one interface frame against the store's scripts (interface/store.c):
// XINTERFACE's event list (pushed at its head by MSG_INTERFACE_SET_EVENT, run from its head in
// the next Execute) and the two handlers a re-selection reaches. Selecting row A, then row B
// sends QTY_CANCEL_BUTTON press (A's deal) and TABLE_LIST activate (B) in one batch.
#include "mobile_action_frame.hpp"

#include <cassert>
#include <cstdio>
#include <deque>
#include <string>

namespace
{

struct Event
{
    std::string name;
    Event *next = nullptr;
};

// The store's script state and handlers (ProcessCancelExit, ShowItemInfo with TransactionOK at
// BuyOrSell 0, EndTooltip).
struct Store
{
    bool open = true;
    bool deal = false; // bShowChangeWin
    int row = -1;      // the row the deal shows
    int selected = -1; // TABLE_LIST.select
    int amount = 0;    // qty_edit

    void Run(const std::string &event)
    {
        if (event == "exitCancel")
        {
            if (deal)
                deal = false; // EndTooltip
            else
                open = false; // IDoExit
        }
        else if (event == "ShowItemInfo")
        {
            if (deal)
                deal = false; // TransactionOK, nothing set: EndTooltip
            else
            {
                deal = true;
                row = selected;
                amount = 0; // qty_edit.str = "0"
            }
        }
    }
};

struct Interface
{
    Store store;
    Event *events = nullptr; // XINTERFACE::m_pEvents
    std::deque<std::string> actions;

    void Queue(const std::string &name)
    {
        events = new Event{name, events};
    }
    // XINTERFACE's frame: the widget actions (DoControl), then the queued events (Execute).
    void Frame(bool gated)
    {
        const storm::mobile::ActionFrame frame(events);
        while (!actions.empty() && (!gated || frame.Open(events)))
        {
            const std::string action = actions.front();
            actions.pop_front();
            if (action == "cancel")
                Queue("exitCancel"); // QTY_CANCEL_BUTTON: click,event:exitCancel
            else if (action.rfind("row:", 0) == 0)
            {
                store.selected = std::stoi(action.substr(4)); // SelectLine, at once
                Queue("ShowItemInfo");                        // activate,event:ShowItemInfo
            }
        }
        while (events)
        {
            Event *event = events;
            events = event->next;
            store.Run(event->name);
            delete event;
        }
    }
};

void Reselect(Interface &game, bool gated)
{
    game.actions = {"row:0"};
    game.Frame(gated);
    assert(game.store.open && game.store.deal && game.store.row == 0);
    game.store.amount = 40; // the player sets an amount on A
    game.actions = {"cancel", "row:1"}; // trade_commerce.cpp: close A's deal, open B
    game.Frame(gated);
    game.Frame(gated);
}

} // namespace

int main()
{
    {
        Interface game;
        Reselect(game, false);
        assert(!game.store.open); // the defect: both events in one frame run in reverse
    }
    for (int round = 0; round < 3; ++round)
    {
        Interface game;
        Reselect(game, true);
        assert(game.store.open && game.store.deal && game.store.row == 1 && game.store.amount == 0);
        assert(game.actions.empty());
        // A → B → C → B: every re-selection keeps the store open with a fresh deal.
        for (int row : {2, 1, 0})
        {
            game.store.amount = 7;
            game.actions = {"cancel", "row:" + std::to_string(row)};
            game.Frame(true);
            game.Frame(true);
            assert(game.store.open && game.store.deal && game.store.row == row && game.store.amount == 0);
        }
        // A tap on the open row closes its deal only (the UI sends the cancel alone).
        game.actions = {"cancel"};
        game.Frame(true);
        assert(game.store.open && !game.store.deal);
        // Without a deal open, a row opens at once.
        game.actions = {"row:2"};
        game.Frame(true);
        assert(game.store.open && game.store.deal && game.store.row == 2);
    }
    // The interface click belongs to the gesture (the UI's "uisound:press" when a press starts):
    // the widget actions the UI sends are silent in CINODE::DoAction, a re-selection's pair and a
    // quantity drag's 50 texts alike; the game's own presses (no UI action running) still click.
    {
        int clicks = 0;
        const auto do_action = [&] { // CINODE::DoAction with bFirstPress
            if (!storm::mobile::uiAction.Active())
                ++clicks;
        };
        const auto widget_action = [&] {
            const storm::mobile::UiAction::Scope programmatic(storm::mobile::uiAction);
            do_action();
        };
        widget_action(); // QTY_CANCEL_BUTTON press
        widget_action(); // TABLE_LIST activate, the next frame
        for (int step = 0; step < 50; ++step)
            widget_action(); // a drag: one text per step
        assert(clicks == 0 && !storm::mobile::uiAction.Active());
        do_action(); // a press of the game itself
        assert(clicks == 1);
    }
    std::puts("PASS the touch UI's widget actions never click: a re-selection and a 50-step drag are silent");
    std::puts("PASS store re-selection: one script event per frame keeps the store open, the next row's deal starts at 0");
    return 0;
}
