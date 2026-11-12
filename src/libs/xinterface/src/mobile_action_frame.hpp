#pragma once

namespace storm::mobile
{
// One player action per interface frame, as the mouse delivers them. A node raises its script
// event through XINTERFACE's event list, which the next Execute runs newest first, so two
// actions applied in one frame ran their scripts in reverse. In the store, closing the open
// deal (QTY_CANCEL_BUTTON: exitCancel) and opening the next row (TABLE_LIST: ShowItemInfo)
// ran as ShowItemInfo, which closed the deal, and then exitCancel, which with no deal open
// leaves the store. An edit box's text and the event that reads it are the same case (the
// trade's "0" then "277"). Once an action has queued a script event, the rest of the queue
// waits for the next frame, after that event has run.
class ActionFrame
{
  public:
    explicit ActionFrame(const void *events) : before_(events)
    {
    }
    // True while no action of this frame has queued a script event (`events`: the list head now).
    bool Open(const void *events) const
    {
        return events == before_;
    }

  private:
    const void *before_;
};

// The interface's click (the scripts' ISOUND_EVENT on a node's first press) belongs to the
// player's gesture: the touch UI asks for it when a press starts on an interactive element
// (engine command "uisound:press", main.cpp). The widget actions the UI sends for it afterwards
// (a tap's close-the-deal-and-open-the-row pair, a quantity drag's texts, a re-selection) are
// programmatic: a node's first press inside one is silent.
class UiAction
{
  public:
    // Scope of one widget action of the touch UI (XINTERFACE::DoControl).
    class Scope
    {
      public:
        explicit Scope(UiAction &action) : action_(action)
        {
            action_.active_ = true;
        }
        ~Scope()
        {
            action_.active_ = false;
        }
        Scope(const Scope &) = delete;
        Scope &operator=(const Scope &) = delete;

      private:
        UiAction &action_;
    };
    // True while a widget action of the touch UI runs: CINODE::DoAction does not click.
    bool Active() const
    {
        return active_;
    }

  private:
    bool active_ = false;
};
inline UiAction uiAction;
} // namespace storm::mobile
