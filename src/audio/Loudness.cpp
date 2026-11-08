#include "audio/Loudness.h"

#include "audio/Effects.h"
#include "audio/Export.h"
#include "audio/Limiter.h"

#include <numeric>

namespace harmonia::audio::loudness
{
    namespace
    {
        constexpr double absoluteGateLufs = -70.0;
        constexpr double relativeGateLu = -10.0;
        constexpr double stepSeconds = 0.1;         // blocks of 400 ms, overlapping by 75%
        constexpr int stepsPerBlock = 4;

        double toLufs (double meanSquare)
        {
            return meanSquare > 0.0 ? -0.691 + 10.0 * std::log10 (meanSquare) : -std::numeric_limits<double>::infinity();
        }

        struct Biquad
        {
            double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
            double z1 = 0.0, z2 = 0.0;

            double process (double x) noexcept
            {
                const auto y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return y;
            }
        };

        /** BS.1770's K-weighting: a high shelf (the head), then a high-pass, at any sample rate
            (the coefficients as libebur128 works them out). */
        std::array<Biquad, 2> makeKWeighting (double sampleRate)
        {
            std::array<Biquad, 2> filters;

            {
                constexpr double f0 = 1681.974450955533, gainDb = 3.999843853973347, q = 0.7071752369554196;
                const auto k = std::tan (juce::MathConstants<double>::pi * f0 / sampleRate);
                const auto vh = std::pow (10.0, gainDb / 20.0);
                const auto vb = std::pow (vh, 0.4996667741545416);
                const auto a0 = 1.0 + k / q + k * k;

                auto& shelf = filters[0];
                shelf.b0 = (vh + vb * k / q + k * k) / a0;
                shelf.b1 = 2.0 * (k * k - vh) / a0;
                shelf.b2 = (vh - vb * k / q + k * k) / a0;
                shelf.a1 = 2.0 * (k * k - 1.0) / a0;
                shelf.a2 = (1.0 - k / q + k * k) / a0;
            }

            {
                constexpr double f0 = 38.13547087602444, q = 0.5003270373238773;
                const auto k = std::tan (juce::MathConstants<double>::pi * f0 / sampleRate);
                const auto a0 = 1.0 + k / q + k * k;

                auto& highPass = filters[1];
                highPass.b0 = 1.0;
                highPass.b1 = -2.0;
                highPass.b2 = 1.0;
                highPass.a1 = 2.0 * (k * k - 1.0) / a0;
                highPass.a2 = (1.0 - k / q + k * k) / a0;
            }

            return filters;
        }

        /** Takes sound a block at a time; the gating is done at the end. */
        class Gauge
        {
        public:
            Gauge (double rate, int numChannels)
                : sampleRate (rate),
                  stepLength (std::max (1, juce::roundToInt (rate * stepSeconds))),
                  filters ((size_t) juce::jlimit (1, 2, numChannels), makeKWeighting (rate))
            {
            }

            void process (const juce::AudioBuffer<float>& buffer, int numSamples)
            {
                const auto channels = std::min ((int) filters.size(), buffer.getNumChannels());

                for (int i = 0; i < numSamples; ++i)
                {
                    for (int c = 0; c < channels; ++c)
                    {
                        const auto x = buffer.getSample (c, i);
                        peak = std::max (peak, std::abs (x));

                        auto y = (double) x;

                        for (auto& filter : filters[(size_t) c])
                            y = filter.process (y);

                        stepSum += y * y;
                    }

                    if (++inStep == stepLength)
                    {
                        steps.push_back (stepSum / stepLength);
                        stepSum = 0.0;
                        inStep = 0;
                    }
                }

                total += numSamples;
            }

            std::optional<Measurement> finish() const
            {
                if ((int) steps.size() < stepsPerBlock)
                    return std::nullopt;

                // The 400 ms blocks over the absolute gate, then those within 10 LU of their mean.
                std::vector<double> blocks;

                for (size_t i = 0; i + stepsPerBlock <= steps.size(); ++i)
                {
                    const auto meanSquare = std::accumulate (steps.begin() + (std::ptrdiff_t) i,
                                                             steps.begin() + (std::ptrdiff_t) (i + stepsPerBlock), 0.0) / stepsPerBlock;

                    if (toLufs (meanSquare) > absoluteGateLufs)
                        blocks.push_back (meanSquare);
                }

                if (blocks.empty())
                    return std::nullopt;

                const auto relativeGate = toLufs (std::accumulate (blocks.begin(), blocks.end(), 0.0) / (double) blocks.size()) + relativeGateLu;
                double sum = 0.0;
                int count = 0;

                for (auto meanSquare : blocks)
                {
                    if (toLufs (meanSquare) > relativeGate)
                    {
                        sum += meanSquare;
                        ++count;
                    }
                }

                Measurement m;
                m.lufs = toLufs (sum / std::max (1, count));
                m.peakDb = juce::Decibels::gainToDecibels (peak, -100.0f);
                m.seconds = (double) total / sampleRate;
                return m;
            }

        private:
            double sampleRate;
            int stepLength;
            std::vector<std::array<Biquad, 2>> filters;
            std::vector<double> steps;
            double stepSum = 0.0;
            int inStep = 0;
            float peak = 0.0f;
            juce::int64 total = 0;
        };

        juce::String dbText (double db)
        {
            return (db > 0.05 ? "+" : "") + juce::String (db, 1) + " dB";
        }
    }

    struct Meter::State : Gauge
    {
        using Gauge::Gauge;
    };

    Meter::Meter (double sampleRate, int numChannels)
        : state (std::make_unique<State> (sampleRate, numChannels))
    {
    }

    Meter::~Meter() = default;

    void Meter::process (const juce::AudioBuffer<float>& buffer, int numSamples)
    {
        state->process (buffer, numSamples);
    }

    std::optional<Measurement> Meter::finish() const
    {
        return state->finish();
    }

    std::optional<Measurement> measure (const juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        if (sampleRate <= 0.0 || buffer.getNumChannels() == 0)
            return std::nullopt;

        Meter meter (sampleRate, buffer.getNumChannels());
        meter.process (buffer, buffer.getNumSamples());
        return meter.finish();
    }

    std::optional<Measurement> measureFile (const juce::File& file)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        if (reader == nullptr || reader->sampleRate <= 0.0 || reader->numChannels == 0)
            return std::nullopt;

        Meter meter (reader->sampleRate, (int) reader->numChannels);
        juce::AudioBuffer<float> block ((int) reader->numChannels, 65536);

        for (juce::int64 position = 0; position < reader->lengthInSamples; position += block.getNumSamples())
        {
            const auto count = (int) std::min ((juce::int64) block.getNumSamples(), reader->lengthInSamples - position);
            reader->read (&block, 0, count, position, true, true);
            meter.process (block, count);
        }

        return meter.finish();
    }

    std::optional<Measurement> measureSong (te::Edit& edit, juce::String& error)
    {
        const juce::TemporaryFile temp (".wav");

        if (error = exportWav (edit, temp.getFile()); error.isNotEmpty())
            return std::nullopt;

        auto result = measureFile (temp.getFile());     // done with the file before it is deleted

        if (! result.has_value())
            error = "Nothing to measure: the song is silent";

        return result;
    }

    Advice advise (const Measurement& m, double targetLufs, float ceilingDb)
    {
        Advice advice;
        const auto wanted = (float) (targetLufs - m.lufs);
        const auto room = ceilingDb - m.peakDb;     // how far the peaks let it go up
        advice.gainDb = std::min (wanted, room);
        advice.peakLimited = room < wanted;

        auto text = "The song: " + juce::String (m.lufs, 1) + " LUFS, peaks at " + dbText (m.peakDb) + ".\n\n";
        const auto target = juce::String (targetLufs, 0) + " LUFS";

        if (std::abs (advice.gainDb) < 0.5f && ! advice.peakLimited)
        {
            text << "That's right for streaming: Spotify, YouTube and Apple Music play songs at about " << target << ".";
        }
        else if (! advice.peakLimited && advice.gainDb < 0.0f)
        {
            text << "Streaming services play songs at about " << target << ": they will turn this one down by "
                 << juce::String (-advice.gainDb, 1) << " dB anyway. Lowering the master by as much lets you hear it as it "
                 << "will sound there (and leaves the peaks more room).";
        }
        else if (! advice.peakLimited)
        {
            text << "Streaming services play songs at about " << target << ": next to others this one would sound "
                 << juce::String (advice.gainDb, 1) << " dB quieter. Raise the master by as much (the peaks will reach "
                 << dbText (m.peakDb + advice.gainDb) << ").";
        }
        else if (advice.gainDb > 0.05f)
        {
            text << "Streaming services play songs at about " << target << ", but raising this one by " << juce::String (wanted, 1)
                 << " dB would push its peaks to " << dbText (m.peakDb + wanted) << ": they would clip. Raising it by "
                 << juce::String (advice.gainDb, 1) << " dB brings the peaks to " << dbText (ceilingDb) << " (the song to "
                 << juce::String (m.lufs + advice.gainDb, 1) << " LUFS). To go all the way, use the limiter: Harmonia puts "
                 << "it on the master and raises the song to " << target << ", the peaks held under " << dbText (ceilingDb) << ".";
        }
        else
        {
            text << "Its peaks go over " << dbText (ceilingDb) << " (up to " << dbText (m.peakDb) << "): converted to MP3 or for "
                 << "streaming they may crackle. Lower the master by " << juce::String (-advice.gainDb, 1) << " dB, or use the "
                 << "limiter: it holds the peaks under " << dbText (ceilingDb) << " and brings the song to " << target << ".";
        }

        advice.text = text;
        return advice;
    }

    std::optional<LimiterResult> raiseWithLimiter (te::Edit& edit, juce::String& error, double targetLufs,
                                                   const std::function<void (const juce::String&)>& progress)
    {
        const auto say = [&progress] (const juce::String& step)
        {
            if (progress != nullptr)
                progress (step);
        };

        say ("Measuring the song...");
        const auto before = measureSong (edit, error);

        if (! before.has_value())
            return std::nullopt;

        // Harmonia's limiter on the master (the last one, if there are several); added if there's none.
        auto& chain = edit.getMasterPluginList();
        limiter::Plugin* found = nullptr;

        for (auto* plugin : effects::get (chain))
            if (auto* l = dynamic_cast<limiter::Plugin*> (plugin))
                found = l;

        if (found == nullptr)
        {
            auto& undoManager = edit.getUndoManager();
            const auto stepName = undoManager.getCurrentTransactionName();
            found = dynamic_cast<limiter::Plugin*> (effects::add (chain, { "Limiter", limiter::Plugin::xmlTypeName, {} }, error));

            if (found == nullptr)
                return std::nullopt;

            if (stepName.isNotEmpty())
                undoManager.setCurrentTransactionName (stepName);      // adding began a step of its own: keep the caller's name
        }

        float faderChange = 0.0f;

        if (auto master = edit.getMasterVolumePlugin())
        {
            faderChange = -master->getVolumeDb();
            master->setVolumeDb (0.0f);
        }

        // Limiting takes some of each step up away again: measure, correct, measure.
        constexpr int maxPasses = 4;
        auto gain = juce::jlimit (0.0f, limiter::maxGainDb,
                                  found->gain->getCurrentValue() + (float) (targetLufs - before->lufs) - faderChange);
        auto now = *before;

        for (int pass = 1;; ++pass)
        {
            found->gain->setParameter (gain, juce::sendNotificationSync);
            say ("Limiter at " + dbText (gain) + ": measuring the song again...");

            const auto measured = measureSong (edit, error);

            if (! measured.has_value())
                return std::nullopt;

            now = *measured;
            const auto miss = (float) (targetLufs - now.lufs);
            const auto next = juce::jlimit (0.0f, limiter::maxGainDb, gain + miss);

            if (std::abs (miss) < 0.25f || std::abs (next - gain) < 0.05f || pass == maxPasses)
                break;

            gain = next;
        }

        return LimiterResult { *before, now, gain };
    }

    float addToMasterVolume (te::Edit& edit, float gainDb)
    {
        auto master = edit.getMasterVolumePlugin();

        if (master == nullptr)
            return 0.0f;

        const auto before = master->getVolumeDb();
        master->setVolumeDb (before + gainDb);
        return master->getVolumeDb() - before;
    }
}
