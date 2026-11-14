#pragma once
#include <vector>
#include <cmath>

namespace wsho::dsp
{
// Band-limited saw / triangle wavetables, mip-mapped at 4 tables per octave.
// Table m only contains harmonics that stay below Nyquist for every
// oscillator increment it is used with, so the classic waveforms are
// alias-free by construction (not merely alias-reduced as with polyBLEP).
// Indexed by phase increment (cycles / sample) → sample-rate independent.
class WaveTables
{
public:
    static constexpr int kSize = 4096, kPerOct = 4, kNumTables = 41, kStride = kSize + 2;

    static const WaveTables& get();      // built once, off the audio thread

    // Smallest table that is alias-free for increment `dt` (cycles per sample).
    static int indexFor (float dt) noexcept
    {
        if (! (dt > kBaseDt)) return 0;
        const int m = (int) std::ceil ((float) kPerOct * std::log2 (dt * (1.0f / kBaseDt)) - 1.0e-4f);
        return m < kNumTables ? m : kNumTables - 1;
    }

    const float* saw (int m) const noexcept { return sawData.data() + (size_t) (m * kStride); }
    const float* tri (int m) const noexcept { return triData.data() + (size_t) (m * kStride); }
    const float* sine() const noexcept      { return sinData.data(); }

    // Linear-interpolated read, p in [0, 1).
    static float read (const float* t, float p) noexcept
    {
        const float x = p * (float) kSize;
        const int i = (int) x;
        return t[i] + (x - (float) i) * (t[i + 1] - t[i]);
    }

private:
    WaveTables();
    static constexpr float kBaseDt = 0.5f / 1024.0f;   // table 0: 1024 harmonics
    std::vector<float> sawData, triData, sinData;
};
} // namespace wsho::dsp
