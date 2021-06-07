#include <algorithm>
#include <array>
#include <cwchar>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <ShlObj.h>
#include <Windows.h>
#endif

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_stdlib.h"

#include "imguiCustom.h"

#include "GUI.h"
#include "Config.h"
#include "ConfigStructs.h"
#include "Hacks/Misc.h"
#include "Hacks/InventoryChanger.h"
#include "Helpers.h"
#include "Hooks.h"
#include "Interfaces.h"
#include "SDK/InputSystem.h"
#include "Hacks/Visuals.h"
#include "Hacks/Glow.h"
#include "Hacks/AntiAim.h"
#include "Hacks/Backtrack.h"
#include "Hacks/Sound.h"
#include "Security/xorstr.hpp"
#include "Security/VMProtectSDK.h"
constexpr auto windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
| ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

static ImFont* addFontFromVFONT(const std::string& path, float size, const ImWchar* glyphRanges, bool merge) noexcept
{
    auto file = Helpers::loadBinaryFile(path);
    if (!Helpers::decodeVFONT(file))
        return nullptr;

    ImFontConfig cfg;
    cfg.FontData = file.data();
    cfg.FontDataSize = file.size();
    cfg.FontDataOwnedByAtlas = false;
    cfg.MergeMode = merge;
    cfg.GlyphRanges = glyphRanges;
    cfg.SizePixels = size;

    return ImGui::GetIO().Fonts->AddFont(&cfg);
}

GUI::GUI() noexcept
{
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    style.ScrollbarSize = 9.0f;

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    ImFontConfig cfg;
    cfg.SizePixels = 15.0f;

#ifdef _WIN32
    if (PWSTR pathToFonts; SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Fonts, 0, nullptr, &pathToFonts))) {
        const std::filesystem::path path{ pathToFonts };
        CoTaskMemFree(pathToFonts);

        fonts.normal15px = io.Fonts->AddFontFromFileTTF((path / "tahoma.ttf").string().c_str(), 15.0f, &cfg, Helpers::getFontGlyphRanges());
        if (!fonts.normal15px)
            io.Fonts->AddFontDefault(&cfg);

        cfg.MergeMode = true;
        static constexpr ImWchar symbol[]{
            0x2605, 0x2605, // ★
            0
        };
        io.Fonts->AddFontFromFileTTF((path / "seguisym.ttf").string().c_str(), 15.0f, &cfg, symbol);
        cfg.MergeMode = false;
    }
#else
    fonts.normal15px = addFontFromVFONT("csgo/panorama/fonts/notosans-regular.vfont", 15.0f, Helpers::getFontGlyphRanges(), false);
#endif
    if (!fonts.normal15px)
        io.Fonts->AddFontDefault(&cfg);
    addFontFromVFONT("csgo/panorama/fonts/notosanskr-regular.vfont", 15.0f, io.Fonts->GetGlyphRangesKorean(), true);
    addFontFromVFONT("csgo/panorama/fonts/notosanssc-regular.vfont", 17.0f, io.Fonts->GetGlyphRangesChineseFull(), true);
}

void GUI::render() noexcept
{
    if (!config->style.menuStyle) {
        renderMenuBar();
        renderAimbotWindow();
        AntiAim::drawGUI(false);
        renderTriggerbotWindow();
        Backtrack::drawGUI(false);
        Glow::drawGUI(false);
        renderChamsWindow();
        renderStreamProofESPWindow();
        renderVisualsWindow();
        InventoryChanger::drawGUI(false);
        Sound::drawGUI(false);
        renderStyleWindow();
        renderMiscWindow();
        renderConfigWindow();
    } else {
        renderGuiStyle2();
    }
}

void GUI::updateColors() const noexcept
{
    switch (config->style.menuColors) {
    case 0: ImGui::StyleColorsDark(); break;
    case 1: ImGui::StyleColorsLight(); break;
    case 2: ImGui::StyleColorsClassic(); break;
    }
}

#include "InputUtil.h"

static void hotkey2(const char* label, KeyBind& key, float samelineOffset = 0.0f, const ImVec2& size = { 100.0f, 0.0f }) noexcept
{
    const auto id = ImGui::GetID(label);
    ImGui::PushID(label);

    ImGui::TextUnformatted(label);
    ImGui::SameLine(samelineOffset);

    if (ImGui::GetActiveID() == id) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonActive));
        ImGui::Button(xorstr_("..."), size);
        ImGui::PopStyleColor();

        ImGui::GetCurrentContext()->ActiveIdAllowOverlap = true;
        if ((!ImGui::IsItemHovered() && ImGui::GetIO().MouseClicked[0]) || key.setToPressedKey())
            ImGui::ClearActiveID();
    } else if (ImGui::Button(key.toString(), size)) {
        ImGui::SetActiveID(id, ImGui::GetCurrentWindow());
    }

    ImGui::PopID();
}

void GUI::handleToggle() noexcept
{
    if (config->misc.menuKey.isPressed()) {
        open = !open;
        if (!open)
            interfaces->inputSystem->resetInputState();
#ifndef _WIN32
        ImGui::GetIO().MouseDrawCursor = gui->open;
#endif
    }
}

static void menuBarItem(const char* name, bool& enabled) noexcept
{
    if (ImGui::MenuItem(name)) {
        enabled = true;
        ImGui::SetWindowFocus(name);
        ImGui::SetWindowPos(name, { 100.0f, 100.0f });
    }
}

void GUI::renderMenuBar() noexcept
{
    if (ImGui::BeginMainMenuBar()) {
        menuBarItem(xorstr_("Aimbot"), window.aimbot);
        AntiAim::menuBarItem();
        menuBarItem(xorstr_("Triggerbot"), window.triggerbot);
        Backtrack::menuBarItem();
        Glow::menuBarItem();
        menuBarItem(xorstr_("Chams"), window.chams);
        menuBarItem(xorstr_("ESP"), window.streamProofESP);
        menuBarItem(xorstr_("Visuals"), window.visuals);
        InventoryChanger::menuBarItem();
        Sound::menuBarItem();
        menuBarItem(xorstr_("Style"), window.style);
        menuBarItem(xorstr_("Misc"), window.misc);
        menuBarItem(xorstr_("Config"), window.config);
        ImGui::EndMainMenuBar();   
    }
}

void GUI::renderAimbotWindow(bool contentOnly) noexcept
{
    if (!contentOnly) {
        if (!window.aimbot)
            return;
        ImGui::SetNextWindowSize({ 600.0f, 0.0f });
        ImGui::Begin(xorstr_("Aimbot"), &window.aimbot, windowFlags);
    }
    ImGui::Checkbox(xorstr_("On key"), &config->aimbotOnKey);
    ImGui::SameLine();
    ImGui::PushID(xorstr_("Aimbot Key"));
    hotkey2("", config->aimbotKey);
    ImGui::PopID();
    ImGui::SameLine();
    ImGui::PushID(2);
    ImGui::PushItemWidth(70.0f);
    ImGui::Combo("", &config->aimbotKeyMode, "Hold\0Toggle\0");
    ImGui::PopItemWidth();
    ImGui::PopID();
    ImGui::Separator();
    static int currentCategory{ 0 };
    ImGui::PushItemWidth(110.0f);
    ImGui::PushID(0);
    ImGui::Combo("", &currentCategory, xorstr_("All\0Pistols\0Heavy\0SMG\0Rifles\0"));
    ImGui::PopID();
    ImGui::SameLine();
    static int currentWeapon{ 0 };
    ImGui::PushID(1);

    switch (currentCategory) {
    case 0:
        currentWeapon = 0;
        ImGui::NewLine();
        break;
    case 1: {
        static int currentPistol{ 0 };
        static const char* pistols[]=
        { 
            _memdup(xorstr_("All")),
            _memdup(xorstr_("Glock-18")),
            _memdup(xorstr_("P2000")),
            _memdup(xorstr_("USP-S")),
            _memdup(xorstr_("Dual Berettas")),
            _memdup(xorstr_("P250")),
            _memdup(xorstr_("Tec-9")),
            _memdup(xorstr_("Five-Seven")),
            _memdup(xorstr_("CZ-75")),
            _memdup(xorstr_("Desert Eagle")),
            _memdup(xorstr_("Revolver"))
        };

        ImGui::Combo("", &currentPistol, [](void* data, int idx, const char** out_text) {
            if (config->aimbot[idx ? idx : 35].enabled) {
                static std::string name;
                name = pistols[idx];
                *out_text = name.append(" *").c_str();
            } else {
                *out_text = pistols[idx];
            }
            return true;
            }, nullptr, IM_ARRAYSIZE(pistols));

        currentWeapon = currentPistol ? currentPistol : 35;
        break;
    }
    case 2: {
        static int currentHeavy{ 0 };
        static const char* heavies[]=
        { 
            _memdup(xorstr_("All")),
            _memdup(xorstr_("Nova")),
            _memdup(xorstr_("XM1014")),
            _memdup(xorstr_("Sawed-off")),
            _memdup(xorstr_("MAG-7")),
            _memdup(xorstr_("M249")),
            _memdup(xorstr_("Negev"))
        };

        ImGui::Combo("", &currentHeavy, [](void* data, int idx, const char** out_text) {
            if (config->aimbot[idx ? idx + 10 : 36].enabled) {
                static std::string name;
                name = heavies[idx];
                *out_text = name.append(" *").c_str();
            } else {
                *out_text = heavies[idx];
            }
            return true;
            }, nullptr, IM_ARRAYSIZE(heavies));

        currentWeapon = currentHeavy ? currentHeavy + 10 : 36;
        break;
    }
    case 3: {
        static int currentSmg{ 0 };
        static const char* smgs[] = 
        { 
            _memdup(xorstr_("All")),
            _memdup(xorstr_("Mac-10")),
            _memdup(xorstr_("MP9")),
            _memdup(xorstr_("MP7")),
            _memdup(xorstr_("MP5-SD")),
            _memdup(xorstr_("UMP-45")),
            _memdup(xorstr_("P90")),
            _memdup(xorstr_("PP-Bizon"))
        };

        ImGui::Combo("", &currentSmg, [](void* data, int idx, const char** out_text) {
            if (config->aimbot[idx ? idx + 16 : 37].enabled) {
                static std::string name;
                name = smgs[idx];
                *out_text = name.append(" *").c_str();
            } else {
                *out_text = smgs[idx];
            }
            return true;
            }, nullptr, IM_ARRAYSIZE(smgs));

        currentWeapon = currentSmg ? currentSmg + 16 : 37;
        break;
    }
    case 4: {
        static int currentRifle{ 0 };
        static const char* rifles[] = 
        {
            _memdup(xorstr_("All")),
            _memdup(xorstr_("Galil AR")),
            _memdup(xorstr_("Famas")),
            _memdup(xorstr_("AK-47")),
            _memdup(xorstr_("M4A4")),
            _memdup(xorstr_("M4A1-S")),
            _memdup(xorstr_("SSG-08")),
            _memdup(xorstr_("SG-553")),
            _memdup(xorstr_("AUG")),
            _memdup(xorstr_("AWP")),
            _memdup(xorstr_("G3SG1")),
            _memdup(xorstr_("SCAR-20"))
        };

        ImGui::Combo("", &currentRifle, [](void* data, int idx, const char** out_text) {
            if (config->aimbot[idx ? idx + 23 : 38].enabled) {
                static std::string name;
                name = rifles[idx];
                *out_text = name.append(" *").c_str();
            } else {
                *out_text = rifles[idx];
            }
            return true;
            }, nullptr, IM_ARRAYSIZE(rifles));

        currentWeapon = currentRifle ? currentRifle + 23 : 38;
        break;
    }
    }
    ImGui::PopID();
    ImGui::SameLine();
    ImGui::Checkbox("Enabled", &config->aimbot[currentWeapon].enabled);
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnOffset(1, 220.0f);
    ImGui::Checkbox(xorstr_("Aimlock"), &config->aimbot[currentWeapon].aimlock);
    ImGui::Checkbox(xorstr_("Silent"), &config->aimbot[currentWeapon].silent);
    ImGui::Checkbox(xorstr_("Friendly fire"), &config->aimbot[currentWeapon].friendlyFire);
    ImGui::Checkbox(xorstr_("Visible only"), &config->aimbot[currentWeapon].visibleOnly);
    ImGui::Checkbox(xorstr_("Scoped only"), &config->aimbot[currentWeapon].scopedOnly);
    ImGui::Checkbox(xorstr_("Ignore flash"), &config->aimbot[currentWeapon].ignoreFlash);
    ImGui::Checkbox(xorstr_("Ignore smoke"), &config->aimbot[currentWeapon].ignoreSmoke);
    ImGui::Checkbox(xorstr_("Auto shot"), &config->aimbot[currentWeapon].autoShot);
    ImGui::Checkbox(xorstr_("Auto scope"), &config->aimbot[currentWeapon].autoScope);
    ImGui::Combo(xorstr_("Bone"), &config->aimbot[currentWeapon].bone, xorstr_("Nearest\0Best damage\0Head\0Neck\0Sternum\0Chest\0Stomach\0Pelvis\0"));
    ImGui::NextColumn();
    ImGui::PushItemWidth(240.0f);
    ImGui::SliderFloat(xorstr_("Fov"), &config->aimbot[currentWeapon].fov, 0.0f, 255.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat(xorstr_("Smooth"), &config->aimbot[currentWeapon].smooth, 1.0f, 100.0f, "%.2f");
    ImGui::SliderFloat(xorstr_("Max aim inaccuracy"), &config->aimbot[currentWeapon].maxAimInaccuracy, 0.0f, 1.0f, "%.5f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat(xorstr_("Max shot inaccuracy"), &config->aimbot[currentWeapon].maxShotInaccuracy, 0.0f, 1.0f, "%.5f", ImGuiSliderFlags_Logarithmic);
    ImGui::InputInt(xorstr_("Min damage"), &config->aimbot[currentWeapon].minDamage);
    config->aimbot[currentWeapon].minDamage = std::clamp(config->aimbot[currentWeapon].minDamage, 0, 250);
    ImGui::Checkbox(xorstr_("Killshot"), &config->aimbot[currentWeapon].killshot);
    ImGui::Checkbox(xorstr_("Between shots"), &config->aimbot[currentWeapon].betweenShots);
    ImGui::Columns(1);
    if (!contentOnly)
        ImGui::End();
}

void GUI::renderTriggerbotWindow(bool contentOnly) noexcept
{
    if (!contentOnly) {
        if (!window.triggerbot)
            return;
        ImGui::SetNextWindowSize({ 0.0f, 0.0f });
        ImGui::Begin(xorstr_("Triggerbot"), &window.triggerbot, windowFlags);
    }
    static int currentCategory{ 0 };
    ImGui::PushItemWidth(110.0f);
    ImGui::PushID(0);
    ImGui::Combo("", &currentCategory, xorstr_("All\0Pistols\0Heavy\0SMG\0Rifles\0Zeus x27\0"));
    ImGui::PopID();
    ImGui::SameLine();
    static int currentWeapon{ 0 };
    ImGui::PushID(1);
    switch (currentCategory) {
    case 0:
        currentWeapon = 0;
        ImGui::NewLine();
        break;
    case 5:
        currentWeapon = 39;
        ImGui::NewLine();
        break;

    case 1: {
        static int currentPistol{ 0 };
        static const char* pistols[] = 
        {
            _memdup(xorstr_("All")),
            _memdup(xorstr_("Glock-18")),
            _memdup(xorstr_("P2000")),
            _memdup(xorstr_("USP-S")),
            _memdup(xorstr_("Dual Berettas")),
            _memdup(xorstr_("P250")),
            _memdup(xorstr_("Tec-9")),
            _memdup(xorstr_("Five-Seven")),
            _memdup(xorstr_("CZ-75")),
            _memdup(xorstr_("Desert Eagle")),
            _memdup(xorstr_("Revolver"))
        };

        ImGui::Combo("", &currentPistol, [](void* data, int idx, const char** out_text) {
            if (config->triggerbot[idx ? idx : 35].enabled) {
                static std::string name;
                name = pistols[idx];
                *out_text = name.append(" *").c_str();
            } else {
                *out_text = pistols[idx];
            }
            return true;
            }, nullptr, IM_ARRAYSIZE(pistols));

        currentWeapon = currentPistol ? currentPistol : 35;
        break;
    }
    case 2: {
        static int currentHeavy{ 0 };
        static const char* heavies[] =
        {
            _memdup(xorstr_("All")),
            _memdup(xorstr_("Nova")),
            _memdup(xorstr_("XM1014")),
            _memdup(xorstr_("Sawed-off")),
            _memdup(xorstr_("MAG-7")),
            _memdup(xorstr_("M249")),
            _memdup(xorstr_("Negev"))
        };


        ImGui::Combo("", &currentHeavy, [](void* data, int idx, const char** out_text) {
            if (config->triggerbot[idx ? idx + 10 : 36].enabled) {
                static std::string name;
                name = heavies[idx];
                *out_text = name.append(" *").c_str();
            } else {
                *out_text = heavies[idx];
            }
            return true;
            }, nullptr, IM_ARRAYSIZE(heavies));

        currentWeapon = currentHeavy ? currentHeavy + 10 : 36;
        break;
    }
    case 3: {
        static int currentSmg{ 0 };
        static const char* smgs[] =
        {
            _memdup(xorstr_("All")),
            _memdup(xorstr_("Mac-10")),
            _memdup(xorstr_("MP9")),
            _memdup(xorstr_("MP7")),
            _memdup(xorstr_("MP5-SD")),
            _memdup(xorstr_("UMP-45")),
            _memdup(xorstr_("P90")),
            _memdup(xorstr_("PP-Bizon"))
        };

        ImGui::Combo("", &currentSmg, [](void* data, int idx, const char** out_text) {
            if (config->triggerbot[idx ? idx + 16 : 37].enabled) {
                static std::string name;
                name = smgs[idx];
                *out_text = name.append(" *").c_str();
            } else {
                *out_text = smgs[idx];
            }
            return true;
            }, nullptr, IM_ARRAYSIZE(smgs));

        currentWeapon = currentSmg ? currentSmg + 16 : 37;
        break;
    }
    case 4: {
        static int currentRifle{ 0 };
        static const char* rifles[] =
        {
            _memdup(xorstr_("All")),
            _memdup(xorstr_("Galil AR")),
            _memdup(xorstr_("Famas")),
            _memdup(xorstr_("AK-47")),
            _memdup(xorstr_("M4A4")),
            _memdup(xorstr_("M4A1-S")),
            _memdup(xorstr_("SSG-08")),
            _memdup(xorstr_("SG-553")),
            _memdup(xorstr_("AUG")),
            _memdup(xorstr_("AWP")),
            _memdup(xorstr_("G3SG1")),
            _memdup(xorstr_("SCAR-20"))
        };

        ImGui::Combo("", &currentRifle, [](void* data, int idx, const char** out_text) {
            if (config->triggerbot[idx ? idx + 23 : 38].enabled) {
                static std::string name;
                name = rifles[idx];
                *out_text = name.append(" *").c_str();
            } else {
                *out_text = rifles[idx];
            }
            return true;
            }, nullptr, IM_ARRAYSIZE(rifles));

        currentWeapon = currentRifle ? currentRifle + 23 : 38;
        break;
    }
    }
    ImGui::PopID();
    ImGui::SameLine();
    ImGui::Checkbox(xorstr_("Enabled"), &config->triggerbot[currentWeapon].enabled);
    ImGui::Separator();
    hotkey2(xorstr_("Hold Key"), config->triggerbotHoldKey);
    ImGui::Checkbox(xorstr_("Friendly fire"), &config->triggerbot[currentWeapon].friendlyFire);
    ImGui::Checkbox(xorstr_("Scoped only"), &config->triggerbot[currentWeapon].scopedOnly);
    ImGui::Checkbox(xorstr_("Ignore flash"), &config->triggerbot[currentWeapon].ignoreFlash);
    ImGui::Checkbox(xorstr_("Ignore smoke"), &config->triggerbot[currentWeapon].ignoreSmoke);
    ImGui::SetNextItemWidth(85.0f);
    ImGui::Combo(xorstr_("Hitgroup"), &config->triggerbot[currentWeapon].hitgroup, "All\0Head\0Chest\0Stomach\0Left arm\0Right arm\0Left leg\0Right leg\0");
    ImGui::PushItemWidth(220.0f);
    ImGui::SliderInt(xorstr_("Shot delay"), &config->triggerbot[currentWeapon].shotDelay, 0, 250, "%d ms");
    ImGui::InputInt(xorstr_("Min damage"), &config->triggerbot[currentWeapon].minDamage);
    config->triggerbot[currentWeapon].minDamage = std::clamp(config->triggerbot[currentWeapon].minDamage, 0, 250);
    ImGui::Checkbox(xorstr_("Killshot"), &config->triggerbot[currentWeapon].killshot);
    ImGui::SliderFloat(xorstr_("Burst Time"), &config->triggerbot[currentWeapon].burstTime, 0.0f, 0.5f, "%.3f s");

    if (!contentOnly)
        ImGui::End();
}

void GUI::renderChamsWindow(bool contentOnly) noexcept
{
    if (!contentOnly) {
        if (!window.chams)
            return;
        ImGui::SetNextWindowSize({ 0.0f, 0.0f });
        ImGui::Begin(xorstr_("Chams"), &window.chams, windowFlags);
    }

    hotkey2(xorstr_("Toggle Key"), config->chamsToggleKey, 80.0f);
    hotkey2(xorstr_("Hold Key"), config->chamsHoldKey, 80.0f);
    ImGui::Separator();

    static int currentCategory{ 0 };
    ImGui::PushItemWidth(110.0f);
    ImGui::PushID(0);

    static int material = 1;

    if (ImGui::Combo("", &currentCategory, "Allies\0Enemies\0Planting\0Defusing\0Local player\0Weapons\0Hands\0Backtrack\0Sleeves\0"))
        material = 1;

    ImGui::PopID();

    ImGui::SameLine();

    if (material <= 1)
        ImGuiCustom::arrowButtonDisabled("##left", ImGuiDir_Left);
    else if (ImGui::ArrowButton("##left", ImGuiDir_Left))
        --material;

    ImGui::SameLine();
    ImGui::Text("%d", material);

    std::array categories = { xorstr_("Allies"), xorstr_("Enemies"), xorstr_("Planting"), xorstr_("Defusing"), xorstr_("Local player"), xorstr_("Weapons"), xorstr_("Hands"), xorstr_("Backtrack"), xorstr_("Sleeves") };

    ImGui::SameLine();

    if (material >= int(config->chams[categories[currentCategory]].materials.size()))
        ImGuiCustom::arrowButtonDisabled("##right", ImGuiDir_Right);
    else if (ImGui::ArrowButton("##right", ImGuiDir_Right))
        ++material;

    ImGui::SameLine();

    auto& chams{ config->chams[categories[currentCategory]].materials[material - 1] };

    ImGui::Checkbox(xorstr_("Enabled"), &chams.enabled);
    ImGui::Separator();
    ImGui::Checkbox(xorstr_("Health based"), &chams.healthBased);
    ImGui::Checkbox(xorstr_("Blinking"), &chams.blinking);
    ImGui::Combo(xorstr_("Material"), &chams.material, xorstr_("Normal\0Flat\0Animated\0Platinum\0Glass\0Chrome\0Crystal\0Silver\0Gold\0Plastic\0Glow\0Pearlescent\0Metallic\0"));
    ImGui::Checkbox(xorstr_("Wireframe"), &chams.wireframe);
    ImGui::Checkbox(xorstr_("Cover"), &chams.cover);
    ImGui::Checkbox(xorstr_("Ignore-Z"), &chams.ignorez);
    ImGuiCustom::colorPicker(xorstr_("Color"), chams);

    if (!contentOnly) {
        ImGui::End();
    }
}

void GUI::renderStreamProofESPWindow(bool contentOnly) noexcept
{
    if (!contentOnly) {
        if (!window.streamProofESP)
            return;
        ImGui::SetNextWindowSize({ 0.0f, 0.0f });
        ImGui::Begin(xorstr_("ESP"), &window.streamProofESP, windowFlags);
    }

    hotkey2(xorstr_("Toggle Key"), config->streamProofESP.toggleKey, 80.0f);
    hotkey2(xorstr_("Hold Key"), config->streamProofESP.holdKey, 80.0f);
    ImGui::Separator();

    static std::size_t currentCategory;
    static auto currentItem = "All";

    constexpr auto getConfigShared = [](std::size_t category, const char* item) noexcept -> Shared& {
        switch (category) {
        case 0: default: return config->streamProofESP.enemies[item];
        case 1: return config->streamProofESP.allies[item];
        case 2: return config->streamProofESP.weapons[item];
        case 3: return config->streamProofESP.projectiles[item];
        case 4: return config->streamProofESP.lootCrates[item];
        case 5: return config->streamProofESP.otherEntities[item];
        }
    };

    constexpr auto getConfigPlayer = [](std::size_t category, const char* item) noexcept -> Player& {
        switch (category) {
        case 0: default: return config->streamProofESP.enemies[item];
        case 1: return config->streamProofESP.allies[item];
        }
    };

    if (ImGui::BeginListBox("##list", { 170.0f, 300.0f })) {
        constexpr std::array categories{ "Enemies", "Allies", "Weapons", "Projectiles", "Loot Crates", "Other Entities" };

        for (std::size_t i = 0; i < categories.size(); ++i) {
            if (ImGui::Selectable(categories[i], currentCategory == i && std::string_view{ currentItem } == "All")) {
                currentCategory = i;
                currentItem = "All";
            }

            if (ImGui::BeginDragDropSource()) {
                switch (i) {
                case 0: case 1: ImGui::SetDragDropPayload(xorstr_("Player"), &getConfigPlayer(i, "All"), sizeof(Player), ImGuiCond_Once); break;
                case 2: ImGui::SetDragDropPayload(xorstr_("Weapon"), &config->streamProofESP.weapons["All"], sizeof(Weapon), ImGuiCond_Once); break;
                case 3: ImGui::SetDragDropPayload(xorstr_("Projectile"), &config->streamProofESP.projectiles["All"], sizeof(Projectile), ImGuiCond_Once); break;
                default: ImGui::SetDragDropPayload(xorstr_("Entity"), &getConfigShared(i, "All"), sizeof(Shared), ImGuiCond_Once); break;
                }
                ImGui::EndDragDropSource();
            }

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(xorstr_("Player"))) {
                    const auto& data = *(Player*)payload->Data;

                    switch (i) {
                    case 0: case 1: getConfigPlayer(i, "All") = data; break;
                    case 2: config->streamProofESP.weapons["All"] = data; break;
                    case 3: config->streamProofESP.projectiles["All"] = data; break;
                    default: getConfigShared(i, "All") = data; break;
                    }
                }

                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(xorstr_("Weapon"))) {
                    const auto& data = *(Weapon*)payload->Data;

                    switch (i) {
                    case 0: case 1: getConfigPlayer(i, "All") = data; break;
                    case 2: config->streamProofESP.weapons["All"] = data; break;
                    case 3: config->streamProofESP.projectiles["All"] = data; break;
                    default: getConfigShared(i, "All") = data; break;
                    }
                }

                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(xorstr_("Projectile"))) {
                    const auto& data = *(Projectile*)payload->Data;

                    switch (i) {
                    case 0: case 1: getConfigPlayer(i, "All") = data; break;
                    case 2: config->streamProofESP.weapons["All"] = data; break;
                    case 3: config->streamProofESP.projectiles["All"] = data; break;
                    default: getConfigShared(i, "All") = data; break;
                    }
                }

                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(xorstr_("Entity"))) {
                    const auto& data = *(Shared*)payload->Data;

                    switch (i) {
                    case 0: case 1: getConfigPlayer(i, "All") = data; break;
                    case 2: config->streamProofESP.weapons["All"] = data; break;
                    case 3: config->streamProofESP.projectiles["All"] = data; break;
                    default: getConfigShared(i, "All") = data; break;
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::PushID(i);
            ImGui::Indent();

            const auto items = [](std::size_t category) noexcept -> std::vector<const char*> {
                switch (category) {
                case 0:
                case 1: return { xorstr_("Visible"), xorstr_("Occluded") };
                case 2: return { xorstr_("Pistols"), xorstr_("SMGs"), xorstr_("Rifles"), xorstr_("Sniper Rifles"), xorstr_("Shotguns"), xorstr_("Machineguns"), xorstr_("Grenades"), xorstr_("Melee"), xorstr_("Other") };
                case 3: return { xorstr_("Flashbang"), xorstr_("HE Grenade"), xorstr_("Breach Charge"), xorstr_("Bump Mine"), xorstr_("Decoy Grenade"), xorstr_("Molotov"), xorstr_("TA Grenade"), xorstr_("Smoke Grenade"), xorstr_("Snowball") };
                case 4: return { xorstr_("Pistol Case"), xorstr_("Light Case"), xorstr_("Heavy Case"), xorstr_("Explosive Case"), xorstr_("Tools Case"), xorstr_("Cash Dufflebag") };
                case 5: return { xorstr_("Defuse Kit"), xorstr_("Chicken"), xorstr_("Planted C4"), xorstr_("Hostage"), xorstr_("Sentry"), xorstr_("Cash"), xorstr_("Ammo Box"), xorstr_("Radar Jammer"), xorstr_("Snowball Pile"), xorstr_("Collectable Coin") };
                default: return { };
                }
            }(i);

            const auto categoryEnabled = getConfigShared(i, "All").enabled;

            for (std::size_t j = 0; j < items.size(); ++j) {
                static bool selectedSubItem;
                if (!categoryEnabled || getConfigShared(i, items[j]).enabled) {
                    if (ImGui::Selectable(items[j], currentCategory == i && !selectedSubItem && std::string_view{ currentItem } == items[j])) {
                        currentCategory = i;
                        currentItem = items[j];
                        selectedSubItem = false;
                    }

                    if (ImGui::BeginDragDropSource()) {
                        switch (i) {
                        case 0: case 1: ImGui::SetDragDropPayload(xorstr_("Player"), &getConfigPlayer(i, items[j]), sizeof(Player), ImGuiCond_Once); break;
                        case 2: ImGui::SetDragDropPayload(xorstr_("Weapon"), &config->streamProofESP.weapons[items[j]], sizeof(Weapon), ImGuiCond_Once); break;
                        case 3: ImGui::SetDragDropPayload(xorstr_("Projectile"), &config->streamProofESP.projectiles[items[j]], sizeof(Projectile), ImGuiCond_Once); break;
                        default: ImGui::SetDragDropPayload(xorstr_("Entity"), &getConfigShared(i, items[j]), sizeof(Shared), ImGuiCond_Once); break;
                        }
                        ImGui::EndDragDropSource();
                    }

                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(xorstr_("Player"))) {
                            const auto& data = *(Player*)payload->Data;

                            switch (i) {
                            case 0: case 1: getConfigPlayer(i, items[j]) = data; break;
                            case 2: config->streamProofESP.weapons[items[j]] = data; break;
                            case 3: config->streamProofESP.projectiles[items[j]] = data; break;
                            default: getConfigShared(i, items[j]) = data; break;
                            }
                        }

                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(xorstr_("Weapon"))) {
                            const auto& data = *(Weapon*)payload->Data;

                            switch (i) {
                            case 0: case 1: getConfigPlayer(i, items[j]) = data; break;
                            case 2: config->streamProofESP.weapons[items[j]] = data; break;
                            case 3: config->streamProofESP.projectiles[items[j]] = data; break;
                            default: getConfigShared(i, items[j]) = data; break;
                            }
                        }

                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(xorstr_("Projectile"))) {
                            const auto& data = *(Projectile*)payload->Data;

                            switch (i) {
                            case 0: case 1: getConfigPlayer(i, items[j]) = data; break;
                            case 2: config->streamProofESP.weapons[items[j]] = data; break;
                            case 3: config->streamProofESP.projectiles[items[j]] = data; break;
                            default: getConfigShared(i, items[j]) = data; break;
                            }
                        }

                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(xorstr_("Entity"))) {
                            const auto& data = *(Shared*)payload->Data;

                            switch (i) {
                            case 0: case 1: getConfigPlayer(i, items[j]) = data; break;
                            case 2: config->streamProofESP.weapons[items[j]] = data; break;
                            case 3: config->streamProofESP.projectiles[items[j]] = data; break;
                            default: getConfigShared(i, items[j]) = data; break;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                }

                if (i != 2)
                    continue;

                ImGui::Indent();

                const auto subItems = [](std::size_t item) noexcept -> std::vector<const char*> {
                    switch (item) {
                    case 0: return { xorstr_("Glock-18"), xorstr_("P2000"), xorstr_("USP-S"), xorstr_("Dual Berettas"), xorstr_("P250"), xorstr_("Tec-9"), xorstr_("Five-SeveN"), xorstr_("CZ75-Auto"), xorstr_("Desert Eagle"), xorstr_("R8 Revolver") };
                    case 1: return { xorstr_("MAC-10"), xorstr_("MP9"), xorstr_("MP7"), xorstr_("MP5-SD"), xorstr_("UMP-45"), xorstr_("P90"), xorstr_("PP-Bizon") };
                    case 2: return { xorstr_("Galil AR"), xorstr_("FAMAS"), xorstr_("AK-47"), xorstr_("M4A4"), xorstr_("M4A1-S"), xorstr_("SG 553"), xorstr_("AUG") };
                    case 3: return { xorstr_("SSG 08"), xorstr_("AWP"), xorstr_("G3SG1"), xorstr_("SCAR-20") };
                    case 4: return { xorstr_("Nova"), xorstr_("XM1014"), xorstr_("Sawed-Off"), xorstr_("MAG-7") };
                    case 5: return { xorstr_("M249"), xorstr_("Negev") };
                    case 6: return { xorstr_("Flashbang"), xorstr_("HE Grenade"), xorstr_("Smoke Grenade"), xorstr_("Molotov"), xorstr_("Decoy Grenade"), xorstr_("Incendiary"), xorstr_("TA Grenade"), xorstr_("Fire Bomb"), xorstr_("Diversion"), xorstr_("Frag Grenade"), xorstr_("Snowball") };
                    case 7: return { xorstr_("Axe"), xorstr_("Hammer"), xorstr_("Wrench") };
                    case 8: return { xorstr_("C4"), xorstr_("Healthshot"), xorstr_("Bump Mine"), xorstr_("Zone Repulsor"), xorstr_("Shield") };
                    default: return { };
                    }
                }(j);

                const auto itemEnabled = getConfigShared(i, items[j]).enabled;

                for (const auto subItem : subItems) {
                    auto& subItemConfig = config->streamProofESP.weapons[subItem];
                    if ((categoryEnabled || itemEnabled) && !subItemConfig.enabled)
                        continue;

                    if (ImGui::Selectable(subItem, currentCategory == i && selectedSubItem && std::string_view{ currentItem } == subItem)) {
                        currentCategory = i;
                        currentItem = subItem;
                        selectedSubItem = true;
                    }

                    if (ImGui::BeginDragDropSource()) {
                        ImGui::SetDragDropPayload("Weapon", &subItemConfig, sizeof(Weapon), ImGuiCond_Once);
                        ImGui::EndDragDropSource();
                    }

                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(xorstr_("Player"))) {
                            const auto& data = *(Player*)payload->Data;
                            subItemConfig = data;
                        }

                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(xorstr_("Weapon"))) {
                            const auto& data = *(Weapon*)payload->Data;
                            subItemConfig = data;
                        }

                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(xorstr_("Projectile"))) {
                            const auto& data = *(Projectile*)payload->Data;
                            subItemConfig = data;
                        }

                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(xorstr_("Entity"))) {
                            const auto& data = *(Shared*)payload->Data;
                            subItemConfig = data;
                        }
                        ImGui::EndDragDropTarget();
                    }
                }

                ImGui::Unindent();
            }
            ImGui::Unindent();
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }

    ImGui::SameLine();

    if (ImGui::BeginChild("##child", { 400.0f, 0.0f })) {
        auto& sharedConfig = getConfigShared(currentCategory, currentItem);

        ImGui::Checkbox("Enabled", &sharedConfig.enabled);
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 260.0f);
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::BeginCombo("Font", config->getSystemFonts()[sharedConfig.font.index].c_str())) {
            for (size_t i = 0; i < config->getSystemFonts().size(); i++) {
                bool isSelected = config->getSystemFonts()[i] == sharedConfig.font.name;
                if (ImGui::Selectable(config->getSystemFonts()[i].c_str(), isSelected, 0, { 250.0f, 0.0f })) {
                    sharedConfig.font.index = i;
                    sharedConfig.font.name = config->getSystemFonts()[i];
                    config->scheduleFontLoad(sharedConfig.font.name);
                }
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::Separator();

        constexpr auto spacing = 250.0f;
        ImGuiCustom::colorPicker(xorstr_("Snapline"), sharedConfig.snapline);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        ImGui::Combo("##1", &sharedConfig.snapline.type, xorstr_("Bottom\0Top\0Crosshair\0"));
        ImGui::SameLine(spacing);
        ImGuiCustom::colorPicker("Box", sharedConfig.box);
        ImGui::SameLine();

        ImGui::PushID("Box");

        if (ImGui::Button("..."))
            ImGui::OpenPopup("");

        if (ImGui::BeginPopup("")) {
            ImGui::SetNextItemWidth(95.0f);
            ImGui::Combo("Type", &sharedConfig.box.type, "2D\0" "2D corners\0" "3D\0" "3D corners\0");
            ImGui::SetNextItemWidth(275.0f);
            ImGui::SliderFloat3("Scale", sharedConfig.box.scale.data(), 0.0f, 0.50f, "%.2f");
            ImGuiCustom::colorPicker("Fill", sharedConfig.box.fill);
            ImGui::EndPopup();
        }

        ImGui::PopID();

        ImGuiCustom::colorPicker(xorstr_("Name"), sharedConfig.name);
        ImGui::SameLine(spacing);

        if (currentCategory < 2) {
            auto& playerConfig = getConfigPlayer(currentCategory, currentItem);

            ImGuiCustom::colorPicker(xorstr_("Weapon"), playerConfig.weapon);
            ImGuiCustom::colorPicker(xorstr_("Flash Duration"), playerConfig.flashDuration);
            ImGui::SameLine(spacing);
            ImGuiCustom::colorPicker(xorstr_("Skeleton"), playerConfig.skeleton);
            ImGui::Checkbox(xorstr_("Audible Only"), &playerConfig.audibleOnly);
            ImGui::SameLine(spacing);
            ImGui::Checkbox(xorstr_("Spotted Only"), &playerConfig.spottedOnly);

            ImGuiCustom::colorPicker(xorstr_("Head Box"), playerConfig.headBox);
            ImGui::SameLine();

            ImGui::PushID(xorstr_("Head Box"));

            if (ImGui::Button("..."))
                ImGui::OpenPopup("");

            if (ImGui::BeginPopup("")) {
                ImGui::SetNextItemWidth(95.0f);
                ImGui::Combo("Type", &playerConfig.headBox.type, "2D\0" "2D corners\0" "3D\0" "3D corners\0");
                ImGui::SetNextItemWidth(275.0f);
                ImGui::SliderFloat3("Scale", playerConfig.headBox.scale.data(), 0.0f, 0.50f, "%.2f");
                ImGuiCustom::colorPicker("Fill", playerConfig.headBox.fill);
                ImGui::EndPopup();
            }

            ImGui::PopID();
        
            ImGui::SameLine(spacing);
            ImGui::Checkbox(xorstr_("Health Bar"), &playerConfig.healthBar.enabled);
            ImGui::SameLine();

            ImGui::PushID(xorstr_("Health Bar"));

            if (ImGui::Button(xorstr_("...")))
                ImGui::OpenPopup("");

            if (ImGui::BeginPopup("")) {
                ImGui::SetNextItemWidth(95.0f);
                ImGui::Combo("Type", &playerConfig.healthBar.type, xorstr_("Gradient\0Solid\0Health-based\0"));
                if (playerConfig.healthBar.type == HealthBar::Solid) {
                    ImGui::SameLine();
                    ImGuiCustom::colorPicker("", static_cast<Color4&>(playerConfig.healthBar));
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
        } else if (currentCategory == 2) {
            auto& weaponConfig = config->streamProofESP.weapons[currentItem];
            ImGuiCustom::colorPicker(xorstr_("Ammo"), weaponConfig.ammo);
        } else if (currentCategory == 3) {
            auto& trails = config->streamProofESP.projectiles[currentItem].trails;

            ImGui::Checkbox(xorstr_("Trails"), &trails.enabled);
            ImGui::SameLine(spacing + 77.0f);
            ImGui::PushID(xorstr_("Trails"));

            if (ImGui::Button(xorstr_("...")))
                ImGui::OpenPopup("");

            if (ImGui::BeginPopup("")) {
                constexpr auto trailPicker = [](const char* name, Trail& trail) noexcept {
                    ImGui::PushID(name);
                    ImGuiCustom::colorPicker(name, trail);
                    ImGui::SameLine(150.0f);
                    ImGui::SetNextItemWidth(95.0f);
                    ImGui::Combo("", &trail.type, xorstr_("Line\0Circles\0Filled Circles\0"));
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(95.0f);
                    ImGui::InputFloat(xorstr_("Time"), &trail.time, 0.1f, 0.5f, "%.1fs");
                    trail.time = std::clamp(trail.time, 1.0f, 60.0f);
                    ImGui::PopID();
                };

                trailPicker(xorstr_("Local Player"), trails.localPlayer);
                trailPicker(xorstr_("Allies"), trails.allies);
                trailPicker(xorstr_("Enemies"), trails.enemies);
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }

        ImGui::SetNextItemWidth(95.0f);
        ImGui::InputFloat("Text Cull Distance", &sharedConfig.textCullDistance, 0.4f, 0.8f, "%.1fm");
        sharedConfig.textCullDistance = std::clamp(sharedConfig.textCullDistance, 0.0f, 999.9f);
    }

    ImGui::EndChild();

    if (!contentOnly)
        ImGui::End();
}

void GUI::renderVisualsWindow(bool contentOnly) noexcept
{
    if (!contentOnly) {
        if (!window.visuals)
            return;
        ImGui::SetNextWindowSize({ 680.0f, 0.0f });
        ImGui::Begin(xorstr_("Visuals"), &window.visuals, windowFlags);
    }
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnOffset(1, 280.0f);
    auto playerModels = xorstr_("Default\0Special Agent Ava | FBI\0Operator | FBI SWAT\0Markus Delrow | FBI HRT\0Michael Syfers | FBI Sniper\0B Squadron Officer | SAS\0Seal Team 6 Soldier | NSWC SEAL\0Buckshot | NSWC SEAL\0Lt. Commander Ricksaw | NSWC SEAL\0Third Commando Company | KSK\0'Two Times' McCoy | USAF TACP\0Dragomir | Sabre\0Rezan The Ready | Sabre\0'The Doctor' Romanov | Sabre\0Maximus | Sabre\0Blackwolf | Sabre\0The Elite Mr. Muhlik | Elite Crew\0Ground Rebel | Elite Crew\0Osiris | Elite Crew\0Prof. Shahmat | Elite Crew\0Enforcer | Phoenix\0Slingshot | Phoenix\0Soldier | Phoenix\0Pirate\0Pirate Variant A\0Pirate Variant B\0Pirate Variant C\0Pirate Variant D\0Anarchist\0Anarchist Variant A\0Anarchist Variant B\0Anarchist Variant C\0Anarchist Variant D\0Balkan Variant A\0Balkan Variant B\0Balkan Variant C\0Balkan Variant D\0Balkan Variant E\0Jumpsuit Variant A\0Jumpsuit Variant B\0Jumpsuit Variant C\0Street Soldier | Phoenix\0'Blueberries' Buckshot | NSWC SEAL\0'Two Times' McCoy | TACP Cavalry\0Rezan the Redshirt | Sabre\0Dragomir | Sabre Footsoldier\0Cmdr. Mae 'Dead Cold' Jamison | SWAT\0001st Lieutenant Farlow | SWAT\0John 'Van Healen' Kask | SWAT\0Bio-Haz Specialist | SWAT\0Sergeant Bombson | SWAT\0Chem-Haz Specialist | SWAT\0Sir Bloody Miami Darryl | The Professionals\0Sir Bloody Silent Darryl | The Professionals\0Sir Bloody Skullhead Darryl | The Professionals\0Sir Bloody Darryl Royale | The Professionals\0Sir Bloody Loudmouth Darryl | The Professionals\0Safecracker Voltzmann | The Professionals\0Little Kev | The Professionals\0Number K | The Professionals\0Getaway Sally | The Professionals\0");
    ImGui::Combo(xorstr_("T Player Model"), &config->visuals.playerModelT, playerModels);
    ImGui::Combo(xorstr_("CT Player Model"), &config->visuals.playerModelCT, playerModels);
    ImGui::Checkbox(xorstr_("Disable post-processing"), &config->visuals.disablePostProcessing);
    ImGui::Checkbox(xorstr_("Inverse ragdoll gravity"), &config->visuals.inverseRagdollGravity);
    ImGui::Checkbox(xorstr_("No fog"), &config->visuals.noFog);
    ImGui::Checkbox(xorstr_("No 3d sky"), &config->visuals.no3dSky);
    ImGui::Checkbox(xorstr_("No aim punch"), &config->visuals.noAimPunch);
    ImGui::Checkbox(xorstr_("No view punch"), &config->visuals.noViewPunch);
    ImGui::Checkbox(xorstr_("No hands"), &config->visuals.noHands);
    ImGui::Checkbox(xorstr_("No sleeves"), &config->visuals.noSleeves);
    ImGui::Checkbox(xorstr_("No weapons"), &config->visuals.noWeapons);
    ImGui::Checkbox(xorstr_("No smoke"), &config->visuals.noSmoke);
    ImGui::Checkbox(xorstr_("No blur"), &config->visuals.noBlur);
    ImGui::Checkbox(xorstr_("No scope overlay"), &config->visuals.noScopeOverlay);
    ImGui::Checkbox(xorstr_("No grass"), &config->visuals.noGrass);
    ImGui::Checkbox(xorstr_("No shadows"), &config->visuals.noShadows);
    ImGui::Checkbox(xorstr_("Wireframe smoke"), &config->visuals.wireframeSmoke);
    ImGui::NextColumn();
    ImGui::Checkbox(xorstr_("Zoom"), &config->visuals.zoom);
    ImGui::SameLine();
    ImGui::PushID(xorstr_("Zoom Key"));
    hotkey2("", config->visuals.zoomKey);
    ImGui::PopID();
    ImGui::Checkbox(xorstr_("Thirdperson"), &config->visuals.thirdperson);
    ImGui::SameLine();
    ImGui::PushID(xorstr_("Thirdperson Key"));
    hotkey2("", config->visuals.thirdpersonKey);
    ImGui::PopID();
    ImGui::PushItemWidth(290.0f);
    ImGui::PushID(0);
    ImGui::SliderInt("", &config->visuals.thirdpersonDistance, 0, 1000, xorstr_("Thirdperson distance: %d"));
    ImGui::PopID();
    ImGui::PushID(1);
    ImGui::SliderInt("", &config->visuals.viewmodelFov, -60, 60, xorstr_("Viewmodel FOV: %d"));
    ImGui::PopID();
    ImGui::PushID(2);
    ImGui::SliderInt("", &config->visuals.fov, -60, 60, xorstr_("FOV: %d"));
    ImGui::PopID();
    ImGui::PushID(3);
    ImGui::SliderInt("", &config->visuals.farZ, 0, 2000, xorstr_("Far Z: %d"));
    ImGui::PopID();
    ImGui::PushID(4);
    ImGui::SliderInt("", &config->visuals.flashReduction, 0, 100, xorstr_("Flash reduction: %d%%"));
    ImGui::PopID();
    ImGui::PushID(5);
    ImGui::SliderFloat("", &config->visuals.brightness, 0.0f, 1.0f, xorstr_("Brightness: %.2f"));
    ImGui::PopID();
    ImGui::PopItemWidth();
    ImGui::Combo(xorstr_("Skybox"), &config->visuals.skybox, Visuals::skyboxList.data(), Visuals::skyboxList.size());
    ImGuiCustom::colorPicker(xorstr_("World color"), config->visuals.world);
    ImGuiCustom::colorPicker(xorstr_("Sky color"), config->visuals.sky);
    ImGui::Checkbox(xorstr_("Deagle spinner"), &config->visuals.deagleSpinner);
    ImGui::Combo(xorstr_("Screen effect"), &config->visuals.screenEffect, xorstr_("None\0Drone cam\0Drone cam with noise\0Underwater\0Healthboost\0Dangerzone\0"));
    ImGui::Combo(xorstr_("Hit effect"), &config->visuals.hitEffect, xorstr_("None\0Drone cam\0Drone cam with noise\0Underwater\0Healthboost\0Dangerzone\0"));
    ImGui::SliderFloat(xorstr_("Hit effect time"), &config->visuals.hitEffectTime, 0.1f, 1.5f, "%.2fs");
    ImGui::Combo(xorstr_("Hit marker"), &config->visuals.hitMarker, xorstr_("None\0Default (Cross)\0"));
    ImGui::SliderFloat(xorstr_("Hit marker time"), &config->visuals.hitMarkerTime, 0.1f, 1.5f, "%.2fs");
    ImGuiCustom::colorPicker(xorstr_("Bullet Tracers"), config->visuals.bulletTracers.color.data(), &config->visuals.bulletTracers.color[3], nullptr, nullptr, &config->visuals.bulletTracers.enabled);
    ImGuiCustom::colorPicker(xorstr_("Molotov Hull"), config->visuals.molotovHull);

    ImGui::Checkbox(xorstr_("Color correction"), &config->visuals.colorCorrection.enabled);
    ImGui::SameLine();
    bool ccPopup = ImGui::Button("Edit");

    if (ccPopup)
        ImGui::OpenPopup("##popup");

    if (ImGui::BeginPopup("##popup")) {
        ImGui::VSliderFloat("##1", { 40.0f, 160.0f }, &config->visuals.colorCorrection.blue, 0.0f, 1.0f, "Blue\n%.3f"); ImGui::SameLine();
        ImGui::VSliderFloat("##2", { 40.0f, 160.0f }, &config->visuals.colorCorrection.red, 0.0f, 1.0f, "Red\n%.3f"); ImGui::SameLine();
        ImGui::VSliderFloat("##3", { 40.0f, 160.0f }, &config->visuals.colorCorrection.mono, 0.0f, 1.0f, "Mono\n%.3f"); ImGui::SameLine();
        ImGui::VSliderFloat("##4", { 40.0f, 160.0f }, &config->visuals.colorCorrection.saturation, 0.0f, 1.0f, "Sat\n%.3f"); ImGui::SameLine();
        ImGui::VSliderFloat("##5", { 40.0f, 160.0f }, &config->visuals.colorCorrection.ghost, 0.0f, 1.0f, "Ghost\n%.3f"); ImGui::SameLine();
        ImGui::VSliderFloat("##6", { 40.0f, 160.0f }, &config->visuals.colorCorrection.green, 0.0f, 1.0f, "Green\n%.3f"); ImGui::SameLine();
        ImGui::VSliderFloat("##7", { 40.0f, 160.0f }, &config->visuals.colorCorrection.yellow, 0.0f, 1.0f, "Yellow\n%.3f"); ImGui::SameLine();
        ImGui::EndPopup();
    }
    ImGui::Columns(1);

    if (!contentOnly)
        ImGui::End();
}

void GUI::renderStyleWindow(bool contentOnly) noexcept
{
    if (!contentOnly) {
        if (!window.style)
            return;
        ImGui::SetNextWindowSize({ 0.0f, 0.0f });
        ImGui::Begin("Style", &window.style, windowFlags);
    }

    ImGui::PushItemWidth(150.0f);
    if (ImGui::Combo("Menu style", &config->style.menuStyle, "Classic\0One window\0"))
        window = { };
    if (ImGui::Combo("Menu colors", &config->style.menuColors, "Dark\0Light\0Classic\0Custom\0"))
        updateColors();
    ImGui::PopItemWidth();

    if (config->style.menuColors == 3) {
        ImGuiStyle& style = ImGui::GetStyle();
        for (int i = 0; i < ImGuiCol_COUNT; i++) {
            if (i && i & 3) ImGui::SameLine(220.0f * (i & 3));

            ImGuiCustom::colorPicker(ImGui::GetStyleColorName(i), (float*)&style.Colors[i], &style.Colors[i].w);
        }
    }

    if (!contentOnly)
        ImGui::End();
}

void GUI::renderMiscWindow(bool contentOnly) noexcept
{
    if (!contentOnly) {
        if (!window.misc)
            return;
        ImGui::SetNextWindowSize({ 580.0f, 0.0f });
        ImGui::Begin(xorstr_("Misc"), &window.misc, windowFlags);
    }
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnOffset(1, 230.0f);
    hotkey2(xorstr_("Menu Key"), config->misc.menuKey);
    ImGui::Checkbox(xorstr_("Anti AFK kick"), &config->misc.antiAfkKick);
    ImGui::Checkbox(xorstr_("Auto strafe"), &config->misc.autoStrafe);
    ImGui::Checkbox(xorstr_("Bunny hop"), &config->misc.bunnyHop);
    ImGui::Checkbox(xorstr_("Fast duck"), &config->misc.fastDuck);
    ImGui::Checkbox(xorstr_("Moonwalk"), &config->misc.moonwalk);
    ImGui::Checkbox(xorstr_("Edge Jump"), &config->misc.edgejump);
    ImGui::SameLine();
    ImGui::PushID(xorstr_("Edge Jump Key"));
    hotkey2("", config->misc.edgejumpkey);
    ImGui::PopID();
    ImGui::Checkbox(xorstr_("Slowwalk"), &config->misc.slowwalk);
    ImGui::SameLine();
    ImGui::PushID(xorstr_("Slowwalk Key"));
    hotkey2("", config->misc.slowwalkKey);
    ImGui::PopID();
    ImGuiCustom::colorPicker(xorstr_("Noscope crosshair"), config->misc.noscopeCrosshair);
    ImGuiCustom::colorPicker(xorstr_("Recoil crosshair"), config->misc.recoilCrosshair);
    ImGui::Checkbox(xorstr_("Auto pistol"), &config->misc.autoPistol);
    ImGui::Checkbox(xorstr_("Auto reload"), &config->misc.autoReload);
    ImGui::Checkbox(xorstr_("Auto accept"), &config->misc.autoAccept);
    ImGui::Checkbox(xorstr_("Radar hack"), &config->misc.radarHack);
    ImGui::Checkbox(xorstr_("Reveal ranks"), &config->misc.revealRanks);
    ImGui::Checkbox(xorstr_("Reveal money"), &config->misc.revealMoney);
    ImGui::Checkbox(xorstr_("Reveal suspect"), &config->misc.revealSuspect);
    ImGui::Checkbox(xorstr_("Reveal votes"), &config->misc.revealVotes);

    ImGui::Checkbox(xorstr_("Spectator list"), &config->misc.spectatorList.enabled);
    ImGui::SameLine();

    ImGui::PushID(xorstr_("Spectator list"));
    if (ImGui::Button(xorstr_("...")))
        ImGui::OpenPopup("");

    if (ImGui::BeginPopup("")) {
        ImGui::Checkbox(xorstr_("No Title Bar"), &config->misc.spectatorList.noTitleBar);
        ImGui::EndPopup();
    }
    ImGui::PopID();

    ImGui::Checkbox(xorstr_("Watermark"), &config->misc.watermark.enabled);
    ImGuiCustom::colorPicker(xorstr_("Offscreen Enemies"), config->misc.offscreenEnemies, &config->misc.offscreenEnemies.enabled);
    ImGui::SameLine();
    ImGui::PushID(xorstr_("Offscreen Enemies"));
    if (ImGui::Button(xorstr_("...")))
        ImGui::OpenPopup("");

    if (ImGui::BeginPopup("")) {
        ImGui::Checkbox(xorstr_("Health Bar"), &config->misc.offscreenEnemies.healthBar.enabled);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(95.0f);
        ImGui::Combo("Type", &config->misc.offscreenEnemies.healthBar.type, xorstr_("Gradient\0Solid\0Health-based\0"));
        if (config->misc.offscreenEnemies.healthBar.type == HealthBar::Solid) {
            ImGui::SameLine();
            ImGuiCustom::colorPicker("", static_cast<Color4&>(config->misc.offscreenEnemies.healthBar));
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    ImGui::Checkbox(xorstr_("Fix animation LOD"), &config->misc.fixAnimationLOD);
    ImGui::Checkbox(xorstr_("Fix bone matrix"), &config->misc.fixBoneMatrix);
    ImGui::Checkbox(xorstr_("Fix movement"), &config->misc.fixMovement);
    ImGui::Checkbox(xorstr_("Disable model occlusion"), &config->misc.disableModelOcclusion);
    ImGui::SliderFloat("Aspect Ratio", &config->misc.aspectratio, 0.0f, 5.0f, "%.2f");
    ImGui::NextColumn();
    ImGui::Checkbox(xorstr_("Disable HUD blur"), &config->misc.disablePanoramablur);
    ImGui::Checkbox(xorstr_("Animated clan tag"), &config->misc.animatedClanTag);
    ImGui::Checkbox(xorstr_("Clock tag"), &config->misc.clocktag);
    ImGui::Checkbox(xorstr_("Custom clantag"), &config->misc.customClanTag);
    ImGui::SameLine();
    ImGui::PushItemWidth(120.0f);
    ImGui::PushID(0);

    if (ImGui::InputText("", config->misc.clanTag, sizeof(config->misc.clanTag)))
        Misc::updateClanTag(true);
    ImGui::PopID();
    ImGui::Checkbox(xorstr_("Kill message"), &config->misc.killMessage);
    ImGui::SameLine();
    ImGui::PushItemWidth(120.0f);
    ImGui::PushID(1);
    ImGui::InputText("", &config->misc.killMessageString);
    ImGui::PopID();
    ImGui::Checkbox(xorstr_("Name stealer"), &config->misc.nameStealer);
    ImGui::PushID(3);
    ImGui::SetNextItemWidth(100.0f);
    ImGui::Combo("", &config->misc.banColor, "White\0Red\0Purple\0Green\0Light green\0Turquoise\0Light red\0Gray\0Yellow\0Gray 2\0Light blue\0Gray/Purple\0Blue\0Pink\0Dark orange\0Orange\0");
    ImGui::PopID();
    ImGui::SameLine();
    ImGui::PushID(4);
    ImGui::InputText("", &config->misc.banText);
    ImGui::PopID();
    ImGui::SameLine();
    if (ImGui::Button(xorstr_("Setup fake ban")))
        Misc::fakeBan(true);
    ImGui::Checkbox(xorstr_("Fast plant"), &config->misc.fastPlant);
    ImGui::Checkbox(xorstr_("Fast Stop"), &config->misc.fastStop);
    ImGuiCustom::colorPicker(xorstr_("Bomb timer"), config->misc.bombTimer);
    ImGui::Checkbox(xorstr_("Quick reload"), &config->misc.quickReload);
    ImGui::Checkbox(xorstr_("Prepare revolver"), &config->misc.prepareRevolver);
    ImGui::SameLine();
    ImGui::PushID(xorstr_("Prepare revolver Key"));
    hotkey2("", config->misc.prepareRevolverKey);
    ImGui::PopID();
    ImGui::Combo(xorstr_("Hit Sound"), &config->misc.hitSound, xorstr_("None\0Metal\0Gamesense\0Bell\0Glass\0Custom\0"));
    if (config->misc.hitSound == 5) {
        ImGui::InputText(xorstr_("Hit Sound filename"), &config->misc.customHitSound);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(xorstr_("audio file must be put in csgo/sound/ directory"));
    }
    ImGui::PushID(5);
    ImGui::Combo(xorstr_("Kill Sound"), &config->misc.killSound, "None\0Metal\0Gamesense\0Bell\0Glass\0Custom\0");
    if (config->misc.killSound == 5) {
        ImGui::InputText(xorstr_("Kill Sound filename"), &config->misc.customKillSound);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("audio file must be put in csgo/sound/ directory");
    }
    ImGui::PopID();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::InputInt(xorstr_("Choked packets"), &config->misc.chokedPackets, 1, 5);
    config->misc.chokedPackets = std::clamp(config->misc.chokedPackets, 0, 64);
    ImGui::SameLine();
    ImGui::PushID(xorstr_("Choked packets Key"));
    hotkey2("", config->misc.chokedPacketsKey);
    ImGui::PopID();
    /*
    ImGui::Text("Quick healthshot");
    ImGui::SameLine();
    hotkey(config->misc.quickHealthshotKey);
    */
    ImGui::Checkbox(xorstr_("Grenade Prediction"), &config->misc.nadePredict);
    ImGui::Checkbox(xorstr_("Fix tablet signal"), &config->misc.fixTabletSignal);
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat(xorstr_("Max angle delta"), &config->misc.maxAngleDelta, 0.0f, 255.0f, "%.2f");
    ImGui::Checkbox(xorstr_("Opposite Hand Knife"), &config->misc.oppositeHandKnife);
    ImGui::Checkbox(xorstr_("Preserve Killfeed"), &config->misc.preserveKillfeed.enabled);
    ImGui::SameLine();

    ImGui::PushID(xorstr_("Preserve Killfeed"));
    if (ImGui::Button(xorstr_("...")))
        ImGui::OpenPopup("");

    if (ImGui::BeginPopup("")) {
        ImGui::Checkbox(xorstr_("Only Headshots"), &config->misc.preserveKillfeed.onlyHeadshots);
        ImGui::EndPopup();
    }
    ImGui::PopID();

    ImGui::Checkbox(xorstr_("Purchase List"), &config->misc.purchaseList.enabled);
    ImGui::SameLine();

    ImGui::PushID(xorstr_("Purchase List"));
    if (ImGui::Button(xorstr_("...")))
        ImGui::OpenPopup("");

    if (ImGui::BeginPopup("")) {
        ImGui::SetNextItemWidth(75.0f);
        ImGui::Combo("Mode", &config->misc.purchaseList.mode, "Details\0Summary\0");
        ImGui::Checkbox(xorstr_("Only During Freeze Time"), &config->misc.purchaseList.onlyDuringFreezeTime);
        ImGui::Checkbox(xorstr_("Show Prices"), &config->misc.purchaseList.showPrices);
        ImGui::Checkbox(xorstr_("No Title Bar"), &config->misc.purchaseList.noTitleBar);
        ImGui::EndPopup();
    }
    ImGui::PopID();

    ImGui::Checkbox(xorstr_("Reportbot"), &config->misc.reportbot.enabled);
    ImGui::SameLine();
    ImGui::PushID(xorstr_("Reportbot"));

    if (ImGui::Button("..."))
        ImGui::OpenPopup("");

    if (ImGui::BeginPopup("")) {
        ImGui::PushItemWidth(80.0f);
        ImGui::Combo(xorstr_("Target"), &config->misc.reportbot.target, ("Enemies\0Allies\0All\0"));
        ImGui::InputInt("Delay (s)", &config->misc.reportbot.delay);
        config->misc.reportbot.delay = (std::max)(config->misc.reportbot.delay, 1);
        ImGui::InputInt(xorstr_("Rounds"), &config->misc.reportbot.rounds);
        config->misc.reportbot.rounds = (std::max)(config->misc.reportbot.rounds, 1);
        ImGui::PopItemWidth();
        ImGui::Checkbox(xorstr_("Abusive Communications"), &config->misc.reportbot.textAbuse);
        ImGui::Checkbox(xorstr_("Griefing"), &config->misc.reportbot.griefing);
        ImGui::Checkbox(xorstr_("Wall Hacking"), &config->misc.reportbot.wallhack);
        ImGui::Checkbox(xorstr_("Aim Hacking"), &config->misc.reportbot.aimbot);
        ImGui::Checkbox(xorstr_("Other Hacking"), &config->misc.reportbot.other);
        if (ImGui::Button(xorstr_("Reset")))
            Misc::resetReportbot();
        ImGui::EndPopup();
    }
    ImGui::PopID();

    if (ImGui::Button(xorstr_("Unhook")))
        hooks->uninstall();

    ImGui::Columns(1);
    if (!contentOnly)
        ImGui::End();
}

void GUI::renderConfigWindow(bool contentOnly) noexcept
{
    if (!contentOnly) {
        if (!window.config)
            return;
        ImGui::SetNextWindowSize({ 320.0f, 0.0f });
        if (!ImGui::Begin(xorstr_("Config"), &window.config, windowFlags)) {
            ImGui::End();
            return;
        }
    }

    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnOffset(1, 170.0f);

    static bool incrementalLoad = false;
    ImGui::Checkbox(xorstr_("Incremental Load"), &incrementalLoad);

    ImGui::PushItemWidth(160.0f);

    auto& configItems = config->getConfigs();
    static int currentConfig = -1;

    static std::string buffer;

    timeToNextConfigRefresh -= ImGui::GetIO().DeltaTime;
    if (timeToNextConfigRefresh <= 0.0f) {
        config->listConfigs();
        if (const auto it = std::find(configItems.begin(), configItems.end(), buffer); it != configItems.end())
            currentConfig = std::distance(configItems.begin(), it);
        timeToNextConfigRefresh = 0.1f;
    }

    if (static_cast<std::size_t>(currentConfig) >= configItems.size())
        currentConfig = -1;

    if (ImGui::ListBox("", &currentConfig, [](void* data, int idx, const char** out_text) {
        auto& vector = *static_cast<std::vector<std::string>*>(data);
        *out_text = vector[idx].c_str();
        return true;
        }, &configItems, configItems.size(), 5) && currentConfig != -1)
            buffer = configItems[currentConfig];

        ImGui::PushID(0);
        if (ImGui::InputTextWithHint("", "config name", &buffer, ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (currentConfig != -1)
                config->rename(currentConfig, buffer.c_str());
        }
        ImGui::PopID();
        ImGui::NextColumn();

        ImGui::PushItemWidth(100.0f);

        if (ImGui::Button(xorstr_("Open config directory")))
            config->openConfigDir();

        if (ImGui::Button(xorstr_("Create config"), { 100.0f, 25.0f }))
            config->add(buffer.c_str());

        if (ImGui::Button(xorstr_("Reset config"), { 100.0f, 25.0f }))
            ImGui::OpenPopup(xorstr_("Config to reset"));

        if (ImGui::BeginPopup(xorstr_("Config to reset"))) {
            static  const char* names[13] =
            { 
                _memdup(xorstr_("Whole")),
                _memdup(xorstr_("Aimbot")),
                _memdup(xorstr_("Triggerbot")),
                _memdup(xorstr_("Backtrack")),
                _memdup(xorstr_("Anti aim")),
                _memdup(xorstr_("Glow")),
                _memdup(xorstr_("Chams")),
                _memdup(xorstr_("ESP")), 
                _memdup(xorstr_("Visuals")),
                _memdup(xorstr_("Inventory Changer")),
                _memdup(xorstr_("Sound")),
                _memdup(xorstr_("Style")),
                _memdup(xorstr_("Misc" ))
            };
            for (int i = 0; i < IM_ARRAYSIZE(names); i++) {
                if (i == 1) ImGui::Separator();

                if (ImGui::Selectable(names[i])) {
                    switch (i) {
                    case 0: config->reset(); updateColors(); Misc::updateClanTag(true); InventoryChanger::scheduleHudUpdate(); break;
                    case 1: config->aimbot = { }; break;
                    case 2: config->triggerbot = { }; break;
                    case 3: Backtrack::resetConfig(); break;
                    case 4: AntiAim::resetConfig(); break;
                    case 5: Glow::resetConfig(); break;
                    case 6: config->chams = { }; break;
                    case 7: config->streamProofESP = { }; break;
                    case 8: config->visuals = { }; break;
                    case 9: InventoryChanger::resetConfig(); InventoryChanger::scheduleHudUpdate(); break;
                    case 10: Sound::resetConfig(); break;
                    case 11: config->style = { }; updateColors(); break;
                    case 12: config->misc = { };  Misc::updateClanTag(true); break;
                    }
                }
            }
            ImGui::EndPopup();
        }
        if (currentConfig != -1) {
            if (ImGui::Button(xorstr_("Load selected"), { 100.0f, 25.0f })) {
                config->load(currentConfig, incrementalLoad);
                updateColors();
                InventoryChanger::scheduleHudUpdate();
                Misc::updateClanTag(true);
            }
            if (ImGui::Button(xorstr_("Save selected"), { 100.0f, 25.0f }))
                config->save(currentConfig);
            if (ImGui::Button(xorstr_("Delete selected"), { 100.0f, 25.0f })) {
                config->remove(currentConfig);

                if (static_cast<std::size_t>(currentConfig) < configItems.size())
                    buffer = configItems[currentConfig];
                else
                    buffer.clear();
            }
        }
        ImGui::Columns(1);
        if (!contentOnly)
            ImGui::End();
}

void GUI::renderGuiStyle2() noexcept
{
    ImGui::Begin(xorstr_("Osiris"), nullptr, windowFlags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize);

    if (ImGui::BeginTabBar(xorstr_("TabBar"), ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_NoTooltip)) {
        if (ImGui::BeginTabItem(xorstr_("Aimbot"))) {
            renderAimbotWindow(true);
            ImGui::EndTabItem();
        }
        AntiAim::tabItem();
        if (ImGui::BeginTabItem(xorstr_("Triggerbot"))) {
            renderTriggerbotWindow(true);
            ImGui::EndTabItem();
        }
        Backtrack::tabItem();
        Glow::tabItem();
        if (ImGui::BeginTabItem(xorstr_("Chams"))) {
            renderChamsWindow(true);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(xorstr_("ESP"))) {
            renderStreamProofESPWindow(true);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(xorstr_("Visuals"))) {
            renderVisualsWindow(true);
            ImGui::EndTabItem();
        }
        InventoryChanger::tabItem();
        Sound::tabItem();
        if (ImGui::BeginTabItem(xorstr_("Style"))) {
            renderStyleWindow(true);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(xorstr_("Misc"))) {
            renderMiscWindow(true);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(xorstr_("Config"))) {
            renderConfigWindow(true);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}
