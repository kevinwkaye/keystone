#pragma once
// Off-audio-thread analysis: FFT, pitch detection (tuner and drone capture), partial
// estimation for the drone. Nothing here runs on the audio thread.

#include <complex>
#include <vector>

namespace ks
{
// In-place radix-2 complex FFT.
void fft (std::vector<std::complex<double>>& a, bool inverse = false);

// YIN pitch estimate. Returns 0 if no clear pitch. clarity is 1 - the YIN minimum (0..1).
double yinPitch (const float* x, int n, double sr, double fMin, double fMax, double& clarity);

struct Partial
{
    double freq = 0;
    double amp = 0;    // linear peak amplitude
};

// Estimates the partials of a held note. f0 from yinPitch, then each harmonic's frequency
// from the phase change between two FFT frames one hop apart (the Afterglow lesson),
// and its amplitude from the windowed magnitude. x must hold fftSize + hop samples.
std::vector<Partial> estimatePartials (const float* x, int fftSize, int hop, double sr, double f0,
                                       int maxPartials, double maxFreq);

// Note naming for the tuner.
struct NoteReading
{
    int midi = -1;
    double cents = 0;
    double freq = 0;
};
NoteReading nameNote (double freq, double refA = 440.0);
const char* noteName (int midi);   // "E", "F#" ...
} // namespace ks
