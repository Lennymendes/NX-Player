// NX Player — MIT License
#include "AudioAnalyzer.h"

namespace nx
{

AudioAnalyzer::AudioAnalyzer (AnalyzerTap& t) : tap (t)
{
    const int n = 1 << fftOrder;
    history.assign ((size_t) n * 2, 0.0f);     // interleaved stereo history
    window.resize ((size_t) n);
    for (int i = 0; i < n; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (n - 1));
    fftData.assign ((size_t) n * 2, 0.0f);
    pullBuffer.assign (1 << 16, 0.0f);
}

void AudioAnalyzer::update (double dt, bool playing)
{
    rate = tap.getSampleRate();
    dsd = tap.isDsdDensity.load();
    const int n = 1 << fftOrder;

    // drain everything new (keep the most recent n frames)
    int got = 0;
    float rmsSum[2] = { 0, 0 }, pk[2] = { 0, 0 };
    while (true)
    {
        const int m = tap.pull (pullBuffer.data(), (int) pullBuffer.size() / 2);
        if (m <= 0) break;
        for (int i = 0; i < m; ++i)
        {
            const float l = pullBuffer[(size_t) i * 2], r = pullBuffer[(size_t) i * 2 + 1];
            history[(size_t) historyPos * 2] = l;
            history[(size_t) historyPos * 2 + 1] = r;
            historyPos = (historyPos + 1) % n;
            rmsSum[0] += l * l; rmsSum[1] += r * r;
            pk[0] = juce::jmax (pk[0], std::abs (l)); pk[1] = juce::jmax (pk[1], std::abs (r));
        }
        got += m;
    }

    // VU (300 ms integration) and peak (fast attack, 1.5 s release)
    const float vuCoeff = (float) (1.0 - std::exp (-dt / 0.3));
    const float peakRelease = (float) std::exp (-dt / 1.5);
    for (int c = 0; c < 2; ++c)
    {
        const float blockRms = got > 0 ? std::sqrt (rmsSum[c] / (float) got) : 0.0f;
        rms[(size_t) c] += ((playing ? blockRms : 0.0f) - rms[(size_t) c]) * vuCoeff;
        peak[(size_t) c] = juce::jmax (playing ? pk[c] : 0.0f, peak[(size_t) c] * peakRelease);
    }

    // oscilloscope: latest scopeSize frames, mid channel
    for (int i = 0; i < scopeSize; ++i)
    {
        const int idx = (historyPos - scopeSize + i + n) % n;
        scope[(size_t) i] = 0.5f * (history[(size_t) idx * 2] + history[(size_t) idx * 2 + 1]);
    }

    // spectrum
    for (int i = 0; i < n; ++i)
    {
        const int idx = (historyPos + i) % n;
        fftData[(size_t) i] = 0.5f * (history[(size_t) idx * 2] + history[(size_t) idx * 2 + 1]) * window[(size_t) i];
    }
    std::fill (fftData.begin() + n, fftData.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    const double nyquist = rate * 0.5;
    const double fMin = 20.0, fMax = juce::jmin (20000.0, nyquist);
    const float norm = 2.0f / (float) n * 2.0f;   // Hann coherent gain 0.5
    const float release = (float) std::exp (-dt / 0.25);
    for (int b = 0; b < numBands; ++b)
    {
        const double f0 = fMin * std::pow (fMax / fMin, (double) b / numBands);
        const double f1 = fMin * std::pow (fMax / fMin, (double) (b + 1) / numBands);
        int i0 = (int) (f0 / rate * n), i1 = juce::jmax (i0 + 1, (int) (f1 / rate * n));
        i0 = juce::jlimit (1, n / 2 - 1, i0); i1 = juce::jlimit (i0 + 1, n / 2, i1);
        float mag = 0.0f;
        for (int i = i0; i < i1; ++i) mag = juce::jmax (mag, fftData[(size_t) i]);
        const float db = juce::Decibels::gainToDecibels (mag * norm, -120.0f);
        const float v = playing ? juce::jlimit (0.0f, 1.0f, (db + 90.0f) / 90.0f) : 0.0f;
        bands[(size_t) b] = v > bands[(size_t) b] ? v : bands[(size_t) b] * release + v * (1.0f - release);
    }
}

juce::var AudioAnalyzer::toVar() const
{
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> spec, sc;
    for (auto v : bands) spec.add (v);
    for (int i = 0; i < scopeSize; i += 2) sc.add (scope[(size_t) i]);
    o->setProperty ("spectrum", spec);
    o->setProperty ("scope", sc);
    o->setProperty ("rmsL", rms[0]);
    o->setProperty ("rmsR", rms[1]);
    o->setProperty ("peakL", peak[0]);
    o->setProperty ("peakR", peak[1]);
    o->setProperty ("rate", rate);
    o->setProperty ("dsd", dsd);
    return juce::var (o);
}

} // namespace nx
