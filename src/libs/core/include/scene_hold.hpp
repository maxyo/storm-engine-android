#pragma once

namespace storm::core
{
// The scene's realize layers (REALIZE, SEA_REALIZE) are skipped while the renderer presents a held picture of them
// (render::GamePictureHeld: a pause menu over the game); independent of the scripts' LayerFreeze.
void HoldSceneRealize(bool held);
} // namespace storm::core
