#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

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

int64_t ts_math_ceil(double a) {
    return (int64_t)std::ceil(a);
}

int64_t ts_math_round(double a) {
    return (int64_t)std::round(a);
}

double ts_math_sqrt(double a) {
    return std::sqrt(a);
}

double ts_math_pow(double a, double b) {
    return std::pow(a, b);
}

double ts_math_random() {
    static std::mt19937 gen(std::random_device{}());
    static std::uniform_real_distribution<> dis(0.0, 1.0);
    return dis(gen);
}

double ts_math_PI() {
    return 3.14159265358979323846;
}

}
