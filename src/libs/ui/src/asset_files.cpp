#include "storm_ui/asset_files.h"

namespace storm::ui
{

namespace
{

class AssetSource final : public FileSource
{
  public:
    AssetSource(AAssetManager *assets, std::string directory) : assets_(assets), directory_(std::move(directory))
    {
    }

    bool Read(const std::string &path, std::vector<uint8_t> &out) override
    {
        if (path.find("..") != std::string::npos)
            return false;
        const std::string full = directory_.empty() ? path : directory_ + "/" + path;
        AAsset *asset = AAssetManager_open(assets_, full.c_str(), AASSET_MODE_BUFFER);
        if (!asset)
            return false;
        const off64_t length = AAsset_getLength64(asset);
        out.resize(static_cast<size_t>(length));
        const int read = length > 0 ? AAsset_read(asset, out.data(), out.size()) : 0;
        AAsset_close(asset);
        return read == static_cast<int>(length);
    }

  private:
    AAssetManager *assets_;
    std::string directory_;
};

} // namespace

std::unique_ptr<FileSource> AssetFiles(AAssetManager *assets, std::string directory)
{
    return std::make_unique<AssetSource>(assets, std::move(directory));
}

} // namespace storm::ui
