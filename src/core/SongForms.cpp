#include "core/SongForms.h"

#include "core/Clips.h"
#include "core/Drums.h"
#include "core/Markers.h"
#include "core/Sections.h"
#include "core/Tracks.h"

namespace harmonia::core::song_forms
{
    namespace
    {
        constexpr double tolerance = 1.0e-6;
        constexpr double barsPerPart = 8.0;
        constexpr double bassUnder = 48.0;      // C3

        juce::Array<te::Clip*> clipsWithin (te::AudioTrack& track, double startBeat, double endBeat)
        {
            juce::Array<te::Clip*> within;

            for (auto* clip : clips::getClips (track))
                if (clips::getStartBeat (*clip) >= startBeat - tolerance && clips::getEndBeat (*clip) <= endBeat + tolerance)
                    within.add (clip);

            return within;
        }

        /** The average pitch of the notes the track plays in the stretch (none: no notes). */
        std::optional<double> averagePitch (te::AudioTrack& track, double startBeat, double endBeat)
        {
            double sum = 0.0;
            int count = 0;

            for (auto* c : clips::getClips (track))
                if (auto* clip = dynamic_cast<te::MidiClip*> (c))
                    for (const auto& played : clips::getPlayedNotes (*clip))
                        if (! played.note.muted && played.endBeat > startBeat + tolerance && played.startBeat < endBeat - tolerance)
                        {
                            sum += played.note.pitch;
                            ++count;
                        }

            return count > 0 ? std::optional<double> (sum / count) : std::nullopt;
        }
    }

    juce::String getName (Role role)
    {
        switch (role)
        {
            case Role::drums:   return "drums";
            case Role::bass:    return "bass";
            case Role::harmony: return "harmony";
            case Role::lead:    return "lead";
        }

        return {};
    }

    std::vector<std::pair<te::AudioTrack*, Role>> getRoles (te::Edit& edit, double startBeat, double endBeat)
    {
        std::vector<std::pair<te::AudioTrack*, Role>> roles;
        std::vector<std::pair<te::AudioTrack*, double>> pitched;    // the others, with their average pitch

        for (auto* track : te::getAudioTracks (edit))
        {
            if (tracks::isReturn (*track) || clipsWithin (*track, startBeat, endBeat).isEmpty())
                continue;

            if (drums::showsNames (*track))
                roles.emplace_back (track, Role::drums);
            else
                pitched.emplace_back (track, averagePitch (*track, startBeat, endBeat).value_or (60.0));
        }

        std::stable_sort (pitched.begin(), pitched.end(), [] (const auto& a, const auto& b) { return a.second < b.second; });

        auto first = pitched.begin(), last = pitched.end();

        if (first != last && first->second < bassUnder && ! tracks::isAudio (*first->first))
            roles.emplace_back ((first++)->first, Role::bass);

        if (std::distance (first, last) >= 2)
            roles.emplace_back ((--last)->first, Role::lead);

        for (auto it = first; it != last; ++it)
            roles.emplace_back (it->first, Role::harmony);

        return roles;
    }

    const std::vector<Form>& getForms()
    {
        using enum Role;
        static const std::vector<Form> forms {
            { "Pop: intro, verse, chorus, verse, chorus, outro",
              { { "Intro", 0.5, { harmony } },
                { "Verse", 1.0, { drums, bass, harmony } },
                { "Chorus", 1.0, { drums, bass, harmony, lead } },
                { "Verse", 1.0, { drums, bass, harmony } },
                { "Chorus", 1.0, { drums, bass, harmony, lead } },
                { "Outro", 0.5, { harmony, lead } } } },
            { "Dance: intro, build, drop, break, drop, outro",
              { { "Intro", 1.0, { drums, harmony } },
                { "Build", 0.5, { harmony, lead } },
                { "Drop", 1.0, { drums, bass, harmony, lead } },
                { "Break", 0.5, { harmony, lead } },
                { "Drop", 1.0, { drums, bass, harmony, lead } },
                { "Outro", 0.5, { drums, harmony } } } },
            { "Short: intro, main, break, main, outro",
              { { "Intro", 0.5, { harmony } },
                { "Main", 1.0, { drums, bass, harmony, lead } },
                { "Break", 0.5, { harmony, lead } },
                { "Main", 1.0, { drums, bass, harmony, lead } },
                { "Outro", 0.5, { harmony } } } }
        };

        return forms;
    }

    int getLoops (const Section& section, double loopBars)
    {
        const auto perPart = std::max (1.0, std::round (barsPerPart / std::max (1.0e-3, loopBars)));
        return std::max (1, juce::roundToInt (section.length * perPart));
    }

    double build (te::Edit& edit, double startBeat, double endBeat, const Form& form, double beatsPerBar)
    {
        startBeat = std::max (0.0, startBeat);
        const auto loopLength = endBeat - startBeat;

        if (loopLength <= tolerance || form.sections.empty())
            return 0.0;

        sections::splitAt (edit, startBeat);
        sections::splitAt (edit, endBeat);

        const auto roles = getRoles (edit, startBeat, endBeat);

        if (roles.empty())
            return 0.0;

        // The loop's clips, track by track (kept alive while the song is laid out).
        std::vector<std::pair<Role, std::vector<te::Clip::Ptr>>> loop;

        for (const auto& [track, role] : roles)
        {
            std::vector<te::Clip::Ptr> clips;

            for (auto* clip : clipsWithin (*track, startBeat, endBeat))
                clips.push_back (clip);

            loop.emplace_back (role, std::move (clips));
        }

        std::vector<int> loops;
        auto total = 0;

        for (const auto& section : form.sections)
        {
            loops.push_back (getLoops (section, loopLength / std::max (1.0, beatsPerBar)));
            total += loops.back();
        }

        // Room for the song; the loop's own markers go (the sections bring theirs).
        sections::insertSpace (edit, endBeat, (total - 1) * loopLength);

        for (const auto& marker : markers::getAll (edit))
            if (marker.beat >= startBeat - tolerance && marker.beat < endBeat - tolerance)
                markers::remove (edit, marker);

        const auto plays = [] (const Section& section, Role role, const std::vector<Role>& present)
        {
            const auto has = [] (const std::vector<Role>& list, Role r) { return std::find (list.begin(), list.end(), r) != list.end(); };
            const auto anyPresent = std::any_of (section.roles.begin(), section.roles.end(),
                                                 [&] (Role r) { return has (present, r); });
            return ! anyPresent || has (section.roles, role);
        };

        std::vector<Role> present;

        for (const auto& entry : roles)
            present.push_back (entry.second);

        auto position = startBeat;
        auto keepOriginals = std::vector<bool> (loop.size(), false);

        for (size_t s = 0; s < form.sections.size(); ++s)
        {
            const auto& section = form.sections[s];
            markers::add (edit, position, section.name);

            for (int repeat = 0; repeat < loops[s]; ++repeat, position += loopLength)
            {
                for (size_t t = 0; t < loop.size(); ++t)
                {
                    if (! plays (section, loop[t].first, present))
                        continue;

                    // The first time round is the loop itself.
                    if (std::abs (position - startBeat) < tolerance)
                    {
                        keepOriginals[t] = true;
                        continue;
                    }

                    for (auto& clip : loop[t].second)
                        clips::duplicate (*clip, clips::getStartBeat (*clip) - startBeat + position, true);
                }
            }
        }

        // Parts the first section leaves out: the loop's own clips go.
        juce::Array<te::Clip*> unused;

        for (size_t t = 0; t < loop.size(); ++t)
            if (! keepOriginals[t])
                for (auto& clip : loop[t].second)
                    unused.add (clip.get());

        clips::remove (unused);
        return total * loopLength;
    }
}
