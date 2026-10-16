#ifndef BRANCH_CONDITION_HPP
#define BRANCH_CONDITION_HPP

#include <array>
#include <cstddef>
#include <cstdint>

// Minimal declarations required by OPCode.hpp so the language server can
// parse translation units that include it.
#define VB_NOEXCEPT noexcept

namespace vb {

enum class BC : uint8_t { EQZ, EQ, NE, LT_S, LT_U, GT_S, GT_U, LE_S, LE_U, GE_S, GE_U, EQ_F, NE_F, LT_F, GT_F, LE_F, GE_F };

template <typename T, typename... Ts> constexpr std::array<T, 1 + sizeof...(Ts)> make_array(T first, Ts... rest) {
    return {first, rest...};
}

} // namespace vb

#endif
