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
} // namespace storm::mobile
