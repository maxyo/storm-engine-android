#pragma once

#include "storm_ui/ui.h"

#include <android/asset_manager.h>
#include <memory>
#include <string>

namespace storm::ui
{

// Android: reads the UI pack from the APK assets under `directory` (for example "ui").
std::unique_ptr<FileSource> AssetFiles(AAssetManager *assets, std::string directory);

} // namespace storm::ui
