#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "Config.h"
#include "Hacks/InventoryChanger.h"
#include "Interfaces.h"
#include "Netvars.h"

#include "SDK/ClassId.h"
#include "SDK/Client.h"
#include "SDK/ClientClass.h"
#include "SDK/Entity.h"
#include "SDK/EntityList.h"
#include "SDK/LocalPlayer.h"
#include "SDK/Platform.h"
#include "SDK/Recv.h"
#include "Security/VMProtectSDK.h"
static std::unordered_map<std::uint32_t, std::pair<recvProxy, recvProxy*>> proxies;

static void __CDECL spottedHook(recvProxyData& data, void* arg2, void* arg3) noexcept
{
    VMProtectBeginMutation("spottedHook");

    if (config->misc.radarHack)
        data.value._int = 1;

    constexpr auto hash{ FNV("CBaseEntity->m_bSpotted") };
    proxies[hash].first(data, arg2, arg3);
    VMProtectEnd();
}

static void __CDECL viewModelSequence(recvProxyData& data, void* outStruct, void* arg3) noexcept
{
    VMProtectBeginMutation("viewModelSequence");

    const auto viewModel = reinterpret_cast<Entity*>(outStruct);

    if (localPlayer && interfaces->entityList->getEntityFromHandle(viewModel->owner()) == localPlayer.get()) {
        if (const auto weapon = interfaces->entityList->getEntityFromHandle(viewModel->weapon())) {
            if (config->visuals.deagleSpinner && weapon->getClientClass()->classId == ClassId::Deagle && data.value._int == 7)
                data.value._int = 8;

            InventoryChanger::fixKnifeAnimation(weapon, data.value._int);
        }
    }
    constexpr auto hash{ FNV("CBaseViewModel->m_nSequence") };
    proxies[hash].first(data, outStruct, arg3);
    VMProtectEnd();
}

Netvars::Netvars() noexcept
{
    VMProtectBeginMutation("Netvars");

    for (auto clientClass = interfaces->client->getAllClasses(); clientClass; clientClass = clientClass->next)
        walkTable(clientClass->networkName, clientClass->recvTable);

    std::ranges::sort(offsets, {}, &std::pair<uint32_t, uint16_t>::first);
    offsets.shrink_to_fit();
    VMProtectEnd();

}

void Netvars::restore() noexcept
{
    for (const auto& [hash, proxyPair] : proxies)
        *proxyPair.second = proxyPair.first;

    proxies.clear();
    offsets.clear();
}

void Netvars::walkTable(const char* networkName, RecvTable* recvTable, const std::size_t offset) noexcept
{
    VMProtectBeginMutation("Netvars::walkTable");
    for (int i = 0; i < recvTable->propCount; ++i) {
        auto& prop = recvTable->props[i];

        if (std::isdigit(prop.name[0]))
            continue;

        if (fnv::hash_runtime(prop.name) == FNV("baseclass"))
            continue;

        if (prop.type == 6
            && prop.dataTable
            && prop.dataTable->netTableName[0] == 'D')
            walkTable(networkName, prop.dataTable, prop.offset + offset);

        const auto hash{ fnv::hash_runtime((networkName + std::string{ "->" } + prop.name).c_str()) };

        constexpr auto getHook{ [](std::uint32_t hash) noexcept -> recvProxy {
             switch (hash) {
             case FNV("CBaseEntity->m_bSpotted"):
                 return spottedHook;
             case FNV("CBaseViewModel->m_nSequence"):
                 return viewModelSequence;
             default:
                 return nullptr;
             }
        } };

        offsets.emplace_back(hash, std::uint16_t(offset + prop.offset));

        constexpr auto hookProperty{ [](std::uint32_t hash, recvProxy& originalProxy, recvProxy proxy) noexcept {
            if (originalProxy != proxy) {
                proxies[hash].first = originalProxy;
                proxies[hash].second = &originalProxy;
                originalProxy = proxy;
            }
        } };

        if (auto hook{ getHook(hash) })
            hookProperty(hash, prop.proxy, hook);
    }
    VMProtectEnd();
}
