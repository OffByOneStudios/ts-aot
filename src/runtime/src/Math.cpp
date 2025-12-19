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

double ts_math_log10(double a) { return std::log10(a); }
double ts_math_log2(double a) { return std::log2(a); }
double ts_math_log1p(double a) { return std::log1p(a); }
double ts_math_expm1(double a) { return std::expm1(a); }
double ts_math_cosh(double a) { return std::cosh(a); }
double ts_math_sinh(double a) { return std::sinh(a); }
double ts_math_tanh(double a) { return std::tanh(a); }
double ts_math_acosh(double a) { return std::acosh(a); }
double ts_math_asinh(double a) { return std::asinh(a); }
double ts_math_atanh(double a) { return std::atanh(a); }
double ts_math_cbrt(double a) { return std::cbrt(a); }
double ts_math_hypot(double a, double b) { return std::hypot(a, b); }
double ts_math_trunc(double a) { return std::trunc(a); }
double ts_math_sign(double a) {
    if (a > 0) return 1.0;
    if (a < 0) return -1.0;
    return a; // handles 0, -0, NaN
}
float ts_math_fround(double a) { return (float)a; }

int32_t ts_math_clz32(int32_t a) {
    if (a == 0) return 32;
#ifdef _MSC_VER
    unsigned long leading_zero = 0;
    if (_BitScanReverse(&leading_zero, (unsigned long)a)) {
        return 31 - (int32_t)leading_zero;
    }
    return 32;
#else
    return __builtin_clz(a);
#endif
}

}
