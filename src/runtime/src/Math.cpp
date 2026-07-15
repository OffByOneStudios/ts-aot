#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

extern "C" {

double ts_math_min(double a, double b) {
    return std::min(a, b);
}

double ts_math_max(double a, double b) {
    return std::max(a, b);
}

double ts_math_abs(double a) {
    return std::fabs(a);
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

double ts_math_log(double a) { return std::log(a); }
double ts_math_log10(double a) { return std::log10(a); }
double ts_math_log2(double a) { return std::log2(a); }
double ts_math_log1p(double a) { return std::log1p(a); }
double ts_math_exp(double a) { return std::exp(a); }
double ts_math_expm1(double a) { return std::expm1(a); }
double ts_math_sin(double a) { return std::sin(a); }
double ts_math_cos(double a) { return std::cos(a); }
double ts_math_tan(double a) { return std::tan(a); }
double ts_math_asin(double a) { return std::asin(a); }
double ts_math_acos(double a) { return std::acos(a); }
double ts_math_atan(double a) { return std::atan(a); }
double ts_math_atan2(double a, double b) { return std::atan2(a, b); }
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
// ABI NOTE: the compiler lowers these with the generic Math conventions —
// fround as double(double) (MathHandler single-arg-double list) and
// imul/clz32 as i64(i64) — so the C signatures MUST match. fround used to
// return float (callers read xmm0 as double -> fround(1.5) printed
// 1.5000002...), and imul returned int32 (callers read zero-extended rax ->
// imul(-1,5) printed 4294967291 instead of -5).
double ts_math_fround(double a) { return (double)(float)a; }

int64_t ts_math_clz32(int64_t a) {
    uint32_t v = (uint32_t)(uint64_t)a;   // ES ToUint32
    if (v == 0) return 32;
#ifdef _MSC_VER
    unsigned long leading_zero = 0;
    if (_BitScanReverse(&leading_zero, (unsigned long)v)) {
        return 31 - (int64_t)leading_zero;
    }
    return 32;
#else
    return __builtin_clz(v);
#endif
}

int64_t ts_math_imul(int64_t a, int64_t b) {
    // ES Math.imul: ToInt32 both operands, 32-bit wrapping multiply,
    // SIGNED 32-bit result.
    uint32_t x = (uint32_t)(uint64_t)a;
    uint32_t y = (uint32_t)(uint64_t)b;
    return (int64_t)(int32_t)(x * y);
}

}
