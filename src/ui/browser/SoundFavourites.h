#pragma once

#include "core/Session.h"

namespace harmonia::ui::browser
{
    /** The sounds and MIDI files the user marked with a star in the sound browser. They are the
        user's, not a song's: kept in the program's settings, as the full paths of the files. A
        file that isn't there (moved away, or on a drive that isn't plugged in) stays a favourite:
        it is only left out of what is listed until it is back. */
    class Favourites final
    {
    public:
        explicit Favourites (core::Session&);

        bool contains (const juce::File&) const;

        /** Marks or unmarks a file, and keeps that. */
        void set (const juce::File&, bool shouldBe);

        /** The favourites that are there now, by name. */
        juce::Array<juce::File> getFiles() const;

        bool isEmpty() const noexcept       { return paths.isEmpty(); }

    private:
        core::Session& session;
        juce::StringArray paths;

        JUCE_DECLARE_NON_COPYABLE (Favourites)
    };
}
