#include "core/Templates.h"
#include "core/ProjectFiles.h"
#include "core/Session.h"

namespace harmonia::core::templates
{
    namespace
    {
        const juce::String startTemplateKey ("startTemplate");

        juce::String legalName (const juce::String& name)
        {
            return juce::File::createLegalFileName (name.trim()).trim();
        }
    }

    juce::File getDefaultFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Harmonia").getChildFile ("Templates");
    }

    juce::String getName (const juce::File& file)
    {
        return file.getFileNameWithoutExtension();
    }

    juce::Array<juce::File> list (const juce::File& folder)
    {
        auto files = folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + project_files::extension);

        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
        {
            return getName (a).compareNatural (getName (b)) < 0;
        });

        return files;
    }

    juce::File getFileFor (const juce::String& name, const juce::File& folder)
    {
        const auto legal = legalName (name);
        return legal.isNotEmpty() ? folder.getChildFile (legal + project_files::extension) : juce::File();
    }

    juce::String save (Project& project, const juce::String& name, const juce::File& folder)
    {
        const auto file = getFileFor (name, folder);

        if (file == juce::File())
            return "Type a name for the template";

        // A copy: the song keeps its own file and its unsaved changes, and the template names
        // the song's sounds by their full path.
        return project.saveCopy (file);
    }

    std::unique_ptr<Project> createSong (Session& session, const juce::File& templateFile, juce::String& error)
    {
        return Project::loadTemplate (session, templateFile, error);
    }

    juce::File getStartTemplate (Session& session)
    {
        const auto path = session.getEngine().getPropertyStorage().getPropertiesFile().getValue (startTemplateKey);

        if (path.isEmpty() || ! juce::File::isAbsolutePath (path))
            return {};

        const juce::File file (path);
        return file.existsAsFile() ? file : juce::File();
    }

    std::unique_ptr<Project> createStartSong (Session& session, juce::String& problem)
    {
        problem.clear();
        const auto start = getStartTemplate (session);

        if (start == juce::File())
            return {};

        juce::String error;
        auto song = createSong (session, start, error);

        if (song == nullptr)
            problem = "The template \"" + getName (start) + "\" couldn't be opened (" + error + "): this is the empty song";

        return song;
    }

    void setStartTemplate (Session& session, const juce::File& file)
    {
        auto& settings = session.getEngine().getPropertyStorage().getPropertiesFile();

        if (file == juce::File())
            settings.removeValue (startTemplateKey);
        else
            settings.setValue (startTemplateKey, file.getFullPathName());

        settings.saveIfNeeded();
    }
}
