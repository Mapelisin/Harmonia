#pragma once

#include "core/Session.h"

namespace harmonia::core
{
    /** A Harmonia project: one Tracktion Edit plus Harmonia's own metadata
        (stored as extra properties on the Edit's ValueTree). */
    class Project
    {
    public:
        /** Bumped whenever the on-disk format changes in a way older versions can't read. */
        static constexpr int currentFormatVersion = 1;

        static std::unique_ptr<Project> createEmpty (Session&);

        /** Loads a project file. Returns nullptr and fills `error` on failure. */
        static std::unique_ptr<Project> load (Session&, const juce::File&, juce::String& error);

        ~Project();

        /** Writes the project to `file`. Returns an error message, empty on success. */
        juce::String save (const juce::File& file);

        te::Edit& getEdit() noexcept                { return *edit; }
        const te::Edit& getEdit() const noexcept    { return *edit; }

        int getFormatVersion() const;
        int getNumTracks() const;
        double getTempoBpm() const;

    private:
        explicit Project (std::unique_ptr<te::Edit>);

        std::unique_ptr<te::Edit> edit;

        JUCE_DECLARE_NON_COPYABLE (Project)
    };
}
