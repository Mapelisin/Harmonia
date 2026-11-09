#include "audio/EffectChains.h"
#include "audio/Effects.h"

namespace harmonia::audio::chains
{
    namespace
    {
        const juce::Identifier chainTag ("HARMONIACHAIN");
        const juce::Identifier formatVersionId ("formatVersion");
        constexpr int formatVersion = 1;

        juce::String legalName (const juce::String& name)
        {
            return juce::File::createLegalFileName (name.trim()).trim();
        }
    }

    juce::File getDefaultFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Harmonia").getChildFile ("Chains");
    }

    juce::Array<juce::File> list (const juce::File& folder)
    {
        auto files = folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension);

        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
        {
            return getName (a).compareNatural (getName (b)) < 0;
        });

        return files;
    }

    juce::String getName (const juce::File& chain)
    {
        return chain.getFileNameWithoutExtension();
    }

    juce::File getFileFor (const juce::String& name, const juce::File& folder)
    {
        return folder.getChildFile (legalName (name) + fileExtension);
    }

    juce::String save (te::PluginList& chain, const juce::String& name, const juce::File& folder)
    {
        if (legalName (name).isEmpty())
            return "The chain needs a name";

        const auto plugins = effects::get (chain);

        if (plugins.isEmpty())
            return "There are no effects here to save";

        juce::ValueTree root (chainTag);
        root.setProperty (formatVersionId, formatVersion, nullptr);

        for (auto* plugin : plugins)
        {
            auto state = effects::makeCopyState (*plugin);

            // What it listens to (a track of this song) means nothing elsewhere.
            state.removeProperty (te::IDs::sidechainSourceID, nullptr);
            root.appendChild (state, nullptr);
        }

        const auto file = getFileFor (name, folder);

        if (! folder.createDirectory())
            return "Cannot create folder: " + folder.getFullPathName();

        const auto xml = root.createXml();

        if (xml == nullptr || ! xml->writeTo (file))
            return "Cannot write " + file.getFullPathName();

        return {};
    }

    Loaded load (te::PluginList& chain, const juce::File& file, bool replace)
    {
        Loaded result;
        const auto xml = juce::parseXML (file);
        const auto root = xml != nullptr ? juce::ValueTree::fromXml (*xml) : juce::ValueTree();

        if (! root.hasType (chainTag) || root.getNumChildren() == 0)
        {
            result.error = file.getFileName() + " isn't an effect chain";
            return result;
        }

        chain.getEdit().getUndoManager().beginNewTransaction ("Chain " + getName (file));

        if (replace)
            for (auto* plugin : effects::get (chain))
                plugin->deleteFromParent();

        for (const auto& state : root)
        {
            juce::String error;

            if (effects::addFromState (chain, state, error) != nullptr)
                ++result.added;
            else
                result.problems.add (error);
        }

        return result;
    }
}
