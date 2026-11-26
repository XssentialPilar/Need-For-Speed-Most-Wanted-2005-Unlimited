#pragma once

#include <initializer_list>
#include <memory>
#include <new>

namespace {

// The game positions, tints and hit-tests stock FNG objects in logical units.
// Replace only their GPU image. The original TextureInfo dimensions also
// drive FEng's UV and bounding-box calculations and must remain unchanged.
enum class MenuGlyph {
    Career, Resume, NewCareer, QuickRace, Challenge, Profile, AddProfile, RemoveProfile,
    Standings, Dossier, Bounty, Car, Customize, Network, Lan, Split, Audio, Display, Controls,
    Sliders, Tracks, Credits, Movies, Lock, Check, Unchecked, Info, Question, Alert, Save, Load,
    Cart, Arrow, ThinArrow, Circuit, Sprint, Drag, Knockout, Infraction, Police, Toll, Radar,
    WrongWay, SpeedCamera, Helicopter, Panel, Vignette, Selection, ScrollHandle,
    Rail, SplitRail, SliderMark, MeterSegment, Finish, PoliceOut, PoliceDamage,
    Timer, TimerMax, TimerMin, Cost, Barricade, Spikes, Gearbox, BodyKit, Hood, CarbonHood,
    Gauge, Needle, GaugeLabel, Rim, Tyre, Brake, Turbo, Blower, Suspension, Tint, Scoop, CarbonScoop,
    Numbers, Wing, Paint, Vinyl, Decal, Box, Nitrous, Engine, Heat
};
struct FrontendImage {
    std::uint32_t hash;
    const char* nativeName;
    unsigned logicalSize;
    MenuGlyph glyph;
};
constexpr FrontendImage frontendImages[] = {
    {0x03704f3d, "MAIN_ICON_CAREER", 64, MenuGlyph::Career},
    {0xc1c089ce, "MAIN_ICON_RESUMECAREER", 64, MenuGlyph::Resume},
    {0xe7353be7, "MAIN_ICON_NEWCAREER", 64, MenuGlyph::NewCareer},
    {0x12bb5ea2, "INGAME_ICON_RESUME_RACE", 64, MenuGlyph::Resume},
    {0x4e6fbb02, "MAIN_ICON_QUICK_RACE", 64, MenuGlyph::QuickRace},
    {0x2a49b5e2, "MODE_ICON_QUICK_RACE", 64, MenuGlyph::QuickRace},
    {0xe6313967, "MODE_ICON_QUICK_SELECT", 64, MenuGlyph::QuickRace},
    {0x9a962438, "MODE_ICON_CHALLENGE_SER", 64, MenuGlyph::Challenge},
    {0xd708efef, "OPTIONS_ICON_PLAYER_SET", 64, MenuGlyph::Profile},
    {0x6b303856, "MAIN_ICON_PROFILE_MANAG", 64, MenuGlyph::Profile},
    {0x43798644, "MAIN_ICON_PROFILE_ADD", 64, MenuGlyph::AddProfile},
    {0xde03d4a6, "MAIN_ICON_NEWPROFILE", 64, MenuGlyph::AddProfile},
    {0x0d9035ce, "MAIN_ICON_PROFILE_DELET", 64, MenuGlyph::RemoveProfile},
    {0x2c14ac23, "MAIN_ICON_TOP_15", 64, MenuGlyph::Standings},
    {0x2fd8b206, "MAIN_ICON_RAP_SHEET", 64, MenuGlyph::Dossier},
    {0xe0e0058a, "MAIN_ICON_REP_SHEET", 64, MenuGlyph::Dossier},
    {0xe97e4e83, "MAIN_ICON_RIVALBIO", 64, MenuGlyph::Dossier},
    {0x021a4b0c, "MAIN_ICON_BOUNTY", 64, MenuGlyph::Bounty},
    {0xc6a1a6e0, "MAIN_ICON_CAR_SELECT", 64, MenuGlyph::Car},
    {0xb0c46023, "MAIN_ICON_CUSTOMIZE_CAR", 64, MenuGlyph::Customize},
    {0x771849a5, "MAIN_ICON_GO_ONLINE", 64, MenuGlyph::Network},
    {0xd13a2e66, "MAIN_ICON_LAN", 64, MenuGlyph::Lan},
    {0xf365b5f5, "MODE_ICON_2PLAYER_SPLIT", 64, MenuGlyph::Split},
    {0xf37af144, "OPTIONS_ICON_AUDIO", 64, MenuGlyph::Audio},
    {0x8a006328, "OPTIONS_ICON_DISPLAY", 64, MenuGlyph::Display},
    {0xa04a7b26, "OPTIONS_ICON_CONTROLS", 64, MenuGlyph::Controls},
    {0x3058fe37, "MAIN_ICON_OPTIONS", 64, MenuGlyph::Sliders},
    {0x4df98fb2, "OPTIONS_ICON_GAMEPLAY_S", 64, MenuGlyph::Sliders},
    {0xc52ccbf6, "OPTIONS_ICON_EA_TRAX", 64, MenuGlyph::Tracks},
    {0x51009e20, "OPTIONS_ICON_CREDITS", 64, MenuGlyph::Credits},
    {0x187ac298, "OPTIONS_ICON_TRAILERS", 64, MenuGlyph::Movies},
    {0x0018ed48, "LOCK", 32, MenuGlyph::Lock},
    {0xac43094d, "GENERIC_LOCKED", 32, MenuGlyph::Lock},
    {0xf0574bb2, "GENERIC_LOCK_ITEM", 32, MenuGlyph::Lock},
    {0x028feadd, "CHECK", 32, MenuGlyph::Check},
    {0x696ae039, "GENERIC_CHECK", 32, MenuGlyph::Check},
    {0xe719881c, "GENERIC_UNCHECK", 32, MenuGlyph::Unchecked},
    {0x1a7afe27, "GENERIC_INFO", 32, MenuGlyph::Info},
    {0x39949433, "GENERIC_QUESTION", 32, MenuGlyph::Question},
    {0x6948e2b3, "GENERIC_ALERT", 32, MenuGlyph::Alert},
    {0x982b9803, "GENERIC_AUTOSAVE", 64, MenuGlyph::Save},
    {0x228b7e32, "GENERIC_ICON_SAVE", 64, MenuGlyph::Save},
    {0x2287e063, "GENERIC_ICON_LOAD", 64, MenuGlyph::Load},
    {0x1a777e25, "GENERIC_CART", 32, MenuGlyph::Cart},
    {0x1150840e, "ARROW_MAIN", 32, MenuGlyph::Arrow},
    {0x1a6d3d2c, "ARROW_SKINNIER", 32, MenuGlyph::ThinArrow},
    {0xe9638d3e, "MODE_ICON_CIRCUIT", 64, MenuGlyph::Circuit},
    {0x2521e5eb, "MODE_ICON_SPRINT", 64, MenuGlyph::Sprint},
    {0xaaab31e9, "MODE_ICON_DRAG", 64, MenuGlyph::Drag},
    {0x3a015595, "MODE_ICON_LAP_KNOCKOUT", 64, MenuGlyph::Knockout},
    {0x13da8128, "GENERIC_INFRACTION", 64, MenuGlyph::Infraction},
    {0x3787231c, "COPS_ICON", 32, MenuGlyph::Police},
    {0x424590de, "TOLLBOOTH_ICON", 64, MenuGlyph::Toll},
    {0x6ccbbbf1, "RADAR_ICON", 32, MenuGlyph::Radar},
    {0x84073c05, "ICON_WRONGWAY", 64, MenuGlyph::WrongWay},
    {0xec370c33, "SPEEDTRAP_FLASHER_ICON", 64, MenuGlyph::SpeedCamera},
    {0xf66ac1b6, "HELICOPTER_ICON", 64, MenuGlyph::Helicopter},
    {0xa67c3fe2, "U2_MENU_BOX", 32, MenuGlyph::Panel},
    {0x39375faa, "BLACK_SQUARE_VINETTE", 64, MenuGlyph::Vignette},
    {0x249db7b7, "HIGHLIGHT", 32, MenuGlyph::Selection},
    {0x95043b2d, "SCROLL_BAR_HANDLE", 32, MenuGlyph::ScrollHandle},
    {0x14025bd8, "LED_MAIN", 32, MenuGlyph::Rail},
    {0xd353ecad, "LED_MAIN_SLIDERS", 32, MenuGlyph::SplitRail},
    {0xacec619f, "PC_LED_MAIN_SLIDERS", 32, MenuGlyph::Rail},
    {0x72a88a23, "SLIDER_BAR_WATERMARK", 16, MenuGlyph::SliderMark},
    {0x89e26388, "BAR_LED", 16, MenuGlyph::MeterSegment},
    {0x054f9862, "MILESTONE_RACING", 64, MenuGlyph::Finish},
    {0x0fe608e6, "COPS_TAKENOUT_ICON", 64, MenuGlyph::PoliceOut},
    {0x1a091045, "MILESTONE_TOLLBOOTH", 64, MenuGlyph::Toll},
    {0x1b5c9962, "WORLD_RACEFINISH", 64, MenuGlyph::Finish},
    {0x450a616a, "MILESTONE_PURSUIT", 64, MenuGlyph::Police},
    {0x66c9a7b6, "MILESTONE_SPEEDTRAP", 64, MenuGlyph::SpeedCamera},
    {0x6784a80e, "MILESTONE_INFRACTIONS", 64, MenuGlyph::Infraction},
    {0x831b7ebe, "MILESTONE_PURSUIT_MAXTI", 64, MenuGlyph::TimerMax},
    {0x87807869, "MILESTONE_COST_TO_STATE", 64, MenuGlyph::Cost},
    {0x88e8de9e, "COPS_DAMAGED_ICON", 64, MenuGlyph::PoliceDamage},
    {0x8c76cd0f, "MILESTONE_PURSUIT_DURAT", 64, MenuGlyph::Timer},
    {0x950fcebc, "MILESTONE_PURSUIT_MINTI", 64, MenuGlyph::TimerMin},
    {0xb4e6456b, "MILESTONE_PURSUITBOUNTY", 64, MenuGlyph::Bounty},
    {0xc43959d2, "MILESTONE_ROADBLOCKS", 64, MenuGlyph::Barricade},
    {0xe621b2ef, "MILESTONE_SPIKESTRIPS", 64, MenuGlyph::Spikes},
    {0x01a29ffa, "PERFORMANCE_TRANSMISSIO", 64, MenuGlyph::Gearbox},
    {0x028c24f6, "VISUAL_PART_BODY", 64, MenuGlyph::BodyKit},
    {0x028f7092, "VISUAL_PART_HOOD", 64, MenuGlyph::Hood},
    {0x028f88bc, "VISUAL_PART_HUDS", 64, MenuGlyph::Gauge},
    {0x0294d2a3, "VISUAL_PART_RIMS", 64, MenuGlyph::Rim},
    {0x06e8e477, "PERFORMANCE_TIRES", 64, MenuGlyph::Tyre},
    {0x06ef789c, "PERFORMANCE_TURBO", 64, MenuGlyph::Turbo},
    {0x178475e7, "PERFORMANCE_SUSPENSION", 64, MenuGlyph::Suspension},
    {0x3987d054, "BROWSER_PERFORMANCE", 64, MenuGlyph::Gauge},
    {0x3a541f7f, "CARBUCKET_STOCKCARS", 64, MenuGlyph::Car},
    {0x3e31ba56, "BROWSER_VISUAL", 64, MenuGlyph::Paint},
    {0x3f23165c, "VISUAL_PART_WINDOW_TINT", 64, MenuGlyph::Tint},
    {0x45a1c644, "VISUAL_PART_NUMBERS", 64, MenuGlyph::Numbers},
    {0x4d1c18ba, "VISUAL_PART_SPOILER_CAR", 64, MenuGlyph::Wing},
    {0x550681a4, "VISUAL_PART_PAINT", 64, MenuGlyph::Paint},
    {0x55778e5a, "VISUAL_PART_VINYL", 64, MenuGlyph::Vinyl},
    {0x65818ee8, "MILESTONE_TOLLBOOTHCHAL", 64, MenuGlyph::Toll},
    {0x68495926, "VISUAL_PART_HOOD_CARBON", 64, MenuGlyph::CarbonHood},
    {0x6e0ca66c, "BROWSER_PARTS", 64, MenuGlyph::Box},
    {0x70f56628, "VISUAL_PART_HUDS_NEEDLE", 64, MenuGlyph::Needle},
    {0x79165861, "VISUAL_PART_ROOF_SCOOP", 64, MenuGlyph::Scoop},
    {0x8bdd4eb6, "PERFORMANCE_DETAILS", 32, MenuGlyph::Sliders},
    {0x93603dfb, "PERFORMANCE_SUPERCHARGE", 64, MenuGlyph::Blower},
    {0x9701bde4, "PERFORMANCE_NITROUS", 64, MenuGlyph::Nitrous},
    {0x9996ca1e, "CARBUCKET_CUSTOMIZECARS", 64, MenuGlyph::Customize},
    {0xb8c8c0d4, "PERFORMANCE_ICON_PACKAG", 64, MenuGlyph::Box},
    {0xbaa23a28, "PERFORMANCE_BRAKES", 64, MenuGlyph::Brake},
    {0xbb034ea6, "VISUAL_PART_SPOILER", 64, MenuGlyph::Wing},
    {0xbe5ad8a2, "CARBUCKET_BONUSCARS", 64, MenuGlyph::Challenge},
    {0xbf6682c9, "VISUAL_PART_HUDS_LETTER", 64, MenuGlyph::GaugeLabel},
    {0xc15c94e6, "PERFORMANCE_ENGINE", 64, MenuGlyph::Engine},
    {0xc30ea0a9, "HEAT_ICON", 32, MenuGlyph::Heat},
    {0xcc9e1ce4, "VISUAL_PART_HUDS_GAUGE", 64, MenuGlyph::Gauge},
    {0xd807e9b3, "MILESTONE_MAIN", 64, MenuGlyph::Finish},
    {0xda1dae54, "VISUAL_PART_DECALS", 64, MenuGlyph::Decal},
    {0xdd5621f0, "CHECKMARK_ICON", 16, MenuGlyph::Check},
    {0xf0bddecd, "CARBUCKET_CAREERCARS", 64, MenuGlyph::Career},
    {0xfc618215, "VISUAL_PART_ROOF_SCOOP_", 64, MenuGlyph::CarbonScoop},
    {0xff37755e, "PERFORMANCE_ICON_BASE_P", 64, MenuGlyph::Box}
};
struct IconPoint { float x, y; };
enum class IconInk : std::uint32_t { Light = 0xf6f7f7, Accent = 0xbedee2 };

class IconPainter {
    std::uint32_t* pixels;
    int edge;
    float scale;
    template<class Distance>
    void Stroke(float x0, float y0, float x1, float y1, float width, IconInk ink, Distance distance) {
        const float radius = width * 0.5f;
        const int beginX = std::max(0, static_cast<int>(std::floor((x0 - radius) * scale - 1)));
        const int endX = std::min(edge, static_cast<int>(std::ceil((x1 + radius) * scale + 1)));
        const int beginY = std::max(0, static_cast<int>(std::floor((y0 - radius) * scale - 1)));
        const int endY = std::min(edge, static_cast<int>(std::ceil((y1 + radius) * scale + 1)));
        for (int y = beginY; y < endY; ++y) {
            for (int x = beginX; x < endX; ++x) {
                const float coverage = (radius - distance((x + 0.5f) / scale, (y + 0.5f) / scale)) * scale + 0.5f;
                if (coverage <= 0) continue;
                const auto alpha = static_cast<unsigned>(std::min(1.0f, coverage) * 255 + 0.5f);
                auto& pixel = pixels[y * edge + x];
                if (alpha >= (pixel >> 24)) pixel = (alpha << 24) | static_cast<std::uint32_t>(ink);
            }
        }
    }
public:
    IconPainter(std::uint32_t* image, int size) : pixels(image), edge(size), scale(size / 32.0f) {}
    void Line(IconPoint a, IconPoint b, float width = 2.2f, IconInk ink = IconInk::Light) {
        const float dx = b.x - a.x, dy = b.y - a.y, lengthSq = dx * dx + dy * dy;
        Stroke(std::min(a.x,b.x), std::min(a.y,b.y), std::max(a.x,b.x), std::max(a.y,b.y), width, ink,
               [=](float x, float y) {
                   const float t = lengthSq ? std::clamp(((x - a.x)*dx + (y - a.y)*dy)/lengthSq, 0.0f, 1.0f) : 0;
                   const float u = x - a.x - t*dx, v = y - a.y - t*dy;
                   return std::sqrt(u*u + v*v);
               });
    }
    void Path(std::initializer_list<IconPoint> points, float width = 2.2f, IconInk ink = IconInk::Light) {
        auto it = points.begin();
        if (it == points.end()) return;
        IconPoint previous = *it++;
        for (; it != points.end(); ++it) {
            Line(previous, *it, width, ink);
            previous = *it;
        }
    }
    void Ring(float x, float y, float radius, float width = 2.2f, IconInk ink = IconInk::Light) {
        Stroke(x-radius,y-radius,x+radius,y+radius,width,ink,[=](float u,float v) {
            const float dx = u-x, dy = v-y;
            return std::fabs(std::sqrt(dx*dx + dy*dy) - radius);
        });
    }
    void Dot(float x, float y, float width, IconInk ink = IconInk::Light) {
        Line({x,y}, {x,y}, width, ink);
    }
    template<class Paint>
    void Surface(Paint paint) {
        for (int y = 0; y < edge; ++y) for (int x = 0; x < edge; ++x) {
            pixels[y*edge+x] = paint((x+0.5f)/edge, (y+0.5f)/edge);
        }
    }
    void Frame(float l,float t,float r,float b) { Path({{l,t},{r,t},{r,b},{l,b},{l,t}}); }
};

void PaintFrontendGlyph(IconPainter& g, MenuGlyph glyph) {
    constexpr auto accent = IconInk::Accent;
    switch (glyph) {
    case MenuGlyph::Career:
    case MenuGlyph::NewCareer:
        g.Path({{6,13},{8,23},{24,23},{26,13},{20,17},{16,9},{12,17},{6,13}});
        g.Line({9,27},{23,27},1.8f,accent);
        if (glyph == MenuGlyph::NewCareer) {
            g.Line({24,5},{29,5},1.7f); g.Line({26.5f,2.5f},{26.5f,7.5f},1.7f);
        }
        break;
    case MenuGlyph::Resume:
        g.Path({{9,5},{26,16},{9,27},{9,5}},3);
        g.Line({4,9},{4,23},1.5f,accent);
        break;
    case MenuGlyph::QuickRace:
    case MenuGlyph::Sprint:
        g.Path({{12,8},{20,16},{12,24}},3.6f);
        g.Path({{19,8},{27,16},{19,24}},2,accent);
        if (glyph == MenuGlyph::QuickRace) {
            g.Line({3,12},{10,12},2); g.Line({5,19},{10,19},1.5f,accent);
        } else {
            g.Line({4,7},{4,25},2); g.Line({7,7},{7,10},1.6f,accent);
        }
        break;
    case MenuGlyph::Challenge:
        g.Path({{10,5},{22,5},{21,15},{18,19},{14,19},{11,15},{10,5}});
        g.Path({{10,8},{5,8},{6,12},{11,15}},1.8f,accent);
        g.Path({{22,8},{27,8},{26,12},{21,15}},1.8f,accent);
        g.Line({16,20},{16,26}); g.Line({10,27},{22,27});
        break;
    case MenuGlyph::Profile:
    case MenuGlyph::AddProfile:
    case MenuGlyph::RemoveProfile:
        g.Ring(14,10,4.5f);
        g.Path({{4,26},{5,22},{9,18},{18,18},{21,20}},2.3f);
        g.Line({5,26},{17,26},2,accent);
        if (glyph != MenuGlyph::Profile) g.Line({21,24},{29,24},2.4f,accent);
        if (glyph == MenuGlyph::AddProfile) g.Line({25,20},{25,28},2.4f,accent);
        break;
    case MenuGlyph::Standings:
        g.Path({{3,25},{3,17},{11,17},{11,8},{20,8},{20,20},{28,20},{28,25},{3,25}});
        g.Line({15,12},{15,17},1.8f,accent);
        break;
    case MenuGlyph::Dossier:
    case MenuGlyph::Infraction:
        g.Path({{7,3},{21,3},{26,8},{26,28},{7,28},{7,3}});
        g.Path({{20,4},{20,10},{25,10}},1.5f,accent);
        if (glyph == MenuGlyph::Dossier) {
            g.Line({11,15},{21,15}); g.Line({11,20},{18,20},1.6f,accent);
        } else {
            g.Line({15,12},{15,18},2.6f); g.Dot(15,23,2.9f);
        }
        break;
    case MenuGlyph::Bounty:
    case MenuGlyph::Police:
        g.Path({{5,7},{12,7},{16,4},{20,7},{27,7},{25,20},{16,28},{7,20},{5,7}});
        if (glyph == MenuGlyph::Bounty) {
            g.Ring(16,15,4,1.8f,accent); g.Dot(16,15,2);
        } else {
            g.Path({{11,16},{16,12},{21,16}},2,accent); g.Path({{12,20},{16,17},{20,20}},1.6f);
        }
        break;
    case MenuGlyph::Car:
    case MenuGlyph::Customize:
        g.Path({{4,22},{4,16},{8,15},{11,9},{20,9},{24,15},{28,17},{28,22},{25,22}});
        g.Line({10,22},{21,22},1.7f,accent);
        g.Ring(8,22,2.5f); g.Ring(24,22,2.5f);
        g.Line({10,15},{21,15},1.6f,accent);
        if (glyph == MenuGlyph::Customize) g.Path({{22,5},{25,2.8f},{28,4}},1.5f,accent);
        break;
    case MenuGlyph::Network:
        g.Ring(16,16,11);
        g.Path({{16,5},{12,10},{11,16},{12,22},{16,27},{20,22},{21,16},{20,10},{16,5}},1.7f,accent);
        g.Line({5,16},{27,16},1.8f);
        break;
    case MenuGlyph::Lan:
        g.Frame(11,4,21,11); g.Frame(3,22,12,28); g.Frame(20,22,29,28);
        g.Line({16,11},{16,17},1.8f,accent); g.Path({{7,21},{7,17},{25,17},{25,21}},1.8f,accent);
        break;
    case MenuGlyph::Display:
    case MenuGlyph::Split:
        g.Frame(3,6,29,23);
        if (glyph == MenuGlyph::Split) g.Line({15,7},{15,22},2.4f,accent);
        g.Line({16,23},{16,27}); g.Line({10,27},{22,27},2,accent);
        break;
    case MenuGlyph::Audio:
        g.Path({{4,12},{9,12},{15,7},{15,25},{9,20},{4,20},{4,12}});
        g.Path({{20,11},{22,14},{22,18},{20,21}},2,accent);
        g.Path({{24,7},{27,12},{28,16},{27,20},{24,25}});
        break;
    case MenuGlyph::Controls:
        g.Path({{3.5f,23},{5,13},{8,9},{24,9},{27,13},{28.5f,23},{26,24},{21,19},{11,19},{6,24},{3.5f,23}});
        g.Line({8,14},{13,14},1.6f); g.Line({10.5f,11.5f},{10.5f,16.5f},1.6f);
        g.Dot(21,15,2.1f,accent); g.Dot(24,12,2.1f,accent);
        break;
    case MenuGlyph::Sliders:
        for (int row=0; row<3; ++row) {
            const float y=7.0f+row*9; const float x= row==1 ? 12.0f : 20.0f;
            g.Line({4,y},{x-4,y},1.8f); g.Line({x+4,y},{28,y},1.8f); g.Ring(x,y,3,2,accent);
        }
        break;
    case MenuGlyph::Tracks:
        g.Path({{11,23},{11,9},{25,5},{25,20}}); g.Line({12,13},{24,10},1.8f,accent);
        g.Ring(8,23,3); g.Ring(22,20,3);
        break;
    case MenuGlyph::Credits:
        g.Path({{5,5},{28,5},{25,21},{16,21},{10,27},{10,21},{4,21},{5,5}});
        g.Line({9,11},{22,11},1.8f); g.Line({9,16},{17,16},1.6f,accent);
        break;
    case MenuGlyph::Movies:
        g.Frame(3,7,29,25); g.Path({{13,11},{21,16},{13,21},{13,11}},1.8f,accent);
        for (float x : {6.5f,25.5f}) for (float y : {11.f,16.f,21.f}) g.Dot(x,y,1.8f);
        break;
    case MenuGlyph::Lock:
        g.Frame(6,14,26,27);
        g.Path({{10,13},{10,10},{12,6},{16,4.5f},{20,6},{22,10},{22,13}},2.5f);
        g.Line({16,19},{16,23},2.8f,accent);
        break;
    case MenuGlyph::Check:
        g.Path({{5,16},{12,23},{27,8}},3.8f);
        break;
    case MenuGlyph::Unchecked:
        g.Ring(16,16,11); g.Line({9,23},{23,9},2.7f,accent);
        break;
    case MenuGlyph::Info:
    case MenuGlyph::Question:
        g.Ring(16,16,12);
        if (glyph == MenuGlyph::Info) {
            g.Dot(16,10,3,accent); g.Line({16,15},{16,22},2.6f);
        } else {
            g.Path({{12,12},{13.5f,9},{18.5f,9},{20,11},{20,14},{16,17},{16,19}},2);
            g.Dot(16,23,2.3f,accent);
        }
        break;
    case MenuGlyph::Alert:
        g.Path({{16,4},{29,26},{3,26},{16,4}},2.2f);
        g.Line({16,12},{16,18},2.4f); g.Dot(16,22,2.5f,accent);
        break;
    case MenuGlyph::Save:
    case MenuGlyph::Load:
        g.Path({{5,19},{5,26},{27,26},{27,19}});
        if (glyph == MenuGlyph::Save) {
            g.Line({16,5},{16,21}); g.Path({{10,15},{16,21},{22,15}},2.4f,accent);
        } else {
            g.Line({16,6},{16,22}); g.Path({{10,12},{16,6},{22,12}},2.4f,accent);
        }
        break;
    case MenuGlyph::Cart:
        g.Path({{3,6},{7,6},{10,20},{24,20},{27,10},{9,10}});
        g.Dot(11,25,4,accent); g.Dot(23,25,4,accent);
        break;
    case MenuGlyph::Arrow:
    case MenuGlyph::ThinArrow:
        g.Path({{22,6},{11,16},{22,26}},glyph == MenuGlyph::Arrow ? 4.0f : 2.4f);
        break;
    case MenuGlyph::Circuit:
    case MenuGlyph::Knockout:
        g.Path({{5,20},{9,8},{14,6},{25,8},{27,12},{23,16},{16,16},{15,23},{11,26},{6,25},{5,20}},2.6f);
        if (glyph == MenuGlyph::Knockout) {
            g.Line({21,21},{27,27},1.8f,accent); g.Line({27,21},{21,27},1.8f,accent);
        }
        break;
    case MenuGlyph::Drag:
        g.Path({{5,26},{13,6},{17,6},{13,26}},2.2f);
        g.Path({{19,26},{19,6},{23,6},{27,26}},2.2f,accent);
        break;
    case MenuGlyph::Toll:
        g.Path({{5,26},{5,10},{18,10},{18,26}}); g.Frame(8,13,15,18);
        g.Line({18,17},{28,10},2.2f,accent); g.Line({3,27},{21,27},1.5f);
        break;
    case MenuGlyph::Radar:
        g.Ring(16,16,11); g.Ring(16,16,5.5f,1.3f,accent); g.Line({16,16},{24,8}); g.Dot(12,19,2.8f);
        break;
    case MenuGlyph::WrongWay:
        g.Ring(16,16,12,2.6f); g.Line({9,16},{23,16},3.8f);
        break;
    case MenuGlyph::SpeedCamera:
        g.Path({{4,11},{10,11},{12,7},{19,7},{21,11},{28,11},{28,24},{4,24},{4,11}});
        g.Ring(16,17,4,2,accent); g.Dot(24,14,1.8f);
        break;
    case MenuGlyph::Panel:
        // Rectangular plates are frequently nine-sliced or stretched.
        // A smooth diagonal graphite finish has no direction-dependent glyphs.
        g.Surface([](float x, float y) {
            const unsigned value = static_cast<unsigned>(10 + 10*(1-y) + 4*x);
            const unsigned alpha = static_cast<unsigned>(185 + 24*y);
            return (alpha<<24) | (value<<16) | ((value+6)<<8) | (value+11);
        });
        break;
    case MenuGlyph::Vignette:
        g.Surface([](float x, float y) {
            const float u = std::fabs(2*x-1), v = std::fabs(2*y-1);
            const float coverage = std::clamp((u*u + v*v - 0.56f) / 1.22f, 0.0f, 1.0f);
            return (static_cast<unsigned>(231*coverage*coverage*(3-2*coverage))<<24) | 0x000b1015u;
        });
        break;
    case MenuGlyph::Selection:
        g.Surface([](float, float y) {
            const float distance = std::fabs(2*y-1);
            const float coverage = std::clamp((0.90f - distance)*3.6f, 0.0f, 1.0f);
            return (static_cast<unsigned>(255*coverage)<<24) | 0x00e9f3f4u;
        });
        break;
    case MenuGlyph::ScrollHandle:
        g.Surface([](float x, float y) {
            const float inset = std::min(std::min(x,1-x),std::min(y,1-y));
            if (inset < 0.035f) return 0xbb33474du;
            return y > 0.27f && y < 0.73f && std::fabs(x-0.5f)<0.055f ? 0xffe0edefu : 0xfb87969bu;
        });
        break;
    case MenuGlyph::Rail:
    case MenuGlyph::SplitRail:
        if (glyph == MenuGlyph::SplitRail) {
            g.Line({2,16},{13.5f,16},3.6f); g.Line({18.5f,16},{30,16},3.6f);
        } else {
            g.Line({2,16},{30,16},3.6f);
        }
        break;
    case MenuGlyph::SliderMark:
        g.Path({{16,5},{20,10},{20,22},{16,27},{12,22},{12,10},{16,5}},1.8f);
        g.Line({16,11},{16,21},1.8f,accent);
        break;
    case MenuGlyph::MeterSegment:
        g.Surface([](float x, float y) {
            const unsigned v = static_cast<unsigned>(136 + 62*(1-y) + 12*x);
            return 0xff000000u | (v<<16) | ((v+16)<<8) | (v+20);
        });
        break;
    case MenuGlyph::Finish:
        g.Line({7,5},{7,28}); g.Path({{7,6},{26,6},{23,18},{7,18}});
        g.Path({{14,7},{14,12},{8,12}},1.8f,accent);
        g.Path({{20,17},{20,12},{25,12}},1.8f,accent);
        break;
    case MenuGlyph::PoliceOut:
    case MenuGlyph::PoliceDamage:
        g.Path({{5,7},{12,7},{16,4},{20,7},{26,7},{24,18},{16,26},{8,20},{5,7}});
        if (glyph == MenuGlyph::PoliceOut) {
            g.Line({12,12},{19,19},2.2f,accent); g.Line({19,12},{12,19},2.2f,accent);
        } else g.Path({{18,8},{13,14},{18,14},{13,21}},2,accent);
        break;
    case MenuGlyph::Timer:
    case MenuGlyph::TimerMax:
    case MenuGlyph::TimerMin:
        g.Ring(16,18,10); g.Line({13,3},{19,3}); g.Line({16,3},{16,7});
        g.Line({16,17},{20,12},1.8f,accent); g.Dot(16,18,2.8f);
        if (glyph == MenuGlyph::TimerMin) g.Path({{4,12},{4,7},{9,7}},1.7f,accent);
        if (glyph == MenuGlyph::TimerMax) g.Path({{23,7},{28,7},{28,12}},1.7f,accent);
        break;
    case MenuGlyph::Cost:
        g.Path({{3,12},{22,12},{26,15},{26,25},{3,25},{3,12}});
        g.Path({{7,8},{26,8},{29,11},{29,21}},1.7f,accent);
        g.Ring(15,18.5f,3.5f); g.Dot(7.5f,18.5f,2,accent);
        break;
    case MenuGlyph::Barricade:
        g.Frame(3,11,29,21); g.Line({7,22},{7,27},2.3f); g.Line({25,22},{25,27},2.3f);
        for (float x : {5.f,13.f,21.f}) g.Line({x,19},{x+5,13},2,accent);
        break;
    case MenuGlyph::Spikes:
        g.Line({3,23},{29,23},2.8f,accent);
        for (float x : {4.f,12.f,20.f}) g.Path({{x,21},{x+3,9},{x+6,21}},2);
        break;
    case MenuGlyph::Gearbox:
        g.Line({7,9},{7,24}); g.Line({16,9},{16,24}); g.Line({25,9},{25,17});
        g.Line({7,16},{25,16},2.2f,accent);
        for(float x : {7.f,16.f,25.f}) g.Dot(x,8,4);
        g.Dot(7,25,4,accent); g.Dot(16,25,4,accent);
        break;
    case MenuGlyph::BodyKit:
        g.Path({{3,17},{5,14},{27,14},{29,17},{26,22},{6,22},{3,17}});
        g.Path({{6,14},{10,9},{22,9},{26,14}});
        g.Line({7,17},{11,17},1.8f,accent); g.Line({21,17},{25,17},1.8f,accent);
        g.Line({4,25},{28,25},2.4f);
        break;
    case MenuGlyph::Hood:
    case MenuGlyph::CarbonHood:
        g.Path({{8,7},{24,7},{27,22},{23,25},{9,25},{5,22},{8,7}});
        g.Path({{11,10},{10,19},{13,22}},1.8f,accent); g.Path({{21,10},{22,19},{19,22}},1.8f,accent);
        if (glyph==MenuGlyph::CarbonHood) g.Path({{14,12},{18,10},{15,17},{18,15}},1.5f,accent);
        break;
    case MenuGlyph::Gauge:
    case MenuGlyph::Needle:
    case MenuGlyph::GaugeLabel:
        g.Path({{4,22},{4,15},{7,10},{12,7},{19,7},{25,11},{28,17},{28,22}},2.5f);
        if (glyph==MenuGlyph::GaugeLabel) {
            g.Path({{7,17},{9,15},{9,22}},1.8f); g.Path({{22,16},{25,16},{22,21},{25,21}},1.8f);
        } else {
            g.Line({16,21},{22,13},glyph==MenuGlyph::Needle ? 3.0f : 2.0f,accent);
            g.Dot(16,21,4);
            if (glyph==MenuGlyph::Gauge) g.Line({7,17},{9,17},1.7f,accent);
        }
        break;
    case MenuGlyph::Rim:
    case MenuGlyph::Tyre:
    case MenuGlyph::Brake:
        g.Ring(16,16,11.5f,2.3f);
        if (glyph==MenuGlyph::Rim) {
            g.Ring(16,16,3.0f,2,accent);
            for(auto edge : {IconPoint{16,6}, {25,12}, {22,24}, {10,24}, {7,12}}) {
                const IconPoint hub{16+(edge.x-16)*.32f,16+(edge.y-16)*.32f};
                g.Line(hub,edge,2);
            }
        } else if (glyph==MenuGlyph::Tyre) {
            g.Ring(16,16,7,2,accent);
            g.Line({10,6},{12,10},1.6f); g.Line({21,22},{24,24},1.6f);
        } else {
            g.Ring(16,16,3,1.8f,accent); g.Path({{7,12},{10,7},{15,6},{14,11},{10,14},{7,14}},2.6f);
            g.Dot(22,15,1.8f); g.Dot(20,21,1.8f); g.Dot(14,23,1.8f);
        }
        break;
    case MenuGlyph::Turbo:
    case MenuGlyph::Blower:
        g.Ring(14,18,8.5f); g.Ring(14,18,3.2f,1.8f,accent);
        if (glyph==MenuGlyph::Turbo) {
            g.Path({{14,9},{14,5},{27,5},{27,11},{22,11}});
            for(auto edge : {IconPoint{14,11}, {21,18}, {14,25}, {7,18}})
                g.Line({14+(edge.x-14)*.45f,18+(edge.y-18)*.45f},edge,1.4f,accent);
        } else {
            g.Path({{7,12},{19,5},{26,6},{28,12},{21,23}}); g.Ring(22,10,2.4f,1.5f,accent);
        }
        break;
    case MenuGlyph::Suspension:
        g.Line({11,4},{21,4}); g.Line({16,4},{16,7},1.7f);
        g.Path({{10,8},{22,10},{10,13},{22,16},{10,19},{22,22},{12,24}},2.2f);
        g.Line({16,24},{16,27},1.7f,accent); g.Line({11,28},{21,28});
        break;
    case MenuGlyph::Tint:
    case MenuGlyph::Scoop:
    case MenuGlyph::CarbonScoop:
        g.Path({{4,23},{10,10},{23,10},{28,23},{4,23}});
        if (glyph==MenuGlyph::Tint) {
            g.Line({13,13},{9,20},1.6f,accent); g.Line({18,13},{14,20},1.6f,accent);
        } else {
            g.Path({{8,21},{11,16},{22,16},{25,21}},1.8f,accent);
            if (glyph==MenuGlyph::CarbonScoop) g.Line({15,12},{19,12},1.8f,accent);
        }
        break;
    case MenuGlyph::Numbers:
        g.Line({13,5},{9,27},2.6f); g.Line({23,5},{19,27},2.6f);
        g.Line({6,12},{28,12},2.4f,accent); g.Line({4,20},{26,20},2.4f,accent);
        break;
    case MenuGlyph::Wing:
        g.Path({{4,9},{6,13},{26,13},{28,8},{24,10},{7,10},{4,9}});
        g.Line({10,14},{8,23}); g.Line({21,14},{24,23}); g.Line({5,25},{27,25},1.8f,accent);
        break;
    case MenuGlyph::Paint:
        g.Path({{9,10},{22,10},{23,14},{23,27},{8,27},{8,14},{9,10}});
        g.Frame(12,5,18,9); g.Line({21,6},{27,4},1.5f,accent);
        g.Line({10,21},{20,16},2.4f,accent);
        break;
    case MenuGlyph::Vinyl:
    case MenuGlyph::Decal:
        g.Path({{6,5},{26,5},{26,19},{18,27},{6,27},{6,5}});
        g.Path({{26,18},{18,18},{18,26}},1.8f,accent);
        if (glyph==MenuGlyph::Vinyl) g.Path({{10,19},{15,9},{20,9},{15,16}},1.8f);
        else g.Ring(13,13,3.2f,1.8f);
        break;
    case MenuGlyph::Box:
        g.Path({{4,10},{16,5},{28,10},{28,22},{16,28},{4,22},{4,10}});
        g.Path({{4,10},{16,16},{28,10}},1.8f,accent); g.Line({16,16},{16,27},1.8f);
        break;
    case MenuGlyph::Nitrous:
        g.Path({{11,9},{12,7},{20,7},{21,9},{23,23},{21,27},{11,27},{9,23},{11,9}});
        g.Frame(13,3,19,7); g.Path({{18,11},{13,18},{18,18},{14,23}},1.9f,accent);
        break;
    case MenuGlyph::Engine:
        g.Path({{6,12},{9,12},{12,8},{23,8},{23,11},{27,12},{27,21},{23,23},{22,25},{12,25},{8,21},{6,21},{6,12}});
        g.Line({12,4},{21,4},1.8f,accent); g.Line({3,14},{3,20},1.8f); g.Line({12,15},{20,15},1.8f,accent);
        break;
    case MenuGlyph::Heat:
        g.Path({{9,26},{5,21},{6,15},{11,11},{11,16},{16,13},{18,4},{23,10},{22,16},{26,13},{27,20},{23,26},{16,28},{9,26}});
        g.Path({{13,24},{13,20},{16,17},{18,21},{17,25}},1.8f,accent);
        break;
    case MenuGlyph::Helicopter:
        g.Path({{4,12},{9,16},{14,14},{21,14},{27,18},{25,21},{12,21},{9,18},{4,17},{4,12}});
        g.Line({18,9},{18,14},1.8f); g.Line({8,9},{28,9},1.9f,accent);
        g.Line({14,25},{26,25},1.6f); g.Line({16,22},{16,25},1.6f);
        break;

    }
}


// Four-to-one box filtering in straight-alpha space. Weight the color by
// coverage first so transparent borders cannot pull a dark halo into a mip.
void ShrinkFrontendMip(std::uint32_t* pixels, unsigned edge) {
    const unsigned next = edge / 2;
    for (unsigned y = 0; y < next; ++y) {
        for (unsigned x = 0; x < next; ++x) {
            const auto* a = pixels + (2*y)*edge + 2*x;
            const std::uint32_t colors[]{a[0], a[1], a[edge], a[edge+1]};
            unsigned alpha=0, red=0, green=0, blue=0;
            for (const auto color : colors) {
                const unsigned weight = color >> 24;
                alpha += weight;
                red += weight*((color >> 16)&255); green += weight*((color >> 8)&255); blue += weight*(color&255);
            }
            pixels[y*next+x] = alpha ? (((alpha+2)/4) << 24) | (((red+alpha/2)/alpha) << 16) |
                (((green+alpha/2)/alpha) << 8) | ((blue+alpha/2)/alpha) : 0x00f6f7f7;
        }
    }
}

bool FillFrontendTexture(IDirect3DTexture9* texture, const FrontendImage& icon, unsigned size) {
    std::unique_ptr<std::uint32_t[]> pixels(new(std::nothrow) std::uint32_t[size*size]);
    if (!pixels) return false;
    std::fill_n(pixels.get(), size*size, 0x00f6f7f7u);
    IconPainter painter(pixels.get(), size);
    PaintFrontendGlyph(painter, icon.glyph);
    const unsigned levels = texture->GetLevelCount();
    for (unsigned level=0; level<levels; ++level) {
        D3DLOCKED_RECT image{};
        if (FAILED(texture->LockRect(level, &image, nullptr, 0))) return false;
        const bool valid = image.pBits && image.Pitch >= static_cast<int>(size * sizeof(std::uint32_t));
        if (valid) {
            auto* row = static_cast<std::uint8_t*>(image.pBits);
            for (unsigned y = 0; y < size; ++y, row += image.Pitch)
                std::memcpy(row, pixels.get() + y*size, size*sizeof(std::uint32_t));
        }
        const auto unlocked = texture->UnlockRect(level);
        if (!valid || FAILED(unlocked)) return false;
        if (level+1 < levels) {
            ShrinkFrontendMip(pixels.get(), size);
            size /= 2;
        }
    }
    return true;
}

const FrontendImage* MatchFrontendArtwork(const std::uint8_t* info) {
    if (!info) return nullptr;
    std::uint32_t hash;
    std::memcpy(&hash, info + 0x24, sizeof(hash));
    for (const auto& image : frontendImages) {
        if (image.hash != hash || std::strncmp(reinterpret_cast<const char*>(info + 0xc), image.nativeName, 24) != 0)
            continue;
        std::uint16_t width, height;
        std::memcpy(&width, info + 0x44, sizeof(width));
        std::memcpy(&height, info + 0x46, sizeof(height));
        if (width == image.logicalSize && height == image.logicalSize) return &image;
    }
    return nullptr;
}

bool FrontendArtworkEnabled() {
    static const bool enabled = !settingsPath[0] ||
        GetPrivateProfileIntW(L"Display", L"RedesignedMenuArtwork", 1, settingsPath) != 0;
    return enabled;
}

bool UploadFrontendArtwork(IDirect3DTexture9** destination, const std::uint8_t* info) {
    if (!destination || !*destination) return false;
    const auto* icon = MatchFrontendArtwork(info);
    if (!icon) return false;

    if (!FrontendArtworkEnabled()) return false;

    auto* original = *destination;
    D3DSURFACE_DESC desc{};
    if (FAILED(original->GetLevelDesc(0, &desc)) || desc.Usage != 0 ||
        desc.Width != icon->logicalSize || desc.Height != icon->logicalSize) return false;
    IDirect3DDevice9* device = nullptr;
    if (FAILED(original->GetDevice(&device))) return false;

    const unsigned size = icon->logicalSize * 4;
    // Retail creates managed textures. A default-pool device (including
    // D3D9Ex) needs a system-memory staging image for the same resource.
    IDirect3DTexture9* replacement = nullptr;
    IDirect3DTexture9* staging = nullptr;
    bool uploaded = SUCCEEDED(device->CreateTexture(size, size, 0, 0, D3DFMT_A8R8G8B8, desc.Pool, &replacement, nullptr));
    if (uploaded && desc.Pool == D3DPOOL_DEFAULT) {
        uploaded = SUCCEEDED(device->CreateTexture(size, size, replacement->GetLevelCount(), 0,
                             D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &staging, nullptr));
    }
    if (uploaded) uploaded = FillFrontendTexture(staging ? staging : replacement, *icon, size);
    if (uploaded && staging) uploaded = SUCCEEDED(device->UpdateTexture(staging, replacement));
    if (staging) staging->Release();
    device->Release();
    if (!uploaded) {
        if (replacement) replacement->Release();
        return false;
    }
    replacement->SetPriority(original->GetPriority());
    *destination = replacement;
    original->Release();
    static volatile LONG logged[std::size(frontendImages)]{};
    if (logPath[0] && InterlockedCompareExchange(&logged[icon - frontendImages], 1, 0) == 0) {
        if (LogFile log; log.file)
            std::fprintf(log.file, "Frontend artwork: %s logical=%ux%u uploaded=%ux%u\n",
                         icon->nativeName, icon->logicalSize, icon->logicalSize, size, size);
    }
    return true;
}

} // namespace

