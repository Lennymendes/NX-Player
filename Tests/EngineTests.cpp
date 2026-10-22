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
