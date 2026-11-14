#include "Tables.h"

namespace wsho::dsp
{
const WaveTables& WaveTables::get()
{
    static const WaveTables instance;
    return instance;
}

WaveTables::WaveTables()
{
    const int N = kSize;
    const double twoPi = 6.283185307179586;
    std::vector<double> base ((size_t) N), accSaw ((size_t) N, 0.0), accTri ((size_t) N, 0.0);
    for (int i = 0; i < N; ++i)
        base[(size_t) i] = std::sin (twoPi * (double) i / (double) N);

    sinData.resize ((size_t) kStride);
    for (int i = 0; i < kStride; ++i)
        sinData[(size_t) i] = (float) base[(size_t) (i & (N - 1))];

    sawData.assign ((size_t) (kNumTables * kStride), 0.0f);
    triData.assign ((size_t) (kNumTables * kStride), 0.0f);

    // Build from the top table (1 harmonic) downwards, adding only the new
    // harmonics each step. sin (2*pi*h*i/N) is just a strided read of `base`.
    int have = 0;
    for (int m = kNumTables - 1; m >= 0; --m)
    {
        const int want = (int) std::floor (1024.0 * std::pow (2.0, -(double) m / (double) kPerOct) + 1.0e-9);
        for (int h = have + 1; h <= want; ++h)
        {
            // saw: 0 at phase 0, rising, step at phase 0.5
            const double a = (2.0 / 3.141592653589793) * ((h & 1) ? 1.0 : -1.0) / (double) h;
            for (int i = 0; i < N; ++i)
                accSaw[(size_t) i] += a * base[(size_t) ((h * i) & (N - 1))];

            if (h & 1)   // triangle: 0 at phase 0, rising, peak at 0.25
            {
                const double b = (8.0 / (3.141592653589793 * 3.141592653589793))
                                 * ((((h - 1) / 2) & 1) ? -1.0 : 1.0) / ((double) h * (double) h);
                for (int i = 0; i < N; ++i)
                    accTri[(size_t) i] += b * base[(size_t) ((h * i) & (N - 1))];
            }
        }
        have = want;

        float* s = sawData.data() + (size_t) (m * kStride);
        float* t = triData.data() + (size_t) (m * kStride);
        for (int i = 0; i < kStride; ++i)
        {
            s[i] = (float) accSaw[(size_t) (i & (N - 1))];
            t[i] = (float) accTri[(size_t) (i & (N - 1))];
        }
    }
}
} // namespace wsho::dsp
