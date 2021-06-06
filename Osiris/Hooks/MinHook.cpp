#include "MinHook.h"
#include "../MinHook/MinHook.h"
#include "../Helpers.h"

void MinHook::init(void* base) noexcept
{
    VMProtectBeginMutation("MinHook::init");

    this->base = base;
    originals = std::make_unique<uintptr_t[]>(Helpers::calculateVmtLength(*reinterpret_cast<uintptr_t**>(base)));

    VMProtectEnd();
}

void MinHook::hookAt(std::size_t index, void* fun) noexcept
{
    void* orig;
    MH_CreateHook((*reinterpret_cast<void***>(base))[index], fun, &orig);
    originals[index] = uintptr_t(orig);
}
