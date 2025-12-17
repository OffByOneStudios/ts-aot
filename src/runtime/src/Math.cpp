#include <algorithm>
#include <cmath>
#include <cstdint>

extern "C" {

int64_t ts_math_min(int64_t a, int64_t b) {
    return std::min(a, b);
}

int64_t ts_math_max(int64_t a, int64_t b) {
    return std::max(a, b);
}

int64_t ts_math_abs(int64_t a) {
    return std::abs(a);
}

int64_t ts_math_floor(double a) {
    return (int64_t)std::floor(a);
}

}
