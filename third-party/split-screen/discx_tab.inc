extern "C" uint32_t __cdecl NfsmwFontAtlasSize(const uint8_t* texture);

struct DiscTexturePixels {
    unsigned width = 0, height = 0;
    std::vector<uint32_t> data;
};

static IDirect3DTexture9* FindNativeTexture(uint8_t* info) {
    auto* platform = info ? At<uint8_t*>(info, 0) : nullptr;
    return platform ? At<IDirect3DTexture9*>(platform, 0x18) : nullptr;
}

static DiscTexturePixels ReadDiscTexture(IDirect3DDevice9* device, IDirect3DTexture9* texture) {
    using Load = HRESULT (WINAPI*)(IDirect3DSurface9*, const PALETTEENTRY*, const RECT*,
                                  IDirect3DSurface9*, const PALETTEENTRY*, const RECT*, DWORD, D3DCOLOR);
    const auto library = GetModuleHandleW(L"d3dx9_26.dll");
    const auto load = library ? reinterpret_cast<Load>(GetProcAddress(library, "D3DXLoadSurfaceFromSurface")) : nullptr;
    D3DSURFACE_DESC desc{};
    if (!load || !texture || FAILED(texture->GetLevelDesc(0, &desc)) ||
        !desc.Width || !desc.Height || desc.Width > 2048 || desc.Height > 2048) return {};
    DiscTexturePixels pixels{desc.Width, desc.Height, std::vector<uint32_t>(desc.Width * desc.Height)};
    IDirect3DSurface9* source = nullptr;
    IDirect3DSurface9* staging = nullptr;
    if (FAILED(texture->GetSurfaceLevel(0, &source))) return {};
    const auto created = device->CreateOffscreenPlainSurface(desc.Width, desc.Height, D3DFMT_A8R8G8B8,
                                                             D3DPOOL_SYSTEMMEM, &staging, nullptr);
    const auto copied = SUCCEEDED(created) ? load(staging, nullptr, nullptr, source, nullptr, nullptr, 1, 0) : created;
    source->Release();
    if (!staging) return {};
    D3DLOCKED_RECT lock{};
    const bool ready = SUCCEEDED(copied) && SUCCEEDED(staging->LockRect(&lock, nullptr, D3DLOCK_READONLY));
    if (ready) {
        for (unsigned y = 0; y < pixels.height; ++y)
            std::memcpy(pixels.data.data() + y * pixels.width,
                        static_cast<const uint8_t*>(lock.pBits) + y * lock.Pitch, pixels.width * 4);
        staging->UnlockRect();
    }
    staging->Release();
    return ready ? std::move(pixels) : DiscTexturePixels{};
}

static int DiscTabBrightness(uint32_t pixel) {
    return (3 * ((pixel >> 16) & 255) + 6 * ((pixel >> 8) & 255) + (pixel & 255)) / 10;
}

static uint32_t BlendDiscTabInk(uint32_t fill, uint32_t ink, unsigned coverage) {
    uint32_t pixel = fill & 0xff000000u;
    for (unsigned shift : {0u, 8u, 16u}) {
        const auto background = (fill >> shift) & 255;
        const auto foreground = (ink >> shift) & 255;
        pixel |= ((background * (1020 - coverage) + foreground * coverage + 510) / 1020) << shift;
    }
    return pixel;
}

static bool PaintDiscTab(DiscTexturePixels& tab, IDirect3DDevice9* device) {
    // Read the current title font from the loaded package, including any
    // font substitution made by the game's language and HD assets.
    using FindObject = uint8_t* (__cdecl*)(const char*, uint32_t);
    auto* title = ((FindObject)0x524850)("EA_Trax.fng", 0xf5387816);
    if (!title || At<unsigned>(title, 0x18) != 2) return false;
    auto* font = ((uint8_t* (__cdecl*)(unsigned))0x585b50)(At<unsigned>(title, 0x24));
    auto* atlasInfo = font ? At<uint8_t*>(font, 8) : nullptr;
    auto* fontData = font ? At<uint8_t*>(font, 0xc) : nullptr;
    if (!atlasInfo || !fontData) return false;
    const auto atlas = ReadDiscTexture(device, FindNativeTexture(atlasInfo));
    const auto dimensions = NfsmwFontAtlasSize(atlasInfo);
    const unsigned logicalWidth = dimensions & 0xffff, logicalHeight = dimensions >> 16;
    if (atlas.data.empty() || !logicalWidth || !logicalHeight) return false;

    // The menu and race packages share the texture hash, but use opposite
    // lettering colors. Compare the label against the flat fill at its left.
    const unsigned fillColumn = tab.width / 16;
    int left = tab.width, top = tab.height, right = -1, bottom = -1;
    uint32_t ink = 0;
    int strongestContrast = 0;
    for (unsigned y = tab.height / 8; y < tab.height * 7 / 8; ++y) {
        const auto* row = tab.data.data() + y * tab.width;
        const int fill = DiscTabBrightness(row[fillColumn]);
        for (unsigned x = fillColumn + 1; x < tab.width / 2; ++x) {
            const uint32_t pixel = row[x];
            const int contrast = std::abs(DiscTabBrightness(pixel) - fill);
            if ((pixel >> 24) < 224 || contrast < 64) continue;
            left = std::min(left, int(x)); right = std::max(right, int(x));
            top = std::min(top, int(y)); bottom = std::max(bottom, int(y));
            if (contrast > strongestContrast) {
                strongestContrast = contrast;
                ink = pixel;
            }
        }
    }
    if (right <= left || bottom <= top) return false;

    constexpr char label[] = "DISCX";
    struct Glyph { uint8_t* data; float x, y; };
    std::array<Glyph, sizeof(label) - 1> glyphs{};
    float pen = 0.f, minX = INFINITY, minY = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
    for (unsigned i = 0; i < glyphs.size(); ++i) {
        auto* glyph = ((uint8_t* (THISCALL*)(void*, int))0x569bf0)(fontData, label[i]);
        if (!glyph) return false;
        const float x = pen + static_cast<int8_t>(glyph[9]), y = static_cast<int8_t>(glyph[10]);
        glyphs[i] = {glyph, x, y};
        minX = std::min(minX, x); maxX = std::max(maxX, x + glyph[2]);
        minY = std::min(minY, y); maxY = std::max(maxY, y + glyph[3]);
        pen += ((float (THISCALL*)(void*, short, short, unsigned))0x586050)(font, label[i], 0, 0);
    }
    if (!(maxX > minX && maxY > minY)) return false;
    const float scale = std::min((right - left + 1) / (maxY - minY),
                                 (bottom - top + 1) * 1.13f / (maxX - minX));
    const float centerX = (left + right + 1) * .5f, centerY = (top + bottom + 1) * .5f;
    const float centerU = (minX + maxX) * .5f, centerV = (minY + maxY) * .5f;
    const int padding = std::max(1, int(tab.width / 32));
    for (int y = std::max(0, top - padding); y <= std::min(int(tab.height) - 1, bottom + padding); ++y) {
        auto* row = tab.data.data() + y * tab.width;
        const auto fill = row[fillColumn] & 0xffffffu;
        for (int x = std::max(int(fillColumn) + 1, left - padding);
             x <= std::min(int(tab.width / 2) - 1, right + padding); ++x)
            row[x] = (row[x] & 0xff000000u) | fill;
    }

    const float fx = float(atlas.width) / logicalWidth, fy = float(atlas.height) / logicalHeight;
    for (const auto& item : glyphs) {
        auto* glyph = item.data;
        const unsigned u = At<uint16_t>(glyph, 4), v = At<uint16_t>(glyph, 6);
        const float x0 = centerX - (item.y + glyph[3] - centerV) * scale;
        const float y0 = centerY + (item.x - centerU) * scale;
        const float x1 = x0 + glyph[3] * scale, y1 = y0 + glyph[2] * scale;
        for (int y = std::max(0, int(std::floor(y0))); y < std::min(int(tab.height), int(std::ceil(y1))); ++y) {
            for (int x = std::max(0, int(std::floor(x0))); x < std::min(int(tab.width), int(std::ceil(x1))); ++x) {
                unsigned coverage = 0;
                for (float sy : {.25f, .75f}) for (float sx : {.25f, .75f}) {
                    const float cu = (y + sy - y0) / scale, cv = (x1 - x - sx) / scale;
                    if (cu < 0 || cv < 0 || cu >= glyph[2] || cv >= glyph[3]) continue;
                    const unsigned ax = std::min(unsigned((u + cu) * fx), atlas.width - 1);
                    const unsigned ay = std::min(unsigned((v + cv) * fy), atlas.height - 1);
                    coverage += atlas.data[ay * atlas.width + ax] >> 24;
                }
                auto& pixel = tab.data[y * tab.width + x];
                if (coverage && (pixel >> 24) >= 224) pixel = BlendDiscTabInk(pixel, ink, coverage);
            }
        }
    }
    return true;
}

static bool BuildDiscTab(EmbeddedIcon& tab, IDirect3DTexture9* source) {
    auto* device = Dev();
    if (!device) return false;
    auto pixels = ReadDiscTexture(device, source);
    if (pixels.data.empty() || !PaintDiscTab(pixels, device)) return false;
    IDirect3DTexture9* output = nullptr;
    if (FAILED(device->CreateTexture(pixels.width, pixels.height, 1, 0,
                                     D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &output, nullptr))) return false;
    D3DLOCKED_RECT lock{};
    if (FAILED(output->LockRect(0, &lock, nullptr, 0))) {
        output->Release();
        return false;
    }
    for (unsigned y = 0; y < pixels.height; ++y)
        std::memcpy(static_cast<uint8_t*>(lock.pBits) + y * lock.Pitch,
                    pixels.data.data() + y * pixels.width, pixels.width * 4);
    if (FAILED(output->UnlockRect(0))) {
        output->Release();
        return false;
    }
    if (tab.texture) tab.texture->Release();
    tab.texture = output;
    return true;
}
