#pragma once

namespace {

// In these packages FEng lays out indexed label/value pairs. Use their authored
// anchors and arrow bounds, including the shorter controls in the pause menu.
constexpr const char* optionPackages[]{
    "Options.fng", "OptionsPCDisplay.fng", "UI_OptionsController.fng", "Track_Options.fng",
    "Pause_Options.fng", "Pause_PCDisplayOptions.fng", "Pause_Controller.fng"
};
constexpr std::uint32_t firstOptionName = 0xb2980ba8u, firstOptionValue = 0x9b483b01u;
constexpr std::uint32_t firstLeftArrow = 0x7bcd6704u, firstRightArrow = 0x7c51b6d7u;

// FEng hashes the decimal suffix with h = 33*h + character. Names 10
// through 15 consequently live in a second range after the one-digit names.
constexpr std::uint32_t OptionRowHash(std::uint32_t first,unsigned index) {
    return index<9 ? first+index : first*33u+'0'+(index-9);
}
unsigned OptionRowIndex(std::uint32_t hash,std::uint32_t first) {
    if(hash-first<9)return hash-first;
    const auto suffix=hash-OptionRowHash(first,9);
    return suffix<6?9+suffix:15;
}

struct OptionColor { int blue, green, red, alpha; };
struct OptionPlate { float left, top, right, bottom; };

bool GetOptionArrow(void* object, float& center, float& radius) {
    if (!object || HudField<unsigned>(object,0x18) != 1) return false;
    auto* data = HudField<void*>(object,0x2c);
    if (!data) return false;
    center = HudField<float>(data,0x1c);
    radius = std::fabs(HudField<float>(data,0x38)) * .5f;
    return std::isfinite(center) && radius > 0 && radius < 64;
}

float OptionValueHalfWidth(const char* package, unsigned index, float origin) {
    float a = 0, b = 0, ra = 0, rb = 0;
    if (!GetOptionArrow(FindHudObject(package,OptionRowHash(firstLeftArrow,index)),a,ra) ||
        !GetOptionArrow(FindHudObject(package,OptionRowHash(firstRightArrow,index)),b,rb)) return 68;
    if (a > b) { std::swap(a,b); std::swap(ra,rb); }
    return std::min(origin-a-ra,b-rb-origin) - 2;
}

void AlignOptionLabel(D3DMATRIX& matrix, const char* package, void* label, float parentScale) {
    auto* first = FindHudObject(package, firstOptionName);
    if (!first || first == label || HudField<unsigned>(first, 0x18) != 2 ||
        !HudField<void*>(first, 0x2c)) return;
    // Several native widgets move their titles by a fraction of the string
    // width when positioned. Keep the original label column for every row.
    const float offset = ReadHudPosition(first).x - ReadHudPosition(label).x;
    if (std::isfinite(offset) && std::fabs(offset) <= 160.0f) matrix._41 += offset * parentScale;
}

bool FitOptionText(D3DMATRIX& transform, OptionPlate& plate, const AspectLayout& aspect,
                   bool value, unsigned format, float naturalWidth, float fontHeight,
                   float parentScale, float nativeHalfWidth) {
    if (std::fabs(transform._12) > .001f || std::fabs(transform._21) > .001f ||
        std::fabs(transform._13) > .001f || std::fabs(transform._23) > .001f ||
        !(transform._11 > 0 && transform._22 > 0 && parentScale > 0 && parentScale < 12 &&
          fontHeight > 0 && fontHeight < 256 && naturalWidth > 0 && naturalWidth < 65536)) return false;
    const float viewHalfWidth = 320 / aspect.hudX, viewHalfHeight = 240 / aspect.hudY;
    const float xInset = std::min(viewHalfWidth * .04f,24.0f);
    const float yInset = std::min(viewHalfHeight * .04f,18.0f);
    const float x = transform._41;
    const float leftRoom = x-(320-viewHalfWidth+xInset);
    const float rightRoom = 320+viewHalfWidth-xInset-x;
    const float half = std::min({leftRoom,rightRoom,nativeHalfWidth*parentScale});
    const float available = value ? 2*(half-4*parentScale) : std::min(leftRoom,252*parentScale)-3*parentScale;
    if (!(available > 14*parentScale)) return false;
    const float lineHeight = fontHeight * transform._22;
    const float alignment = (format & 4) ? 0.0f : (format & 8) ? -.5f : .5f;
    const float center = transform._42 + alignment*lineHeight;
    const float halfHeight = 11.5f*parentScale;
    if (center-halfHeight < 240-viewHalfHeight+yInset || center+halfHeight > 240+viewHalfHeight-yInset) return false;
    const float size = std::min({transform._22, available/naturalWidth, 22.0f*parentScale/fontHeight});
    if (!std::isfinite(size) || !(size > 0)) return false;
    transform._11 = transform._22 = size;
    transform._42 = center - alignment*fontHeight*size;
    plate = {x-half, center-halfHeight, x+half, center+halfHeight};
    return true;
}

// FE converts its byte-order channels to half-bright EPoly colour at submission.
std::uint32_t PlateTint(unsigned red, unsigned green, unsigned blue, unsigned alpha) {
    return (alpha<<24) | ((blue/2)<<16) | ((green/2)<<8) | (red/2);
}

bool SubmitOptionPlate(void* cached, void* renderInfo, const OptionPlate& bounds, int alpha) {
    constexpr std::uint32_t whiteHash = 0x0e55b9c1;
    void* pool = GameValue<void*>(0x51cfa0);
    if (!pool || HudField<int>(pool,0x2c)-HudField<int>(pool,0x18) < 96) return false;
    auto* white = GameFunction<void*(__cdecl*)(std::uint32_t,int,int)>(0x103400)(whiteHash,0,0);
    if (!white || HudField<std::uint32_t>(white,0x24) != whiteHash) return false;
    auto& matrix = HudField<D3DMATRIX>(cached,0x30);
    const auto saved = matrix;
    matrix = {};
    matrix._11 = matrix._22 = matrix._33 = matrix._44 = 1;
    const unsigned fade = static_cast<unsigned>(std::clamp(alpha,0,255)) * 196 / 255;
    const auto left = PlateTint(21,31,32,fade), right = PlateTint(13,20,21,fade);
    const std::uint32_t colors[4]{left,right,right,left};
    const auto add = GameFunction<void(__thiscall*)(void*,float,float,float,float,float,float,float,float,float,
                                                     const std::uint32_t*,void*,void*)>(0x182820);
    add(cached,bounds.left,bounds.top,bounds.right,bounds.bottom,1,0,0,1,1,colors,white,renderInfo);
    matrix = saved;
    return true;
}

// Text is fit in the same undistorted canvas as the splash art. Keep native
// positions when they fit, including animated prompts and the version label.
bool FitSplashText(D3DMATRIX& matrix, unsigned format, float width, float height) {
    if (!(width > 0 && width < 65536 && height > 0 && height < 256 &&
          matrix._11 > 0 && matrix._22 > 0 &&
          std::fabs(matrix._12) < .001f && std::fabs(matrix._21) < .001f)) return false;
    const float halfWidth = 320.0f / activeLayout.hudX;
    const float halfHeight = 240.0f / activeLayout.hudY;
    const float margin = 240.0f * .10f;
    const float left = 320.0f - halfWidth + margin, right = 320.0f + halfWidth - margin;
    const float top = 240.0f - halfHeight + margin, bottom = 240.0f + halfHeight - margin;
    const float scale = std::min({1.0f, (right-left)/(width*matrix._11), (bottom-top)/(height*matrix._22)});
    const float lineWidth = std::min(right-left, width * matrix._11 * scale);
    const float lineHeight = std::min(bottom-top, height * matrix._22 * scale);
    const float xOrigin = (format & 2) ? 1.0f : (format & 1) ? .5f : 0.0f;
    const float yOrigin = (format & 4) ? .5f : (format & 8) ? 1.0f : 0.0f;
    matrix._11 *= scale;
    matrix._22 *= scale;
    matrix._41 = std::clamp(matrix._41, left+xOrigin*lineWidth, right-(1-xOrigin)*lineWidth);
    matrix._42 = std::clamp(matrix._42, top+yOrigin*lineHeight, bottom-(1-yOrigin)*lineHeight);
    return true;
}

void __fastcall RenderResponsiveOption(void* font, void*, const OptionColor& color, const std::uint16_t* text,
                                       void* object, D3DMATRIX* source, void* cached, void* renderInfo) {
    const auto render = GameFunction<void(__thiscall*)(void*,const OptionColor&,const std::uint16_t*,void*,
                                                       D3DMATRIX*,void*,void*)>(0x19ac60);
    auto actualColor = color;
    auto* drawMatrix = source;
    D3DMATRIX fitted{};
    if (font && object && source && cached && text && *text && color.alpha > 0 && HudField<void*>(font,8)) {
        const auto hash = HudField<std::uint32_t>(object,0x10);
        const unsigned valueRow=OptionRowIndex(hash,firstOptionValue);
        const bool value=valueRow<15;
        const unsigned row=value?valueRow:OptionRowIndex(hash,firstOptionName);
        const unsigned format = HudField<unsigned>(object,0x6c);
        auto* data = HudField<void*>(object,0x2c);
        auto* renderingPackage = GameValue<void*>(0x51cfac);
        const char* name = renderingPackage ? HudField<const char*>(renderingPackage,0x0c) : nullptr;
        const auto measure = GameFunction<float(__thiscall*)(void*,const std::uint16_t*,unsigned,unsigned,bool)>(0x186130);
        if (name && !_stricmp(name, "MW_LS_Splash.fng")) {
            // The splash retains unused off-canvas strings, including a debug
            // placeholder. A tall viewport would otherwise expose them.
            if (hash == 0xf1414fde || !(source->_41 >= 0 && source->_41 <= 640 &&
                                        source->_42 >= 0 && source->_42 <= 480)) return;
            fitted = *source;
            const float width = measure(font,text,HudField<unsigned>(object,0x1c),0,false);
            if (FitSplashText(fitted,format,width,HudField<float>(font,0x24))) drawMatrix = &fitted;
        }
        // RenderString also receives subtitles, help text, status messages and
        // on-screen keyboard strings. Leave those native rendering paths intact.
        else if (row < 15 && data && !(format&16) && (format&3) == (value ? 1u : 2u)) {
            bool known = false;
            for (const auto* candidate : optionPackages)
                if (name && !_stricmp(name,candidate)) { known = true; break; }
            bool singleLine = true;
            for (const auto* c = text; known && *c; ++c)
                if (*c == '\n' || *c == '^') { singleLine = false; break; }
            const float scale = HudField<float>(data,0x3c);
            if (known && singleLine && scale > 0) {
                const bool display = !_stricmp(name,"OptionsPCDisplay.fng") || !_stricmp(name,"Pause_PCDisplayOptions.fng");
                if (display && !value && color.red > 0 && color.red < 168 &&
                    color.red * 3 > color.green * 4 && color.green > color.blue * 2) {
                    // The dark trough of the native amber focus pulse disappears on
                    // the black stripes. Preserve the hue and the screen fade.
                    actualColor = {color.blue*168/color.red,color.green*168/color.red,168,color.alpha};
                }
                const float width = measure(font,text,HudField<unsigned>(object,0x1c),0,false);
                const float half = value ? OptionValueHalfWidth(name,row,ReadHudPosition(object).x) : 0;
                const float parentScale = source->_22 / scale;
                fitted = *source;
                if (!value) AlignOptionLabel(fitted, name, object, parentScale);
                OptionPlate plate{};
                if (FitOptionText(fitted,plate,activeLayout,value,format,width,HudField<float>(font,0x24),parentScale,half)) {
                    drawMatrix = &fitted;
                    const auto* category = unlimited_language::CategoryIndex();
                    if (value && row == 0 && !_stricmp(name,"Options.fng") &&
                        category && *category == unlimited_language::category &&
                        SubmitOptionPlate(cached,renderInfo,plate,color.alpha)) {
                        // Stock focus/disabled state still sets opacity and luminance.
                        const int luma = (77*color.red+150*color.green+29*color.blue)/256;
                        const int level = 147 + std::clamp(luma,0,255)*99/255;
                        actualColor = {level+5,level+8,level-7,color.alpha};
                    }
                }
            }
        }
    }
    render(font,actualColor,text,object,drawMatrix,cached,renderInfo);
}

bool InstallResponsiveOptionLayout() {
    return RedirectCall(0x1a45a1,0x19ac60,reinterpret_cast<std::uintptr_t>(&RenderResponsiveOption));
}

} // namespace

