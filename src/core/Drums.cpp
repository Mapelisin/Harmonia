#include "core/Drums.h"

namespace harmonia::core::drums
{
    namespace
    {
        const juce::Identifier showNamesId ("hDrumNames");

        constexpr int firstPitch = 35;

        const char* const names[] {
            "Kick 2",           // 35
            "Kick",             // 36
            "Side stick",
            "Snare",
            "Clap",
            "Snare 2",          // 40
            "Low tom 2",
            "Closed hat",
            "Low tom",
            "Pedal hat",
            "Mid tom 2",        // 45
            "Open hat",
            "Mid tom",
            "High tom 2",
            "Crash",
            "High tom",         // 50
            "Ride",
            "China",
            "Ride bell",
            "Tambourine",
            "Splash",           // 55
            "Cowbell",
            "Crash 2",
            "Vibraslap",
            "Ride 2",
            "High bongo",       // 60
            "Low bongo",
            "Mute conga",
            "High conga",
            "Low conga",
            "High timbale",     // 65
            "Low timbale",
            "High agogo",
            "Low agogo",
            "Cabasa",
            "Maracas",          // 70
            "Short whistle",
            "Long whistle",
            "Short guiro",
            "Long guiro",
            "Claves",           // 75
            "High wood block",
            "Low wood block",
            "Mute cuica",
            "Open cuica",
            "Mute triangle",    // 80
            "Open triangle"     // 81
        };
    }

    juce::String getName (int pitch)
    {
        const auto index = pitch - firstPitch;
        return juce::isPositiveAndBelow (index, (int) std::size (names)) ? juce::String (names[index]) : juce::String();
    }

    bool looksLikeDrums (const juce::String& trackName)
    {
        for (const auto* word : { "drum", "beat", "perc", "kick", "snare", "hat" })
            if (trackName.containsIgnoreCase (word))
                return true;

        return false;
    }

    bool showsNames (const te::Track& track)
    {
        if (track.state.hasProperty (showNamesId))
            return (bool) track.state[showNamesId];

        return looksLikeDrums (track.getName());
    }

    void setShowsNames (te::Track& track, bool shouldShow)
    {
        track.state.setProperty (showNamesId, shouldShow, &track.edit.getUndoManager());
    }
}
