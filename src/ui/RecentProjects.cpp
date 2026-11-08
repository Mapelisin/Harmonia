#include "ui/RecentProjects.h"

namespace harmonia::ui
{
    namespace
    {
        const char* const key = "recentProjects";
    }

    RecentProjects::RecentProjects (juce::PropertiesFile& s)
        : settings (s)
    {
    }

    void RecentProjects::add (const juce::File& file)
    {
        if (file == juce::File())
            return;

        auto paths = load();
        paths.removeString (file.getFullPathName(), true);
        paths.insert (0, file.getFullPathName());

        while (paths.size() > maxFiles)
            paths.remove (paths.size() - 1);

        save (paths);
    }

    void RecentProjects::remove (const juce::File& file)
    {
        auto paths = load();
        paths.removeString (file.getFullPathName(), true);
        save (paths);
    }

    void RecentProjects::clear()
    {
        save ({});
    }

    juce::Array<juce::File> RecentProjects::getFiles() const
    {
        juce::Array<juce::File> files;

        for (const auto& path : load())
            if (juce::File::isAbsolutePath (path) && juce::File (path).existsAsFile())
                files.add (juce::File (path));

        return files;
    }

    juce::StringArray RecentProjects::load() const
    {
        auto paths = juce::StringArray::fromLines (settings.getValue (key));
        paths.removeEmptyStrings();
        return paths;
    }

    void RecentProjects::save (const juce::StringArray& paths)
    {
        settings.setValue (key, paths.joinIntoString ("\n"));
        settings.saveIfNeeded();
    }
}
