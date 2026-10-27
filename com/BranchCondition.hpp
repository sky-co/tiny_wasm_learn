#ifndef BRANCH_CONDITION_HPP
#define BRANCH_CONDITION_HPP

#include <cstdint>

#include "util.hpp"

namespace vb {

enum class BC : uint8_t { EQZ, EQ, NE, LT_S, LT_U, GT_S, GT_U, LE_S, LE_U, GE_S, GE_U, EQ_F, NE_F, LT_F, GT_F, LE_F, GE_F };

} // namespace vb

#endif
