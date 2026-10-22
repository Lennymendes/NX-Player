// NX Player — MIT License
#include "PlaybackPlan.h"

namespace nx
{

namespace
{
    bool supports (const DeviceCapabilities& caps, double rate)
    {
        for (auto r : caps.sampleRates)
            if (std::abs (r - rate) < 0.5) return true;
        return false;
    }

    double bestPcmRate (const DeviceCapabilities& caps, double wanted)
    {
        if (supports (caps, wanted) || caps.sampleRates.isEmpty()) return wanted;

        // 1. smallest supported integer multiple of the source (pure upsampling)
        double best = 0.0;
        for (auto r : caps.sampleRates)
            if (r > wanted && std::fmod (r, wanted) == 0.0 && (best == 0.0 || r < best)) best = r;
        if (best > 0.0) return best;

        // 2. largest supported rate of the same family (pure decimation or ×2^k)
        for (auto r : caps.sampleRates)
            if (isFamily441 (r) == isFamily441 (wanted) && r > best) best = r;
        if (best > 0.0) return best;

        // 3. anything: the highest rate
        for (auto r : caps.sampleRates) best = juce::jmax (best, r);
        return best;
    }
}

PlaybackPlan makePlan (const StreamFormat& src, const DeviceCapabilities& caps, const EngineSettings& s)
{
    PlaybackPlan p;
    p.source = src;
    p.channels = src.numChannels == 1 ? 2 : src.numChannels;

    if (src.isDsd())
    {
        const bool nativeOk = caps.nativeDsdRates.contains (src.sampleRate);
        const bool dopOk = s.dopAllowed && supports (caps, src.sampleRate / 16.0);

        if ((s.dsdMode == DsdMode::Auto || s.dsdMode == DsdMode::Native) && nativeOk)
        {
            p.wire = WireFormat::NativeDsd;
            p.deviceRate = src.sampleRate;
            p.ringBitsHint = 1;
            p.reason = "DSD natif (ASIO) : le flux 1 bit part tel quel vers le DAC";
            return p;
        }
        if ((s.dsdMode == DsdMode::Auto || s.dsdMode == DsdMode::DoP || (s.dsdMode == DsdMode::Native && ! nativeOk)) && dopOk)
        {
            p.wire = WireFormat::DoP;
            p.deviceRate = src.sampleRate / 16.0;
            p.ringBitsHint = 24;
            p.reason = "DoP : le flux DSD voyage intact dans des mots PCM de 24 bits à "_t + formatRate (p.deviceRate);
            return p;
        }

        // DSD -> PCM: the highest 44,1 kHz-family rate the DAC accepts, DXD at most.
        double target = 0.0;
        for (double r : { 352800.0, 176400.0, 88200.0, 44100.0 })
            if (r <= s.maxDsdToPcmRate && r <= src.sampleRate / 8.0 && supports (caps, r)) { target = r; break; }

        p.wire = WireFormat::Pcm;
        p.conversion = PlaybackPlan::Conversion::DsdToPcm;
        if (target > 0.0)
        {
            p.conversionRate = target;
            p.deviceRate = target;
        }
        else
        {
            p.conversionRate = juce::jmin (352800.0, src.sampleRate / 8.0);
            p.deviceRate = bestPcmRate (caps, p.conversionRate);
        }
        p.ringBitsHint = 32;
        p.needsRequantize = true;
        p.reason = juce::String (caps.nativeDsdRates.isEmpty() && ! dopOk ? "Le DAC n'accepte ni DSD natif ni DoP à ce débit : "_t
                                                                          : "Conversion demandée : "_t)
                   + "DSD -> PCM " + formatRate (p.deviceRate) + " (filtre phase linéaire 64 bits)"_t;
        return p;
    }

    // PCM
    double wanted = src.sampleRate;
    if (s.upsampleToDxd)
    {
        const double dxd = isFamily441 (src.sampleRate) ? 352800.0 : 384000.0;
        for (double r = dxd; r > src.sampleRate; r *= 0.5)
            if (supports (caps, r) && std::fmod (r, src.sampleRate) == 0.0) { wanted = r; break; }
    }
    p.deviceRate = bestPcmRate (caps, wanted);

    if (p.deviceRate != src.sampleRate)
    {
        p.conversion = PlaybackPlan::Conversion::Resample;
        p.ringBitsHint = 32;
        p.needsRequantize = true;
        p.reason = wanted != src.sampleRate && p.deviceRate == wanted
                       ? "Suréchantillonnage sinc "_t + formatRate (src.sampleRate) + " -> " + formatRate (p.deviceRate)
                       : "Le DAC n'accepte pas " + formatRate (src.sampleRate) + " : conversion vers " + formatRate (p.deviceRate);
    }
    else if (src.kind == SampleKind::PcmFloat)
    {
        p.conversion = PlaybackPlan::Conversion::FloatToInt;
        p.ringBitsHint = 32;
        p.needsRequantize = true;
        p.reason = src.lossy ? "Source compressée avec pertes ("_t + src.codec + ")"
                             : "Source en virgule flottante : quantification vers la résolution du DAC"_t;
    }
    else
    {
        p.ringBitsHint = juce::jlimit (16, 32, src.bitsPerSample);
        p.needsRequantize = src.bitsPerSample > caps.maxBits;
        p.reason = p.needsRequantize ? "Le DAC est limité à "_t + juce::String (caps.maxBits) + " bits"
                                     : "Bit-perfect : aucun traitement";
    }
    return p;
}

} // namespace nx
