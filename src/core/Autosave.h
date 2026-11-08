#pragma once

#include "core/Project.h"

namespace harmonia::core
{
    /** Keeps a copy of the open project's unsaved work, so that a crash (a plugin taking Harmonia
        down, a power cut) loses at most the last half minute of it. The copy goes away once the
        work is saved or deliberately dropped, and when Harmonia closes normally: a copy found at
        start means Harmonia didn't close normally, and the work can be restored. */
    class Autosave
    {
    public:
        /** Copies go into `folder` (created when needed). */
        explicit Autosave (const juce::File& folder = getDefaultFolder());

        /** %APPDATA%\Harmonia\Autosave */
        static juce::File getDefaultFolder();

        /** A copy at most this often while changes keep coming. */
        static constexpr int defaultIntervalMs = 30000;

        /** Call regularly (the main window does twice a second). Copies the project when it has
            changes that aren't saved or copied yet: at once after a quiet spell, else at most once
            per interval. Removes the copy once nothing is unsaved. `original` is the project's own
            file (none if it was never saved). Returns what went wrong, empty if nothing did. */
        juce::String update (Project&, const juce::File& original, juce::Time now = juce::Time::getCurrentTime());

        /** Another project took the open one's place (New, Open, restored work): the next update
            copies it at once if it has unsaved changes, else removes the old copy. */
        void projectReplaced();

        /** Removes the copy: Harmonia is closing normally. */
        void clear();

        //==============================================================================
        /** Work left by a Harmonia that didn't close normally. */
        struct Leftover
        {
            juce::File copy;
            juce::File original;     // the project's own file; none if it was never saved
            juce::Time savedAt;

            /** The project's name, "Untitled" if it was never saved. */
            juce::String getName() const;
        };

        std::optional<Leftover> findLeftover() const;

        /** Opens leftover work as the project it was, with unsaved changes. The copy stays until
            the next update replaces it. */
        static std::unique_ptr<Project> restore (Session&, const Leftover&, juce::String& error);

        /** Leftover work isn't wanted: it's kept once more as getDiscardedFile() (just in case),
            and no longer offered. */
        void discard();

        juce::File getCopyFile() const;
        juce::File getDiscardedFile() const;

    private:
        juce::File folder;
        std::optional<juce::uint64> copiedChange;   // the project's change count at the last copy
        juce::Time lastCopy;

        juce::File getInfoFile() const;
    };
}
