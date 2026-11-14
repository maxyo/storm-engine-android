// Where the relief networks run (README.md): onnxruntime on the CPU (the int8 models) or ncnn on the GPU through
// Vulkan compute (the fp16 graphs pnnx makes of the same float models). One interface for both, for the relief maps
// (prepare.cpp) and the texture upscale. Which device a game uses is measured once, logged with both numbers and
// kept in the cache root (ChooseDevice).
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace storm::relief
{

// One network on one device. Not thread-safe (Cancel aside).
class Network
{
  public:
    virtual ~Network() = default;
    // One image: three colour planes (planar, `width` x `height`, 0..1) in, three planes of `scale` times the size out
    // (`scale`: the network's own, 1 for the relief networks). A GPU network leaves the outer `halo` texels of the
    // output (the input's padding: what the network needs around a texel) unset. False on an error or after Cancel.
    virtual bool Infer(const float *image, int width, int height, float *planes, std::string &error) = 0;
    // Any thread: the running inference stops as soon as it can, and every later one.
    virtual void Cancel() = 0;
    // "CPU, 6 threads of 8 cores" / "GPU Adreno (TM) 740, Vulkan, 176 px tiles".
    virtual std::string Describe() const = 0;
};

// onnxruntime's environment (one thread pool and one memory arena for all its networks, which run one after the other).
class CpuRuntime
{
  public:
    // `threads` cores, waiting threads sleeping. Null and `error` on failure.
    static std::unique_ptr<CpuRuntime> Open(int threads, std::string &error);
    virtual ~CpuRuntime() = default;
    // An int8 ONNX model of the relief (model/README.md).
    virtual std::unique_ptr<Network> Load(std::span<const uint8_t> onnx, std::string &error) = 0;
};

// The Vulkan compute device ncnn would use (its default one): created on first use, kept for the process.
struct GpuDevice
{
    bool available = false;
    std::string name, driver; // the device's name and driver version
    std::string why;          // when not available
};
const GpuDevice &Gpu();

// An ncnn graph (.ncnn.param text, .ncnn.bin weights; both must outlive the network) on the GPU. The image is run in
// tiles of the inner area with `halo` texels around each (the network's reach: the tiles join without a seam), each
// tile one short GPU submission, so the game's frames are not held up. `scale`: the network's output scale.
std::unique_ptr<Network> LoadGpuNetwork(std::span<const uint8_t> param, std::span<const uint8_t> bin, int halo, int scale,
                                        std::string &error);

enum class Device
{
    Cpu,
    Gpu,
};
const char *DeviceName(Device device);

// The device for this game's relief networks: measured once (a probe tile of `probe` on both, after a warm-up),
// the faster taken; kept in `cache` (the cache root: «Очистить кэш» measures again) for this GPU, driver, model and
// thread count. Logged with both numbers either way; no Vulkan compute device: the CPU, logged with the reason.
// `cpu`: the probe network on the CPU; `gpu_param` / `gpu_bin`: its ncnn graph.
Device ChooseDevice(const std::filesystem::path &cache, Network &cpu, std::span<const uint8_t> gpu_param,
                    std::span<const uint8_t> gpu_bin, int halo, int threads);

// The crash guard of the GPU work (ChooseDevice): `cache`'s marker holds GpuLabel of the GPU (kGpuOpening while Vulkan
// opens), written before GPU work starts and cleared after it succeeded. One left behind at the next start means the
// process died in the driver: logged at error, that GPU and driver take the CPU («Очистить кэш» clears it).
inline constexpr const char *kGpuOpening = "opening Vulkan";
std::string GpuLabel(const GpuDevice &gpu); // "<name>, driver <version>"
void MarkGpuRunning(const std::filesystem::path &cache, const std::string &what);
void ClearGpuRunning(const std::filesystem::path &cache);
// The GPU (GpuLabel) or kGpuOpening that crashed a previous run, or empty.
std::string GpuCrash(const std::filesystem::path &cache);

} // namespace storm::relief
