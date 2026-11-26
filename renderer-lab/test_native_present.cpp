#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "movie_dxgi_present.hpp"
#include "movie_vulkan_present.hpp"

#include <array>
#include <fstream>

namespace {

using namespace movie_pass;
using Color = std::array<std::uint8_t, 4>;

class HiddenWindow {
public:
    HiddenWindow() : handle(CreateWindowExW(0, L"STATIC", L"", WS_POPUP,
                                          0, 0, 64, 48, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr)) {
        if (!handle) throw std::runtime_error("Create hidden presenter test window failed");
    }
    HiddenWindow(const HiddenWindow&) = delete;
    HiddenWindow& operator=(const HiddenWindow&) = delete;
    ~HiddenWindow() { DestroyWindow(handle); }
    HWND Get() const { return handle; }

    void Resize(Extent size) {
        if (!SetWindowPos(handle, nullptr, 0, 0, static_cast<int>(size.width), static_cast<int>(size.height),
                          SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)) {
            throw std::runtime_error("Resize hidden presenter test window failed");
        }
    }

private:
    HWND handle;
};

Color Pattern(unsigned x, unsigned y, unsigned tick) {
    return {std::uint8_t(x * 47 + y * 93 + tick), std::uint8_t(x * 39 + y * 61 + tick * 2),
            std::uint8_t(x * 11 + y * 23 + 9), std::uint8_t(57 + tick)};
}

Pixels MakeFrame(Extent source, unsigned tick, std::size_t pitch) {
    Pixels bytes(pitch * source.height, 0xBC);
    for (unsigned y = 0; y < source.height; ++y) {
        for (unsigned x = 0; x < source.width; ++x) {
            auto color = Pattern(x, y, tick);
            std::swap(color[0], color[2]);
            std::memcpy(bytes.data() + y * pitch + x * 4, color.data(), color.size());
        }
    }
    return bytes;
}

void CheckFrame(Extent source, Extent target, Extent actual, unsigned tick, const Pixels& pixels) {
    if (actual.width != target.width || actual.height != target.height ||
        pixels.size() != std::size_t(target.width) * target.height * 4) {
        throw std::runtime_error("Resize did not propagate to swap chain");
    }
    const auto view = Fit(source, target);
    for (unsigned y = 0; y < target.height; ++y) {
        for (unsigned x = 0; x < target.width; ++x) {
            Color expected{0, 0, 0, 255};
            if (x >= view.left && x < view.left + view.size.width && y >= view.top && y < view.top + view.size.height) {
                const unsigned sourceX = ((x - view.left) * 2 + 1) * source.width / (2 * view.size.width);
                const unsigned sourceY = ((y - view.top) * 2 + 1) * source.height / (2 * view.size.height);
                expected = Pattern(sourceX, sourceY, tick);
            }
            const auto* pixel = pixels.data() + (std::size_t(y) * target.width + x) * 4;
            if (std::memcmp(pixel, expected.data(), expected.size()) != 0) {
                throw std::runtime_error("Swap chain pixel mismatch");
            }
        }
    }
}

template <typename Presenter>
void RenderAndVerify(Presenter& presenter, Extent source, Extent target, unsigned tick, bool vsync) {
    const std::size_t pitch = source.width * 4 + 20;
    const auto bytes = MakeFrame(source, tick, pitch);
    if (presenter.Draw(Frame{source, bytes.data(), pitch, bytes.size()}) != MoviePresentation::Drawn) {
        throw std::runtime_error("Draw did not produce a DXGI frame");
    }
    CheckFrame(source, target, presenter.BackBufferExtent(), tick, presenter.ReadBack());
    const auto result = presenter.Present(vsync);
    if (result != MoviePresentation::Presented && result != MoviePresentation::Occluded) {
        throw std::runtime_error("DXGI Present did not succeed");
    }
    std::printf("  %ux%u -> %ux%u, exact back buffer, Present(%u) %s\n", source.width, source.height,
                target.width, target.height, vsync ? 1 : 0,
                result == MoviePresentation::Presented ? "accepted" : "occluded");
}

void RenderAndVerify(VulkanMoviePresenter& presenter, Extent source, Extent target, unsigned tick, bool vsync) {
    const std::size_t pitch = source.width * 4 + 20;
    const auto bytes = MakeFrame(source, tick, pitch);
    Pixels capture;
    auto result = presenter.Submit(Frame{source, bytes.data(), pitch, bytes.size()}, vsync, &capture);
    if (result == MoviePresentation::SurfaceChanged) {
        result = presenter.Submit(Frame{source, bytes.data(), pitch, bytes.size()}, vsync, &capture);
    }
    if (result != MoviePresentation::Presented) throw std::runtime_error("Vulkan presentation did not succeed");
    CheckFrame(source, target, presenter.BackBufferExtent(), tick, capture);
    std::printf("  %ux%u -> %ux%u, exact swap image, vkQueuePresentKHR accepted\n",
                source.width, source.height, target.width, target.height);
}

template <typename Presenter>
void TestAdapter(IDXGIAdapter1* adapter, ID3DBlob* vertex, ID3DBlob* pixel) {
    HiddenWindow window;
    Presenter presenter(window.Get(), adapter, vertex, pixel);
    RenderAndVerify(presenter, {4, 2}, {64, 48}, 1, true);
    RenderAndVerify(presenter, {4, 2}, {64, 48}, 2, false);
    const auto movie = MakeFrame({4, 2}, 2, 16);
    for (unsigned frame = 0; frame < 24; ++frame) {
        const auto result = presenter.Submit(Frame{{4, 2}, movie.data(), 16, movie.size()}, false);
        if (result != MoviePresentation::Presented) throw std::runtime_error("DXGI steady presentation failed");
    }
    window.Resize({113, 40});
    RenderAndVerify(presenter, {4, 2}, {113, 40}, 3, false);
    window.Resize({40, 98});
    RenderAndVerify(presenter, {4, 2}, {40, 98}, 4, true);
    window.Resize({1920, 1200});
    RenderAndVerify(presenter, {1920, 1080}, {1920, 1200}, 5, false);
    window.Resize({0, 0});
    if (presenter.Draw({}) != MoviePresentation::Suspended) {
        throw std::runtime_error("Zero-extent hidden window did not suspend DXGI drawing");
    }
    window.Resize({64, 48});
    RenderAndVerify(presenter, {4, 2}, {64, 48}, 6, false);
    presenter.ResetDevice();
    RenderAndVerify(presenter, {4, 2}, {64, 48}, 7, true);
    std::puts("  Verified 24 steady frames, window-size suspension and device/swap-chain recreation");
}

std::vector<std::uint32_t> ReadShader(const char* path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto size = input.tellg();
    if (size <= 0 || size % 4 != 0 || size > 65536) throw std::runtime_error("Invalid SPIR-V shader size");
    std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / 4);
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(words.data()), size) || words.front() != 0x07230203) {
        throw std::runtime_error("Could not read SPIR-V shader");
    }
    return words;
}

void TestVulkan() {
    VulkanInstance instance;
    instance.Create(true);
    const auto vertex = ReadShader("movie-vs.spv");
    const auto fragment = ReadShader("movie-fs.spv");
    std::uint32_t count = 0;
    VkRequire(instance.EnumeratePhysicalDevices(instance.instance, &count, nullptr), "Count Vulkan GPUs");
    std::vector<VkPhysicalDevice> devices(count);
    VkRequire(instance.EnumeratePhysicalDevices(instance.instance, &count, devices.data()), "Read Vulkan GPUs");
    unsigned tested = 0;
    for (const auto physical : devices) {
        VkPhysicalDeviceProperties props{};
        instance.GetPhysicalDeviceProperties(physical, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) continue;
        std::printf("Vulkan presentation on %s\n", props.deviceName);
        HiddenWindow window;
        VulkanMoviePresenter presenter(window.Get(), instance, physical, vertex, fragment);
        RenderAndVerify(presenter, {4, 2}, {64, 48}, 1, true);
        RenderAndVerify(presenter, {4, 2}, {64, 48}, 2, true);
        const auto movie = MakeFrame({4, 2}, 2, 16);
        for (unsigned frame = 0; frame < 24; ++frame) {
            const auto result = presenter.Submit(Frame{{4, 2}, movie.data(), 16, movie.size()}, true);
            if (result != MoviePresentation::Presented) throw std::runtime_error("Vulkan steady presentation failed");
        }
        window.Resize({113, 40});
        RenderAndVerify(presenter, {4, 2}, {113, 40}, 3, false);
        window.Resize({40, 98});
        RenderAndVerify(presenter, {4, 2}, {40, 98}, 4, true);
        window.Resize({1920, 1200});
        RenderAndVerify(presenter, {1920, 1080}, {1920, 1200}, 5, false);
        window.Resize({0, 0});
        if (presenter.Submit({}, false) != MoviePresentation::Suspended) {
            throw std::runtime_error("Zero-extent hidden window did not suspend Vulkan drawing");
        }
        window.Resize({64, 48});
        RenderAndVerify(presenter, {4, 2}, {64, 48}, 6, false);
        presenter.ResetDevice();
        RenderAndVerify(presenter, {4, 2}, {64, 48}, 7, true);
        std::puts("  Verified 24 steady frames, window-size suspension and device/surface recreation");
        ++tested;
    }
    if (!tested) throw std::runtime_error("No hardware Vulkan adapter");
}

} // namespace

int main(int argc, char** argv) {
    std::printf("Native presentation test (%zu-bit)\n", sizeof(void*) * 8);
    try {
        if (argc == 2 && std::strcmp(argv[1], "--isolate-windows-hooks") == 0) {
            PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY policy{};
            policy.DisableExtensionPoints = 1;
            if (!SetProcessMitigationPolicy(ProcessExtensionPointDisablePolicy, &policy, sizeof(policy))) {
                throw std::runtime_error("Could not isolate Windows hooks in the test process");
            }
        } else if (argc != 1) {
            throw std::invalid_argument("Usage: test_native_present [--isolate-windows-hooks]");
        }
        Com<IDXGIFactory1> factory;
        Require(CreateDXGIFactory1(__uuidof(IDXGIFactory1), factory.AsVoid()), "Get DXGI factory");
        Com<ID3DBlob> vertex, pixel;
        CompileShader("VertexMain", "vs_5_0", vertex);
        CompileShader("PixelMain", "ps_5_0", pixel);
        unsigned tested = 0;
        for (UINT i = 0; ; ++i) {
            Com<IDXGIAdapter1> adapter;
            const auto next = factory->EnumAdapters1(i, adapter.Put());
            if (next == DXGI_ERROR_NOT_FOUND) break;
            Require(next, "Enumerate DXGI adapter");
            DXGI_ADAPTER_DESC1 desc{};
            Require(adapter->GetDesc1(&desc), "Get hardware adapter description");
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            std::printf("DirectX 11 presentation on %ls\n", desc.Description);
            TestAdapter<D3D11MoviePresenter>(adapter.Get(), vertex.Get(), pixel.Get());
            std::printf("DirectX 12 presentation on %ls\n", desc.Description);
            TestAdapter<D3D12MoviePresenter>(adapter.Get(), vertex.Get(), pixel.Get());
            ++tested;
        }
        if (!tested) throw std::runtime_error("No hardware DXGI adapter");
        TestVulkan();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
