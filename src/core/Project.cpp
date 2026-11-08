#include "core/Project.h"

namespace harmonia::core
{
    namespace
    {
        const juce::Identifier formatVersionId ("hFormatVersion");
        const juce::Identifier editTag ("EDIT");
    }

    Project::Project (std::unique_ptr<te::Edit> e)
        : edit (std::move (e))
    {
        jassert (edit != nullptr);
    }

    Project::~Project() = default;

    std::unique_ptr<Project> Project::createEmpty (Session& session)
    {
        auto edit = te::createEmptyEdit (session.getEngine(), juce::File());
        edit->state.setProperty (formatVersionId, currentFormatVersion, nullptr);
        return std::unique_ptr<Project> (new Project (std::move (edit)));
    }

    std::unique_ptr<Project> Project::load (Session& session, const juce::File& file, juce::String& error)
    {
        if (! file.existsAsFile())
        {
            error = "Project file not found: " + file.getFullPathName();
            return {};
        }

        const auto xml = juce::parseXML (file);

        if (xml == nullptr || ! xml->hasTagName (editTag.toString()))
        {
            error = "Not a Harmonia project: " + file.getFullPathName();
            return {};
        }

        const auto version = xml->getIntAttribute (formatVersionId, 0);

        if (version > currentFormatVersion)
        {
            error = "This project was saved by a newer version of Harmonia (format "
                  + juce::String (version) + ")";
            return {};
        }

        auto edit = te::loadEditFromFile (session.getEngine(), file);

        if (edit == nullptr)
        {
            error = "Could not open project: " + file.getFullPathName();
            return {};
        }

        edit->state.setProperty (formatVersionId, currentFormatVersion, nullptr);
        return std::unique_ptr<Project> (new Project (std::move (edit)));
    }

    juce::String Project::save (const juce::File& file)
    {
        edit->state.setProperty (formatVersionId, currentFormatVersion, nullptr);

        if (! file.getParentDirectory().createDirectory())
            return "Cannot create folder: " + file.getParentDirectory().getFullPathName();

        if (! te::EditFileOperations (*edit).writeToFile (file, false))
            return "Cannot write project file: " + file.getFullPathName();

        return {};
    }

    int Project::getFormatVersion() const
    {
        return edit->state.getProperty (formatVersionId, 0);
    }

    int Project::getNumTracks() const
    {
        return te::getAudioTracks (*edit).size();
    }

    double Project::getTempoBpm() const
    {
        return edit->tempoSequence.getTempo (0)->getBpm();
    }
}
