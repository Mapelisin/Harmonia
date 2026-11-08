#include "audio/MixCheck.h"

#include "audio/Export.h"
#include "core/Tracks.h"

namespace harmonia::audio::mix_check
{
    namespace
    {
        constexpr int blockSize = 65536;

        // The rules' limits: wide on purpose, so a mix is only told off for what is clearly off.
        constexpr float clipDb = -0.1f;
        constexpr double opposedCorrelation = 0.0, wideCorrelation = 0.2;
        constexpr double heavyBassDb = 10.0, thinBassDb = -12.0;
        constexpr double harshTopDb = -6.0, dullTopDb = -36.0;
        constexpr double lostLu = 15.0;
        constexpr int maxLostTracks = 3;
        constexpr int maxListedTracks = 12;
        constexpr float maxFaderDb = 6.0f;

        juce::String dbText (double db)
        {
            return (db > 0.05 ? "+" : "") + juce::String (db, 1) + " dB";
        }

        double ratioDb (double energy, double reference)
        {
            constexpr double tiny = 1.0e-12, limit = 60.0;
            return juce::jlimit (-limit, limit, 10.0 * std::log10 ((energy + tiny) / (reference + tiny)));
        }

        /** The track a stem was made from, and its name: the number in the stem's file name
            ("Check - 03 Bass.wav") counts the song's tracks from the top. */
        std::pair<juce::String, te::EditItemID> trackOfStem (te::Edit& edit, const juce::File& stem, const juce::String& prefix)
        {
            const auto rest = stem.getFileNameWithoutExtension().fromFirstOccurrenceOf (prefix + " - ", false, false);
            const auto number = rest.upToFirstOccurrenceOf (" ", false, false).getIntValue();
            int count = 0;

            for (auto* track : te::getAudioTracks (edit))
                if (! core::tracks::isReturn (*track) && ++count == number)
                    return { track->getName().trim().isNotEmpty() ? track->getName().trim() : "Track " + juce::String (number),
                             track->itemID };

            return { rest.fromFirstOccurrenceOf (" ", false, false), {} };
        }

        /** A new folder for the check's files; deleted (with them) when it goes. */
        struct TemporaryFolder
        {
            juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                    .getNonexistentChildFile ("Harmonia mix check", {}, false);

            TemporaryFolder()       { folder.createDirectory(); }
            ~TemporaryFolder()      { folder.deleteRecursively(); }

            JUCE_DECLARE_NON_COPYABLE (TemporaryFolder)
        };
    }

    //==============================================================================
    void BalanceMeter::Steep::setCoefficients (const juce::IIRCoefficients& coefficients) noexcept
    {
        first.setCoefficients (coefficients);
        second.setCoefficients (coefficients);
    }

    float BalanceMeter::Steep::process (float x) noexcept
    {
        return second.processSingleSampleRaw (first.processSingleSampleRaw (x));
    }

    BalanceMeter::BalanceMeter (double sampleRate)
    {
        low.setCoefficients (juce::IIRCoefficients::makeLowPass (sampleRate, lowSplitHz));
        middleHighPass.setCoefficients (juce::IIRCoefficients::makeHighPass (sampleRate, lowSplitHz));
        middleLowPass.setCoefficients (juce::IIRCoefficients::makeLowPass (sampleRate, highSplitHz));
        high.setCoefficients (juce::IIRCoefficients::makeHighPass (sampleRate, highSplitHz));
    }

    void BalanceMeter::process (const juce::AudioBuffer<float>& buffer, int numSamples)
    {
        if (buffer.getNumChannels() == 0)
            return;

        const auto* left = buffer.getReadPointer (0);
        const auto* right = buffer.getReadPointer (buffer.getNumChannels() > 1 ? 1 : 0);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto l = (double) left[i], r = (double) right[i];
            leftRight += l * r;
            leftLeft += l * l;
            rightRight += r * r;

            const auto both = 0.5f * (left[i] + right[i]);
            const auto lowPart = (double) low.process (both);
            const auto middlePart = (double) middleLowPass.process (middleHighPass.process (both));
            const auto highPart = (double) high.process (both);
            lowEnergy += lowPart * lowPart;
            middleEnergy += middlePart * middlePart;
            highEnergy += highPart * highPart;
        }
    }

    Balance BalanceMeter::getBalance() const
    {
        Balance balance;
        balance.correlation = leftLeft > 0.0 && rightRight > 0.0 ? juce::jlimit (-1.0, 1.0, leftRight / std::sqrt (leftLeft * rightRight))
                                                                 : 1.0;
        balance.lowDb = ratioDb (lowEnergy, middleEnergy);
        balance.highDb = ratioDb (highEnergy, middleEnergy);
        return balance;
    }

    //==============================================================================
    std::optional<Report> check (te::Edit& edit, juce::String& error, const std::function<void (const juce::String&)>& progress)
    {
        const auto say = [&progress] (const juce::String& step)
        {
            if (progress != nullptr)
                progress (step);
        };

        Report report;

        // The mix: its loudness and balance, in one pass over the rendered song.
        say ("Mix Doctor: listening to the mix...");

        {
            const juce::TemporaryFile mixFile (".wav");

            if (error = exportWav (edit, mixFile.getFile()); error.isNotEmpty())
            {
                error = error.replace ("Nothing to export", "Nothing to check");
                return std::nullopt;
            }

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (mixFile.getFile()));

            if (reader == nullptr || reader->sampleRate <= 0.0 || reader->numChannels == 0)
            {
                error = "Can't read the rendered song back";
                return std::nullopt;
            }

            loudness::Meter loudnessMeter (reader->sampleRate, (int) reader->numChannels);
            BalanceMeter balanceMeter (reader->sampleRate);
            juce::AudioBuffer<float> block ((int) reader->numChannels, blockSize);

            for (juce::int64 position = 0; position < reader->lengthInSamples; position += blockSize)
            {
                const auto count = (int) std::min ((juce::int64) blockSize, reader->lengthInSamples - position);
                reader->read (&block, 0, count, position, true, true);
                loudnessMeter.process (block, count);
                balanceMeter.process (block, count);
            }

            reader.reset();     // let go of the file before it is deleted
            const auto measured = loudnessMeter.finish();

            if (! measured.has_value())
            {
                error = "Nothing to check: the song is silent";
                return std::nullopt;
            }

            report.mix = *measured;
            report.balance = balanceMeter.getBalance();
        }

        // Each track as it is heard: with its effects and what it sends to the reverb or delay.
        {
            const TemporaryFolder stems;
            const juce::String prefix ("Check");
            const auto result = exportStems (edit, stems.folder, prefix, [&say] (int done, int total)
            {
                if (done < total)
                    say ("Mix Doctor: listening to track " + juce::String (done + 1) + " of " + juce::String (total) + "...");
            });

            if (result.error.isNotEmpty())
            {
                error = result.error.replace ("Nothing to export", "Nothing to check");
                return std::nullopt;
            }

            for (const auto& file : result.files)
            {
                const auto [name, id] = trackOfStem (edit, file, prefix);
                report.tracks.push_back ({ name, loudness::measureFile (file), id });
            }
        }

        std::stable_sort (report.tracks.begin(), report.tracks.end(), [] (const TrackLevel& a, const TrackLevel& b)
        {
            const auto levelOf = [] (const TrackLevel& t) { return t.level.has_value() ? t.level->lufs : -1000.0; };
            return levelOf (a) > levelOf (b);
        });

        return report;
    }

    Fixes findFixes (const Report& report, te::Edit& edit)
    {
        Fixes fixes;
        fixes.clips = report.mix.peakDb > clipDb;

        // The same tracks findProblems calls lost.
        const auto loudest = std::find_if (report.tracks.begin(), report.tracks.end(),
                                           [] (const TrackLevel& t) { return t.level.has_value(); });

        if (loudest != report.tracks.end())
        {
            int lost = 0;

            for (auto it = std::next (loudest); it != report.tracks.end() && lost < maxLostTracks; ++it)
            {
                if (! it->level.has_value())
                    continue;

                const auto below = loudest->level->lufs - it->level->lufs;

                if (below <= lostLu)
                    continue;

                ++lost;
                auto* track = dynamic_cast<te::AudioTrack*> (te::findTrackForID (edit, it->track));
                auto* volume = track != nullptr ? track->getVolumePlugin() : nullptr;

                if (volume == nullptr)
                    continue;

                const auto up = std::min ((float) (below - raisedLu), maxFaderDb - volume->getVolumeDb());

                if (up >= 0.5f)
                {
                    fixes.raise.emplace_back (it->track, up);
                    fixes.descriptions.add ("raise \"" + it->name + "\" by " + juce::String (up, 1) + " dB");
                }
            }
        }

        if (fixes.clips)
            fixes.descriptions.add ("bring the master down until the peaks are at " + dbText (loudness::peakCeilingDb));

        return fixes;
    }

    juce::StringArray applyFixes (te::Edit& edit, const Fixes& fixes, juce::String& error,
                                  const std::function<void (const juce::String&)>& progress)
    {
        juce::StringArray done;
        bool raised = false;

        for (const auto& [id, gainDb] : fixes.raise)
        {
            auto* track = dynamic_cast<te::AudioTrack*> (te::findTrackForID (edit, id));
            auto* volume = track != nullptr ? track->getVolumePlugin() : nullptr;

            if (volume == nullptr)
                continue;

            const auto before = volume->getVolumeDb();
            volume->setVolumeDb (std::min (maxFaderDb, before + gainDb));

            if (const auto added = volume->getVolumeDb() - before; added > 0.05f)
            {
                done.add ("\"" + track->getName() + "\" " + dbText (added));
                raised = true;
            }
        }

        if (! fixes.clips && ! raised)
            return done;

        // The peaks to the ceiling: over 0 dB the render can't say by how much, so down 3 dB, then
        // 6, then 12, listening again each time; under it, exactly to the ceiling (back up a little
        // if it went down too far, never above where the master was).
        auto lowered = 0.0f, step = 3.0f;

        for (int attempt = 0; attempt < 6; ++attempt)
        {
            if (progress != nullptr)
                progress ("Mix Doctor: listening to the mix again...");

            const auto measured = loudness::measureSong (edit, error);

            if (! measured.has_value())
                break;

            if (measured->peakDb > clipDb)
            {
                lowered -= loudness::addToMasterVolume (edit, -step);
                step *= 2.0f;
                continue;
            }

            if (measured->peakDb > loudness::peakCeilingDb + 0.05f || lowered > 0.05f)
                lowered -= loudness::addToMasterVolume (edit, std::min (loudness::peakCeilingDb - measured->peakDb, lowered));

            break;
        }

        if (lowered > 0.05f)
            done.insert (0, "master " + dbText (-lowered));

        return done;
    }

    juce::StringArray findProblems (const Report& report)
    {
        juce::StringArray problems;
        const auto& balance = report.balance;

        if (report.mix.peakDb > clipDb)
            problems.add ("It clips: the peaks hit 0 dB, the most a file can hold, so the loudest moments are cut off "
                          "and crackle. Lower the master (or the loudest tracks) until File > Check loudness shows the "
                          "peaks under -1 dB, or put a compressor or limiter on the master.");

        if (balance.correlation < opposedCorrelation)
            problems.add ("Left and right work against each other (correlation " + juce::String (balance.correlation, 2)
                          + "): on a phone or any one speaker, parts of the song will vanish. Look for a very wide stereo "
                            "effect, or a sound doubled with its polarity flipped.");
        else if (balance.correlation < wideCorrelation)
            problems.add ("Very wide stereo (correlation " + juce::String (balance.correlation, 2)
                          + "): listen to it in mono (on one speaker); some parts may get much quieter.");

        if (balance.lowDb > heavyBassDb)
            problems.add ("A lot of bass: below " + juce::String ((int) lowSplitHz) + " Hz is " + juce::String (balance.lowDb, 1)
                          + " dB over the middle. On big speakers it may boom, and it takes up room the mix could be louder "
                            "with. Lower the bass or the kick, or cut the lows of the other tracks with an EQ.");
        else if (balance.lowDb < thinBassDb)
            problems.add ("Thin: hardly any bass (below " + juce::String ((int) lowSplitHz) + " Hz is " + juce::String (-balance.lowDb, 1)
                          + " dB under the middle). If the song should have a low end, a bass part or a fuller kick would "
                            "give it one.");

        if (balance.highDb > harshTopDb)
            problems.add ("Bright, maybe harsh: above " + juce::String ((int) (highSplitHz / 1000.0)) + " kHz is "
                          + (balance.highDb >= 0.0 ? juce::String (balance.highDb, 1) + " dB over"
                                                   : "only " + juce::String (-balance.highDb, 1) + " dB under")
                          + " the middle. Tame the hi-hats or cymbals, or the top of the "
                            "brightest track, with an EQ.");
        else if (balance.highDb < dullTopDb)
            problems.add ("Dull: almost nothing above " + juce::String ((int) (highSplitHz / 1000.0)) + " kHz ("
                          + juce::String (-balance.highDb, 1) + " dB under the middle). Hi-hats, a brighter sound or a little EQ on "
                            "top would open it up.");

        // A track far under the loudest one may be lost.
        const auto loudest = std::find_if (report.tracks.begin(), report.tracks.end(),
                                           [] (const TrackLevel& t) { return t.level.has_value(); });

        if (loudest != report.tracks.end())
        {
            int lost = 0;

            for (auto it = std::next (loudest); it != report.tracks.end() && lost < maxLostTracks; ++it)
            {
                if (! it->level.has_value())
                    continue;

                if (const auto below = loudest->level->lufs - it->level->lufs; below > lostLu)
                {
                    problems.add ("\"" + it->name + "\" is " + juce::String (juce::roundToInt (below)) + " LU quieter than \""
                                  + loudest->name + "\": it may get lost. Raise it, unless it's meant to be that quiet.");
                    ++lost;
                }
            }
        }

        return problems;
    }

    juce::String describe (const Report& report)
    {
        const auto& balance = report.balance;
        juce::String text;
        text << "The mix: " << juce::String (report.mix.lufs, 1) << " LUFS, peaks at " << dbText (report.mix.peakDb)
             << ". Stereo correlation " << juce::String (balance.correlation, 2) << ". Bass " << dbText (balance.lowDb)
             << " and top " << dbText (balance.highDb) << " against the middle.\n\n";

        const auto problems = findProblems (report);

        if (problems.isEmpty())
            text << "Nothing stands out: no clipping, the stereo holds up in mono, the bass and the top are within the "
                    "usual range, no track is lost under the others.\n";
        else
            for (const auto& problem : problems)
                text << juce::String::fromUTF8 ("\xe2\x80\xa2 ") << problem << "\n";

        if (! report.tracks.empty())
        {
            text << "\nTracks, loudest first:\n";
            int listed = 0;

            for (const auto& track : report.tracks)
            {
                if (++listed > maxListedTracks)
                {
                    text << "... and " << juce::String ((int) report.tracks.size() - maxListedTracks) << " more\n";
                    break;
                }

                text << track.name << ": "
                     << (track.level.has_value() ? juce::String (track.level->lufs, 1) + " LUFS" : juce::String ("silent")) << "\n";
            }
        }

        text << "\nFor the level on Spotify or YouTube: File > Check loudness for streaming.";
        return text;
    }
}
