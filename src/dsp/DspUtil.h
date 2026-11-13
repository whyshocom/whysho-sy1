#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

// Small shared helpers for the SY1 engine (header-only, allocation-free).
namespace wsho::dsp
{
// Control rate: per-voice modulation (LFOs, filter env, mod matrix, pitch) is
// evaluated every kCtrl samples and linearly ramped in between. Voices count
// their own ticks from note-on, so rendering is independent of host block size.
constexpr int   kCtrl = 16;
constexpr float kPi   = 3.14159265358979323846f;

inline float clampf (float x, float lo, float hi) noexcept { return x < lo ? lo : (x > hi ? hi : x); }
inline float sq (float x) noexcept { return x * x; }
// Exact comparisons, spelled so -Wfloat-equal knows they are deliberate.
inline bool nonZero (float x) noexcept { return x > 0.0f || x < 0.0f; }
template <typename T> inline bool differs (T a, T b) noexcept { return a < b || a > b; }

// Padé tanh, monotonic, exactly ±1 at |x| >= 3. ~2% max error — fine for saturation.
inline float fastTanh (float x) noexcept
{
    x = clampf (x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// sin (pi/2 * x) for x in [0, 1] (constant-power pan laws). Error < 3e-4.
inline float sinHalfPi (float x) noexcept
{
    const float x2 = x * x;
    return x * (1.5707963f + x2 * (-0.6459641f + x2 * (0.0796926f - x2 * 0.0046818f)));
}

// Triangle wavefolder: identity on [-1, 1], reflects anything beyond.
inline float foldTri (float v) noexcept
{
    float t = (v + 1.0f) * 0.25f;
    t -= (float) (int) t;
    if (t < 0.0f) t += 1.0f;
    return 1.0f - std::abs (t * 4.0f - 2.0f);
}

inline float wrap01 (float p) noexcept
{
    p -= (float) (int) p;
    return p < 0.0f ? p + 1.0f : p;
}

struct Rng
{
    uint32_t s = 0x12345678u;
    void seed (uint32_t v) noexcept { s = v ? v : 0x9E3779B9u; }
    uint32_t nextInt() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float next01() noexcept     { return (float) (nextInt() >> 8) * (1.0f / 16777216.0f); }
    float nextBipolar() noexcept { return next01() * 2.0f - 1.0f; }
};

inline uint32_t hash32 (uint32_t x) noexcept
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
inline float hashBipolar (uint32_t x) noexcept { return (float) (hash32 (x) >> 8) * (2.0f / 16777216.0f) - 1.0f; }

} // namespace wsho::dsp
