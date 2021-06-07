#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <system_error>

#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>
#include <ShlObj.h>
#endif

#include "nlohmann/json.hpp"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include "Config.h"
#include "Hacks/AntiAim.h"
#include "Hacks/Backtrack.h"
#include "Hacks/Glow.h"
#include "Hacks/Sound.h"
#include "Security/xorstr.hpp"

#ifdef _WIN32
int CALLBACK fontCallback(const LOGFONTW* lpelfe, const TEXTMETRICW*, DWORD, LPARAM lParam)
{
    const wchar_t* const fontName = reinterpret_cast<const ENUMLOGFONTEXW*>(lpelfe)->elfFullName;

    if (fontName[0] == L'@')
        return TRUE;

    if (HFONT font = CreateFontW(0, 0, 0, 0,
        FW_NORMAL, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        DEFAULT_PITCH, fontName)) {

        DWORD fontData = GDI_ERROR;

        if (HDC hdc = CreateCompatibleDC(nullptr)) {
            SelectObject(hdc, font);
            // Do not use TTC fonts as we only support TTF fonts
            fontData = GetFontData(hdc, 'fctt', 0, NULL, 0);
            DeleteDC(hdc);
        }
        DeleteObject(font);

        if (fontData == GDI_ERROR) {
            if (char buff[1024]; WideCharToMultiByte(CP_UTF8, 0, fontName, -1, buff, sizeof(buff), nullptr, nullptr))
                reinterpret_cast<std::vector<std::string>*>(lParam)->emplace_back(buff);
        }
    }
    return TRUE;
}
#endif

Config::Config() noexcept
{
#ifdef _WIN32
    if (PWSTR pathToDocuments; SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &pathToDocuments))) {
        path = pathToDocuments;
        CoTaskMemFree(pathToDocuments);
    }
#else
    if (const char* homeDir = getenv("HOME"))
        path = homeDir;
#endif

    path /= xorstr_("Osiris");
    listConfigs();
    misc.clanTag[0] = '\0';

    load(u8"default.json", false);

#ifdef _WIN32
    LOGFONTW logfont;
    logfont.lfCharSet = ANSI_CHARSET;
    logfont.lfPitchAndFamily = DEFAULT_PITCH;
    logfont.lfFaceName[0] = L'\0';

    EnumFontFamiliesExW(GetDC(nullptr), &logfont, fontCallback, (LPARAM)&systemFonts, 0);
#endif

    std::sort(std::next(systemFonts.begin()), systemFonts.end());
}

static void from_json(const json& j, ColorToggle& ct)
{
    from_json(j, static_cast<Color4&>(ct));
    read(j, xorstr_("Enabled"), ct.enabled);
}

static void from_json(const json& j, Color3& c)
{
    read(j, xorstr_("Color"), c.color);
    read(j, xorstr_("Rainbow"), c.rainbow);
    read(j, xorstr_("Rainbow Speed"), c.rainbowSpeed);
}

static void from_json(const json& j, ColorToggle3& ct)
{
    from_json(j, static_cast<Color3&>(ct));
    read(j, xorstr_("Enabled"), ct.enabled);
}

static void from_json(const json& j, ColorToggleRounding& ctr)
{
    from_json(j, static_cast<ColorToggle&>(ctr));

    read(j, xorstr_("Rounding"), ctr.rounding);
}

static void from_json(const json& j, ColorToggleThickness& ctt)
{
    from_json(j, static_cast<ColorToggle&>(ctt));

    read(j, xorstr_("Thickness"), ctt.thickness);
}

static void from_json(const json& j, ColorToggleThicknessRounding& cttr)
{
    from_json(j, static_cast<ColorToggleRounding&>(cttr));

    read(j, xorstr_("Thickness"), cttr.thickness);
}

static void from_json(const json& j, Font& f)
{
    read<value_t::string>(j, xorstr_("Name"), f.name);

    if (!f.name.empty())
        config->scheduleFontLoad(f.name);

    if (const auto it = std::find_if(config->getSystemFonts().begin(), config->getSystemFonts().end(), [&f](const auto& e) { return e == f.name; }); it != config->getSystemFonts().end())
        f.index = std::distance(config->getSystemFonts().begin(), it);
    else
        f.index = 0;
}

static void from_json(const json& j, Snapline& s)
{
    from_json(j, static_cast<ColorToggleThickness&>(s));

    read(j, xorstr_("Type"), s.type);
}

static void from_json(const json& j, Box& b)
{
    from_json(j, static_cast<ColorToggleRounding&>(b));

    read(j, xorstr_("Type"), b.type);
    read(j, xorstr_("Scale"), b.scale);
    read<value_t::object>(j, xorstr_("Fill"), b.fill);
}

static void from_json(const json& j, Shared& s)
{
    read(j, xorstr_("Enabled"), s.enabled);
    read<value_t::object>(j, xorstr_("Font"), s.font);
    read<value_t::object>(j, xorstr_("Snapline"), s.snapline);
    read<value_t::object>(j, xorstr_("Box"), s.box);
    read<value_t::object>(j, xorstr_("Name"), s.name);
    read(j, xorstr_("Text Cull Distance"), s.textCullDistance);
}

static void from_json(const json& j, Weapon& w)
{
    from_json(j, static_cast<Shared&>(w));

    read<value_t::object>(j, xorstr_("Ammo"), w.ammo);
}

static void from_json(const json& j, Trail& t)
{
    from_json(j, static_cast<ColorToggleThickness&>(t));

    read(j, xorstr_("Type"), t.type);
    read(j, xorstr_("Time"), t.time);
}

static void from_json(const json& j, Trails& t)
{
    read(j, xorstr_("Enabled"), t.enabled);
    read<value_t::object>(j, xorstr_("Local Player"), t.localPlayer);
    read<value_t::object>(j, xorstr_("Allies"), t.allies);
    read<value_t::object>(j, xorstr_("Enemies"), t.enemies);
}

static void from_json(const json& j, Projectile& p)
{
    from_json(j, static_cast<Shared&>(p));

    read<value_t::object>(j, xorstr_("Trails"), p.trails);
}

static void from_json(const json& j, HealthBar& o)
{
    from_json(j, static_cast<ColorToggle&>(o));
    read(j, xorstr_("Type"), o.type);
}

static void from_json(const json& j, Player& p)
{
    from_json(j, static_cast<Shared&>(p));

    read<value_t::object>(j, xorstr_("Weapon"), p.weapon);
    read<value_t::object>(j, xorstr_("Flash Duration"), p.flashDuration);
    read(j, xorstr_("Audible Only"), p.audibleOnly);
    read(j, xorstr_("Spotted Only"), p.spottedOnly);
    read<value_t::object>(j, xorstr_("Health Bar"), p.healthBar);
    read<value_t::object>(j, xorstr_("Skeleton"), p.skeleton);
    read<value_t::object>(j, xorstr_("Head Box"), p.headBox);
}

static void from_json(const json& j, OffscreenEnemies& o)
{
    from_json(j, static_cast<ColorToggle&>(o));

    read<value_t::object>(j, xorstr_("Health Bar"), o.healthBar);
}

static void from_json(const json& j, BulletTracers& o)
{
    from_json(j, static_cast<ColorToggle&>(o));
}

static void from_json(const json& j, ImVec2& v)
{
    read(j, xorstr_("X"), v.x);
    read(j, xorstr_("Y"), v.y);
}

static void from_json(const json& j, Config::Aimbot& a)
{
    read(j, xorstr_("Enabled"), a.enabled);
    read(j, xorstr_("Aimlock"), a.aimlock);
    read(j, xorstr_("Silent"), a.silent);
    read(j, xorstr_("Friendly fire"), a.friendlyFire);
    read(j, xorstr_("Visible only"), a.visibleOnly);
    read(j, xorstr_("Scoped only"), a.scopedOnly);
    read(j, xorstr_("Ignore flash"), a.ignoreFlash);
    read(j, xorstr_("Ignore smoke"), a.ignoreSmoke);
    read(j, xorstr_("Auto shot"), a.autoShot);
    read(j, xorstr_("Auto scope"), a.autoScope);
    read(j, xorstr_("Fov"), a.fov);
    read(j, xorstr_("Smooth"), a.smooth);
    read(j, xorstr_("Bone"), a.bone);
    read(j, xorstr_("Max aim inaccuracy"), a.maxAimInaccuracy);
    read(j, xorstr_("Max shot inaccuracy"), a.maxShotInaccuracy);
    read(j, xorstr_("Min damage"), a.minDamage);
    read(j, xorstr_("Killshot"), a.killshot);
    read(j, xorstr_("Between shots"), a.betweenShots);
}

static void from_json(const json& j, Config::Triggerbot& t)
{
    read(j, xorstr_("Enabled"), t.enabled);
    read(j, xorstr_("Friendly fire"), t.friendlyFire);
    read(j, xorstr_("Scoped only"), t.scopedOnly);
    read(j, xorstr_("Ignore flash"), t.ignoreFlash);
    read(j, xorstr_("Ignore smoke"), t.ignoreSmoke);
    read(j, xorstr_("Hitgroup"), t.hitgroup);
    read(j, xorstr_("Shot delay"), t.shotDelay);
    read(j, xorstr_("Min damage"), t.minDamage);
    read(j, xorstr_("Killshot"), t.killshot);
    read(j, xorstr_("Burst Time"), t.burstTime);
}

static void from_json(const json& j, Config::Chams::Material& m)
{
    from_json(j, static_cast<Color4&>(m));

    read(j, xorstr_("Enabled"), m.enabled);
    read(j, xorstr_("Health based"), m.healthBased);
    read(j, xorstr_("Blinking"), m.blinking);
    read(j, xorstr_("Wireframe"), m.wireframe);
    read(j, xorstr_("Cover"), m.cover);
    read(j, xorstr_("Ignore-Z"), m.ignorez);
    read(j, xorstr_("Material"), m.material);
}

static void from_json(const json& j, Config::Chams& c)
{
    read_array_opt(j, xorstr_("Materials"), c.materials);
}

static void from_json(const json& j, Config::StreamProofESP& e)
{
    read(j, xorstr_("Toggle Key"), e.toggleKey);
    read(j, xorstr_("Hold Key"), e.holdKey);
    read(j, xorstr_("Allies"), e.allies);
    read(j, xorstr_("Enemies"), e.enemies);
    read(j, xorstr_("Weapons"), e.weapons);
    read(j, xorstr_("Projectiles"), e.projectiles);
    read(j, xorstr_("Loot Crates"), e.lootCrates);
    read(j, xorstr_("Other Entities"), e.otherEntities);
}

static void from_json(const json& j, Config::Visuals::ColorCorrection& c)
{
    read(j, xorstr_("Enabled"), c.enabled);
    read(j, xorstr_("Blue"), c.blue);
    read(j, xorstr_("Red"), c.red);
    read(j, xorstr_("Mono"), c.mono);
    read(j, xorstr_("Saturation"), c.saturation);
    read(j, xorstr_("Ghost"), c.ghost);
    read(j, xorstr_("Green"), c.green);
    read(j, xorstr_("Yellow"), c.yellow);
}

static void from_json(const json& j, Config::Visuals& v)
{
    read(j, xorstr_("Disable post-processing"), v.disablePostProcessing);
    read(j, xorstr_("Inverse ragdoll gravity"), v.inverseRagdollGravity);
    read(j, xorstr_("No fog"), v.noFog);
    read(j, xorstr_("No 3d sky"), v.no3dSky);
    read(j, xorstr_("No aim punch"), v.noAimPunch);
    read(j, xorstr_("No view punch"), v.noViewPunch);
    read(j, xorstr_("No hands"), v.noHands);
    read(j, xorstr_("No sleeves"), v.noSleeves);
    read(j, xorstr_("No weapons"), v.noWeapons);
    read(j, xorstr_("No smoke"), v.noSmoke);
    read(j, xorstr_("No blur"), v.noBlur);
    read(j, xorstr_("No scope overlay"), v.noScopeOverlay);
    read(j, xorstr_("No grass"), v.noGrass);
    read(j, xorstr_("No shadows"), v.noShadows);
    read(j, xorstr_("Wireframe smoke"), v.wireframeSmoke);
    read(j, xorstr_("Zoom"), v.zoom);
    read(j, xorstr_("Zoom key"), v.zoomKey);
    read(j, xorstr_("Thirdperson"), v.thirdperson);
    read(j, xorstr_("Thirdperson key"), v.thirdpersonKey);
    read(j, xorstr_("Thirdperson distance"), v.thirdpersonDistance);
    read(j, xorstr_("Viewmodel FOV"), v.viewmodelFov);
    read(j, xorstr_("FOV"), v.fov);
    read(j, xorstr_("Far Z"), v.farZ);
    read(j, xorstr_("Flash reduction"), v.flashReduction);
    read(j, xorstr_("Brightness"), v.brightness);
    read(j, xorstr_("Skybox"), v.skybox);
    read<value_t::object>(j, xorstr_("World"), v.world);
    read<value_t::object>(j, xorstr_("Sky"), v.sky);
    read(j, xorstr_("Deagle spinner"), v.deagleSpinner);
    read(j, xorstr_("Screen effect"), v.screenEffect);
    read(j, xorstr_("Hit effect"), v.hitEffect);
    read(j, xorstr_("Hit effect time"), v.hitEffectTime);
    read(j, xorstr_("Hit marker"), v.hitMarker);
    read(j, xorstr_("Hit marker time"), v.hitMarkerTime);
    read(j, xorstr_("Playermodel T"), v.playerModelT);
    read(j, xorstr_("Playermodel CT"), v.playerModelCT);
    read<value_t::object>(j, xorstr_("Color correction"), v.colorCorrection);
    read<value_t::object>(j, xorstr_("Bullet Tracers"), v.bulletTracers);
    read<value_t::object>(j, xorstr_("Molotov Hull"), v.molotovHull);
}

static void from_json(const json& j, Config::Style& s)
{
    read(j, xorstr_("Menu style"), s.menuStyle);
    read(j, xorstr_("Menu colors"), s.menuColors);

    if (j.contains(xorstr_("Colors")) && j[xorstr_("Colors")].is_object()) {
        const auto& colors = j[xorstr_("Colors")];

        ImGuiStyle& style = ImGui::GetStyle();

        for (int i = 0; i < ImGuiCol_COUNT; i++) {
            if (const char* name = ImGui::GetStyleColorName(i); colors.contains(name)) {
                std::array<float, 4> temp;
                read(colors, name, temp);
                style.Colors[i].x = temp[0];
                style.Colors[i].y = temp[1];
                style.Colors[i].z = temp[2];
                style.Colors[i].w = temp[3];
            }
        }
    }
}

static void from_json(const json& j, PurchaseList& pl)
{
    read(j, xorstr_("Enabled"), pl.enabled);
    read(j, xorstr_("Only During Freeze Time"), pl.onlyDuringFreezeTime);
    read(j, xorstr_("Show Prices"), pl.showPrices);
    read(j, xorstr_("No Title Bar"), pl.noTitleBar);
    read(j, xorstr_("Mode"), pl.mode);
}

static void from_json(const json& j, Config::Misc::SpectatorList& sl)
{
    read(j, xorstr_("Enabled"), sl.enabled);
    read(j, xorstr_("No Title Bar"), sl.noTitleBar);
    read<value_t::object>(j, xorstr_("Pos"), sl.pos);
    read<value_t::object>(j, xorstr_("Size"), sl.size);
}

static void from_json(const json& j, Config::Misc::Watermark& o)
{
    read(j, xorstr_("Enabled"), o.enabled);
}

static void from_json(const json& j, PreserveKillfeed& o)
{
    read(j, xorstr_("Enabled"), o.enabled);
    read(j, xorstr_("Only Headshots"), o.onlyHeadshots);
}

static void from_json(const json& j, Config::Misc& m)
{
    read(j, xorstr_("Menu key"), m.menuKey);
    read(j, xorstr_("Anti AFK kick"), m.antiAfkKick);
    read(j, xorstr_("Auto strafe"), m.autoStrafe);
    read(j, xorstr_("Bunny hop"), m.bunnyHop);
    read(j, xorstr_("Custom clan tag"), m.customClanTag);
    read(j, xorstr_("Clock tag"), m.clocktag);
    read(j, xorstr_("Clan tag"), m.clanTag, sizeof(m.clanTag));
    read(j, xorstr_("Animated clan tag"), m.animatedClanTag);
    read(j, xorstr_("Fast duck"), m.fastDuck);
    read(j, xorstr_("Moonwalk"), m.moonwalk);
    read(j, xorstr_("Edge Jump"), m.edgejump);
    read(j, xorstr_("Edge Jump Key"), m.edgejumpkey);
    read(j, xorstr_("Slowwalk"), m.slowwalk);
    read(j, xorstr_("Slowwalk key"), m.slowwalkKey);
    read<value_t::object>(j, xorstr_("Noscope crosshair"), m.noscopeCrosshair);
    read<value_t::object>(j, xorstr_("Recoil crosshair"), m.recoilCrosshair);
    read(j, xorstr_("Auto pistol"), m.autoPistol);
    read(j, xorstr_("Auto reload"), m.autoReload);
    read(j, xorstr_("Auto accept"), m.autoAccept);
    read(j, xorstr_("Radar hack"), m.radarHack);
    read(j, xorstr_("Reveal ranks"), m.revealRanks);
    read(j, xorstr_("Reveal money"), m.revealMoney);
    read(j, xorstr_("Reveal suspect"), m.revealSuspect);
    read(j, xorstr_("Reveal votes"), m.revealVotes);
    read<value_t::object>(j, xorstr_("Spectator list"), m.spectatorList);
    read<value_t::object>(j, xorstr_("Watermark"), m.watermark);
    read<value_t::object>(j, xorstr_("Offscreen Enemies"), m.offscreenEnemies);
    read(j, xorstr_("Fix animation LOD"), m.fixAnimationLOD);
    read(j, xorstr_("Fix bone matrix"), m.fixBoneMatrix);
    read(j, xorstr_("Fix movement"), m.fixMovement);
    read(j, xorstr_("Disable model occlusion"), m.disableModelOcclusion);
    read(j, xorstr_("Aspect Ratio"), m.aspectratio);
    read(j, xorstr_("Kill message"), m.killMessage);
    read<value_t::string>(j, xorstr_("Kill message string"), m.killMessageString);
    read(j, xorstr_("Name stealer"), m.nameStealer);
    read(j, xorstr_("Disable HUD blur"), m.disablePanoramablur);
    read(j, xorstr_("Ban color"), m.banColor);
    read<value_t::string>(j, xorstr_("Ban text"), m.banText);
    read(j, xorstr_("Fast plant"), m.fastPlant);
    read(j, xorstr_("Fast Stop"), m.fastStop);
    read<value_t::object>(j, xorstr_("Bomb timer"), m.bombTimer);
    read(j, xorstr_("Quick reload"), m.quickReload);
    read(j, xorstr_("Prepare revolver"), m.prepareRevolver);
    read(j, xorstr_("Prepare revolver key"), m.prepareRevolverKey);
    read(j, xorstr_("Hit sound"), m.hitSound);
    read(j, xorstr_("Choked packets"), m.chokedPackets);
    read(j, xorstr_("Choked packets key"), m.chokedPacketsKey);
    read(j, xorstr_("Quick healthshot key"), m.quickHealthshotKey);
    read(j, xorstr_("Grenade predict"), m.nadePredict);
    read(j, xorstr_("Fix tablet signal"), m.fixTabletSignal);
    read(j, xorstr_("Max angle delta"), m.maxAngleDelta);
    read(j, xorstr_("Fix tablet signal"), m.fixTabletSignal);
    read<value_t::string>(j, xorstr_("Custom Hit Sound"), m.customHitSound);
    read(j, xorstr_("Kill sound"), m.killSound);
    read<value_t::string>(j, xorstr_("Custom Kill Sound"), m.customKillSound);
    read<value_t::object>(j, xorstr_("Purchase List"), m.purchaseList);
    read<value_t::object>(j, xorstr_("Reportbot"), m.reportbot);
    read(j, xorstr_("Opposite Hand Knife"), m.oppositeHandKnife);
    read<value_t::object>(j, xorstr_("Preserve Killfeed"), m.preserveKillfeed);
}

static void from_json(const json& j, Config::Misc::Reportbot& r)
{
    read(j, xorstr_("Enabled"), r.enabled);
    read(j, xorstr_("Target"), r.target);
    read(j, xorstr_("Delay"), r.delay);
    read(j, xorstr_("Rounds"), r.rounds);
    read(j, xorstr_("Abusive Communications"), r.textAbuse);
    read(j, xorstr_("Griefing"), r.griefing);
    read(j, xorstr_("Wall Hacking"), r.wallhack);
    read(j, xorstr_("Aim Hacking"), r.aimbot);
    read(j, xorstr_("Other Hacking"), r.other);
}

void Config::load(size_t id, bool incremental) noexcept
{
    load((const char8_t*)configs[id].c_str(), incremental);
}

void Config::load(const char8_t* name, bool incremental) noexcept
{
    json j;

    if (std::ifstream in{ path / name }; in.good()) {
        j = json::parse(in, nullptr, false, true);
        if (j.is_discarded())
            return;
    } else {
        return;
    }

    if (!incremental)
        reset();

    read(j, xorstr_("Aimbot"), aimbot);
    read(j, xorstr_("Aimbot On key"), aimbotOnKey);
    read(j, xorstr_("Aimbot Key"), aimbotKey);
    read(j, xorstr_("Aimbot Key mode"), aimbotKeyMode);

    read(j, xorstr_("Triggerbot"), triggerbot);
    read(j, xorstr_("Triggerbot Key"), triggerbotHoldKey);

    read(j, xorstr_("Chams"), chams);
    read(j[xorstr_("Chams")], xorstr_("Toggle Key"), chamsToggleKey);
    read(j[xorstr_("Chams")], xorstr_("Hold Key"), chamsHoldKey);
    read<value_t::object>(j, xorstr_("ESP"), streamProofESP);
    read<value_t::object>(j, xorstr_("Visuals"), visuals);
    read<value_t::object>(j, xorstr_("Style"), style);
    read<value_t::object>(j, xorstr_("Misc"), misc);

    AntiAim::fromJson(j[xorstr_("Anti aim")]);
    Backtrack::fromJson(j[xorstr_("Backtrack")]);
    Glow::fromJson(j[xorstr_("Glow")]);
    InventoryChanger::fromJson(j[xorstr_("Inventory Changer")]);
    Sound::fromJson(j[xorstr_("Sound")]);
}

static void to_json(json& j, const ColorToggle& o, const ColorToggle& dummy = {})
{
    to_json(j, static_cast<const Color4&>(o), dummy);
    WRITE(xorstr_("Enabled"), enabled);
}

static void to_json(json& j, const Color3& o, const Color3& dummy = {})
{
    WRITE(xorstr_("Color"), color);
    WRITE(xorstr_("Rainbow"), rainbow);
    WRITE(xorstr_("Rainbow Speed"), rainbowSpeed);
}

static void to_json(json& j, const ColorToggle3& o, const ColorToggle3& dummy = {})
{
    to_json(j, static_cast<const Color3&>(o), dummy);
    WRITE(xorstr_("Enabled"), enabled);
}

static void to_json(json& j, const ColorToggleRounding& o, const ColorToggleRounding& dummy = {})
{
    to_json(j, static_cast<const ColorToggle&>(o), dummy);
    WRITE(xorstr_("Rounding"), rounding);
}

static void to_json(json& j, const ColorToggleThickness& o, const ColorToggleThickness& dummy = {})
{
    to_json(j, static_cast<const ColorToggle&>(o), dummy);
    WRITE(xorstr_("Thickness"), thickness);
}

static void to_json(json& j, const ColorToggleThicknessRounding& o, const ColorToggleThicknessRounding& dummy = {})
{
    to_json(j, static_cast<const ColorToggleRounding&>(o), dummy);
    WRITE(xorstr_("Thickness"), thickness);
}

static void to_json(json& j, const Font& o, const Font& dummy = {})
{
    WRITE(xorstr_("Name"), name);
}

static void to_json(json& j, const Snapline& o, const Snapline& dummy = {})
{
    to_json(j, static_cast<const ColorToggleThickness&>(o), dummy);
    WRITE(xorstr_("Type"), type);
}

static void to_json(json& j, const Box& o, const Box& dummy = {})
{
    to_json(j, static_cast<const ColorToggleRounding&>(o), dummy);
    WRITE(xorstr_("Type"), type);
    WRITE(xorstr_("Scale"), scale);
    WRITE(xorstr_("Fill"), fill);
}

static void to_json(json& j, const Shared& o, const Shared& dummy = {})
{
    WRITE(xorstr_("Enabled"), enabled);
    WRITE(xorstr_("Font"), font);
    WRITE(xorstr_("Snapline"), snapline);
    WRITE(xorstr_("Box"), box);
    WRITE(xorstr_("Name"), name);
    WRITE(xorstr_("Text Cull Distance"), textCullDistance);
}

static void to_json(json& j, const HealthBar& o, const HealthBar& dummy = {})
{
    to_json(j, static_cast<const ColorToggle&>(o), dummy);
    WRITE(xorstr_("Type"), type);
}

static void to_json(json& j, const Player& o, const Player& dummy = {})
{
    to_json(j, static_cast<const Shared&>(o), dummy);
    WRITE(xorstr_("Weapon"), weapon);
    WRITE(xorstr_("Flash Duration"), flashDuration);
    WRITE(xorstr_("Audible Only"), audibleOnly);
    WRITE(xorstr_("Spotted Only"), spottedOnly);
    WRITE(xorstr_("Health Bar"), healthBar);
    WRITE(xorstr_("Skeleton"), skeleton);
    WRITE(xorstr_("Head Box"), headBox);
}

static void to_json(json& j, const Weapon& o, const Weapon& dummy = {})
{
    to_json(j, static_cast<const Shared&>(o), dummy);
    WRITE(xorstr_("Ammo"), ammo);
}

static void to_json(json& j, const Trail& o, const Trail& dummy = {})
{
    to_json(j, static_cast<const ColorToggleThickness&>(o), dummy);
    WRITE(xorstr_("Type"), type);
    WRITE(xorstr_("Time"), time);
}

static void to_json(json& j, const Trails& o, const Trails& dummy = {})
{
    WRITE(xorstr_("Enabled"), enabled);
    WRITE(xorstr_("Local Player"), localPlayer);
    WRITE(xorstr_("Allies"), allies);
    WRITE(xorstr_("Enemies"), enemies);
}

static void to_json(json& j, const OffscreenEnemies& o, const OffscreenEnemies& dummy = {})
{
    to_json(j, static_cast<const ColorToggle&>(o), dummy);

    WRITE(xorstr_("Health Bar"), healthBar);
}

static void to_json(json& j, const BulletTracers& o, const BulletTracers& dummy = {})
{
    to_json(j, static_cast<const ColorToggle&>(o), dummy);
}

static void to_json(json& j, const Projectile& o, const Projectile& dummy = {})
{
    j = static_cast<const Shared&>(o);

    WRITE(xorstr_("Trails"), trails);
}

static void to_json(json& j, const ImVec2& o, const ImVec2& dummy = {})
{
    WRITE(xorstr_("X"), x);
    WRITE(xorstr_("Y"), y);
}

static void to_json(json& j, const Config::Aimbot& o, const Config::Aimbot& dummy = {})
{
    WRITE(xorstr_("Enabled"), enabled);
    WRITE(xorstr_("Aimlock"), aimlock);
    WRITE(xorstr_("Silent"), silent);
    WRITE(xorstr_("Friendly fire"), friendlyFire);
    WRITE(xorstr_("Visible only"), visibleOnly);
    WRITE(xorstr_("Scoped only"), scopedOnly);
    WRITE(xorstr_("Ignore flash"), ignoreFlash);
    WRITE(xorstr_("Ignore smoke"), ignoreSmoke);
    WRITE(xorstr_("Auto shot"), autoShot);
    WRITE(xorstr_("Auto scope"), autoScope);
    WRITE(xorstr_("Fov"), fov);
    WRITE(xorstr_("Smooth"), smooth);
    WRITE(xorstr_("Bone"), bone);
    WRITE(xorstr_("Max aim inaccuracy"), maxAimInaccuracy);
    WRITE(xorstr_("Max shot inaccuracy"), maxShotInaccuracy);
    WRITE(xorstr_("Min damage"), minDamage);
    WRITE(xorstr_("Killshot"), killshot);
    WRITE(xorstr_("Between shots"), betweenShots);
}

static void to_json(json& j, const Config::Triggerbot& o, const Config::Triggerbot& dummy = {})
{
    WRITE(xorstr_("Enabled"), enabled);
    WRITE(xorstr_("Friendly fire"), friendlyFire);
    WRITE(xorstr_("Scoped only"), scopedOnly);
    WRITE(xorstr_("Ignore flash"), ignoreFlash);
    WRITE(xorstr_("Ignore smoke"), ignoreSmoke);
    WRITE(xorstr_("Hitgroup"), hitgroup);
    WRITE(xorstr_("Shot delay"), shotDelay);
    WRITE(xorstr_("Min damage"), minDamage);
    WRITE(xorstr_("Killshot"), killshot);
    WRITE(xorstr_("Burst Time"), burstTime);
}

static void to_json(json& j, const Config::Chams::Material& o)
{
    const Config::Chams::Material dummy;

    to_json(j, static_cast<const Color4&>(o), dummy);
    WRITE(xorstr_("Enabled"), enabled);
    WRITE(xorstr_("Health based"), healthBased);
    WRITE(xorstr_("Blinking"), blinking);
    WRITE(xorstr_("Wireframe"), wireframe);
    WRITE(xorstr_("Cover"), cover);
    WRITE(xorstr_("Ignore-Z"), ignorez);
    WRITE(xorstr_("Material"), material);
}

static void to_json(json& j, const Config::Chams& o)
{
    j[xorstr_("Materials")] = o.materials;
}

static void to_json(json& j, const Config::StreamProofESP& o, const Config::StreamProofESP& dummy = {})
{
    WRITE(xorstr_("Toggle Key"), toggleKey);
    WRITE(xorstr_("Hold Key"), holdKey);
    j[xorstr_("Allies")] = o.allies;
    j[xorstr_("Enemies")] = o.enemies;
    j[xorstr_("Weapons")] = o.weapons;
    j[xorstr_("Projectiles")] = o.projectiles;
    j[xorstr_("Loot Crates")] = o.lootCrates;
    j[xorstr_("Other Entities")] = o.otherEntities;
}

static void to_json(json& j, const Config::Misc::Reportbot& o, const Config::Misc::Reportbot& dummy = {})
{
    WRITE(xorstr_("Enabled"), enabled);
    WRITE(xorstr_("Target"), target);
    WRITE(xorstr_("Delay"), delay);
    WRITE(xorstr_("Rounds"), rounds);
    WRITE(xorstr_("Abusive Communications"), textAbuse);
    WRITE(xorstr_("Griefing"), griefing);
    WRITE(xorstr_("Wall Hacking"), wallhack);
    WRITE(xorstr_("Aim Hacking"), aimbot);
    WRITE(xorstr_("Other Hacking"), other);
}

static void to_json(json& j, const PurchaseList& o, const PurchaseList& dummy = {})
{
    WRITE(xorstr_("Enabled"), enabled);
    WRITE(xorstr_("Only During Freeze Time"), onlyDuringFreezeTime);
    WRITE(xorstr_("Show Prices"), showPrices);
    WRITE(xorstr_("No Title Bar"), noTitleBar);
    WRITE(xorstr_("Mode"), mode);
}

static void to_json(json& j, const Config::Misc::SpectatorList& o, const Config::Misc::SpectatorList& dummy = {})
{
    WRITE(xorstr_("Enabled"), enabled);
    WRITE(xorstr_("No Title Bar"), noTitleBar);

    if (const auto window = ImGui::FindWindowByName(xorstr_("Spectator list"))) {
        j[xorstr_("Pos")] = window->Pos;
        j[xorstr_("Size")] = window->SizeFull;
    }
}

static void to_json(json& j, const Config::Misc::Watermark& o, const Config::Misc::Watermark& dummy = {})
{
    WRITE(xorstr_("Enabled"), enabled);
}

static void to_json(json& j, const PreserveKillfeed& o, const PreserveKillfeed& dummy = {})
{
    WRITE(xorstr_("Enabled"), enabled);
    WRITE(xorstr_("Only Headshots"), onlyHeadshots);
}

static void to_json(json& j, const Config::Misc& o)
{
    const Config::Misc dummy;

    WRITE(xorstr_("Menu key"), menuKey);
    WRITE(xorstr_("Anti AFK kick"), antiAfkKick);
    WRITE(xorstr_("Auto strafe"), autoStrafe);
    WRITE(xorstr_("Bunny hop"), bunnyHop);
    WRITE(xorstr_("Custom clan tag"), customClanTag);
    WRITE(xorstr_("Clock tag"), clocktag);

    if (o.clanTag[0])
        j[xorstr_("Clan tag")] = o.clanTag;

    WRITE(xorstr_("Animated clan tag"), animatedClanTag);
    WRITE(xorstr_("Fast duck"), fastDuck);
    WRITE(xorstr_("Moonwalk"), moonwalk);
    WRITE(xorstr_("Edge Jump"), edgejump);
    WRITE(xorstr_("Edge Jump Key"), edgejumpkey);
    WRITE(xorstr_("Slowwalk"), slowwalk);
    WRITE(xorstr_("Slowwalk key"), slowwalkKey);
    WRITE(xorstr_("Noscope crosshair"), noscopeCrosshair);
    WRITE(xorstr_("Recoil crosshair"), recoilCrosshair);
    WRITE(xorstr_("Auto pistol"), autoPistol);
    WRITE(xorstr_("Auto reload"), autoReload);
    WRITE(xorstr_("Auto accept"), autoAccept);
    WRITE(xorstr_("Radar hack"), radarHack);
    WRITE(xorstr_("Reveal ranks"), revealRanks);
    WRITE(xorstr_("Reveal money"), revealMoney);
    WRITE(xorstr_("Reveal suspect"), revealSuspect);
    WRITE(xorstr_("Reveal votes"), revealVotes);
    WRITE(xorstr_("Spectator list"), spectatorList);
    WRITE(xorstr_("Watermark"), watermark);
    WRITE(xorstr_("Offscreen Enemies"), offscreenEnemies);
    WRITE(xorstr_("Fix animation LOD"), fixAnimationLOD);
    WRITE(xorstr_("Fix bone matrix"), fixBoneMatrix);
    WRITE(xorstr_("Fix movement"), fixMovement);
    WRITE(xorstr_("Disable model occlusion"), disableModelOcclusion);
    WRITE(xorstr_("Aspect Ratio"), aspectratio);
    WRITE(xorstr_("Kill message"), killMessage);
    WRITE(xorstr_("Kill message string"), killMessageString);
    WRITE(xorstr_("Name stealer"), nameStealer);
    WRITE(xorstr_("Disable HUD blur"), disablePanoramablur);
    WRITE(xorstr_("Ban color"), banColor);
    WRITE(xorstr_("Ban text"), banText);
    WRITE(xorstr_("Fast plant"), fastPlant);
    WRITE(xorstr_("Fast Stop"), fastStop);
    WRITE(xorstr_("Bomb timer"), bombTimer);
    WRITE(xorstr_("Quick reload"), quickReload);
    WRITE(xorstr_("Prepare revolver"), prepareRevolver);
    WRITE(xorstr_("Prepare revolver key"), prepareRevolverKey);
    WRITE(xorstr_("Hit sound"), hitSound);
    WRITE(xorstr_("Choked packets"), chokedPackets);
    WRITE(xorstr_("Choked packets key"), chokedPacketsKey);
    WRITE(xorstr_("Quick healthshot key"), quickHealthshotKey);
    WRITE(xorstr_("Grenade predict"), nadePredict);
    WRITE(xorstr_("Fix tablet signal"), fixTabletSignal);
    WRITE(xorstr_("Max angle delta"), maxAngleDelta);
    WRITE(xorstr_("Fix tablet signal"), fixTabletSignal);
    WRITE(xorstr_("Custom Hit Sound"), customHitSound);
    WRITE(xorstr_("Kill sound"), killSound);
    WRITE(xorstr_("Custom Kill Sound"), customKillSound);
    WRITE(xorstr_("Purchase List"), purchaseList);
    WRITE(xorstr_("Reportbot"), reportbot);
    WRITE(xorstr_("Opposite Hand Knife"), oppositeHandKnife);
    WRITE(xorstr_("Preserve Killfeed"), preserveKillfeed);
}

static void to_json(json& j, const Config::Visuals::ColorCorrection& o, const Config::Visuals::ColorCorrection& dummy)
{
    WRITE(xorstr_("Enabled"), enabled);
    WRITE(xorstr_("Blue"), blue);
    WRITE(xorstr_("Red"), red);
    WRITE(xorstr_("Mono"), mono);
    WRITE(xorstr_("Saturation"), saturation);
    WRITE(xorstr_("Ghost"), ghost);
    WRITE(xorstr_("Green"), green);
    WRITE(xorstr_("Yellow"), yellow);
}

static void to_json(json& j, const Config::Visuals& o)
{
    const Config::Visuals dummy;

    WRITE(xorstr_("Disable post-processing"), disablePostProcessing);
    WRITE(xorstr_("Inverse ragdoll gravity"), inverseRagdollGravity);
    WRITE(xorstr_("No fog"), noFog);
    WRITE(xorstr_("No 3d sky"), no3dSky);
    WRITE(xorstr_("No aim punch"), noAimPunch);
    WRITE(xorstr_("No view punch"), noViewPunch);
    WRITE(xorstr_("No hands"), noHands);
    WRITE(xorstr_("No sleeves"), noSleeves);
    WRITE(xorstr_("No weapons"), noWeapons);
    WRITE(xorstr_("No smoke"), noSmoke);
    WRITE(xorstr_("No blur"), noBlur);
    WRITE(xorstr_("No scope overlay"), noScopeOverlay);
    WRITE(xorstr_("No grass"), noGrass);
    WRITE(xorstr_("No shadows"), noShadows);
    WRITE(xorstr_("Wireframe smoke"), wireframeSmoke);
    WRITE(xorstr_("Zoom"), zoom);
    WRITE(xorstr_("Zoom key"), zoomKey);
    WRITE(xorstr_("Thirdperson"), thirdperson);
    WRITE(xorstr_("Thirdperson key"), thirdpersonKey);
    WRITE(xorstr_("Thirdperson distance"), thirdpersonDistance);
    WRITE(xorstr_("Viewmodel FOV"), viewmodelFov);
    WRITE(xorstr_("FOV"), fov);
    WRITE(xorstr_("Far Z"), farZ);
    WRITE(xorstr_("Flash reduction"), flashReduction);
    WRITE(xorstr_("Brightness"), brightness);
    WRITE(xorstr_("Skybox"), skybox);
    WRITE(xorstr_("World"), world);
    WRITE(xorstr_("Sky"), sky);
    WRITE(xorstr_("Deagle spinner"), deagleSpinner);
    WRITE(xorstr_("Screen effect"), screenEffect);
    WRITE(xorstr_("Hit effect"), hitEffect);
    WRITE(xorstr_("Hit effect time"), hitEffectTime);
    WRITE(xorstr_("Hit marker"), hitMarker);
    WRITE(xorstr_("Hit marker time"), hitMarkerTime);
    WRITE(xorstr_("Playermodel T"), playerModelT);
    WRITE(xorstr_("Playermodel CT"), playerModelCT);
    WRITE(xorstr_("Color correction"), colorCorrection);
    WRITE(xorstr_("Bullet Tracers"), bulletTracers);
    WRITE(xorstr_("Molotov Hull"), molotovHull);
}

static void to_json(json& j, const ImVec4& o)
{
    j[0] = o.x;
    j[1] = o.y;
    j[2] = o.z;
    j[3] = o.w;
}

static void to_json(json& j, const Config::Style& o)
{
    const Config::Style dummy;

    WRITE(xorstr_("Menu style"), menuStyle);
    WRITE(xorstr_("Menu colors"), menuColors);

    auto& colors = j[xorstr_("Colors")];
    ImGuiStyle& style = ImGui::GetStyle();

    for (int i = 0; i < ImGuiCol_COUNT; i++)
        colors[ImGui::GetStyleColorName(i)] = style.Colors[i];
}

void removeEmptyObjects(json& j) noexcept
{
    for (auto it = j.begin(); it != j.end();) {
        auto& val = it.value();
        if (val.is_object() || val.is_array())
            removeEmptyObjects(val);
        if (val.empty() && !j.is_array())
            it = j.erase(it);
        else
            ++it;
    }
}

void Config::save(size_t id) const noexcept
{
    json j;

    j[xorstr_("Aimbot")] = aimbot;
    j[xorstr_("Aimbot On key")] = aimbotOnKey;
    to_json(j[xorstr_("Aimbot Key")], aimbotKey, KeyBind::NONE);
    j[xorstr_("Aimbot Key mode")] = aimbotKeyMode;

    j[xorstr_("Triggerbot")] = triggerbot;
    to_json(j[xorstr_("Triggerbot Key")], triggerbotHoldKey, KeyBind::NONE);

    j[xorstr_("Backtrack")] = Backtrack::toJson();
    j[xorstr_("Anti aim")] = AntiAim::toJson();
    j[xorstr_("Glow")] = Glow::toJson();
    j[xorstr_("Chams")] = chams;
    to_json(j[xorstr_("Chams")][xorstr_("Toggle Key")], chamsToggleKey, KeyBind::NONE);
    to_json(j[xorstr_("Chams")][xorstr_("Hold Key")], chamsHoldKey, KeyBind::NONE);
    j[xorstr_("ESP")] = streamProofESP;
    j[xorstr_("Sound")] = ::Sound::toJson();
    j[xorstr_("Visuals")] = visuals;
    j[xorstr_("Misc")] = misc;
    j[xorstr_("Style")] = style;
    j[xorstr_("Inventory Changer")] = InventoryChanger::toJson();

    removeEmptyObjects(j);

    createConfigDir();
    if (std::ofstream out{ path / (const char8_t*)configs[id].c_str() }; out.good())
        out << std::setw(2) << j;
}

void Config::add(const char* name) noexcept
{
    if (*name && std::find(configs.cbegin(), configs.cend(), name) == configs.cend()) {
        configs.emplace_back(name);
        save(configs.size() - 1);
    }
}

void Config::remove(size_t id) noexcept
{
    std::error_code ec;
    std::filesystem::remove(path / (const char8_t*)configs[id].c_str(), ec);
    configs.erase(configs.cbegin() + id);
}

void Config::rename(size_t item, const char* newName) noexcept
{
    std::error_code ec;
    std::filesystem::rename(path / (const char8_t*)configs[item].c_str(), path / (const char8_t*)newName, ec);
    configs[item] = newName;
}

void Config::reset() noexcept
{
    aimbot = { };
    triggerbot = { };
    chams = { };
    streamProofESP = { };
    visuals = { };
    style = { };
    misc = { };

    AntiAim::resetConfig();
    Backtrack::resetConfig();
    Glow::resetConfig();
    InventoryChanger::resetConfig();
    Sound::resetConfig();
}

void Config::listConfigs() noexcept
{
    configs.clear();

    std::error_code ec;
    std::transform(std::filesystem::directory_iterator{ path, ec },
        std::filesystem::directory_iterator{ },
        std::back_inserter(configs),
        [](const auto& entry) { return std::string{ (const char*)entry.path().filename().u8string().c_str() }; });
}

void Config::createConfigDir() const noexcept
{
    std::error_code ec; std::filesystem::create_directory(path, ec);
}

void Config::openConfigDir() const noexcept
{
    createConfigDir();
#ifdef _WIN32
    ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    int ret = std::system(("xdg-open " + path.string()).c_str());
#endif
}

void Config::scheduleFontLoad(const std::string& name) noexcept
{
    scheduledFonts.push_back(name);
}

#ifdef _WIN32
static auto getFontData(const std::string& fontName) noexcept
{
    HFONT font = CreateFontA(0, 0, 0, 0,
        FW_NORMAL, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        DEFAULT_PITCH, fontName.c_str());

    std::unique_ptr<std::byte[]> data;
    DWORD dataSize = GDI_ERROR;

    if (font) {
        HDC hdc = CreateCompatibleDC(nullptr);

        if (hdc) {
            SelectObject(hdc, font);
            dataSize = GetFontData(hdc, 0, 0, nullptr, 0);

            if (dataSize != GDI_ERROR) {
                data = std::make_unique<std::byte[]>(dataSize);
                dataSize = GetFontData(hdc, 0, 0, data.get(), dataSize);

                if (dataSize == GDI_ERROR)
                    data.reset();
            }
            DeleteDC(hdc);
        }
        DeleteObject(font);
    }
    return std::make_pair(std::move(data), dataSize);
}
#endif

bool Config::loadScheduledFonts() noexcept
{
    bool result = false;

    for (const auto& fontName : scheduledFonts) {
        if (fontName == "Default") {
            if (fonts.find("Default") == fonts.cend()) {
                ImFontConfig cfg;
                cfg.OversampleH = cfg.OversampleV = 1;
                cfg.PixelSnapH = true;
                cfg.RasterizerMultiply = 1.7f;

                Font newFont;

                cfg.SizePixels = 13.0f;
                newFont.big = ImGui::GetIO().Fonts->AddFontDefault(&cfg);

                cfg.SizePixels = 10.0f;
                newFont.medium = ImGui::GetIO().Fonts->AddFontDefault(&cfg);

                cfg.SizePixels = 8.0f;
                newFont.tiny = ImGui::GetIO().Fonts->AddFontDefault(&cfg);

                fonts.emplace(fontName, newFont);
                result = true;
            }
            continue;
        }

#ifdef _WIN32
        const auto [fontData, fontDataSize] = getFontData(fontName);
        if (fontDataSize == GDI_ERROR)
            continue;

        if (fonts.find(fontName) == fonts.cend()) {
            const auto ranges = Helpers::getFontGlyphRanges();
            ImFontConfig cfg;
            cfg.FontDataOwnedByAtlas = false;
            cfg.RasterizerMultiply = 1.7f;

            Font newFont;
            newFont.tiny = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(fontData.get(), fontDataSize, 8.0f, &cfg, ranges);
            newFont.medium = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(fontData.get(), fontDataSize, 10.0f, &cfg, ranges);
            newFont.big = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(fontData.get(), fontDataSize, 13.0f, &cfg, ranges);
            fonts.emplace(fontName, newFont);
            result = true;
        }
#endif
    }
    scheduledFonts.clear();
    return result;
}
