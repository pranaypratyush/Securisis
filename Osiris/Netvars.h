#pragma once

#include <algorithm>
#include <cassert>
#include <memory>
#include <vector>
#include <type_traits>
#include <utility>

#include "fnv.h"

struct RecvTable;

class Netvars {
public:
    Netvars() noexcept;

    void restore() noexcept;

    uint16_t operator[](const uint32_t hash) const noexcept
    {
        const auto it = std::ranges::lower_bound(offsets, hash, {}, &std::pair<uint32_t, uint16_t>::first);
        if (it != offsets.end() && it->first == hash)
            return it->second;
        assert(false);
        return 0;
    }
private:
    void walkTable(const char*, RecvTable*, const std::size_t = 0) noexcept;
    std::vector<std::pair<uint32_t, uint16_t>> offsets;
};

inline std::unique_ptr<Netvars> netvars;

#define PNETVAR_OFFSET(funcname, class_name, var_name, offset, type) \
[[nodiscard]] auto funcname() noexcept \
{ \
    constexpr auto hash = FNV(class_name "->" var_name); \
    return reinterpret_cast<std::add_pointer_t<type>>(this + netvars->operator[](hash) + offset); \
}

#define PNETVAR(funcname, class_name, var_name, type) \
    PNETVAR_OFFSET(funcname, class_name, var_name, 0, type)

#define NETVAR_OFFSET(funcname, class_name, var_name, offset, type) \
[[nodiscard]] std::add_lvalue_reference_t<type> funcname() noexcept \
{ \
    constexpr auto hash = FNV(class_name "->" var_name); \
    return *reinterpret_cast<std::add_pointer_t<type>>(this + netvars->operator[](hash) + offset); \
}

#define NETVAR(funcname, class_name, var_name, type) \
    NETVAR_OFFSET(funcname, class_name, var_name, 0, type)
