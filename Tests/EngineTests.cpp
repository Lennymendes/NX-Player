// NX Player — MIT License
// Brique 1: decoders, DSP, exact float transport, and the engine end to end through a virtual DAC.
#include "TestFramework.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "TestAudioFiles.h"
#include "../Source/Core/SpscRing.h"
#include "../Source/Core/ExactSampleEncoder.h"
#include "../Source/Audio/AudioDecoder.h"
#include "../Source/DSP/DsdToPcm.h"
#include "../Source/DSP/Resampler.h"
#include "../Source/DSP/FilterDesign.h"
#include "../Source/Engine/DeviceProfiles.h"
#include "../Source/Audio/LoopingDecoder.h"
#include <complex>
#include "../Source/Engine/PlaybackEngine.h"
#include "../Source/Output/CaptureBackend.h"
#include <cmath>
#include <thread>

extern thread_local bool nxtest_countAllocations;
extern std::atomic<long> nxtest_allocationCount;

using namespace nxtest;

namespace
{
    std::vector<int32_t> decodeAllInt (const juce::File& f, nx::StreamFormat* fmtOut = nullptr)
    {
        juce::String err;
        auto d = nx::openDecoder (f, err);
        if (d == nullptr) return {};
        if (fmtOut) *fmtOut = d->getFormat();
        std::vector<int32_t> out;
        std::vector<int32_t> buf (4096 * (size_t) d->getFormat().numChannels);
        while (true)
        {
            const int n = d->readInt (buf.data(), 4096);
            if (n <= 0) break;
            out.insert (out.end(), buf.begin(), buf.begin() + n * d->getFormat().numChannels);
        }
        return out;
    }

    std::vector<uint8_t> decodeAllDsd (const juce::File& f, nx::StreamFormat* fmtOut = nullptr)
    {
        juce::String err;
        auto d = nx::openDecoder (f, err);
        if (d == nullptr) return {};
        if (fmtOut) *fmtOut = d->getFormat();
        std::vector<uint8_t> out, buf (8192 * (size_t) d->getFormat().numChannels);
        while (true)
        {
            const int n = d->readDsd (buf.data(), 8192);
            if (n <= 0) break;
            out.insert (out.end(), buf.begin(), buf.begin() + n * d->getFormat().numChannels);
        }
        return out;
    }

    /** Least-squares fit of a sine at freq: returns {amplitude, residual RMS}. */
    std::pair<double, double> fitSine (const double* x, int n, int stride, double rate, double freq)
    {
        double ss = 0, sc = 0, cc = 0, xs = 0, xc = 0;
        for (int i = 0; i < n; ++i)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * freq * i / rate;
            const double s = std::sin (w), c = std::cos (w);
            ss += s * s; sc += s * c; cc += c * c; xs += x[(size_t) i * stride] * s; xc += x[(size_t) i * stride] * c;
        }
        const double det = ss * cc - sc * sc;
        const double a = (xs * cc - xc * sc) / det, b = (xc * ss - xs * sc) / det;
        double res = 0;
        for (int i = 0; i < n; ++i)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * freq * i / rate;
            const double e = x[(size_t) i * stride] - (a * std::sin (w) + b * std::cos (w));
            res += e * e;
        }
        return { std::sqrt (a * a + b * b), std::sqrt (res / n) };
    }

    struct EngineRig
    {
        std::unique_ptr<nx::PlaybackEngine> engine;
        nx::CaptureBackend* cap = nullptr;

        explicit EngineRig (std::function<void (nx::CaptureBackend&)> configure = {})
        {
            auto c = std::make_unique<nx::CaptureBackend>();
            if (configure) configure (*c);
            cap = c.get();
            engine = std::make_unique<nx::PlaybackEngine> (std::move (c));
        }

        static nx::PlayableItem item (const juce::File& f)
        {
            nx::PlayableItem i;
            i.id = f.getFileName();
            i.uri = f.getFullPathName();
            i.title = f.getFileNameWithoutExtension();
            return i;
        }

        /** Pumps blocks until the queue ends (or timeout). Waits for the decoder so nothing underruns. */
        void runToEnd (int block = 1024, double timeoutSeconds = 60.0, std::function<void (int)> perBlock = {})
        {
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            int blocks = 0;
            while (juce::Time::getMillisecondCounterHiRes() - t0 < timeoutSeconds * 1000.0)
            {
                engine->serviceMessageThread();
                if (engine->getState() == nx::PlaybackEngine::State::Stopped && blocks > 0) break;
                if (! cap->isRunning()) { juce::Thread::sleep (1); continue; }
                for (int w = 0; w < 4000 && engine->isDecoderBusy() && engine->getBufferedBytes() < (1u << 19); ++w)
                    juce::Thread::sleep (1);
                cap->pump (block);
                if (perBlock) perBlock (blocks);
                ++blocks;
            }
        }
    };
}

//==============================================================================
NX_TEST_SECTION (101, "Ring SPSC et encodeur exact (2^24 valeurs)")
{
    nx::SpscRing<int> ring (10);
    int in[7] = { 1, 2, 3, 4, 5, 6, 7 }, out[10] {};
    bool ok = true;
    for (int round = 0; round < 50; ++round)
    {
        ok &= ring.write (in, 7) == 7;
        ok &= ring.read (out, 7) == 7;
        for (int i = 0; i < 7; ++i) ok &= out[i] == in[i];
    }
    ok &= ring.write (in, 7) == 7 && ring.write (in, 7) == 3 && ring.writable() == 0;
    t.check (ok, "ring : bouclage, plein / vide, ordre conservé"_t);

    // Exhaustive: every 24-bit integer through each float -> int rule.
    struct Case { nx::FloatToIntRule rule; int bits; juce::String name; int exactMsb; };
    const Case cases[] = {
        { nx::FloatToIntRule::Scale2N,        24, "CoreAudio / AudioData Int24 (×2^23)"_t, 24 },
        { nx::FloatToIntRule::RoundMaxMinus1, 24, "convertisseur ASIO JUCE 24 bits (×(2^23-1))"_t, 24 },
        { nx::FloatToIntRule::TruncMaxMinus1, 32, "AudioData Int32 tronqué (×(2^31-1))"_t, 24 },
        { nx::FloatToIntRule::Scale2N,        32, "HAL 32 bits (×2^31)"_t, 24 },
    };
    for (auto& c : cases)
    {
        nx::ExactSampleEncoder enc (c.rule, c.bits);
        long bad = 0, notFullyExact = 0;
        for (int64_t k = -(1 << 23) + 1; k < (1 << 23); ++k)
        {
            const auto v = (int32_t) ((uint32_t) k << 8);
            const auto got = enc.apply (enc.encode (v));
            const auto want = (int64_t) (v >> (32 - c.bits));
            if ((got >> (c.bits - c.exactMsb)) != (want >> (c.bits - c.exactMsb))) ++bad;
            if (got != want) ++notFullyExact;
        }
        t.check (bad == 0, juce::String (c.name) + " : 24 bits de poids fort exacts pour les 16 777 215 valeurs ("
                               + juce::String (notFullyExact) + " mots 32 bits avec octet bas non nul)");
    }

    // DoP words (markers 0x05 / 0xFA + 16 bits) through the truncating 32-bit rule.
    nx::ExactSampleEncoder trunc32 (nx::FloatToIntRule::TruncMaxMinus1, 32);
    long dopBad = 0;
    for (int m = 0; m < 2; ++m)
        for (int v = 0; v < 65536; ++v)
        {
            const auto w = (int32_t) (((uint32_t) (m ? 0xFA : 0x05) << 24) | ((uint32_t) v << 8));
            if ((trunc32.apply (trunc32.encode (w)) >> 8) != (int64_t) (w >> 8)) ++dopBad;
        }
    t.check (dopBad == 0, "DoP : les 131 072 mots possibles gardent marqueur + 16 bits DSD exacts avec la règle 32 bits tronquée"_t);
}

NX_TEST_SECTION (102, "Décodeurs PCM : WAV, RF64, AIFF, AIFF-C (bit à bit)")
{
    struct WavCase { int container, valid; bool rf64, ext; const char* name; };
    const WavCase wavs[] = {
        { 8, 8, false, false, "WAV 8 bits" }, { 16, 16, false, false, "WAV 16 bits" }, { 24, 24, false, false, "WAV 24 bits" },
        { 32, 32, false, false, "WAV 32 bits entiers" }, { 32, 24, false, true, "WAV 24 dans 32 (extensible)" },
        { 24, 24, true, false, "RF64 24 bits" },
    };
    for (auto& c : wavs)
    {
        const auto pcm = randomPcm (30011, 2, c.valid, (uint32_t) c.container * 7 + (uint32_t) c.valid);
        auto f = t.tempDir.getChildFile (juce::String (c.name).replaceCharacters (" ", "_") + ".wav");
        writeWav (f, &pcm, nullptr, 2, 96000, c.container, c.valid, false, c.rf64, c.ext);
        nx::StreamFormat fmt;
        const auto back = decodeAllInt (f, &fmt);
        t.check (back == pcm && fmt.bitsPerSample == c.valid && fmt.sampleRate == 96000,
                 juce::String (c.name) + " : " + juce::String ((int) back.size()) + " échantillons identiques"_t);
    }

    // float WAV 32 / 64: exact doubles
    for (int bits : { 32, 64 })
    {
        std::vector<double> x (20000);
        juce::Random r (bits);
        for (auto& v : x) v = (bits == 32 ? (double) (float) (r.nextDouble() * 2 - 1) : r.nextDouble() * 2 - 1);
        auto f = t.tempDir.getChildFile ("float" + juce::String (bits) + ".wav");
        writeWav (f, nullptr, &x, 2, 352800, bits, bits, true);
        juce::String err;
        auto d = nx::openDecoder (f, err);
        std::vector<double> back (x.size());
        const int n = d != nullptr ? d->readDouble (back.data(), (int) x.size() / 2) : 0;
        t.check (n * 2 == (int) x.size() && back == x && d->getFormat().kind == nx::SampleKind::PcmFloat,
                 "WAV " + juce::String (bits) + " bits flottant DXD : valeurs identiques");
    }

    for (const char* comp : { "NONE", "sowt" })
        for (int bits : { 16, 24 })
        {
            const auto pcm = randomPcm (12007, 2, bits, (uint32_t) bits);
            auto f = t.tempDir.getChildFile (juce::String ("aiff_") + comp + juce::String (bits) + ".aif");
            writeAiff (f, &pcm, nullptr, 2, 192000, bits, comp);
            nx::StreamFormat fmt;
            const auto back = decodeAllInt (f, &fmt);
            t.check (back == pcm && std::abs (fmt.sampleRate - 192000) < 1e-6,
                     juce::String (juce::String (comp) == "NONE" ? "AIFF " : "AIFF-C sowt ") + juce::String (bits) + " bits / 192 kHz identique");
        }

    {
        std::vector<double> x (8000);
        juce::Random r (5);
        for (auto& v : x) v = r.nextDouble() - 0.5;
        auto f = t.tempDir.getChildFile ("aifc_fl64.aif");
        writeAiff (f, nullptr, &x, 2, 384000, 64, "fl64");
        juce::String err;
        auto d = nx::openDecoder (f, err);
        std::vector<double> back (x.size());
        const int n = d != nullptr ? d->readDouble (back.data(), 4000) : 0;
        t.check (n == 4000 && back == x, "AIFF-C fl64 384 kHz identique");
    }

    // Seek: frame-accurate
    {
        const auto pcm = randomPcm (50000, 2, 24, 99);
        auto f = t.tempDir.getChildFile ("seek.wav");
        writeWav (f, &pcm, nullptr, 2, 44100, 24, 24, false);
        juce::String err;
        auto d = nx::openDecoder (f, err);
        d->seek (31234);
        std::vector<int32_t> buf (200);
        d->readInt (buf.data(), 100);
        t.check (std::equal (buf.begin(), buf.end(), pcm.begin() + 31234 * 2), "WAV : seek à l'échantillon près"_t);
    }
}

NX_TEST_SECTION (103, "Décodeurs sans perte : FLAC et ALAC (bit à bit)")
{
    for (int bits : { 16, 24 })
    {
        const auto pcm = sinePcm (48000, 2, bits, 96000, 997, 0.8);
        auto f = t.tempDir.getChildFile ("flac" + juce::String (bits) + ".flac");
        {
            juce::FlacAudioFormat flac;
            f.deleteFile();
            std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
            auto w = flac.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (96000).withNumChannels (2)
                                                   .withBitsPerSample (bits).withQualityOptionIndex (8));
            std::vector<int> l (48000), r (48000);
            for (int i = 0; i < 48000; ++i) { l[(size_t) i] = pcm[(size_t) i * 2]; r[(size_t) i] = pcm[(size_t) i * 2 + 1]; }
            const int* chans[] = { l.data(), r.data(), nullptr };
            if (w) w->write (chans, 48000);
        }
        nx::StreamFormat fmt;
        const auto back = decodeAllInt (f, &fmt);
        t.check (back == pcm && fmt.codec == "FLAC" && fmt.kind == nx::SampleKind::PcmInt,
                 "FLAC " + juce::String (bits) + " bits / 96 kHz : décodage entier identique"_t);
    }

    for (int bits : { 16, 24 })
    {
        const auto pcm = sinePcm (20000, 2, bits, 192000, 1234.5, 0.7);
        auto f = t.tempDir.getChildFile ("alac" + juce::String (bits) + ".m4a");
        writeAlacM4a (f, pcm, 2, 192000, bits);
        nx::StreamFormat fmt;
        const auto back = decodeAllInt (f, &fmt);
        t.check (back == pcm && fmt.codec == "ALAC" && fmt.bitsPerSample == bits,
                 "ALAC " + juce::String (bits) + " bits / 192 kHz (M4A) : identique");

        juce::String err;
        auto d = nx::openDecoder (f, err);
        d->seek (12345);
        std::vector<int32_t> buf (100);
        d->readInt (buf.data(), 50);
        t.check (std::equal (buf.begin(), buf.end(), pcm.begin() + 12345 * 2), "ALAC : seek au milieu d'un paquet exact");
    }
}

NX_TEST_SECTION (104, "Décodeurs DSD : DSF (LSB / MSB) et DFF")
{
    const auto dsd = dsdSine (2822400.0 * 2, 0.25, 2, 1000.0, 0.5);   // DSD128
    for (bool lsb : { true, false })
    {
        auto f = t.tempDir.getChildFile (lsb ? "dsd_lsb.dsf" : "dsd_msb.dsf");
        writeDsf (f, dsd, 2, 5644800.0, lsb);
        nx::StreamFormat fmt;
        const auto back = decodeAllDsd (f, &fmt);
        t.check (back == dsd && fmt.dsdMultiple() == 128,
                 juce::String ("DSF ") + (lsb ? "LSB-first" : "MSB-first") + " DSD128 : " + juce::String ((int) back.size()) + " octets identiques");
    }
    {
        auto f = t.tempDir.getChildFile ("dsd.dff");
        writeDff (f, dsd, 2, 5644800.0);
        nx::StreamFormat fmt;
        const auto back = decodeAllDsd (f, &fmt);
        t.check (back == dsd && fmt.codec == "DFF", "DFF DSD128 : octets identiques");

        juce::String err;
        auto d = nx::openDecoder (f, err);
        d->seek (50001);
        uint8_t buf[64];
        d->readDsd (buf, 32);
        t.check (std::equal (buf, buf + 64, dsd.begin() + 50001 * 2), "DFF : seek à l'octet près"_t);
    }
}

NX_TEST_SECTION (105, "DSP 64 bits : DSD -> DXD et rééchantillonnage sinc")
{
    for (int mult : { 64, 128 })
    {
        const double dsdRate = 44100.0 * mult;
        const auto dsd = dsdSine (dsdRate, 0.5, 1, 1000.0, 0.5);
        nx::dsp::DsdToPcm conv;
        conv.prepare (dsdRate, 352800.0, 1);
        std::vector<double> pcm ((size_t) conv.maxOutputFor ((int) dsd.size()));
        const int n = conv.process (dsd.data(), (int) dsd.size(), pcm.data(), (int) pcm.size());

        // Band-limit to 20 kHz (352.8 -> 44.1 kHz) then fit the 1 kHz sine.
        nx::dsp::Resampler down;
        down.prepare (352800.0, 44100.0, 1);
        std::vector<double> base ((size_t) down.maxOutputFor (n));
        const int m = down.process (pcm.data(), n, base.data(), (int) base.size());
        const int skip = 2000;
        const auto [amp, res] = fitSine (base.data() + skip, m - 2 * skip, 1, 44100.0, 1000.0);
        const double snr = 20.0 * std::log10 (amp / std::sqrt (2.0) / res);
        t.check (std::abs (amp - 0.5) < 0.005 && snr > (mult == 64 ? 70.0 : 85.0),
                 "DSD" + juce::String (mult) + " -> DXD 352,8 kHz : " + juce::String (conv.getNumTaps()) + " coefficients, amplitude "
                     + juce::String (amp, 4) + ", SNR 20 kHz " + juce::String (snr, 1) + " dB (modulateur d'ordre 2 du test)");
    }

    struct RCase { double in, out; };
    for (auto rc : { RCase { 44100, 352800 }, RCase { 96000, 44100 }, RCase { 44100, 48000 }, RCase { 192000, 384000 } })
    {
        nx::dsp::Resampler r;
        r.prepare (rc.in, rc.out, 1);
        const int n = (int) rc.in;   // 1 s
        std::vector<double> x ((size_t) n);
        for (int i = 0; i < n; ++i) x[(size_t) i] = 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / rc.in);
        std::vector<double> y ((size_t) r.maxOutputFor (n));
        int m = 0;
        for (int start = 0; start < n; start += 4096)   // streaming in blocks
            m += r.process (x.data() + start, juce::jmin (4096, n - start), y.data() + m, (int) y.size() - m);
        const double lat = r.getLatencyInputSamples();
        double err = 0; int cnt = 0;
        for (int k = (int) (rc.out * 0.05); k < (int) (rc.out * 0.9); ++k)
        {
            const double tIn = (double) k * rc.in / rc.out - lat;
            const double ref = 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * tIn / rc.in);
            err += (y[(size_t) k] - ref) * (y[(size_t) k] - ref); ++cnt;
        }
        const double db = 10.0 * std::log10 (err / cnt / (0.125));
        t.check (db < -140.0, "Sinc " + nx::formatRate (rc.in) + " -> " + nx::formatRate (rc.out) + " (L/M = " + juce::String (r.getL()) + "/"
                                  + juce::String (r.getM()) + ", " + juce::String (r.getTapsPerPhase()) + " coef./phase) : erreur " + juce::String (db, 1) + " dB");
    }
}

NX_TEST_SECTION (106, "Moteur : bit-perfect, gapless, changement de format")
{
    // Three 24/96 tracks with random data: the virtual DAC must receive their exact concatenation.
    std::vector<int32_t> all;
    std::vector<nx::PlayableItem> items;
    for (int k = 0; k < 3; ++k)
    {
        const auto pcm = randomPcm (40000 + 1234 * k, 2, 24, 500u + (uint32_t) k);
        auto f = t.tempDir.getChildFile ("gapless" + juce::String (k) + ".wav");
        writeWav (f, &pcm, nullptr, 2, 96000, 24, 24, false);
        all.insert (all.end(), pcm.begin(), pcm.end());
        items.push_back (EngineRig::item (f));
    }

    {
        EngineRig rig;
        rig.cap->renderFlag = &nxtest_countAllocations;
        nxtest_allocationCount = 0;
        int trackChanges = 0;
        rig.engine->onTrackChanged = [&] { ++trackChanges; };
        rig.engine->setQueue (items, 0, true);
        rig.runToEnd();

        const auto& got = rig.cap->capturedInt;
        size_t firstDiff = all.size();
        for (size_t i = 0; i < all.size(); ++i)
            if (i >= got.size() || got[i] != all[i]) { firstDiff = i; break; }
        bool tailSilent = true;
        for (size_t i = all.size(); i < got.size(); ++i) tailSilent &= got[i] == 0;

        t.check (firstDiff == all.size(), "3 pistes 24/96 enchaînées : "_t + juce::String ((int) all.size() / 2)
                                              + " trames identiques, sans trou ni recouvrement"
                                              + (firstDiff < all.size() ? " (écart à "_t + juce::String ((int) firstDiff) + ")" : juce::String()));
        t.check (tailSilent, "silence numérique après la fin de la file"_t);
        t.check (rig.cap->opens == 1, "un seul (ré)ouverture du DAC pour un album homogène (gapless)"_t);
        t.check (rig.engine->getStats().underruns.load() == 0, "aucun sous-débit"_t);
        t.check (nxtest_allocationCount.load() == 0, "0 allocation sur le thread audio (" + juce::String ((int) rig.engine->getStats().callbacks.load()) + " blocs)");
        t.check (trackChanges >= 3, "changements de piste signalés : "_t + juce::String (trackChanges));
        t.check (rig.cap->config.sampleRate == 96000, "DAC commuté automatiquement à 96 kHz"_t);
    }

    // Format change 44.1/16 -> 96/24: the DAC is reopened at the boundary, both tracks stay exact.
    {
        const auto a = randomPcm (30000, 2, 16, 1);
        const auto b = randomPcm (20000, 2, 24, 2);
        auto fa = t.tempDir.getChildFile ("cd.wav"), fb = t.tempDir.getChildFile ("hires.wav");
        writeWav (fa, &a, nullptr, 2, 44100, 16, 16, false);
        writeWav (fb, &b, nullptr, 2, 96000, 24, 24, false);

        EngineRig rig;
        std::vector<double> ratesSeen;
        rig.engine->setQueue ({ EngineRig::item (fa), EngineRig::item (fb) }, 0, true);
        rig.runToEnd (1024, 60.0, [&] (int) { if (ratesSeen.empty() || ratesSeen.back() != rig.cap->config.sampleRate) ratesSeen.push_back (rig.cap->config.sampleRate); });

        std::vector<int32_t> expected (a);
        expected.insert (expected.end(), b.begin(), b.end());
        auto got = rig.cap->capturedInt;
        // Remove the silence the DAC played while it was being reconfigured.
        std::vector<int32_t> nonSilentStart;
        bool ok = got.size() >= expected.size();
        size_t gi = 0;
        for (size_t i = 0; ok && i < a.size(); ++i) ok = got[gi++] == a[i];
        while (ok && gi + 1 < got.size() && got[gi] == 0 && got[gi + 1] == 0 && b[0] != 0) gi += 2;
        for (size_t i = 0; ok && i < b.size(); ++i) ok = gi < got.size() && got[gi++] == b[i];
        t.check (ok, "44,1 kHz / 16 bits puis 96 kHz / 24 bits : les deux pistes arrivent intactes");
        t.check (rig.cap->opens == 2 && ratesSeen.size() >= 2, "DAC rouvert à la frontière (44,1 -> 96 kHz)"_t);
    }

    // Mono 24/48 -> both channels, still bit-perfect
    {
        const auto m = randomPcm (10000, 1, 24, 77);
        auto f = t.tempDir.getChildFile ("mono.wav");
        writeWav (f, &m, nullptr, 1, 48000, 24, 24, false);
        EngineRig rig;
        rig.engine->setQueue ({ EngineRig::item (f) }, 0, true);
        rig.runToEnd();
        bool ok = rig.cap->capturedInt.size() >= m.size() * 2;
        for (size_t i = 0; ok && i < m.size(); ++i) ok = rig.cap->capturedInt[i * 2] == m[i] && rig.cap->capturedInt[i * 2 + 1] == m[i];
        t.check (ok, "mono -> deux voies, bit-perfect");
    }
}

NX_TEST_SECTION (107, "Moteur : DoP, DSD natif, DSD -> PCM")
{
    const auto dsd = dsdSine (2822400.0, 0.4, 2, 1000.0, 0.5);
    auto f = t.tempDir.getChildFile ("engine.dsf");
    writeDsf (f, dsd, 2, 2822400.0, true);

    // DoP at 176.4 kHz
    {
        EngineRig rig;
        rig.engine->setQueue ({ EngineRig::item (f) }, 0, true);
        rig.runToEnd();
        const auto& got = rig.cap->capturedInt;
        bool markersOk = true, payloadOk = true;
        size_t frames = juce::jmin (got.size() / 2, dsd.size() / 4);
        uint8_t lastMarker = 0;
        for (size_t i = 0; i < frames; ++i)
        {
            const auto wl = (uint32_t) got[i * 2], wr = (uint32_t) got[i * 2 + 1];
            const uint8_t ml = (uint8_t) (wl >> 24), mr = (uint8_t) (wr >> 24);
            markersOk &= (ml == 0x05 || ml == 0xFA) && ml == mr && ml != lastMarker && (wl & 0xFF) == 0;
            lastMarker = ml;
            payloadOk &= (uint8_t) (wl >> 16) == dsd[i * 4] && (uint8_t) (wl >> 8) == dsd[i * 4 + 2]
                      && (uint8_t) (wr >> 16) == dsd[i * 4 + 1] && (uint8_t) (wr >> 8) == dsd[i * 4 + 3];
        }
        t.check (rig.cap->config.wire == nx::WireFormat::DoP && rig.cap->config.sampleRate == 176400.0,
                 "DSD64 -> DoP à 176,4 kHz"_t);
        t.check (markersOk, "marqueurs DoP 0x05 / 0xFA alternés sur "_t + juce::String ((int) frames) + " trames");
        t.check (payloadOk, "charge DSD intacte dans les mots DoP");
        t.check (rig.engine->getSignalPath().overall == nx::PathQuality::BitPerfect, "Signal Path : bit-perfect");
    }

    // Native DSD
    {
        EngineRig rig ([] (nx::CaptureBackend& c) { c.capabilities.nativeDsdRates = { 2822400.0, 5644800.0 }; });
        rig.engine->setQueue ({ EngineRig::item (f) }, 0, true);
        rig.runToEnd (512);
        const auto& got = rig.cap->capturedDsd;
        const bool ok = got.size() >= dsd.size() && std::equal (dsd.begin(), dsd.end(), got.begin());
        t.check (rig.cap->config.wire == nx::WireFormat::NativeDsd && ok, "DSD natif : " + juce::String ((int) dsd.size()) + " octets identiques");
    }

    // DSD -> PCM when the DAC only takes 44.1 / 88.2 / 352.8 kHz and DoP is off
    {
        EngineRig rig ([] (nx::CaptureBackend& c) { c.capabilities.sampleRates = { 44100.0, 88200.0, 352800.0 }; });
        auto s = rig.engine->getSettings();
        s.dopAllowed = false;
        rig.engine->setSettings (s);
        rig.engine->setQueue ({ EngineRig::item (f) }, 0, true);
        rig.runToEnd();
        const auto& got = rig.cap->capturedInt;
        std::vector<double> l (got.size() / 2);
        for (size_t i = 0; i < l.size(); ++i) l[i] = got[i * 2] / 2147483648.0;
        const int skip = 20000;
        const auto fit = l.size() > (size_t) (3 * skip) ? fitSine (l.data() + skip, (int) l.size() - 3 * skip, 1, 352800.0, 1000.0)
                                                         : std::pair<double, double> { 0, 1 };
        t.check (rig.cap->config.sampleRate == 352800.0 && std::abs (fit.first - 0.5) < 0.01,
                 "DSD64 -> PCM 352,8 kHz : amplitude " + juce::String (fit.first, 4) + " (SACD 0 dB = −6 dBFS)"_t);
        t.check (rig.engine->getCurrentPlan().conversion == nx::PlaybackPlan::Conversion::DsdToPcm,
                 "plan : conversion DSD -> PCM choisie quand ni DoP ni DSD natif");
    }
}

NX_TEST_SECTION (108, "Moteur : seek, volume, suréchantillonnage, Signal Path")
{
    const auto pcm = randomPcm (96000 * 3, 2, 24, 4242);
    auto f = t.tempDir.getChildFile ("seek_engine.wav");
    writeWav (f, &pcm, nullptr, 2, 96000, 24, 24, false);

    {
        EngineRig rig;
        rig.engine->setQueue ({ EngineRig::item (f) }, 0, true);
        bool seeked = false;
        size_t mark = 0;
        rig.runToEnd (1024, 60.0, [&] (int block)
        {
            if (block == 20 && ! seeked)
            {
                rig.engine->seek (2.0);
                seeked = true;
                mark = rig.cap->capturedInt.size();
            }
        });
        const auto& got = rig.cap->capturedInt;
        // After the seek, find the first non-silent frame and compare with the source at 2.0 s.
        size_t i = mark;
        while (i + 1 < got.size() && got[i] == 0 && got[i + 1] == 0) i += 2;
        const size_t srcStart = 192000 * 2;
        bool ok = got.size() - i >= 1000;
        for (size_t k = 0; ok && k < 1000; ++k) ok = got[i + k] == pcm[srcStart + k];
        t.check (ok, "seek à 2,000 s : reprise exacte à l'échantillon 192 000"_t);
    }

    {
        EngineRig rig;
        rig.engine->setFixedVolume (false);
        rig.engine->setVolume (0.5);
        rig.engine->setQueue ({ EngineRig::item (f) }, 0, true);
        nx::SignalPath path;
        rig.runToEnd (1024, 60.0, [&] (int block) { if (block == 10) path = rig.engine->getSignalPath(); });
        const auto& got = rig.cap->capturedInt;
        double maxErr = 0;
        for (size_t i = 48000; i < 96000 && i < got.size(); ++i)
            maxErr = juce::jmax (maxErr, std::abs ((double) (got[i] >> 8) - (double) (pcm[i] >> 8) * 0.5));
        t.check (maxErr <= 2.0, "volume numérique −6,02 dB : erreur max "_t + juce::String (maxErr, 2) + " LSB (dither TPDF)");
        t.check (path.overall == nx::PathQuality::HighQuality, "Signal Path : volume -> « Haute qualité » (plus bit-perfect)"_t);
    }

    {
        const auto s = sinePcm (44100, 2, 24, 44100, 1000.0, 0.5);
        auto f2 = t.tempDir.getChildFile ("up.wav");
        writeWav (f2, &s, nullptr, 2, 44100, 24, 24, false);
        EngineRig rig;
        auto st = rig.engine->getSettings();
        st.upsampleToDxd = true;
        rig.engine->setSettings (st);
        rig.engine->setQueue ({ EngineRig::item (f2) }, 0, true);
        nx::SignalPath path;
        rig.runToEnd (1024, 60.0, [&] (int block) { if (block == 10) path = rig.engine->getSignalPath(); });
        const auto& got = rig.cap->capturedInt;
        std::vector<double> l (got.size() / 2);
        for (size_t i = 0; i < l.size(); ++i) l[i] = got[i * 2] / 2147483648.0;
        const int skip = 30000;
        const auto fit = l.size() > (size_t) (3 * skip) ? fitSine (l.data() + skip, (int) l.size() - 3 * skip, 1, 352800.0, 1000.0)
                                                         : std::pair<double, double> { 0, 1 };
        const double snr = fit.second > 0 ? 20 * std::log10 (fit.first / std::sqrt (2.0) / fit.second) : 0;
        t.check (rig.cap->config.sampleRate == 352800.0 && snr > 130.0,
                 "sinc 44,1 -> DXD 352,8 kHz : SNR " + juce::String (snr, 1) + " dB (limité par la quantification 24 bits)"_t);
        t.check (path.overall == nx::PathQuality::Enhanced, "Signal Path : « Amélioré »"_t);
    }

    {
        EngineRig rig;
        rig.engine->setQueue ({ EngineRig::item (f) }, 0, true);
        for (int i = 0; i < 200 && ! rig.cap->isRunning(); ++i) { rig.engine->serviceMessageThread(); juce::Thread::sleep (2); }
        const auto path = rig.engine->getSignalPath();
        juce::String chain;
        for (auto& n : path.nodes) chain << n.stage << " [" << n.title << "] ";
        t.check (path.overall == nx::PathQuality::BitPerfect && path.nodes.size() >= 3, "Signal Path bit-perfect : " + chain);
    }
}

//==============================================================================
NX_TEST_SECTION (109, "Profils matériel : Merging Anubis / RAVENNA (DXD, phase minimale, DoP Sync Safe)")
{
    // Detection
    t.check (nx::isMergingDevice ("Merging RAVENNA/AES67") && nx::isMergingDevice ("MT VAD") && nx::isMergingDevice ("ANUBIS SPS 0x1234")
             && ! nx::isMergingDevice ("RME ADI-2 Pro") && ! nx::isMergingDevice ("FiiO KA17") && ! nx::isMergingDevice ("Invader Speakers"),
             "détection : Merging / Anubis / RAVENNA / VAD, pas les autres DAC"_t);

    // Plans for each profile on an Anubis-like device (PCM up to 384 kHz, DoP-capable)
    nx::DeviceCapabilities caps;
    caps.sampleRates = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0, 352800.0, 384000.0 };
    caps.maxBits = 24;
    nx::StreamFormat dsd128; dsd128.kind = nx::SampleKind::Dsd; dsd128.sampleRate = 5644800.0; dsd128.bitsPerSample = 1; dsd128.codec = "DSF";
    nx::StreamFormat dsd512 = dsd128; dsd512.sampleRate = 22579200.0;
    nx::StreamFormat pcm441; pcm441.sampleRate = 44100.0; pcm441.bitsPerSample = 16; pcm441.codec = "FLAC";
    nx::EngineSettings base;
    const juce::String anubis = "Merging RAVENNA/AES67";

    auto ref = nx::resolveProfile (base, anubis);
    const auto pRefDsd = nx::makePlan (dsd128, caps, ref), pRefDsd512 = nx::makePlan (dsd512, caps, ref), pRefPcm = nx::makePlan (pcm441, caps, ref);
    t.check (pRefDsd.wire == nx::WireFormat::Pcm && pRefDsd.conversion == nx::PlaybackPlan::Conversion::DsdToPcm && pRefDsd.deviceRate == 352800.0
             && pRefDsd512.deviceRate == 352800.0 && ref.dsdToPcmGainDb == 6.0 && ref.limiter,
             "Studio DXD Reference : DSD128 / DSD512 -> DXD 352,8 kHz, +6 dB + limiteur, aucun DoP"_t);
    t.check (pRefPcm.conversion == nx::PlaybackPlan::Conversion::None && pRefPcm.deviceRate == 44100.0,
             "Studio DXD Reference : PCM 44,1 kHz bit-perfect, sans rééchantillonnage"_t);
    t.check (nx::pipelineLabel (pRefDsd, "Amélioré"_t) == juce::String::fromUTF8 ("DSD128 \xe2\x86\x92 DXD 352,8 kHz [Anubis Ref]"),
             "badge : "_t + nx::pipelineLabel (pRefDsd, "Amélioré"_t));

    base.anubisProfile = nx::AnubisProfile::AnalogWarmth;
    auto warm = nx::resolveProfile (base, anubis);
    const auto pWarmPcm = nx::makePlan (pcm441, caps, warm);
    t.check (pWarmPcm.conversion == nx::PlaybackPlan::Conversion::Resample && pWarmPcm.deviceRate == 352800.0
             && pWarmPcm.filterShape == nx::FilterShape::MinimumPhase && warm.warmth,
             "Analog Warmth : PCM 44,1 -> DXD 352,8 kHz, phase minimale, modelage harmonique"_t);
    t.check (nx::pipelineLabel (pWarmPcm, "Amélioré"_t) == juce::String::fromUTF8 ("PCM 44,1 \xe2\x86\x92 DXD [Warm]"),
             "badge : "_t + nx::pipelineLabel (pWarmPcm, "Amélioré"_t));

    base.anubisProfile = nx::AnubisProfile::PureDoP;
    auto dop = nx::resolveProfile (base, anubis);
    nx::StreamFormat dsd64 = dsd128; dsd64.sampleRate = 2822400.0;
    const auto pDop = nx::makePlan (dsd64, caps, dop);
    t.check (pDop.wire == nx::WireFormat::DoP && pDop.deviceRate == 176400.0 && dop.bufferFrames == 2048 && dop.maxChannels == 2 && dop.dopPrerollMs == 1500
             && nx::pipelineLabel (pDop, {}) == "DSD64 DoP [Bit-Perfect]",
             "Pure DoP : DoP 176,4 kHz, tampon 2048, 2 canaux, 1,5 s de silence DoP"_t);

    base.anubisProfile = nx::AnubisProfile::StudioDxdReference;
    t.check (nx::resolveProfile (base, "FiiO KA17").profileTag.isEmpty()
             && nx::makePlan (dsd64, caps, nx::resolveProfile (base, "FiiO KA17")).wire == nx::WireFormat::DoP,
             "autres DAC : réglages génériques inchangés (DoP)"_t);

    // RME ADI-2 (USB)
    const juce::String rme = "ADI-2 Pro (72241234)";
    t.check (nx::isRmeDevice (rme) && nx::isRmeDevice ("RME ADI-2 DAC") && ! nx::isRmeDevice ("Merging RAVENNA") && ! nx::isRmeDevice ("Thermostat")
             && nx::activeFamily (base, rme) == nx::DeviceFamily::Rme, "détection RME / ADI-2"_t);
    auto rmeDop = nx::resolveProfile (base, rme);
    const auto pRme = nx::makePlan (dsd128, caps, rmeDop);
    t.check (pRme.wire == nx::WireFormat::DoP && pRme.deviceRate == 352800.0 && rmeDop.dopPrerollMs == 150 && rmeDop.lockVolume
             && nx::makePlan (pcm441, caps, rmeDop).conversion == nx::PlaybackPlan::Conversion::None
             && nx::pipelineLabel (pRme, {}) == "DSD128 DoP [RME Bit-Perfect]",
             "RME SteadyClock DoP Native : DSD128 en DoP 352,8 kHz, synchro 150 ms, volume verrouillé, PCM bit-perfect"_t);
    base.rmeProfile = nx::RmeProfile::VelvetDxd;
    auto velvet = nx::resolveProfile (base, rme);
    const auto pVelvet = nx::makePlan (dsd128, caps, velvet);
    t.check (pVelvet.conversion == nx::PlaybackPlan::Conversion::DsdToPcm && pVelvet.deviceRate == 352800.0 && pVelvet.filterShape == nx::FilterShape::MinimumPhase
             && velvet.dsdToPcmGainDb == 6.0 && ! velvet.lockVolume
             && nx::pipelineLabel (pVelvet, {}) == juce::String::fromUTF8 ("DSD128 \xe2\x86\x92 DXD 352,8 kHz [RME Velvet]"),
             "RME AKM Velvet DXD : DSD -> DXD 352,8 kHz +6 dB, phase minimale"_t);
    base.rmeProfile = nx::RmeProfile::SteadyClockDoP;

    // Minimum-phase filter: same magnitude, energy at the start (no pre-ringing)
    {
        const auto lin = nx::dsp::designLowpass (255, 0.2, 150.0);
        const auto mp = nx::dsp::toMinimumPhase (lin);
        auto magDb = [] (const std::vector<double>& h, double f)
        {
            std::complex<double> acc;
            for (size_t n = 0; n < h.size(); ++n) acc += h[n] * std::polar (1.0, -2.0 * nx::dsp::kPi * f * (double) n);
            return 20.0 * std::log10 (std::max (std::abs (acc), 1.0e-20));
        };
        double maxPassErr = 0.0, worstStop = -1000.0;
        for (double f = 0.0; f < 0.15; f += 0.005) maxPassErr = std::max (maxPassErr, std::abs (magDb (mp, f) - magDb (lin, f)));
        for (double f = 0.26; f < 0.5; f += 0.003) worstStop = std::max (worstStop, magDb (mp, f));
        double total = 0.0, early = 0.0;
        for (size_t n = 0; n < mp.size(); ++n) { total += mp[n] * mp[n]; if (n < mp.size() / 4) early += mp[n] * mp[n]; }
        double linEarly = 0.0;
        for (size_t n = 0; n < lin.size() / 4; ++n) linEarly += lin[n] * lin[n];
        t.check (maxPassErr < 0.001 && worstStop < -120.0 && early / total > 0.97 && linEarly / total < 0.01,
                 "phase minimale : bande passante identique ("_t + juce::String (maxPassErr, 5) + " dB), réjection "
                 + juce::String (-worstStop, 0) + " dB, " + juce::String (100.0 * early / total, 2) + " % de l'énergie au début"_t);
    }

    // The whole engine on a virtual Anubis: DSD64 at −6 dBFS -> DXD with +6 dB, limited, never clipped
    const auto dsd = dsdSine (2822400.0, 0.4, 2, 1000.0, 0.5);
    auto f = t.tempDir.getChildFile ("anubis109.dsf");
    writeDsf (f, dsd, 2, 2822400.0, true);
    {
        EngineRig rig ([&] (nx::CaptureBackend& c) { c.name = anubis; });
        rig.engine->setDevice (anubis);
        rig.engine->setQueue ({ EngineRig::item (f) }, 0, true);
        bool profileNode = false;
        rig.runToEnd (1024, 60.0, [&] (int)
        {
            for (auto& n : rig.engine->getSignalPath().nodes) profileNode |= n.stage == "Profil" && n.title.contains ("Studio DXD Reference");
        });
        const auto& got = rig.cap->capturedInt;
        double peak = 0.0;
        for (auto v : got) peak = std::max (peak, std::abs (v / 2147483648.0));
        std::vector<double> l (got.size() / 2);
        for (size_t i = 0; i < l.size(); ++i) l[i] = got[i * 2] / 2147483648.0;
        const int skip = 20000;
        const auto fit = l.size() > (size_t) (3 * skip) ? fitSine (l.data() + skip, (int) l.size() - 3 * skip, 1, 352800.0, 1000.0)
                                                         : std::pair<double, double> { 0, 1 };
        t.check (rig.cap->config.wire == nx::WireFormat::Pcm && rig.cap->config.sampleRate == 352800.0,
                 "moteur : DSD64 -> DXD 352,8 kHz sur l'Anubis virtuelle, aucun DoP"_t);
        t.check (fit.first > 0.95 && peak <= 0.9773 && rig.engine->getStats().clipped == 0,
                 "+6 dB : amplitude "_t + juce::String (fit.first, 4) + ", crête " + juce::String (peak, 4) + " (limiteur −0,2 dBFS), aucun écrêtage"_t);
        t.check (profileNode, "Signal Path : profil Anubis affiché"_t);
    }

    // Pure DoP "Sync Safe": 1,5 s of DoP silence (0x69 + markers) before the first DSD byte, 2048-frame buffer
    {
        EngineRig rig ([&] (nx::CaptureBackend& c) { c.name = anubis; });
        rig.engine->setDevice (anubis);
        auto s = rig.engine->getSettings();
        s.anubisProfile = nx::AnubisProfile::PureDoP;
        rig.engine->setSettings (s);
        rig.engine->setQueue ({ EngineRig::item (f) }, 0, true);
        rig.runToEnd();
        const auto& got = rig.cap->capturedInt;
        size_t idleFrames = 0;
        bool markersOk = true;
        uint8_t last = 0;
        for (size_t i = 0; i + 1 < got.size(); i += 2)
        {
            const auto w = (uint32_t) got[i];
            const uint8_t marker = (uint8_t) (w >> 24);
            markersOk &= (marker == 0x05 || marker == 0xFA) && marker != last;
            last = marker;
            if (((w >> 16) & 0xFF) == 0x69 && ((w >> 8) & 0xFF) == 0x69) ++idleFrames; else break;
        }
        const size_t musicStart = idleFrames;
        bool payloadOk = got.size() / 2 >= musicStart + 1000;
        for (size_t i = 0; payloadOk && i < 1000; ++i)
        {
            const auto wl = (uint32_t) got[(musicStart + i) * 2];
            payloadOk &= (uint8_t) (wl >> 16) == dsd[i * 4] && (uint8_t) (wl >> 8) == dsd[i * 4 + 2];
        }
        t.check (rig.cap->config.wire == nx::WireFormat::DoP && rig.cap->config.bufferFrames == 2048 && rig.cap->config.numChannels == 2,
                 "Pure DoP : DoP 176,4 kHz, tampon matériel 2048, 2 canaux"_t);
        t.check (idleFrames >= (size_t) (1.5 * 176400.0) && markersOk && payloadOk,
                 "silence DoP de "_t + juce::String (idleFrames / 176400.0, 2) + " s (marqueurs alternés) puis DSD intact"_t);
    }
}

//==============================================================================
NX_TEST_SECTION (110, "Boucle A-B sans coupure : le DAC n'est jamais rouvert, fondu de 4 ms en PCM")
{
    bool stillRunning = false, loopWasActive = false;
    juce::uint64 reconfigAfter = 0;
    // PCM 44,1 kHz / 24 bits, 3 s of noise (every frame unique)
    const int rate = 44100;
    const auto pcm = randomPcm (rate * 3, 2, 24, 110);
    auto f = t.tempDir.getChildFile ("loop110.wav");
    writeWav (f, &pcm, nullptr, 2, (double) rate, 24, 24, false);

    EngineRig rig;
    rig.engine->setQueue ({ EngineRig::item (f) }, 0, true);
    // play 0,3 s, then loop [0,5 s, 0,8 s) for 2,5 s
    size_t mark = 0;
    juce::uint64 reconfigBefore = 0;
    double maxPos = 0.0, minPosAfterWrap = 1.0e9;
    rig.runToEnd (512, 60.0, [&] (int)
    {
        const size_t outFrames = rig.cap->capturedInt.size() / 2;
        if (mark == 0 && outFrames >= (size_t) (rate * 0.3))
        {
            mark = outFrames;
            reconfigBefore = rig.engine->getStats().reconfigurations.load();
            rig.engine->setLoop (0.5, 0.8);
            juce::Thread::sleep (250);   // the virtual DAC runs faster than real time: let the decoder refill first
        }
        else if (mark > 0 && outFrames > mark + (size_t) (rate * 0.5))
        {
            const double p = rig.engine->getPositionSeconds();
            maxPos = juce::jmax (maxPos, p);
            minPosAfterWrap = juce::jmin (minPosAfterWrap, p);
        }
        if (mark > 0 && outFrames >= mark + (size_t) (rate * 2.5) && rig.engine->getState() != nx::PlaybackEngine::State::Stopped)
        {
            stillRunning = rig.cap->isRunning();
            reconfigAfter = rig.engine->getStats().reconfigurations.load();
            loopWasActive = rig.engine->isLoopActive();
            rig.engine->stop();
        }
    });
    const auto& got = rig.cap->capturedInt;
    t.check (reconfigAfter == reconfigBefore && stillRunning && loopWasActive,
             "le périphérique n'est ni arrêté ni rouvert pendant la boucle"_t);

    // Find the loop in the output: frames of A + xf onwards repeat with a period of (B − A − xf) frames.
    const int a = (int) (0.5 * rate), b = (int) (0.8 * rate);
    const int xf = (int) (nx::PlaybackEngine::loopCrossfadeMs * 0.001 * rate);
    const int period = b - a - xf;
    const size_t frames = got.size() / 2;
    auto frameAt = [&] (size_t i, int c) { return got[i * 2 + (size_t) c]; };
    size_t first = 0;
    for (size_t i = mark; i + 64 < frames && first == 0; ++i)
    {
        bool same = true;
        for (int k = 0; k < 64 && same; ++k)
            same = frameAt (i + (size_t) k, 0) == pcm[(size_t) (a + xf + k) * 2] && frameAt (i + (size_t) k, 1) == pcm[(size_t) (a + xf + k) * 2 + 1];
        if (same) first = i;
    }
    int cycles = 0;
    bool exact = first > 0;
    for (size_t start = first; exact && start + (size_t) period < frames; start += (size_t) period, ++cycles)
        for (int k = 0; k < period - xf && exact; ++k)
            exact = frameAt (start + (size_t) k, 0) == pcm[(size_t) (a + xf + k) * 2] && frameAt (start + (size_t) k, 1) == pcm[(size_t) (a + xf + k) * 2 + 1];
    t.check (exact && cycles >= 4, juce::String (cycles) + " tours de boucle, période exacte de "_t + juce::String (period)
                                   + " trames, données bit-perfect hors des 4 ms de fondu"_t);

    // The crossfade: no jump larger than the signal's own range at the wrap (continuity of the mix)
    bool mixed = true;
    for (size_t start = first + (size_t) (period - xf); start + (size_t) xf < frames && mixed; start += (size_t) period)
    {
        const auto mid = (double) frameAt (start + (size_t) xf / 2, 0);
        const auto t0 = (double) pcm[(size_t) (b - xf + xf / 2) * 2], h0 = (double) pcm[(size_t) (a + xf / 2) * 2];
        mixed = std::abs (mid - 0.5 * (t0 + h0)) < 0.02 * 2147483648.0;
    }
    t.check (mixed, "fondu enchaîné fin -> début au milieu du raccord (moitié / moitié)"_t);
    t.check (maxPos <= 0.8 + 0.02 && minPosAfterWrap >= 0.49, "position affichée dans la boucle ("_t + juce::String (minPosAfterWrap, 2) + " – "
                                                               + juce::String (maxPos, 2) + " s)");

    rig.engine->clearLoop();
    t.check (! rig.engine->isLoopActive(), "boucle désactivée (touche L)"_t);

    // DSD in DoP: the loop cuts on DoP word boundaries, the markers keep alternating, the DAC stays locked.
    {
        const auto dsd = dsdSine (2822400.0, 1.5, 2, 1000.0, 0.5);
        auto fd = t.tempDir.getChildFile ("loop110.dsf");
        writeDsf (fd, dsd, 2, 2822400.0, true);
        EngineRig dr;
        dr.engine->setQueue ({ EngineRig::item (fd) }, 0, true);
        size_t dmark = 0;
        juce::uint64 before = 0, after = 0;
        dr.runToEnd (512, 60.0, [&] (int)
        {
            const size_t n = dr.cap->capturedInt.size() / 2;
            if (dmark == 0 && n >= 30000) { dmark = n; before = dr.engine->getStats().reconfigurations.load(); dr.engine->setLoop (0.3, 0.5); juce::Thread::sleep (250); }
            if (dmark > 0 && n >= dmark + 176400 && dr.engine->getState() != nx::PlaybackEngine::State::Stopped)
            {
                after = dr.engine->getStats().reconfigurations.load();
                dr.engine->stop();
            }
        });
        const auto& got = dr.cap->capturedInt;
        bool markers = true;
        uint8_t last = 0;
        for (size_t i = 0; i + 1 < got.size(); i += 2)
        {
            const uint8_t m = (uint8_t) ((uint32_t) got[i] >> 24);
            markers &= (m == 0x05 || m == 0xFA) && m != last;
            last = m;
        }
        t.check (dr.cap->config.wire == nx::WireFormat::DoP && markers && after == before && dmark > 0,
                 "DoP : boucle sur des mots DoP entiers, marqueurs 0x05 / 0xFA continus, DAC jamais reconfiguré"_t);
    }
}
