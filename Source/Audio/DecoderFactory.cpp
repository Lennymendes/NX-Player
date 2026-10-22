// NX Player — MIT License
// Decoder selection + a wrapper around JUCE readers (FLAC, MP3, Ogg Vorbis, and on macOS the
// CoreAudio formats such as AAC). FLAC goes through JUCE's integer path, which keeps the samples exact.
#include <algorithm>
#include "AudioDecoder.h"
#include "RawPcmDecoder.h"
#include "DsdDecoders.h"
#include "AlacDecoder.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace nx
{

namespace
{
class JuceReaderDecoder final : public AudioDecoder
{
public:
    JuceReaderDecoder (std::unique_ptr<juce::AudioFormatReader> r, const juce::String& codecName, bool isLossy)
        : reader (std::move (r))
    {
        format.numChannels = (int) reader->numChannels;
        format.sampleRate = reader->sampleRate;
        format.bitsPerSample = (int) reader->bitsPerSample;
        format.codec = codecName;
        format.lossy = isLossy;
        format.kind = (reader->usesFloatingPointData || isLossy) ? SampleKind::PcmFloat : SampleKind::PcmInt;
        if (format.kind == SampleKind::PcmFloat && format.bitsPerSample < 32) format.bitsPerSample = 32;
        intBuffers.setSize (format.numChannels, blockSize);
        floatBuffer.setSize (format.numChannels, blockSize);
    }

    StreamFormat getFormat() const override  { return format; }
    juce::int64 getLength() const override    { return reader->lengthInSamples; }
    juce::int64 getPosition() const override  { return position; }
    bool seek (juce::int64 p) override        { position = juce::jlimit<juce::int64> (0, getLength(), p); return true; }

    int readInt (int32_t* dest, int maxFrames) override
    {
        if (format.kind != SampleKind::PcmInt) return 0;
        int done = 0;
        const int ch = format.numChannels;
        while (done < maxFrames && position < getLength())
        {
            const int n = (int) std::min<juce::int64> ({ (juce::int64) (maxFrames - done), (juce::int64) blockSize, getLength() - position });
            auto** chans = reinterpret_cast<int**> (const_cast<float**> (intBuffers.getArrayOfWritePointers()));
            if (! reader->read (chans, ch, position, n, false)) break;
            for (int i = 0; i < n; ++i)
                for (int c = 0; c < ch; ++c)
                    dest[(size_t) (done + i) * ch + c] = (int32_t) chans[c][i];
            done += n;
            position += n;
        }
        return done;
    }

    int readDouble (double* dest, int maxFrames) override
    {
        if (format.kind != SampleKind::PcmFloat) return 0;
        int done = 0;
        const int ch = format.numChannels;
        while (done < maxFrames && position < getLength())
        {
            const int n = (int) std::min<juce::int64> ({ (juce::int64) (maxFrames - done), (juce::int64) blockSize, getLength() - position });
            if (! reader->read (&floatBuffer, 0, n, position, true, true)) break;
            for (int i = 0; i < n; ++i)
                for (int c = 0; c < ch; ++c)
                    dest[(size_t) (done + i) * ch + c] = (double) floatBuffer.getSample (c, i);
            done += n;
            position += n;
        }
        return done;
    }

private:
    static constexpr int blockSize = 8192;
    std::unique_ptr<juce::AudioFormatReader> reader;
    StreamFormat format;
    juce::AudioBuffer<float> intBuffers;   // reused as int storage (same size as float)
    juce::AudioBuffer<float> floatBuffer;
    juce::int64 position = 0;
};

juce::AudioFormatManager& formatManager()
{
    static std::unique_ptr<juce::AudioFormatManager> m = []
    {
        auto fm = std::make_unique<juce::AudioFormatManager>();
        fm->registerFormat (new juce::FlacAudioFormat(), false);
       #if JUCE_USE_MP3AUDIOFORMAT
        fm->registerFormat (new juce::MP3AudioFormat(), false);
       #endif
       #if JUCE_USE_OGGVORBIS
        fm->registerFormat (new juce::OggVorbisAudioFormat(), false);
       #endif
       #if JUCE_MAC || JUCE_IOS
        fm->registerFormat (new juce::CoreAudioFormat(), false);
       #endif
        return fm;
    }();
    return *m;
}

std::unique_ptr<AudioDecoder> wrapJuce (std::unique_ptr<juce::InputStream> s, const juce::String& hint, juce::String& error)
{
    auto& fm = formatManager();
    const auto ext = hint.fromLastOccurrenceOf (".", false, false).toLowerCase();

    for (int i = 0; i < fm.getNumKnownFormats(); ++i)
    {
        auto* af = fm.getKnownFormat (i);
        const bool flac = af->getFormatName().containsIgnoreCase ("flac");
       #if JUCE_USE_MP3AUDIOFORMAT
        // The MP3 reader accepts almost anything: only try it for .mp3 files.
        if (dynamic_cast<juce::MP3AudioFormat*> (af) != nullptr && ext != "mp3") continue;
       #endif
        s->setPosition (0);
        if (auto* raw = af->createReaderFor (s.get(), false))
        {
            s.release();   // the reader owns the stream now
            std::unique_ptr<juce::AudioFormatReader> r (raw);
            const bool lossy = ! flac && ! af->getFormatName().containsIgnoreCase ("wav")
                               && ! af->getFormatName().containsIgnoreCase ("aiff");
            juce::String codec = flac ? "FLAC" : (ext == "mp3" ? "MP3" : (ext == "ogg" ? "Vorbis" : ext.toUpperCase()));
            return std::make_unique<JuceReaderDecoder> (std::move (r), codec, lossy);
        }
    }
    error = "format non reconnu";
    return {};
}
} // namespace

std::unique_ptr<AudioDecoder> openDecoder (std::unique_ptr<juce::InputStream> stream, const juce::String& hint, juce::String& error)
{
    if (stream == nullptr) { error = "flux introuvable"; return {}; }

    uint8_t magic[12] {};
    stream->setPosition (0);
    stream->read (magic, 12);
    stream->setPosition (0);

    using Opener = std::unique_ptr<AudioDecoder> (*) (std::unique_ptr<juce::InputStream>&, juce::String&);
    Opener opener = nullptr;
    if (std::memcmp (magic, "RIFF", 4) == 0 || std::memcmp (magic, "RF64", 4) == 0 || std::memcmp (magic, "BW64", 4) == 0) opener = &RawPcmDecoder::openWav;
    else if (std::memcmp (magic, "FORM", 4) == 0)  opener = &RawPcmDecoder::openAiff;
    else if (std::memcmp (magic, "DSD ", 4) == 0)  opener = &DsfDecoder::open;
    else if (std::memcmp (magic, "FRM8", 4) == 0)  opener = &DffDecoder::open;
    else if (std::memcmp (magic + 4, "ftyp", 4) == 0) opener = &AlacDecoder::open;

    if (opener != nullptr)
    {
        juce::String err;
        if (auto d = opener (stream, err)) return d;
        const bool mp4 = std::memcmp (magic + 4, "ftyp", 4) == 0;
       #if JUCE_MAC || JUCE_IOS
        if (! mp4 || stream == nullptr)   // AAC in MP4: let CoreAudio decode it below
       #else
        juce::ignoreUnused (mp4);
       #endif
        {
            error = err.isNotEmpty() ? err : juce::String ("en-tête invalide"_t);
            return {};
        }
    }
    return wrapJuce (std::move (stream), hint, error);
}

std::unique_ptr<AudioDecoder> openDecoder (const juce::File& file, juce::String& error)
{
    auto in = std::make_unique<juce::FileInputStream> (file);
    if (! in->openedOk()) { error = "impossible d'ouvrir " + file.getFullPathName(); return {}; }
    auto buffered = std::make_unique<juce::BufferedInputStream> (in.release(), 1 << 16, true);
    return openDecoder (std::move (buffered), file.getFileName(), error);
}

const juce::StringArray& supportedAudioExtensions()
{
    static const juce::StringArray exts { "wav", "wave", "bwf", "rf64", "aif", "aiff", "aifc", "flac",
                                          "m4a", "alac", "mp4", "dsf", "dff", "mp3", "ogg" };
    return exts;
}

} // namespace nx
