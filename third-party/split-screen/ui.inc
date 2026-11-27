// ---------------------------------------------------------------- mod menus (drawn with D3D)
// included from main.cpp after the rendering helpers

#include "control_text.inc"

// The atlas follows the live game language and is recreated after a device reset.
static IDirect3DTexture9* g_fontTex = nullptr;
static constexpr int FONT_CELL_W = 32, FONT_CELL_H = 40, FONT_COLS = 16, FONT_TEX_W = 512, FONT_TEX_H = 1024;
static constexpr int fontGlyphCapacity = FONT_COLS * (FONT_TEX_H / FONT_CELL_H);
static wchar_t g_fontCharacters[fontGlyphCapacity];
static float g_glyphW[fontGlyphCapacity];
static int g_glyphCount = 0, g_fontLanguage = -1;

static bool ToWideText(const char* text, wchar_t (&wide)[256]) {
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, std::size(wide)) != 0 ||
           MultiByteToWideChar(CP_ACP, 0, text, -1, wide, std::size(wide)) != 0;
}

static int GlyphIndex(wchar_t character) {
    if (character >= 32 && character < 127) return character - 32;
    auto* end = g_fontCharacters + g_glyphCount;
    const auto* found = std::find(g_fontCharacters, end, character);
    return found == end ? '?' - 32 : static_cast<int>(found - g_fontCharacters);
}

static bool BuildFont() {
    const int language = ControlLanguage();
    if (g_fontTex && g_fontLanguage == language) return true;
    if (g_fontTex) { g_fontTex->Release(); g_fontTex = nullptr; }
    IDirect3DDevice9* d = Dev();
    if (!d) return false;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = FONT_TEX_W; bi.bmiHeader.biHeight = -FONT_TEX_H;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        DeleteDC(dc);
        return false;
    }
    HGDIOBJ oldb = SelectObject(dc, bmp);
    const wchar_t* face = language == 8 ? L"Malgun Gothic" : language == 9 ? L"Microsoft JhengHei" : L"Arial";
    HFONT font = CreateFontW(-30, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
    HGDIOBJ oldf = SelectObject(dc, font);
    memset(bits, 0, FONT_TEX_W * FONT_TEX_H * 4);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
    g_glyphCount = 0;
    for (wchar_t ch = 32; ch < 127; ++ch) g_fontCharacters[g_glyphCount++] = ch;
    for (wchar_t ch = 0xa0; ch < 0x100; ++ch) g_fontCharacters[g_glyphCount++] = ch;
    for (auto* label : kControlText[language]) {
        wchar_t wide[256]{};
        if (!ToWideText(label, wide)) continue;
        for (auto* ch = wide; *ch && g_glyphCount < fontGlyphCapacity; ++ch) {
            auto* end = g_fontCharacters + g_glyphCount;
            if (std::find(g_fontCharacters, end, *ch) == end) g_fontCharacters[g_glyphCount++] = *ch;
        }
    }
    for (int i = 0; i < g_glyphCount; ++i) {
        const auto x = (i % FONT_COLS) * FONT_CELL_W, y = (i / FONT_COLS) * FONT_CELL_H;
        SIZE extent{};
        GetTextExtentPoint32W(dc, g_fontCharacters + i, 1, &extent);
        g_glyphW[i] = static_cast<float>(std::min<LONG>(extent.cx, FONT_CELL_W - 2));
        TextOutW(dc, x + 1, y + 2, g_fontCharacters + i, 1);
    }
    GdiFlush();
    HRESULT hr = d->CreateTexture(FONT_TEX_W, FONT_TEX_H, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
                                  D3DPOOL_DEFAULT, &g_fontTex, nullptr);
    if (SUCCEEDED(hr)) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(g_fontTex->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD))) {
            for (int y = 0; y < FONT_TEX_H; y++) {
                uint32_t* src = (uint32_t*)bits + y * FONT_TEX_W;
                uint32_t* dst = (uint32_t*)((uint8_t*)lr.pBits + y * lr.Pitch);
                for (int x = 0; x < FONT_TEX_W; x++) { uint32_t a = src[x] & 0xFF; dst[x] = (a << 24) | 0xFFFFFF; }
            }
            g_fontTex->UnlockRect(0);
        } else { g_fontTex->Release(); g_fontTex = nullptr; }
    } else Log("font texture failed %08lX", hr);
    SelectObject(dc, oldf); DeleteObject(font);
    SelectObject(dc, oldb); DeleteObject(bmp); DeleteDC(dc);
    if (g_fontTex) g_fontLanguage = language;
    return g_fontTex != nullptr;
}

// ---- 2D drawing (screen pixels)
struct UIV { float x, y, z, rhw; DWORD c; float u, v; };
static float g_uiW, g_uiH, g_uiS;      // screen size, scale (1.0 at 1080p)
static IDirect3DStateBlock9* g_uiSB = nullptr;

static bool UIBegin() {
    IDirect3DDevice9* d = Dev();
    if (!d) return false;
    if (!g_uiSB) d->CreateStateBlock(D3DSBT_ALL, &g_uiSB);
    if (!g_uiSB || FAILED(g_uiSB->Capture())) return false;
    IDirect3DSurface9* rt = nullptr;
    if (FAILED(d->GetRenderTarget(0, &rt)) || !rt) return false;
    D3DSURFACE_DESC desc{};
    const auto result = rt->GetDesc(&desc);
    rt->Release();
    if (FAILED(result) || !desc.Width || !desc.Height) return false;
    g_uiW = static_cast<float>(desc.Width);
    g_uiH = static_cast<float>(desc.Height);
    g_uiS = std::min(g_uiW / 1920.f, g_uiH / 1080.f);
    D3DVIEWPORT9 vp = { 0, 0, desc.Width, desc.Height, 0.f, 1.f };
    d->SetViewport(&vp);
    d->SetVertexShader(nullptr); d->SetPixelShader(nullptr);
    d->SetRenderState(D3DRS_ZENABLE, FALSE);
    d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    d->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    d->SetRenderState(D3DRS_FOGENABLE, FALSE);
    d->SetRenderState(D3DRS_LIGHTING, FALSE);
    d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    d->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    d->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    d->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    d->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    d->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    d->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    d->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    d->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    d->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    return true;
}
static void UIEnd() { if (g_uiSB) g_uiSB->Apply(); }

static void UIRect(float x, float y, float w, float h, DWORD c) {
    IDirect3DDevice9* d = Dev();
    d->SetTexture(0, nullptr);
    d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
    x -= .5f; y -= .5f;
    UIV q[4] = { {x,y,0,1,c,0,0}, {x+w,y,0,1,c,0,0}, {x,y+h,0,1,c,0,0}, {x+w,y+h,0,1,c,0,0} };
    d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(UIV));
    d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
}

static float UITextWidth(const char* s, float size) {
    float w = 0, k = size / 30.f;
    wchar_t wide[256]{};
    if (ToWideText(s, wide)) for (auto* ch = wide; *ch; ++ch) w += g_glyphW[GlyphIndex(*ch)] * k;
    return w;
}
// align: 0 left, 1 centre, 2 right
static void UIText(float x, float y, float size, DWORD c, const char* s, int align = 0) {
    wchar_t wide[256]{};
    if (!g_fontTex || !ToWideText(s, wide)) return;
    IDirect3DDevice9* d = Dev();
    float k = size / 30.f;
    if (align) { float w = UITextWidth(s, size); x -= align == 1 ? w / 2 : w; }
    d->SetTexture(0, g_fontTex);
    UIV v[6 * 64]; int n = 0;
    auto flush = [&]() { if (n) d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, n / 3, v, sizeof(UIV)); n = 0; };
    for (auto* ch = wide; *ch; ++ch) {
        const int i = GlyphIndex(*ch);
        float u0 = (float)((i % FONT_COLS) * FONT_CELL_W) / FONT_TEX_W, v0 = (float)((i / FONT_COLS) * FONT_CELL_H) / FONT_TEX_H;
        float u1 = u0 + (g_glyphW[i] + 2) / FONT_TEX_W, v1 = v0 + (float)FONT_CELL_H / FONT_TEX_H;
        float x0 = x - .5f, y0 = y - .5f, x1 = x0 + (g_glyphW[i] + 2) * k, y1 = y0 + FONT_CELL_H * k;
        UIV a = {x0,y0,0,1,c,u0,v0}, b = {x1,y0,0,1,c,u1,v0}, cc = {x0,y1,0,1,c,u0,v1}, dd = {x1,y1,0,1,c,u1,v1};
        v[n++] = a; v[n++] = b; v[n++] = cc; v[n++] = b; v[n++] = dd; v[n++] = cc;
        if (n >= 6 * 64) flush();
        x += g_glyphW[i] * k;
    }
    flush();
}

static float* GameDeviceValues();
// ---- menu input (keyboard + every XInput pad, edge triggered)
struct MenuInput { bool up, down, left, right, accept, back, clear; int padAccept; bool kbAccept; };
static bool g_keyPrev[256];
static WORD g_padPrev[4]; static int g_padAxisPrev[4];
static XINPUT_STATE g_padNow[4]; static bool g_padConn[4];

static void PollPads() {
    for (int i = 0; i < 4; i++) {
        memset(&g_padNow[i], 0, sizeof(XINPUT_STATE));
        g_padConn[i] = pXInputGetState && pXInputGetState(i, &g_padNow[i]) == ERROR_SUCCESS;
    }
}
static bool KeyPressed(int vk) {
    bool down = GameHasFocus() && KeyDown(vk);
    bool p = down && !g_keyPrev[vk];
    g_keyPrev[vk] = down;
    return p;
}
static MenuInput ReadMenuInput() {
    MenuInput m = {}; m.padAccept = -1;
    m.up = KeyPressed(VK_UP); m.down = KeyPressed(VK_DOWN);
    m.left = KeyPressed(VK_LEFT); m.right = KeyPressed(VK_RIGHT);
    m.kbAccept = KeyPressed(VK_RETURN); m.accept = m.kbAccept;
    m.back = KeyPressed(VK_ESCAPE);
    const bool deletePressed = KeyPressed(VK_DELETE);
    const bool backspacePressed = KeyPressed(VK_BACK);
    m.clear = deletePressed || backspacePressed;
    for (int i = 0; i < 4; i++) {
        WORD b = g_padConn[i] ? g_padNow[i].Gamepad.wButtons : 0;
        WORD pr = b & ~g_padPrev[i]; g_padPrev[i] = b;
        int ax = 0;
        if (g_padConn[i]) {
            SHORT ly = g_padNow[i].Gamepad.sThumbLY, lx = g_padNow[i].Gamepad.sThumbLX;
            if (ly > 20000) ax = 1; else if (ly < -20000) ax = 2; else if (lx < -20000) ax = 3; else if (lx > 20000) ax = 4;
        }
        int axp = ax != g_padAxisPrev[i] ? ax : 0; g_padAxisPrev[i] = ax;
        if ((pr & XINPUT_GAMEPAD_DPAD_UP) || axp == 1) m.up = true;
        if ((pr & XINPUT_GAMEPAD_DPAD_DOWN) || axp == 2) m.down = true;
        if ((pr & XINPUT_GAMEPAD_DPAD_LEFT) || axp == 3) m.left = true;
        if ((pr & XINPUT_GAMEPAD_DPAD_RIGHT) || axp == 4) m.right = true;
        if (pr & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_START)) { m.accept = true; if (m.padAccept < 0) m.padAccept = i; }
        if (pr & XINPUT_GAMEPAD_B) m.back = true;
        if (pr & XINPUT_GAMEPAD_X) m.clear = true;
    }
    return m;
}
static void SyncInputState() {   // swallow anything currently held so it doesn't trigger next frame
    for (int vk = 1; vk < 256; vk++) g_keyPrev[vk] = GameHasFocus() && KeyDown(vk);
    for (int i = 0; i < 4; i++) g_padPrev[i] = g_padConn[i] ? g_padNow[i].Gamepad.wButtons : 0;
}

static void QueueSwitch(const char* pkg, int arg, uint32_t mask) {
    void* fe = *(void**)0x91CADC;
    if (fe && g_qpsTramp) Hook_QueuePackageSwitch(fe, nullptr, pkg, arg, mask, 0);
}

static const DWORD COL_PANEL = 0xE0101418, COL_TITLE = 0xFFFFB400, COL_TEXT = 0xFFE8E8E8,
                   COL_DIM = 0xFF8A8A8A, COL_SEL = 0xFFFFB400, COL_SELBG = 0x60FFB400, COL_OK = 0xFF63D25A;

static int customPlayers = 1;
static int CustomPlayerLimit() { return g_quadPort[0] < 0 ? 2 : g_quadPort[1] < 0 ? 3 : 4; }

static void StartCustomPlayerChoice() {
    customPlayers = 1;
    g_inputBlocked = true;
    PollPads();
    SyncInputState();
}

static void FinishCustomPlayerChoice(bool cancel) {
    const auto pending = customTrackSwitch;
    customTrackSwitch = {};
    if (cancel) {
        Hook_QueuePackageSwitch(pending.frontend, nullptr, "Track_Select.fng", 0, 0, 0);
        return;
    }
    auto* database = *G::pFEDatabase;
    SetLocalPlayerCount(customPlayers);
    database[G::FEDB_NUMPLAYERS] = customPlayers > 1 ? 2 : 1;
    if (customPlayers > 1) {
        SplitCamerasNear();
        *G::pQRMode = 2;
        At<int8_t>(database, G::FEDB_JOYPORT1) = static_cast<int8_t>(g_p2port);
    }
    Log("Custom Race selected with %d player(s)", customPlayers);
    Hook_QueuePackageSwitch(pending.frontend, nullptr, pending.package, pending.arg, pending.mask, pending.flags);
}

static void UpdateCustomPlayerChoice() {
    if (*G::pGameFlowState != 3 || !LocalQuickRace(*G::pFEDatabase)) {
        customTrackSwitch = {};
        return;
    }
    PollPads();
    const auto input = ReadMenuInput();
    const int count = CustomPlayerLimit();
    int index = customPlayers - 1;
    if (input.left || input.up) index = (index + count - 1) % count;
    if (input.right || input.down) index = (index + 1) % count;
    customPlayers = index + 1;
    if (input.accept || input.back) FinishCustomPlayerChoice(input.back);
}

static void DrawCustomPlayerChoice() {
    const float scale = g_uiS;
    const int count = CustomPlayerLimit();
    const float width = (350 * count + 35) * scale;
    const float height = 255 * scale;
    const float x = (g_uiW - width) / 2;
    const float y = (g_uiH - height) / 2;
    const bool spanish = At<int>(0x8F41C0) == 4;
    UIRect(0, 0, g_uiW, g_uiH, 0x90000000);
    UIRect(x, y, width, height, COL_PANEL);
    UIRect(x, y, width, 5 * scale, COL_TITLE);
    UIText(g_uiW / 2, y + 25 * scale, 45 * scale, COL_TITLE,
           spanish ? "Carrera personalizada" : "Custom race", 1);
    for (int col = 0; col < count; ++col) {
        const int players = col + 1;
        const float left = x + (35 + 350 * col) * scale;
        const bool selected = players == customPlayers;
        UIRect(left, y + 100 * scale, 315 * scale, 64 * scale, selected ? COL_SELBG : 0x20FFFFFF);
        char label[24];
        snprintf(label, sizeof(label), players == 1 ? (spanish ? "%d jugador" : "%d player")
                                                    : (spanish ? "%d jugadores" : "%d players"), players);
        UIText(left + 157.5f * scale, y + 111 * scale, 34 * scale, selected ? COL_SEL : COL_TEXT, label, 1);
    }
    UIText(g_uiW / 2, y + 195 * scale, 27 * scale, COL_DIM,
           spanish ? "Flechas: seleccionar    Enter / A: aceptar    Esc / B: volver"
                   : "Arrows to select     Enter / A to confirm     Esc / B to go back", 1);
}

// ================================================================ join screen
// The first device that presses joins as Player 1, the next one as Player 2 (and 3, 4 in the
// 3- and 4-player modes). The join order determines who drives in each view.
static bool g_fourMode = false;             // the expanded race engine is needed
static int g_fourHumans = 4;
static bool g_joinActive = false;
static int g_join[MAX_PLAYERS] = { -2, -2, -2, -2 };   // -2 not joined, -1 keyboard, 0..3 controller
static DWORD g_joinDoneTick = 0;
static int JoinSlots() { return g_localPlayerCount; }
static int JoinCount() { int n = 0; for (int p = 0; p < JoinSlots(); p++) if (g_join[p] != -2) n++; return n; }

static float* GameDeviceValues() {
    uint8_t* dev = At<uint8_t*>(G::pIOModule, 8);
    return dev ? At<float*>(dev, 0x20) : nullptr;
}

static void StartJoinScreen() {
    g_joinActive = true; for (auto& j : g_join) j = -2; g_joinDoneTick = 0;
    PollPads(); SyncInputState();
    Log("Join screen opened (%d players)", JoinSlots());
}

static void JoinScreenCancel() {
    g_joinActive = false;
    uint8_t* db = *G::pFEDatabase;
    bool trackSel = db && (At<uint32_t>(db, G::FEDB_GAMEMODE) & 4) && db[0] == 2 && At<int>(db, 0x18) == 2;
    QueueSwitch(trackSel ? "Track_Select.fng" : "Track_Options.fng", 0, 0);
}

static void JoinDevice(int dev) {
    int n = JoinSlots();
    for (int p = 0; p < n; p++) if (g_join[p] == dev && dev >= 0) return;   // controller already in
    for (int p = 0; p < n; p++) if (g_join[p] == -2) { g_join[p] = dev; return; }
}
static bool IsJoined(int dev) { for (int p = 0; p < JoinSlots(); p++) if (g_join[p] == dev) return true; return false; }
static void LeaveSlot(int p) {             // later players move up
    int n = JoinSlots();
    for (int i = p; i < n - 1; i++) g_join[i] = g_join[i + 1];
    g_join[n - 1] = -2;
}

static void UpdateJoinScreen() {
    PollPads();
    bool esc = KeyPressed(VK_ESCAPE), enter = KeyPressed(VK_RETURN), tab = KeyPressed(VK_TAB);
    DWORD now = GetTickCount();
    const int n = JoinSlots();
    if (g_joinDoneTick) {
        if (now - g_joinDoneTick > 700) {
            g_joinActive = false;
            int joined = JoinCount();
            for (int p = 0; p < n; p++) if (g_join[p] != -2) g_dev[p] = g_join[p];
            cfg.p2pad = g_dev[1];
            for (auto& s : g_lastPadState) s = -1;
            g_fourHumans = g_fourMode ? joined : 2;
            SaveBindings();
            QueueSwitch("Car_Select.fng", 0, 7);
            Log("Players joined (%d): P1 device %d, P2 device %d, P3 device %d, P4 device %d", joined,
                g_join[0], g_join[1], g_join[2], g_join[3]);
        }
        return;
    }
    // Controllers: A or Start joins, B leaves.
    for (int i = 0; i < 4; i++) {
        WORD b = g_padConn[i] ? g_padNow[i].Gamepad.wButtons : 0;
        WORD pr = b & ~g_padPrev[i]; g_padPrev[i] = b;
        if (pr & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_START)) JoinDevice(i);
        if (pr & XINPUT_GAMEPAD_B) {
            bool was = false;
            for (int p = 0; p < n; p++) if (g_join[p] == i) { LeaveSlot(p); was = true; break; }
            if (!was && JoinCount() == 0) { JoinScreenCancel(); return; }
        }
    }
    // Keyboard: Enter joins and Tab adds another keyboard player.
    // (shared keyboard, each with their own keys)
    if (enter) {
        if (!IsJoined(-1)) JoinDevice(-1);
    } else if (tab && IsJoined(-1)) { for (int p = 0; p < n; p++) if (g_join[p] == -2) { g_join[p] = -1; break; } }
    if (esc) {
        if (JoinCount() == 0) { JoinScreenCancel(); return; }
        for (auto& j : g_join) j = -2;
    }
    if (JoinCount() == n) g_joinDoneTick = now;
}

static void DeviceLabel(int dev, char* b, int n) {
    const bool spanish = At<int>(0x8F41C0) == 4;
    if (dev == -1) snprintf(b, n, "%s", spanish ? "Teclado" : "Keyboard");
    else snprintf(b, n, spanish ? "Mando %d" : "Controller %d", dev + 1);
}

static void DrawJoinScreen() {
    float S = g_uiS, W = g_uiW, H = g_uiH;
    int n = JoinSlots();
    const float slotH = 112 * S, gap = 128 * S;
    const float pw = 1100 * S, ph = (320 + n * 128) * S, px = (W - pw) / 2, py = (H - ph) / 2;
    const bool spanish = At<int>(0x8F41C0) == 4;
    UIRect(0, 0, W, H, 0x90000000);
    UIRect(px, py, pw, ph, COL_PANEL);
    UIRect(px, py, pw, 6 * S, COL_TITLE);
    char title[48];
    snprintf(title, sizeof title, spanish ? "Pantalla dividida para %d" : "%d-player split screen", n);
    UIText(W / 2, py + 30 * S, 52 * S, COL_TITLE, title, 1);
    UIText(W / 2, py + 95 * S, 28 * S, COL_DIM,
           spanish ? "Pulsa A en el mando o Enter en el teclado para unirte."
                   : "Press A on a controller or Enter on the keyboard to join.", 1);
    char b[96];
    int next = -1;
    for (int p = 0; p < n; p++) if (g_join[p] == -2) { next = p; break; }
    for (int p = 0; p < n; p++) {
        int st = g_join[p];
        float y = py + 180 * S + p * gap;
        bool waiting = st == -2 && p == next;
        UIRect(px + 60 * S, y, pw - slotH, slotH, waiting ? 0x30FFB400 : 0x20FFFFFF);
        snprintf(b, sizeof b, spanish ? "Jugador %d" : "Player %d", p + 1);
        UIText(px + 90 * S, y + 14 * S, 44 * S, waiting ? COL_SEL : COL_TEXT, b);
        if (st != -2) {
            char dl[32]; DeviceLabel(st, dl, sizeof dl);
            snprintf(b, sizeof b, spanish ? "Listo  -  %s" : "Ready  -  %s", dl);
            UIText(px + 90 * S, y + 64 * S, 30 * S, COL_OK, b);
        } else if (waiting) {
            UIText(px + 90 * S, y + 64 * S, 28 * S, COL_TEXT,
                   IsJoined(-1)
                       ? (spanish ? "Pulsa A en el mando o Tab para compartir teclado"
                                  : "Press A on a controller, or Tab to share the keyboard")
                       : (spanish ? "Pulsa A / Start en el mando o Enter en el teclado"
                                  : "Press A / Start on a controller, or Enter on the keyboard"));
        } else UIText(px + 90 * S, y + 64 * S, 28 * S, COL_DIM, spanish ? "En espera..." : "Waiting...");
    }
    const char* footer = g_joinDoneTick ? (spanish ? "Preparados" : "Get ready")
        : (spanish ? "B: salir    Esc: volver" : "B to leave    Esc to go back");
    UIText(W / 2, py + ph - 55 * S, 26 * S, COL_DIM, footer, 1);
}

// ================================================================ controls screen (both players)
// Only Player 1 / Player 2 is chosen here; which controller each player uses is decided on the
// join screen. Every action has a controller binding and a keyboard binding (two columns).
static bool g_ctlActive = false;
static int g_ctlRow = 0, g_ctlPlayer = 0, g_ctlCol = 0;      // col 0 = controller, 1 = keyboard
static bool g_ctlListening = false; static DWORD g_ctlListenTick = 0; static bool g_ctlArmed = false;
static char g_ctlReturnPkg[64]; static int g_ctlReturnArg = 1;
static const int CTL_ROWS = NUM_BIND + 4;   // player, device, actions, reset, done
static const int CTL_FIRST_ACTION = 2;

static void CycleControlDevice(int player, int direction) {
    constexpr int count = MAX_PLAYERS + 1;
    int index = g_dev[player] + 1;
    for (int candidate = 0; candidate < count; ++candidate) {
        index = (index + direction + count) % count;
        if (index == 0) { g_dev[player] = -1; break; }
        const int device = index - 1;
        if (!g_padConn[device] || std::find(g_dev, g_dev + MAX_PLAYERS, device) != g_dev + MAX_PLAYERS) continue;
        g_dev[player] = device;
        break;
    }
    g_kbSteer[player][0] = g_kbSteer[player][1] = 0;
    g_lastPadState[player] = -1;
    cfg.p2pad = g_dev[1];
    SyncInputState();
}

static void OpenControlsScreen(const char* returnPkg, int arg) {
    g_ctlActive = true; g_ctlRow = 0; g_ctlPlayer = 0; g_ctlCol = 0; g_ctlListening = false;
    snprintf(g_ctlReturnPkg, sizeof g_ctlReturnPkg, "%s", returnPkg ? returnPkg : "Pause_Main.fng");
    g_ctlReturnArg = arg;
    PollPads(); SyncInputState();
    Log("Controls screen opened (return to %s %d)", g_ctlReturnPkg, arg);
}
static void CloseControlsScreen() {
    g_ctlActive = false;
    SaveBindings();
    QueueSwitch(g_ctlReturnPkg, g_ctlReturnArg, 0);
}

static int CapturePadInput(int device) {
    const auto& p = g_padNow[device].Gamepad;
    for (int c = PI_A; c <= PI_DRIGHT; ++c)
        if (p.wButtons & kPadButtonMask[c]) return c;
    if (p.bLeftTrigger > 150) return PI_LT;
    if (p.bRightTrigger > 150) return PI_RT;
    constexpr int threshold = 24000;
    if (p.sThumbLX < -threshold) return PI_LSL;
    if (p.sThumbLX > threshold) return PI_LSR;
    if (p.sThumbLY > threshold) return PI_LSU;
    if (p.sThumbLY < -threshold) return PI_LSD;
    if (p.sThumbRX < -threshold) return PI_RSL;
    if (p.sThumbRX > threshold) return PI_RSR;
    if (p.sThumbRY > threshold) return PI_RSU;
    if (p.sThumbRY < -threshold) return PI_RSD;
    return PI_NONE;
}

static bool PadInputHeld(int device) {
    const auto& p = g_padNow[device].Gamepad;
    return p.wButtons || p.bLeftTrigger > 60 || p.bRightTrigger > 60 ||
        abs(p.sThumbLX) > 16000 || abs(p.sThumbLY) > 16000 ||
        abs(p.sThumbRX) > 16000 || abs(p.sThumbRY) > 16000;
}

static void UpdateControlsScreen() {
    PollPads();
    int pl = g_ctlPlayer;
    if (g_ctlListening) {
        int a = g_ctlRow - CTL_FIRST_ACTION;
        if (GetTickCount() - g_ctlListenTick < 250) { SyncInputState(); return; }
        if (g_ctlCol == 1) {                                  // keyboard
            for (int vk = 8; vk < 256; vk++) {
                if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) continue;   // use the left/right variants
                if (KeyPressed(vk)) {
                    g_keyBind[pl][a] = vk;
                    g_ctlListening = false; SyncInputState(); return;
                }
            }
        } else {                                              // controller
            const int device = g_dev[pl];
            if (device >= 0 && g_padConn[device]) {
                if (!g_ctlArmed) g_ctlArmed = !PadInputHeld(device);
                else if (const auto input = CapturePadInput(device); input != PI_NONE) {
                    g_padBind[pl][a] = input;
                    g_ctlListening = false;
                    g_ctlArmed = false;
                    SyncInputState();
                    return;
                }
            } else {
                g_ctlArmed = false;
            }
            if (KeyPressed(VK_ESCAPE)) { g_ctlListening = false; g_ctlArmed = false; SyncInputState(); }
        }
        return;
    }
    MenuInput m = ReadMenuInput();
    if (m.up) g_ctlRow = (g_ctlRow + CTL_ROWS - 1) % CTL_ROWS;
    if (m.down) g_ctlRow = (g_ctlRow + 1) % CTL_ROWS;
    if (m.back) { CloseControlsScreen(); return; }
    if (g_ctlRow == 0) { if (m.left) g_ctlPlayer = (g_ctlPlayer + MAX_PLAYERS - 1) % MAX_PLAYERS; if (m.right || m.accept) g_ctlPlayer = (g_ctlPlayer + 1) % MAX_PLAYERS; return; }
    if (g_ctlRow == 1) {
        if (m.left || m.right || m.accept) CycleControlDevice(pl, m.left ? -1 : 1);
        return;
    }
    if (g_ctlRow >= CTL_FIRST_ACTION && g_ctlRow < CTL_FIRST_ACTION + NUM_BIND) {
        int a = g_ctlRow - CTL_FIRST_ACTION;
        if (m.left) g_ctlCol = 0;
        if (m.right) g_ctlCol = 1;
        if (m.clear) { if (g_ctlCol == 1) g_keyBind[pl][a] = 0; else g_padBind[pl][a] = PI_NONE; }
        else if (m.accept) { g_ctlListening = true; g_ctlArmed = false; g_ctlListenTick = GetTickCount(); }
    } else if (g_ctlRow == CTL_FIRST_ACTION + NUM_BIND && m.accept) { ResetBindings(pl); }
    else if (g_ctlRow == CTL_FIRST_ACTION + NUM_BIND + 1 && m.accept) { CloseControlsScreen(); }
}

static void DrawControlsScreen() {
    float S = g_uiS, W = g_uiW, H = g_uiH;
    float pw = 1300 * S, ph = 1000 * S, px = (W - pw) / 2, py = (H - ph) / 2;
    int pl = g_ctlPlayer;
    UIRect(0, 0, W, H, 0xA0000000);
    UIRect(px, py, pw, ph, COL_PANEL);
    UIRect(px, py, pw, 6 * S, COL_TITLE);
    UIText(px + 50 * S, py + 24 * S, 50 * S, COL_TITLE, ControlText(ControlTitle));
    char b[96];
    float rowH = 44 * S, y = py + 100 * S, lx = px + 50 * S;
    float c1 = px + 560 * S, c2 = px + 940 * S, cw = 340 * S;        // column centres
    for (int r = 0; r < CTL_ROWS; r++) {
        bool sel = r == g_ctlRow;
        float ts = 30 * S;
        if (r == 0) {
            if (sel) UIRect(px + 30 * S, y - 4 * S, pw - 60 * S, rowH - 6 * S, COL_SELBG);
            snprintf(b, sizeof b, "<  %s %d  >", ControlText(ControlPlayer), pl + 1);
            UIText(px + pw / 2, y + 2 * S, 34 * S, sel ? COL_SEL : COL_TITLE, b, 1);
            y += rowH + 10 * S;
            continue;
        }
        if (r == 1) {
            if (sel) UIRect(px + 30 * S, y - 4 * S, pw - 60 * S, rowH - 6 * S, COL_SELBG);
            if (g_dev[pl] < 0) snprintf(b, sizeof b, "<  %s  >", ControlText(ControlKeyboard));
            else if (g_padConn[g_dev[pl]])
                snprintf(b, sizeof b, "<  %s %d  >", ControlText(ControlController), g_dev[pl] + 1);
            else
                snprintf(b, sizeof b, "<  %s %d (%s)  >", ControlText(ControlController), g_dev[pl] + 1, ControlText(ControlDisconnected));
            UIText(px + pw / 2, y + 2 * S, 30 * S, sel ? COL_SEL : COL_TEXT, b, 1);
            y += rowH + 10 * S;
            UIText(c1, y, 26 * S, COL_DIM, ControlText(ControlController), 1);
            UIText(c2, y, 26 * S, COL_DIM, ControlText(ControlKeyboard), 1);
            y += 36 * S;
            continue;
        }
        if (r < CTL_FIRST_ACTION + NUM_BIND) {
            int a = r - CTL_FIRST_ACTION;
            if (sel) UIRect((g_ctlCol ? c2 : c1) - cw / 2, y - 4 * S, cw, rowH - 6 * S, COL_SELBG);
            UIText(lx, y + 4 * S, ts, sel ? 0xFFFFFFFF : COL_TEXT, ControlText(ControlActionBase + a));
            for (int col = 0; col < 2; col++) {
                const char* val;
                bool here = sel && g_ctlCol == col;
                if (here && g_ctlListening) {
                    const int device = g_dev[pl];
                    val = ControlText(col ? ControlPressKey : device >= 0 && g_padConn[device] ? ControlPressButton : ControlUnavailable);
                }
                else if (col) val = KeyName(g_keyBind[pl][a], b, sizeof b);
                else val = kPadNames[g_padBind[pl][a]];
                UIText(col ? c2 : c1, y + 4 * S, ts, here ? COL_SEL : COL_TEXT, val, 1);
            }
        } else {
            if (r == CTL_FIRST_ACTION + NUM_BIND) y += 14 * S;
            if (sel) UIRect(px + 30 * S, y - 4 * S, pw - 60 * S, rowH - 6 * S, COL_SELBG);
            UIText(lx, y + 4 * S, ts, sel ? COL_SEL : COL_TEXT, ControlText(r == CTL_FIRST_ACTION + NUM_BIND ? ControlDefaults : ControlDone));
        }
        y += rowH;
    }
    const auto* help = ControlText(ControlHelp);
    const float helpSize = std::min(24 * S, 24 * S * .92f * pw / std::max(1.f, UITextWidth(help, 24 * S)));
    UIText(px + pw / 2, py + ph - 55 * S, helpSize, COL_DIM, help, 1);
}

// ================================================================ entry point from Hook_FERender
// Which XInput pad does the game's own input (DirectInput, port 0) read? When the game reports a
// menu press in the same moment one pad presses A/START, that's the one. Our devices then leave
// menu actions on that pad to the game, so menus don't move twice.
static void DetectGamePad() {
    static bool prevAcc = true; static WORD prevB[4]; static DWORD padTick[4];
    float* gv = GameDeviceValues();
    bool acc = gv && (gv[32] > .5f || gv[34] > .5f);
    DWORD now = GetTickCount();
    for (int i = 0; i < 4; i++) {
        XINPUT_STATE st; memset(&st, 0, sizeof st);
        WORD b = (pXInputGetState && pXInputGetState(i, &st) == ERROR_SUCCESS) ? st.Gamepad.wButtons : 0;
        if ((b & ~prevB[i]) & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_START)) padTick[i] = now;
        prevB[i] = b;
    }
    if (acc && !prevAcc) {
        int found = -1, n = 0;
        for (int i = 0; i < 4; i++) if (padTick[i] && now - padTick[i] < 150) { found = i; n++; }
        if (n == 1 && found != g_gamePad) { g_gamePad = found; Log("Game's own controller is XInput pad %d", found); }
    }
    prevAcc = acc;
}

#include "discx_menu.inc"

static void UpdateAndDrawMenus() {
    if (SplitRaceSetup()) DetectGamePad();
    if (unlimited_discx::MusicActive()) ApplyDiscVolume();
    if (g_discxOpen && (*G::pGameFlowState != 3 ||
        ((void* (__cdecl*)(const char*))0x591e90)("MainMenu_Sub.fng") != g_discxOwner))
        CloseDiscScreen(false);
    bool any = g_joinActive || g_ctlActive || customTrackSwitch.frontend;
    g_inputBlocked = any;
    if (g_discxOpen) {
        const auto now = unlimited_discx::NowPlaying();
        UpdateDiscScreen(DiscScreenLayout(g_uiW, g_uiH), now);
        if (g_discxOpen && UIBegin()) {
            DrawDiscScreen(DiscScreenLayout(g_uiW, g_uiH), now);
            UIEnd();
        }
        return;
    }
    if (!any) return;
    if (g_joinActive) UpdateJoinScreen();
    if (g_ctlActive) UpdateControlsScreen();
    if (customTrackSwitch.frontend) UpdateCustomPlayerChoice();
    if (!g_joinActive && !g_ctlActive && !customTrackSwitch.frontend) return;
    if (!BuildFont() || !UIBegin()) return;
    if (g_joinActive) DrawJoinScreen();
    if (g_ctlActive) DrawControlsScreen();
    if (customTrackSwitch.frontend) DrawCustomPlayerChoice();
    UIEnd();
}
static void ReleaseUIResources() {
    if (g_uiSB) { g_uiSB->Release(); g_uiSB = nullptr; }
    if (g_fontTex) { g_fontTex->Release(); g_fontTex = nullptr; }
    if (g_discxTextLayer) { g_discxTextLayer->Release(); g_discxTextLayer = nullptr; }
}
