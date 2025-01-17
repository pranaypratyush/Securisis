#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>

#include "fnv.h"
#include "GameData.h"
#include "Interfaces.h"
#include "Memory.h"

#include "Resources/avatar_ct.h"
#include "Resources/avatar_tt.h"

#include "stb_image.h"

#include "SDK/ClassId.h"
#include "SDK/ClientClass.h"
#include "SDK/Engine.h"
#include "SDK/Entity.h"
#include "SDK/EntityList.h"
#include "SDK/GlobalVars.h"
#include "SDK/Localize.h"
#include "SDK/LocalPlayer.h"
#include "SDK/ModelInfo.h"
#include "SDK/ModelRender.h"
#include "SDK/NetworkChannel.h"
#include "SDK/PlayerResource.h"
#include "SDK/Sound.h"
#include "SDK/Steam.h"
#include "SDK/UtlVector.h"
#include "SDK/WeaponId.h"
#include "SDK/WeaponData.h"
#include "Security/VMProtectSDK.h"
#include "Security/xorstr.hpp"
static Matrix4x4 viewMatrix;
static LocalPlayerData localPlayerData;
static std::vector<PlayerData> playerData;
static std::vector<ObserverData> observerData;
static std::vector<WeaponData> weaponData;
static std::vector<EntityData> entityData;
static std::vector<LootCrateData> lootCrateData;
static std::forward_list<ProjectileData> projectileData;
static BombData bombData;
static std::vector<InfernoData> infernoData;
static std::atomic_int netOutgoingLatency;

static auto playerByHandleWritable(int handle) noexcept
{
    const auto it = std::ranges::find(playerData, handle, &PlayerData::handle);
    return it != playerData.end() ? &(*it) : nullptr;
}

static void updateNetLatency() noexcept
{
    if (const auto networkChannel = interfaces->engine->getNetworkChannel())
        netOutgoingLatency = (std::max)(static_cast<int>(networkChannel->getLatency(0) * 1000.0f), 0);
    else
        netOutgoingLatency = 0;
}

constexpr auto playerVisibilityUpdateDelay = 0.1f;
static float nextPlayerVisibilityUpdateTime = 0.0f;

static bool shouldUpdatePlayerVisibility() noexcept
{
    return nextPlayerVisibilityUpdateTime <= memory->globalVars->realtime;
}

void GameData::update() noexcept
{
    VMProtectBeginMutation("GameData::update");

    static int lastFrame;
    if (lastFrame == memory->globalVars->framecount)
        return;
    lastFrame = memory->globalVars->framecount;

    updateNetLatency();

    Lock lock;
    observerData.clear();
    weaponData.clear();
    entityData.clear();
    lootCrateData.clear();
    infernoData.clear();

    localPlayerData.update();
    bombData.update();

    if (!localPlayer) {
        playerData.clear();
        projectileData.clear();
        return;
    }

    viewMatrix = interfaces->engine->worldToScreenMatrix();

    const auto observerTarget = localPlayer->getObserverMode() == ObsMode::InEye ? localPlayer->getObserverTarget() : nullptr;

    for (int i = 1; i <= interfaces->entityList->getHighestEntityIndex(); ++i) {
        const auto entity = interfaces->entityList->getEntity(i);
        if (!entity)
            continue;

        if (entity->isPlayer()) {
            if (entity == localPlayer.get() || entity == observerTarget)
                continue;

            if (const auto player = playerByHandleWritable(entity->handle())) {
                player->update(entity);
            } else {
                playerData.emplace_back(entity);
            }

            if (!entity->isDormant() && !entity->isAlive()) {
                if (const auto obs = entity->getObserverTarget())
                    observerData.emplace_back(entity, obs, obs == localPlayer.get());
            }
        } else {
            if (entity->isDormant())
                continue;

            if (entity->isWeapon()) {
                if (entity->ownerEntity() == -1)
                    weaponData.emplace_back(entity);
            } else {
                switch (entity->getClientClass()->classId) {
                case ClassId::BaseCSGrenadeProjectile:
                    if (entity->grenadeExploded()) {
                        if (const auto it = std::find(projectileData.begin(), projectileData.end(), entity->handle()); it != projectileData.end())
                            it->exploded = true;
                        break;
                    }
                    [[fallthrough]];
                case ClassId::BreachChargeProjectile:
                case ClassId::BumpMineProjectile:
                case ClassId::DecoyProjectile:
                case ClassId::MolotovProjectile:
                case ClassId::SensorGrenadeProjectile:
                case ClassId::SmokeGrenadeProjectile:
                case ClassId::SnowballProjectile:
                    if (const auto it = std::find(projectileData.begin(), projectileData.end(), entity->handle()); it != projectileData.end())
                        it->update(entity);
                    else
                        projectileData.emplace_front(entity);
                    break;
                case ClassId::DynamicProp:
                    if (const auto model = entity->getModel(); !model || !std::strstr(model->name, "challenge_coin"))
                        break;
                    [[fallthrough]];
                case ClassId::EconEntity:
                case ClassId::Chicken:
                case ClassId::PlantedC4:
                case ClassId::Hostage:
                case ClassId::Dronegun:
                case ClassId::Cash:
                case ClassId::AmmoBox:
                case ClassId::RadarJammer:
                case ClassId::SnowballPile:
                    entityData.emplace_back(entity);
                    break;
                case ClassId::LootCrate:
                    lootCrateData.emplace_back(entity);
                    break;
                case ClassId::Inferno:
                    infernoData.emplace_back(entity);
                    break;
                }
            }
        }
    }

    std::sort(playerData.begin(), playerData.end());
    std::sort(weaponData.begin(), weaponData.end());
    std::sort(entityData.begin(), entityData.end());
    std::sort(lootCrateData.begin(), lootCrateData.end());

    std::for_each(projectileData.begin(), projectileData.end(), [](auto& projectile) {
        if (interfaces->entityList->getEntityFromHandle(projectile.handle) == nullptr)
            projectile.exploded = true;
    });

    std::erase_if(projectileData, [](const auto& projectile) { return interfaces->entityList->getEntityFromHandle(projectile.handle) == nullptr
        && (projectile.trajectory.size() < 1 || projectile.trajectory[projectile.trajectory.size() - 1].first + 60.0f < memory->globalVars->realtime); });

    std::erase_if(playerData, [](const auto& player) { return interfaces->entityList->getEntityFromHandle(player.handle) == nullptr; });

    if (shouldUpdatePlayerVisibility())
        nextPlayerVisibilityUpdateTime = memory->globalVars->realtime + playerVisibilityUpdateDelay;

    VMProtectEnd();
}

void GameData::clearProjectileList() noexcept
{
    Lock lock;
    projectileData.clear();
}

static void clearAvatarTextures() noexcept;

struct PlayerAvatar {
    mutable Texture texture;
    std::unique_ptr<std::uint8_t[]> rgba;
};

static std::unordered_map<int, PlayerAvatar> playerAvatars;

void GameData::clearTextures() noexcept
{
    Lock lock;

    clearAvatarTextures();
    for (const auto& [handle, avatar] : playerAvatars)
        avatar.texture.clear();
}

void GameData::clearUnusedAvatars() noexcept
{
    Lock lock;
    std::erase_if(playerAvatars, [](const auto& pair) { return std::ranges::find(std::as_const(playerData), pair.first, &PlayerData::handle) == playerData.cend(); });
}

int GameData::getNetOutgoingLatency() noexcept
{
    return netOutgoingLatency;
}

const Matrix4x4& GameData::toScreenMatrix() noexcept
{
    return viewMatrix;
}

const LocalPlayerData& GameData::local() noexcept
{
    return localPlayerData;
}

const std::vector<PlayerData>& GameData::players() noexcept
{
    return playerData;
}

const PlayerData* GameData::playerByHandle(int handle) noexcept
{
    return playerByHandleWritable(handle);
}

const std::vector<ObserverData>& GameData::observers() noexcept
{
    return observerData;
}

const std::vector<WeaponData>& GameData::weapons() noexcept
{
    return weaponData;
}

const std::vector<EntityData>& GameData::entities() noexcept
{
    return entityData;
}

const std::vector<LootCrateData>& GameData::lootCrates() noexcept
{
    return lootCrateData;
}

const std::forward_list<ProjectileData>& GameData::projectiles() noexcept
{
    return projectileData;
}

const BombData& GameData::plantedC4() noexcept
{
    return bombData;
}

const std::vector<InfernoData>& GameData::infernos() noexcept
{
    return infernoData;
}

void LocalPlayerData::update() noexcept
{
    VMProtectBeginMutation("LocalPlayerData::update()");

    if (!localPlayer) {
        exists = false;
        return;
    }

    exists = true;
    alive = localPlayer->isAlive();

    if (const auto activeWeapon = localPlayer->getActiveWeapon()) {
        inReload = activeWeapon->isInReload();
        shooting = localPlayer->shotsFired() > 1;
        noScope = activeWeapon->isSniperRifle() && !localPlayer->isScoped();
        nextWeaponAttack = activeWeapon->nextPrimaryAttack();
    }
    fov = localPlayer->fov() ? localPlayer->fov() : localPlayer->defaultFov();
    handle = localPlayer->handle();
    flashDuration = localPlayer->flashDuration();

    aimPunch = localPlayer->getEyePosition() + Vector::fromAngle(interfaces->engine->getViewAngles() + localPlayer->getAimPunch()) * 1000.0f;

    const auto obsMode = localPlayer->getObserverMode();
    VMProtectEnd();
    if (const auto obs = localPlayer->getObserverTarget(); obs && obsMode != ObsMode::Roaming && obsMode != ObsMode::Deathcam)
        origin = obs->getAbsOrigin();
    else
        origin = localPlayer->getAbsOrigin();
}

BaseData::BaseData(Entity* entity) noexcept
{
    VMProtectBeginMutation("BaseData");

    distanceToLocal = entity->getAbsOrigin().distTo(localPlayerData.origin);
 
    if (entity->isPlayer()) {
        const auto collideable = entity->getCollideable();
        obbMins = collideable->obbMins();
        obbMaxs = collideable->obbMaxs();
    } else if (const auto model = entity->getModel()) {
        obbMins = model->mins;
        obbMaxs = model->maxs;
    }

    coordinateFrame = entity->toWorldTransform();
    VMProtectEnd();
}

EntityData::EntityData(Entity* entity) noexcept : BaseData{ entity }
{
    VMProtectBeginMutation("EntityData");

    name = [](Entity* entity) {
        switch (entity->getClientClass()->classId) {
        case ClassId::EconEntity: return xorstr_("Defuse Kit");
        case ClassId::Chicken: return xorstr_("Chicken");
        case ClassId::PlantedC4: return xorstr_("Planted C4");
        case ClassId::Hostage: return xorstr_("Hostage");
        case ClassId::Dronegun: return xorstr_("Sentry");
        case ClassId::Cash: return xorstr_("Cash");
        case ClassId::AmmoBox: return xorstr_("Ammo Box");
        case ClassId::RadarJammer: return xorstr_("Radar Jammer");
        case ClassId::SnowballPile: return xorstr_("Snowball Pile");
        case ClassId::DynamicProp: return xorstr_("Collectable Coin");
        default: assert(false); return xorstr_("unknown");
        }
    }(entity);
    VMProtectEnd();
}

ProjectileData::ProjectileData(Entity* projectile) noexcept : BaseData { projectile }
{
    name = [](Entity* projectile) {
        switch (projectile->getClientClass()->classId) {
        case ClassId::BaseCSGrenadeProjectile:
            if (const auto model = projectile->getModel(); model && strstr(model->name, "flashbang"))
                return xorstr_("Flashbang");
            else
                return xorstr_("HE Grenade");
        case ClassId::BreachChargeProjectile: return xorstr_("Breach Charge");
        case ClassId::BumpMineProjectile: return xorstr_("Bump Mine");
        case ClassId::DecoyProjectile: return xorstr_("Decoy Grenade");
        case ClassId::MolotovProjectile: return xorstr_("Molotov");
        case ClassId::SensorGrenadeProjectile: return xorstr_("TA Grenade");
        case ClassId::SmokeGrenadeProjectile: return xorstr_("Smoke Grenade");
        case ClassId::SnowballProjectile: return xorstr_("Snowball");
        default: assert(false); return xorstr_("unknown");
        }
    }(projectile);

    if (const auto thrower = interfaces->entityList->getEntityFromHandle(projectile->thrower()); thrower && localPlayer) {
        if (thrower == localPlayer.get())
            thrownByLocalPlayer = true;
        else
            thrownByEnemy = memory->isOtherEnemy(localPlayer.get(), thrower);
    }

    handle = projectile->handle();
}

void ProjectileData::update(Entity* projectile) noexcept
{
    static_cast<BaseData&>(*this) = { projectile };

    if (const auto& pos = projectile->getAbsOrigin(); trajectory.size() < 1 || trajectory[trajectory.size() - 1].second != pos)
        trajectory.emplace_back(memory->globalVars->realtime, pos);
}

PlayerData::PlayerData(Entity* entity) noexcept : BaseData{ entity }, handle{ entity->handle() }
{
    VMProtectBeginMutation("PlayerData");
    if (const auto steamID = entity->getSteamId()) {
        const auto ctx = interfaces->engine->getSteamAPIContext();
        const auto avatar = ctx->steamFriends->getSmallFriendAvatar(steamID);
        constexpr auto rgbaDataSize = 4 * 32 * 32;

        PlayerAvatar playerAvatar;
        playerAvatar.rgba = std::make_unique<std::uint8_t[]>(rgbaDataSize);
        if (ctx->steamUtils->getImageRGBA(avatar, playerAvatar.rgba.get(), rgbaDataSize))
            playerAvatars[handle] = std::move(playerAvatar);
    }

    update(entity);
    VMProtectEnd();
}

void PlayerData::update(Entity* entity) noexcept
{
    VMProtectBeginMutation("PlayerData::update");
    name = entity->getPlayerName();

    dormant = entity->isDormant();
    if (dormant)
        return;

    team = entity->getTeamNumber();
    static_cast<BaseData&>(*this) = { entity };
    origin = entity->getAbsOrigin();
    inViewFrustum = !interfaces->engine->cullBox(obbMins + origin, obbMaxs + origin);
    alive = entity->isAlive();
    lastContactTime = alive ? memory->globalVars->realtime : 0.0f;

    if (localPlayer) {
        enemy = memory->isOtherEnemy(entity, localPlayer.get());

        if (!inViewFrustum || !alive)
            visible = false;
        else if (shouldUpdatePlayerVisibility())
            visible = entity->visibleTo(localPlayer.get());
    }

    constexpr auto isEntityAudible = [](int entityIndex) noexcept {
        for (int i = 0; i < memory->activeChannels->count; ++i)
            if (memory->channels[memory->activeChannels->list[i]].soundSource == entityIndex)
                return true;
        return false;
    };

    audible = isEntityAudible(entity->index());
    spotted = entity->spotted();
    health = entity->health();
    immune = entity->gunGameImmunity();
    flashDuration = entity->flashDuration();

    if (const auto weapon = entity->getActiveWeapon()) {
        audible = audible || isEntityAudible(weapon->index());
        if (const auto weaponInfo = weapon->getWeaponData())
            activeWeapon = interfaces->localize->findAsUTF8(weaponInfo->name);
    }

    if (!alive || !inViewFrustum)
        return;

    const auto model = entity->getModel();
    if (!model)
        return;

    const auto studioModel = interfaces->modelInfo->getStudioModel(model);
    if (!studioModel)
        return;

    matrix3x4 boneMatrices[MAXSTUDIOBONES];
    if (!entity->setupBones(boneMatrices, MAXSTUDIOBONES, BONE_USED_BY_HITBOX, memory->globalVars->currenttime))
        return;

    bones.clear();
    bones.reserve(20);

    for (int i = 0; i < studioModel->numBones; ++i) {
        const auto bone = studioModel->getBone(i);

        if (!bone || bone->parent == -1 || !(bone->flags & BONE_USED_BY_HITBOX))
            continue;

        bones.emplace_back(boneMatrices[i].origin(), boneMatrices[bone->parent].origin());
    }

    const auto set = studioModel->getHitboxSet(entity->hitboxSet());
    if (!set)
        return;

    const auto headBox = set->getHitbox(0);

    headMins = headBox->bbMin.transform(boneMatrices[headBox->bone]);
    headMaxs = headBox->bbMax.transform(boneMatrices[headBox->bone]);

    if (headBox->capsuleRadius > 0.0f) {
        headMins -= headBox->capsuleRadius;
        headMaxs += headBox->capsuleRadius;
    }
    VMProtectEnd();
}

struct PNGTexture {
    template <std::size_t N>
    PNGTexture(const std::array<char, N>& png) noexcept : pngData{ png.data() }, pngDataSize{ png.size() } {}

    ImTextureID getTexture() const noexcept
    {
        if (!texture.get()) {
            int width, height;
            stbi_set_flip_vertically_on_load_thread(false);

            if (const auto data = stbi_load_from_memory((const stbi_uc*)pngData, pngDataSize, &width, &height, nullptr, STBI_rgb_alpha)) {
                texture.init(width, height, data);
                stbi_image_free(data);
            } else {
                assert(false);
            }
        }

        return texture.get();
    }

    void clearTexture() const noexcept { texture.clear(); }

private:
    const char* pngData;
    std::size_t pngDataSize;

    mutable Texture texture;
};

static const PNGTexture avatarTT{ Resource::avatar_tt };
static const PNGTexture avatarCT{ Resource::avatar_ct };

static void clearAvatarTextures() noexcept
{
    avatarTT.clearTexture();
    avatarCT.clearTexture();
}

ImTextureID PlayerData::getAvatarTexture() const noexcept
{
    const auto it = std::as_const(playerAvatars).find(handle);
    if (it == playerAvatars.cend())
        return team == Team::TT ? avatarTT.getTexture() : avatarCT.getTexture();

    const auto& avatar = it->second;
    if (!avatar.texture.get())
        avatar.texture.init(32, 32, avatar.rgba.get());
    return avatar.texture.get();
}

float PlayerData::fadingAlpha() const noexcept
{
    constexpr float fadeTime = 1.50f;
    return std::clamp(1.0f - (memory->globalVars->realtime - lastContactTime - 0.25f) / fadeTime, 0.0f, 1.0f);
}

WeaponData::WeaponData(Entity* entity) noexcept : BaseData{ entity }
{
    clip = entity->clip();
    reserveAmmo = entity->reserveAmmoCount();

    if (const auto weaponInfo = entity->getWeaponData()) {
        group = [](WeaponType type, WeaponId weaponId) {
            switch (type) {
            case WeaponType::Pistol: return xorstr_("Pistols");
            case WeaponType::SubMachinegun: return xorstr_("SMGs");
            case WeaponType::Rifle: return xorstr_("Rifles");
            case WeaponType::SniperRifle: return xorstr_("Sniper Rifles");
            case WeaponType::Shotgun: return xorstr_("Shotguns");
            case WeaponType::Machinegun: return xorstr_("Machineguns");
            case WeaponType::Grenade: return xorstr_("Grenades");
            case WeaponType::Melee: return xorstr_("Melee");
            default:
                switch (weaponId) {
                case WeaponId::C4:
                case WeaponId::Healthshot:
                case WeaponId::BumpMine:
                case WeaponId::ZoneRepulsor:
                case WeaponId::Shield:
                    return xorstr_("Other");
                default: return xorstr_("All");
                }
            }
        }(weaponInfo->type, entity->itemDefinitionIndex2());
        name = [](WeaponId weaponId) {
            switch (weaponId) {
            default: return xorstr_("All");

            case WeaponId::Glock: return xorstr_("Glock-18");
            case WeaponId::Hkp2000: return xorstr_("P2000");
            case WeaponId::Usp_s: return xorstr_("USP-S");
            case WeaponId::Elite: return xorstr_("Dual Berettas");
            case WeaponId::P250: return xorstr_("P250");
            case WeaponId::Tec9: return xorstr_("Tec-9");
            case WeaponId::Fiveseven: return xorstr_("Five-SeveN");
            case WeaponId::Cz75a: return xorstr_("CZ75-Auto");
            case WeaponId::Deagle: return xorstr_("Desert Eagle");
            case WeaponId::Revolver: return xorstr_("R8 Revolver");

            case WeaponId::Mac10: return xorstr_("MAC-10");
            case WeaponId::Mp9: return xorstr_("MP9");
            case WeaponId::Mp7: return xorstr_("MP7");
            case WeaponId::Mp5sd: return xorstr_("MP5-SD");
            case WeaponId::Ump45: return xorstr_("UMP-45");
            case WeaponId::P90: return xorstr_("P90");
            case WeaponId::Bizon: return xorstr_("PP-Bizon");

            case WeaponId::GalilAr: return xorstr_("Galil AR");
            case WeaponId::Famas: return xorstr_("FAMAS");
            case WeaponId::Ak47: return xorstr_("AK-47");
            case WeaponId::M4A1: return xorstr_("M4A4");
            case WeaponId::M4a1_s: return xorstr_("M4A1-S");
            case WeaponId::Sg553: return xorstr_("SG 553");
            case WeaponId::Aug: return xorstr_("AUG");

            case WeaponId::Ssg08: return xorstr_("SSG 08");
            case WeaponId::Awp: return xorstr_("AWP");
            case WeaponId::G3SG1: return xorstr_("G3SG1");
            case WeaponId::Scar20: return xorstr_("SCAR-20");

            case WeaponId::Nova: return xorstr_("Nova");
            case WeaponId::Xm1014: return xorstr_("XM1014");
            case WeaponId::Sawedoff: return xorstr_("Sawed-Off");
            case WeaponId::Mag7: return xorstr_("MAG-7");

            case WeaponId::M249: return xorstr_("M249");
            case WeaponId::Negev: return xorstr_("Negev");

            case WeaponId::Flashbang: return xorstr_("Flashbang");
            case WeaponId::HeGrenade: return xorstr_("HE Grenade");
            case WeaponId::SmokeGrenade: return xorstr_("Smoke Grenade");
            case WeaponId::Molotov: return xorstr_("Molotov");
            case WeaponId::Decoy: return xorstr_("Decoy Grenade");
            case WeaponId::IncGrenade: return xorstr_("Incendiary");
            case WeaponId::TaGrenade: return xorstr_("TA Grenade");
            case WeaponId::Firebomb: return xorstr_("Fire Bomb");
            case WeaponId::Diversion: return xorstr_("Diversion");
            case WeaponId::FragGrenade: return xorstr_("Frag Grenade");
            case WeaponId::Snowball: return xorstr_("Snowball");

            case WeaponId::Axe: return xorstr_("Axe");
            case WeaponId::Hammer: return xorstr_("Hammer");
            case WeaponId::Spanner: return xorstr_("Wrench");

            case WeaponId::C4: return xorstr_("C4");
            case WeaponId::Healthshot: return xorstr_("Healthshot");
            case WeaponId::BumpMine: return xorstr_("Bump Mine");
            case WeaponId::ZoneRepulsor: return xorstr_("Zone Repulsor");
            case WeaponId::Shield: return xorstr_("Shield");
            }
        }(entity->itemDefinitionIndex2());

        displayName = interfaces->localize->findAsUTF8(weaponInfo->name);
    }
}

LootCrateData::LootCrateData(Entity* entity) noexcept : BaseData{ entity }
{
    const auto model = entity->getModel();
    if (!model)
        return;

    name = [](const char* modelName) -> const char* {
        switch (fnv::hash_runtime(modelName)) {
        case FNV("models/props_survival/cases/case_pistol.mdl"): return xorstr_("Pistol Case");
        case FNV("models/props_survival/cases/case_light_weapon.mdl"): return xorstr_("Light Case");
        case FNV("models/props_survival/cases/case_heavy_weapon.mdl"): return xorstr_("Heavy Case");
        case FNV("models/props_survival/cases/case_explosive.mdl"): return xorstr_("Explosive Case");
        case FNV("models/props_survival/cases/case_tools.mdl"): return xorstr_("Tools Case");
        case FNV("models/props_survival/cash/dufflebag.mdl"): return xorstr_("Cash Dufflebag");
        default: return nullptr;
        }
    }(model->name);
}

ObserverData::ObserverData(Entity* entity, Entity* obs, bool targetIsLocalPlayer) noexcept : playerHandle{ entity->handle() }, targetHandle{ obs->handle() }, targetIsLocalPlayer{ targetIsLocalPlayer } {}

void BombData::update() noexcept
{
    if (memory->plantedC4s->size > 0 && (!*memory->gameRules || (*memory->gameRules)->mapHasBombTarget())) {
        if (const auto bomb = (*memory->plantedC4s)[0]; bomb && bomb->c4Ticking()) {
            blowTime = bomb->c4BlowTime();
            timerLength = bomb->c4TimerLength();
            defuserHandle = bomb->c4Defuser();
            if (defuserHandle != -1) {
                defuseCountDown = bomb->c4DefuseCountDown();
                defuseLength = bomb->c4DefuseLength();
            }

            if (*memory->playerResource) {
                const auto& bombOrigin = bomb->origin();
                bombsite = bombOrigin.distTo((*memory->playerResource)->bombsiteCenterA()) > bombOrigin.distTo((*memory->playerResource)->bombsiteCenterB());
            }
            return;
        }
    }
    blowTime = 0.0f;
}

InfernoData::InfernoData(Entity* inferno) noexcept
{
    const auto& origin = inferno->getAbsOrigin();

    points.reserve(inferno->fireCount());
    for (int i = 0; i < inferno->fireCount(); ++i) {
        if (inferno->fireIsBurning()[i])
            points.emplace_back(inferno->fireXDelta()[i] + origin.x, inferno->fireYDelta()[i] + origin.y, inferno->fireZDelta()[i] + origin.z);
    }
}
