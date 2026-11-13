#include "ui/browser/SoundFavourites.h"

namespace harmonia::ui::browser
{
    namespace
    {
        const char* const favouritesKey = "soundBrowserFavourites";
    }

    Favourites::Favourites (core::Session& s)
        : session (s)
    {
        paths = juce::StringArray::fromLines (session.getEngine().getPropertyStorage().getPropertiesFile().getValue (favouritesKey));
        paths.removeEmptyStrings();
    }

    bool Favourites::contains (const juce::File& file) const
    {
        // Whatever the case of the letters: as Windows (and macOS) tell files apart.
        return paths.contains (file.getFullPathName(), true);
    }

    void Favourites::set (const juce::File& file, bool shouldBe)
    {
        if (file == juce::File() || contains (file) == shouldBe)
            return;

        if (shouldBe)
            paths.add (file.getFullPathName());
        else
            paths.removeString (file.getFullPathName(), true);

        auto& settings = session.getEngine().getPropertyStorage().getPropertiesFile();
        settings.setValue (favouritesKey, paths.joinIntoString ("\n"));
        settings.saveIfNeeded();
    }

    juce::Array<juce::File> Favourites::getFiles() const
    {
        juce::Array<juce::File> files;

        for (const auto& path : paths)
            if (juce::File::isAbsolutePath (path))
                if (const juce::File file (path); file.existsAsFile())
                    files.add (file);

        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
        {
            if (const auto order = a.getFileName().compareNatural (b.getFileName()); order != 0)
                return order < 0;

            return a.getFullPathName().compareNatural (b.getFullPathName()) < 0;
        });

        return files;
    }
}
