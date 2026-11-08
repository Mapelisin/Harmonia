#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace harmonia::ui
{
    /** The projects opened or saved lately, newest first, kept in the settings (File → Open recent). */
    class RecentProjects
    {
    public:
        explicit RecentProjects (juce::PropertiesFile&);

        static constexpr int maxFiles = 10;

        /** Puts the file first (once). */
        void add (const juce::File&);
        void remove (const juce::File&);
        void clear();

        /** Newest first; files that are gone (deleted, a drive unplugged) are left out. */
        juce::Array<juce::File> getFiles() const;

    private:
        juce::PropertiesFile& settings;

        juce::StringArray load() const;
        void save (const juce::StringArray&);
    };
}
