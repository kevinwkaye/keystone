#include "Analysis.h"

#include <algorithm>
#include <cmath>

namespace ks
{
void fft (std::vector<std::complex<double>>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2 * M_PI / (double) len * (inverse ? 1 : -1);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1);
            for (size_t j = 0; j < len / 2; ++j)
            {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (auto& v : a) v /= (double) n;
}

double yinPitch (const float* x, int n, double sr, double fMin, double fMax, double& clarity)
{
    clarity = 0;
    const int tauMax = std::min (n / 2, (int) (sr / fMin) + 2);
    const int tauMin = std::max (2, (int) (sr / fMax));
    if (tauMax <= tauMin + 2) return 0;
    const int w = n - tauMax;
    if (w < 16) return 0;

    std::vector<double> d ((size_t) tauMax + 1, 0.0);
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double s = 0;
        for (int i = 0; i < w; ++i)
        {
            const double diff = (double) x[i] - (double) x[i + tau];
            s += diff * diff;
        }
        d[(size_t) tau] = s;
    }
    // cumulative mean normalised difference
    std::vector<double> cm ((size_t) tauMax + 1, 1.0);
    double run = 0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        run += d[(size_t) tau];
        cm[(size_t) tau] = run > 0 ? d[(size_t) tau] * tau / run : 1.0;
    }
    int best = -1;
    const double threshold = 0.15;
    for (int tau = tauMin; tau < tauMax; ++tau)
    {
        if (cm[(size_t) tau] < threshold)
        {
            while (tau + 1 < tauMax && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau;
            best = tau;
            break;
        }
    }
    if (best < 0)
    {
        // no dip under the threshold: take the global minimum if it is reasonably clear
        best = tauMin;
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (cm[(size_t) tau] < cm[(size_t) best]) best = tau;
        if (cm[(size_t) best] > 0.35) return 0;
    }
    // parabolic interpolation
    double t = best;
    if (best > 1 && best < tauMax)
    {
        const double a = cm[(size_t) best - 1], b = cm[(size_t) best], c = cm[(size_t) best + 1];
        const double den = a - 2 * b + c;
        if (std::fabs (den) > 1e-12) t += 0.5 * (a - c) / den;
    }
    clarity = 1.0 - std::min (1.0, cm[(size_t) best]);
    return sr / t;
}

std::vector<Partial> estimatePartials (const float* x, int fftSize, int hop, double sr, double f0,
                                       int maxPartials, double maxFreq)
{
    std::vector<Partial> out;
    if (f0 <= 0) return out;

    std::vector<double> win ((size_t) fftSize);
    double winSum = 0;
    for (int i = 0; i < fftSize; ++i)
    {
        win[(size_t) i] = 0.5 - 0.5 * std::cos (2 * M_PI * i / fftSize);
        winSum += win[(size_t) i];
    }
    std::vector<std::complex<double>> a ((size_t) fftSize), b ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
    {
        a[(size_t) i] = x[i] * win[(size_t) i];
        b[(size_t) i] = x[i + hop] * win[(size_t) i];
    }
    fft (a);
    fft (b);

    const double binHz = sr / fftSize;
    for (int k = 1; k <= maxPartials; ++k)
    {
        const double target = f0 * k;
        if (target > maxFreq || target > sr * 0.45) break;
        // strongest bin within +/- 3% of the expected harmonic
        const int lo = std::max (1, (int) std::floor (target * 0.97 / binHz));
        const int hi = std::min (fftSize / 2 - 2, (int) std::ceil (target * 1.03 / binHz));
        int peak = lo;
        double pm = 0;
        for (int i = lo; i <= hi; ++i)
        {
            const double m = std::abs (b[(size_t) i]);
            if (m > pm) { pm = m; peak = i; }
        }
        if (pm <= 0) continue;
        // frequency from the phase advance over one hop
        const double expected = 2 * M_PI * peak * hop / fftSize;
        double dphi = std::arg (b[(size_t) peak]) - std::arg (a[(size_t) peak]) - expected;
        dphi -= 2 * M_PI * std::round (dphi / (2 * M_PI));
        const double freq = (peak + dphi * fftSize / (2 * M_PI * hop)) * binHz;
        // Hann window: sum the energy of the peak and its neighbours so an off-centre
        // partial is not read low (the main lobe covers the peak bin and one each side)
        const double m0 = std::abs (b[(size_t) peak - 1]), m2 = std::abs (b[(size_t) peak + 1]);
        const double amp = 2.0 * std::sqrt ((pm * pm + m0 * m0 + m2 * m2) / 1.5) / winSum;
        if (std::fabs (freq - target) > target * 0.04) continue;
        out.push_back ({ freq, amp });
    }
    return out;
}

static const char* kNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

NoteReading nameNote (double freq, double refA)
{
    NoteReading r;
    if (freq <= 0) return r;
    const double m = 69.0 + 12.0 * std::log2 (freq / refA);
    r.midi = (int) std::lround (m);
    r.cents = (m - r.midi) * 100.0;
    r.freq = freq;
    return r;
}

const char* noteName (int midi)
{
    if (midi < 0) return "-";
    return kNames[midi % 12];
}
} // namespace ks
