// NX Player — MIT License
#include "Waveform.h"
#include "../Audio/AudioDecoder.h"
#include "../DSP/DsdToPcm.h"
#include "../Core/Text.h"

namespace nx
{

namespace
{
    double toDb (double linear) { return linear > 1.0e-10 ? 20.0 * std::log10 (linear) : -200.0; }

    /** Envelope, global Peak / RMS and DR blocks, fed with interleaved doubles (full scale = 1). */
    struct Accumulator
    {
        Accumulator (int numChannels, int numBins, juce::int64 totalFrames, double rate)
            : ch (numChannels), bins (numBins), total (juce::jmax ((juce::int64) 1, totalFrames)),
              blockLen (juce::jmax ((juce::int64) 1, (juce::int64) (3.0 * rate))),
              blockSum ((size_t) numChannels, 0.0), blockPeak ((size_t) numChannels, 0.0), blockRms ((size_t) numChannels), blockPeaks ((size_t) numChannels)
        {
            static std::atomic<juce::uint64> nextId { 1 };
            out = std::make_shared<TrackAnalysis>();
            out->id = nextId++;
            out->channels = ch;
            out->minV.assign ((size_t) bins, 0.0f);
            out->maxV.assign ((size_t) bins, 0.0f);
            out->rms.assign ((size_t) bins, 0.0f);
            binSum.assign ((size_t) bins, 0.0);
            binCount.assign ((size_t) bins, 0);
            binEnd = boundary (0);
        }

        juce::int64 boundary (int b) const { return (juce::int64) ((double) (b + 1) * (double) total / (double) bins); }

        void add (const double* x, int frames)
        {
            for (int i = 0; i < frames; ++i, ++frame)
            {
                while (frame >= binEnd && bin < bins - 1) binEnd = boundary (++bin);
                float& mn = out->minV[(size_t) bin];
                float& mx = out->maxV[(size_t) bin];
                double sq = 0.0;
                for (int c = 0; c < ch; ++c)
                {
                    const double v = x[(size_t) i * (size_t) ch + (size_t) c];
                    const double a = std::abs (v);
                    mn = juce::jmin (mn, (float) v);
                    mx = juce::jmax (mx, (float) v);
                    sq += v * v;
                    peak = juce::jmax (peak, a);
                    blockSum[(size_t) c] += v * v;
                    blockPeak[(size_t) c] = juce::jmax (blockPeak[(size_t) c], a);
                }
                binSum[(size_t) bin] += sq;
                binCount[(size_t) bin] += ch;
                sumSq += sq;
                if (++blockPos == blockLen) closeBlock();
            }
        }

        void closeBlock()
        {
            for (int c = 0; c < ch; ++c)
            {
                blockRms[(size_t) c].push_back (std::sqrt (2.0 * blockSum[(size_t) c] / (double) blockPos));
                blockPeaks[(size_t) c].push_back (blockPeak[(size_t) c]);
                blockSum[(size_t) c] = blockPeak[(size_t) c] = 0.0;
            }
            blockPos = 0;
        }

        std::shared_ptr<TrackAnalysis> finish()
        {
            if (blockPos > 0) closeBlock();
            for (size_t b = 0; b < (size_t) bins; ++b)
                out->rms[b] = binCount[b] > 0 ? (float) std::sqrt (binSum[b] / (double) binCount[b]) : 0.0f;
            out->peakDb = toDb (peak);
            out->rmsDb = frame > 0 ? toDb (std::sqrt (sumSq / ((double) frame * ch))) : -200.0;

            // DR (TT Dynamic Range meter): per channel, 3 s blocks; RMS of the loudest 20 % of the blocks
            // against the second highest block peak; the channels are averaged, then rounded.
            double drSum = 0.0;
            int drChannels = 0;
            for (int c = 0; c < ch; ++c)
            {
                auto rms = blockRms[(size_t) c];
                auto peaks = blockPeaks[(size_t) c];
                if (rms.empty()) continue;
                std::sort (rms.begin(), rms.end(), std::greater<double>());
                std::sort (peaks.begin(), peaks.end(), std::greater<double>());
                const size_t n = juce::jmax ((size_t) 1, (size_t) std::lround (0.2 * (double) rms.size()));
                double s = 0.0;
                for (size_t k = 0; k < n; ++k) s += rms[k] * rms[k];
                const double r = std::sqrt (s / (double) n);
                const double pk2 = peaks.size() >= 2 ? peaks[1] : peaks[0];
                if (r <= 1.0e-10 || pk2 <= 1.0e-10) continue;
                drSum += 20.0 * std::log10 (pk2 / r);
                ++drChannels;
            }
            out->dr = drChannels > 0 ? juce::jmax (0, (int) std::lround (drSum / drChannels)) : 0;
            return out;
        }

        int ch, bins;
        juce::int64 total, blockLen, blockPos = 0, frame = 0, binEnd = 0;
        int bin = 0;
        double peak = 0.0, sumSq = 0.0;
        std::vector<double> blockSum, blockPeak, binSum;
        std::vector<juce::int64> binCount;
        std::vector<std::vector<double>> blockRms, blockPeaks;
        std::shared_ptr<TrackAnalysis> out;
    };
}

juce::String channelBadge (int channels) { return channels > 0 ? juce::String (channels) + "ch" : juce::String(); }

juce::String TrackAnalysis::summary() const
{
    auto num = [] (double v) { auto s = juce::String (v, 1); return i18n::isEnglish() ? s : s.replaceCharacter ('.', ','); };
    const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
    auto s = "Peak " + num (peakDb) + " dBFS" + dot + "RMS " + num (rmsDb) + " dBFS";
    if (dr > 0) s << dot << "DR" << dr;
    return s;
}

//==============================================================================
TrackAnalyzer::TrackAnalyzer() : juce::Thread ("NX track analysis") {}
TrackAnalyzer::~TrackAnalyzer() { ++generation; stopThread (4000); }

void TrackAnalyzer::request (const juce::String& path)
{
    {
        std::lock_guard<std::mutex> l (lock);
        if (path == pending) return;
        for (auto& c : cache) if (c.first == path) return;
        pending = path;
    }
    ++generation;                                        // the scan in progress (another track) stops at its next block
    if (! isThreadRunning()) startThread (juce::Thread::Priority::low);
    else notify();
}

std::shared_ptr<const TrackAnalysis> TrackAnalyzer::get (const juce::String& path) const
{
    std::lock_guard<std::mutex> l (lock);
    for (auto& c : cache) if (c.first == path) return c.second;
    return {};
}

void TrackAnalyzer::run()
{
    while (! threadShouldExit())
    {
        juce::String path;
        int gen = 0;
        {
            std::lock_guard<std::mutex> l (lock);
            path = pending;
            gen = generation.load();
        }
        if (path.isEmpty()) { wait (-1); continue; }

        auto result = juce::File::isAbsolutePath (path)
                          ? analyse (juce::File (path), numBins, [this, gen] { return threadShouldExit() || generation.load() != gen; })
                          : nullptr;
        {
            std::lock_guard<std::mutex> l (lock);
            if (generation.load() != gen) continue;      // cancelled: a newer request is waiting
            pending.clear();
            if (result == nullptr) continue;
            cache.emplace_back (path, std::move (result));
            if (cache.size() > 8) cache.erase (cache.begin());
        }
        sendChangeMessage();
    }
}

std::shared_ptr<TrackAnalysis> TrackAnalyzer::analyse (const juce::File& file, int bins, const std::function<bool()>& cancelled)
{
    juce::String err;
    auto dec = openDecoder (file, err);
    if (dec == nullptr || dec->getLength() <= 0) return nullptr;
    const auto fmt = dec->getFormat();
    const int ch = juce::jmax (1, fmt.numChannels);
    constexpr int block = 16384;
    bins = juce::jlimit (16, bins, (int) (dec->getDurationSeconds() / 0.005));   // >= 5 ms per point

    if (fmt.isDsd())
    {
        // Fast decimation of the 1-bit stream to 88,2 / 96 kHz (gain 2: SACD 0 dB = 50 % modulation -> 0 dBFS).
        const int bytesPerOut = juce::nextPowerOfTwo (juce::jmax (1, (int) std::lround (fmt.sampleRate / (8.0 * 88200.0))));
        dsp::DsdToPcm conv;
        conv.prepare (fmt.sampleRate, fmt.sampleRate / (8.0 * bytesPerOut), ch, 2.0);
        Accumulator acc (ch, bins, dec->getLength() / conv.getBytesPerOutputSample(), conv.getOutputRate());
        std::vector<uint8_t> in ((size_t) block * (size_t) ch);
        std::vector<double> out ((size_t) conv.maxOutputFor (block) * (size_t) ch);
        for (;;)
        {
            if (cancelled()) return nullptr;
            const int n = dec->readDsd (in.data(), block);
            if (n <= 0) break;
            acc.add (out.data(), conv.process (in.data(), n, out.data(), conv.maxOutputFor (n)));
        }
        return acc.finish();
    }

    Accumulator acc (ch, bins, dec->getLength(), fmt.sampleRate);
    std::vector<double> buf ((size_t) block * (size_t) ch);
    std::vector<int32_t> ints (fmt.kind == SampleKind::PcmFloat ? 0 : (size_t) block * (size_t) ch);
    for (;;)
    {
        if (cancelled()) return nullptr;
        int n = 0;
        if (fmt.kind == SampleKind::PcmFloat) n = dec->readDouble (buf.data(), block);
        else
        {
            n = dec->readInt (ints.data(), block);
            for (size_t i = 0; i < (size_t) juce::jmax (0, n) * (size_t) ch; ++i) buf[i] = (double) ints[i] * (1.0 / 2147483648.0);
        }
        if (n <= 0) break;
        acc.add (buf.data(), n);
    }
    return acc.finish();
}

//==============================================================================
void WaveformPainter::paint (juce::Graphics& g, juce::Rectangle<float> area, const TrackAnalysis& a, float progress,
                             juce::Colour played, juce::Colour rest)
{
    if (a.maxV.empty() || area.isEmpty()) return;
    const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto px = juce::Rectangle<int> (juce::roundToInt (area.getWidth() * scale), juce::roundToInt (area.getHeight() * scale));
    if (sourceId != a.id || px != pixels || colours[0] != played || colours[1] != rest)
    {
        sourceId = a.id;
        // Normalise on the track Peak (a -10 dBFS master still fills the height); RMS / Peak stay in proportion.
        float pk = 0.0f;
        for (size_t b = 0; b < a.maxV.size(); ++b) pk = juce::jmax (pk, a.maxV[b], -a.minV[b]);
        gain = pk > 1.0e-4f ? 0.96f / pk : 1.0f;
        pixels = px;
        colours[0] = played;
        colours[1] = rest;
        const int w = juce::jmax (1, px.getWidth()), h = juce::jmax (2, px.getHeight());
        const size_t n = a.maxV.size();
        const float half = (float) h * 0.5f;
        for (int k = 0; k < 2; ++k)
        {
            images[k] = juce::Image (juce::Image::ARGB, w, h, true);
            juce::Graphics ig (images[k]);
            for (int x = 0; x < w; ++x)   // one physical pixel per column: fine, tight vertical lines
            {
                const size_t b0 = (size_t) x * n / (size_t) w;
                const size_t b1 = juce::jmax (b0 + 1, (size_t) (x + 1) * n / (size_t) w);
                float mn = 0.0f, mx = 0.0f, r2 = 0.0f;
                for (size_t b = b0; b < b1 && b < n; ++b)
                {
                    mn = juce::jmin (mn, a.minV[b]);
                    mx = juce::jmax (mx, a.maxV[b]);
                    r2 += a.rms[b] * a.rms[b];
                }
                const float r = std::sqrt (r2 / (float) (b1 - b0));
                // true peaks (min / max): faithful to the master's dynamics, silences and clipping
                const float top = half - juce::jlimit (0.0f, 1.0f, mx * gain) * half, bottom = half - juce::jlimit (-1.0f, 0.0f, mn * gain) * half;
                ig.setColour (colours[k].withMultipliedAlpha (0.45f));
                ig.fillRect (juce::Rectangle<float> ((float) x, top, 1.0f, juce::jmax (1.0f, bottom - top)));
                // RMS core, symmetric
                const float rh = juce::jmin (1.0f, r * gain) * half;
                ig.setColour (colours[k]);
                ig.fillRect (juce::Rectangle<float> ((float) x, half - rh, 1.0f, juce::jmax (1.0f, rh * 2.0f)));
            }
        }
    }
    const float split = area.getX() + area.getWidth() * juce::jlimit (0.0f, 1.0f, progress);
    for (int k = 0; k < 2; ++k)
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion ((k == 0 ? area.withRight (split) : area.withLeft (split)).getSmallestIntegerContainer());
        g.drawImage (images[k], area, juce::RectanglePlacement::stretchToFit);
    }
}

} // namespace nx
