#include "movie_d3d.hpp"
#include "movie_vulkan.hpp"

#include <array>
#include <fstream>

namespace {

using namespace movie_pass;

struct Case {
    Extent frame;
    Extent output;
    Rect expectedView;
};

constexpr Case cases[]{
    {{4, 2}, {64, 48}, {0, 8, {64, 32}}},
    {{4, 2}, {113, 40}, {16, 0, {80, 40}}},
    {{4, 2}, {40, 98}, {0, 39, {40, 20}}},
    {{1920, 1080}, {1920, 1200}, {0, 60, {1920, 1080}}},
};
static_assert(Fit({1920, 1080}, {1, 8192}).size.height == 1);
static_assert(Fit({1080, 1920}, {8192, 1}).size.width == 1);

using Color = std::array<std::uint8_t, 4>;

Color Pattern(unsigned x, unsigned y) {
    return {std::uint8_t(x * 73 + y * 11 + 7), std::uint8_t(x * 3 + y * 113 + 9),
            std::uint8_t(x * 51 + y * 83 + 29), std::uint8_t((x + y) * 41 + 20)};
}

Pixels MakeFrame(Case test, std::size_t pitch) {
    Pixels pixels(pitch * test.frame.height, 0xec);
    for (unsigned y = 0; y < test.frame.height; ++y) {
        for (unsigned x = 0; x < test.frame.width; ++x) {
            auto color = Pattern(x, y);
            std::swap(color[0], color[2]);
            std::memcpy(pixels.data() + y * pitch + x * 4, color.data(), 4);
        }
    }
    return pixels;
}

void CheckOutput(Case test, const Pixels& pixels) {
    const auto rect = test.expectedView;
    if (pixels.size() != std::size_t(test.output.width) * test.output.height * 4) {
        throw std::runtime_error("Unexpected output buffer size");
    }
    for (unsigned y = 0; y < test.output.height; ++y) {
        for (unsigned x = 0; x < test.output.width; ++x) {
            Color expected{0, 0, 0, 255};
            if (x >= rect.left && x < rect.left + rect.size.width &&
                y >= rect.top && y < rect.top + rect.size.height) {
                const unsigned sourceX = ((x - rect.left) * 2 + 1) * test.frame.width / (2 * rect.size.width);
                const unsigned sourceY = ((y - rect.top) * 2 + 1) * test.frame.height / (2 * rect.size.height);
                expected = Pattern(sourceX, sourceY);
            }
            const auto* actual = pixels.data() + (std::size_t(y) * test.output.width + x) * 4;
            if (std::memcmp(expected.data(), actual, 4) != 0) {
                char detail[128]{};
                std::snprintf(detail, sizeof(detail),
                        "%ux%u pixel %u,%u read (%u,%u,%u,%u), expected (%u,%u,%u,%u)",
                        test.output.width, test.output.height, x, y, actual[0], actual[1], actual[2], actual[3],
                        expected[0], expected[1], expected[2], expected[3]);
                throw std::runtime_error(detail);
            }
        }
    }
}

template <typename Render>
void Run(const char* name, Render render) {
    for (const auto test : cases) {
        const auto pitch = test.frame.width * 4 + 12;
        const auto pixels = MakeFrame(test, pitch);
        CheckOutput(test, render(Frame{test.frame, pixels.data(), pitch, pixels.size()}, test.output));
        std::printf("  %s: %ux%u BGRA -> %ux%u RGBA with pixel-exact readback\n",
                    name, test.frame.width, test.frame.height, test.output.width, test.output.height);
    }
}

std::vector<std::uint32_t> ReadSpirv(const char* path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto size = input.tellg();
    if (size <= 0 || size % 4 != 0 || size > 65536) throw std::runtime_error("Invalid SPIR-V shader");
    std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / 4);
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(words.data()), size) || words.front() != 0x07230203) {
        throw std::runtime_error("Could not read SPIR-V shader");
    }
    return words;
}

void TestDxgi() {
    Com<IDXGIFactory1> factory;
    Require(CreateDXGIFactory1(__uuidof(IDXGIFactory1), factory.AsVoid()), "CreateDXGIFactory1");
    Com<ID3DBlob> vertex, pixel;
    CompileShader("VertexMain", "vs_5_0", vertex);
    CompileShader("PixelMain", "ps_5_0", pixel);
    unsigned devices = 0;
    for (UINT i = 0; ; ++i) {
        Com<IDXGIAdapter1> adapter;
        const auto next = factory->EnumAdapters1(i, adapter.Put());
        if (next == DXGI_ERROR_NOT_FOUND) break;
        Require(next, "EnumAdapters1");
        DXGI_ADAPTER_DESC1 desc{};
        Require(adapter->GetDesc1(&desc), "GetDesc1");
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        std::printf("DXGI hardware %ls\n", desc.Description);
        Run("DirectX 11", [&](Frame frame, Extent target) {
            return RenderD3D11(adapter.Get(), frame, target, vertex.Get(), pixel.Get());
        });
        Run("DirectX 12", [&](Frame frame, Extent target) {
            return RenderD3D12(adapter.Get(), frame, target, vertex.Get(), pixel.Get());
        });
        ++devices;
    }
    if (!devices) throw std::runtime_error("No hardware DXGI adapter");
}

void TestVulkan() {
    const auto vertex = ReadSpirv("movie-vs.spv");
    const auto fragment = ReadSpirv("movie-fs.spv");
    VulkanInstance instance;
    instance.Create();
    std::uint32_t count = 0;
    VkRequire(instance.EnumeratePhysicalDevices(instance.instance, &count, nullptr), "vkEnumeratePhysicalDevices");
    std::vector<VkPhysicalDevice> devices(count);
    VkRequire(instance.EnumeratePhysicalDevices(instance.instance, &count, devices.data()), "List Vulkan physical devices");
    unsigned tested = 0;
    for (const auto physical : devices) {
        VkPhysicalDeviceProperties properties{};
        instance.GetPhysicalDeviceProperties(physical, &properties);
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) continue;
        std::printf("Vulkan hardware %s\n", properties.deviceName);
        VulkanDevice device;
        device.Create(instance, physical);
        Run("Vulkan", [&](Frame frame, Extent target) {
            VulkanMoviePass renderer(device);
            return renderer.Draw(frame, target, vertex, fragment);
        });
        ++tested;
    }
    if (!tested) throw std::runtime_error("No hardware Vulkan adapter");
}

} // namespace

int main() {
    static_assert(sizeof(void*) == 4);
    try {
        const std::uint8_t unsupported[]{1, 2, 3};
        try {
            OutputSize(Frame{{1, 1}, unsupported, 4, sizeof(unsupported)}, {1, 1});
            throw std::runtime_error("Accepted truncated movie frame");
        } catch (const std::invalid_argument&) {
            std::puts("Truncated BGRA frame rejected");
        }
        TestDxgi();
        TestVulkan();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
