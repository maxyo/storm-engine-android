#pragma once

#include <cstdint>
#include <optional>

namespace storm::mobile
{

// An interface whose script has ended is leaving: the stock scripts unload its segment at once
// (interface.c EndCancelInterface → UnloadSegment) and open the next interface a few frames
// later, or release this one in the next frame. In between its nodes are still there but the
// state its script kept is gone (xi_refCharacter: the adapter's facts come out empty), so a
// model built then shows the screen half emptied: the items screen lost its worn items for the
// three or four frames before the next screen. From the frame its script is gone an opening
// publishes no model: the screen keeps the last one until it is replaced or closed.
//
// `script` is the application adapter's fact (GameInterface.AndroidScript, event
// AndroidInterfaceFacts): whether the segment of the open interface is loaded; none when the
// adapter does not say. Only a segment seen loaded in this opening can be seen leaving: an
// interface whose script was never reported loaded (opened outside the stock launch path) is
// never held back.
class InterfaceLeaving
{
  public:
    // Once per collected snapshot; true when no model of this opening is to be published any more.
    bool Update(uint64_t opening, std::optional<bool> script)
    {
        if (opening != opening_)
        {
            opening_ = opening;
            loaded_ = left_ = false;
        }
        if (script && *script)
            loaded_ = true;
        else if (script && loaded_)
            left_ = true;
        return left_;
    }

  private:
    uint64_t opening_ = 0;
    bool loaded_ = false;
    bool left_ = false;
};

} // namespace storm::mobile
