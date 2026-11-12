#pragma once

#include <cstdint>

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

// The interface's click (the scripts' ISOUND_EVENT on a node's first press) for widget actions:
// one per player gesture. A tap on the touch UI can reach the game as several actions (closing
// the open deal, then opening the next row), one per frame, and a quantity drag as one edit-box
// text per step: each used to click. An edit box's text never clicks (a drag or a step is no
// press), and an action within kGapMs of the last click is quiet.
class ActionSound
{
  public:
    static constexpr uint32_t kGapMs = 250;
    // Scope of one widget action (DoControl): `now` in ms.
    class Scope
    {
      public:
        Scope(ActionSound &sound, uint32_t now) : sound_(sound), last_(sound.last_), clicked_(sound.clicked_)
        {
            sound_.quiet_ = sound_.clicked_ && now - sound_.last_ < kGapMs;
            if (!sound_.quiet_)
            {
                sound_.last_ = now;
                sound_.clicked_ = true;
            }
        }
        // The action is an edit box's text: no click, and it does not use up the gesture's one.
        void EditText()
        {
            sound_.quiet_ = true;
            sound_.last_ = last_;
            sound_.clicked_ = clicked_;
        }
        ~Scope()
        {
            sound_.quiet_ = false;
        }
        Scope(const Scope &) = delete;
        Scope &operator=(const Scope &) = delete;

      private:
        ActionSound &sound_;
        uint32_t last_;
        bool clicked_;
    };
    // True inside a widget action that must not click.
    bool Quiet() const
    {
        return quiet_;
    }

  private:
    uint32_t last_ = 0;
    bool clicked_ = false, quiet_ = false;
};
inline ActionSound actionSound;
} // namespace storm::mobile
