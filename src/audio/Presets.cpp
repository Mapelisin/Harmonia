#include "audio/Presets.h"
#include "audio/DrumKit.h"
#include "audio/Effects.h"

namespace harmonia::audio::presets
{
    namespace
    {
        const juce::Identifier presetTag ("HARMONIAPRESET");
        const juce::Identifier formatVersionId ("formatVersion");
        const juce::Identifier forPluginId ("plugin");          // which plugin the preset is for
        const juce::Identifier pluginNameId ("pluginName");
        const juce::Identifier lastUsedId ("hPreset");          // on the plugin's node in the song
        constexpr int formatVersion = 1;

        /** Properties of a plugin's node that are not its settings: what and where it is, its window,
            its place in the song. */
        bool isSetting (const juce::Identifier& property)
        {
            static const juce::Identifier others[] {
                te::IDs::type, te::IDs::id, te::IDs::enabled, te::IDs::process, te::IDs::frozen,
                te::IDs::quickParamName, te::IDs::customName, te::IDs::masterPluginID, te::IDs::sidechainSourceID,
                te::IDs::windowX, te::IDs::windowY, te::IDs::parameters,
                te::IDs::srcChan, te::IDs::dstChan, lastUsedId };

            return std::find (std::begin (others), std::end (others), property) == std::end (others);
        }

        te::ExternalPlugin* asExternal (te::Plugin& plugin)
        {
            return dynamic_cast<te::ExternalPlugin*> (&plugin);
        }

        /** What a preset is for: a built-in plugin's type, or a plugin's format, name and ID (not
            where its file is: moved, it keeps its presets). */
        juce::String getIdentity (te::Plugin& plugin)
        {
            if (auto* external = asExternal (plugin))
                return external->desc.pluginFormatName + ":" + external->desc.name + ":"
                     + juce::String::toHexString (external->desc.uniqueId);

            return plugin.getPluginType();
        }

        /** The plugin's settings as a preset keeps them, or an invalid tree and the reason. */
        juce::ValueTree capture (te::Plugin& plugin, juce::String& error)
        {
            if (auto* external = asExternal (plugin))
            {
                auto* instance = external->getAudioPluginInstance();

                if (instance == nullptr)
                {
                    error = getPluginName (plugin) + " isn't loaded";
                    return {};
                }

                juce::ValueTree settings (te::IDs::PLUGIN);

                if (instance->getNumPrograms() > 0)
                    settings.setProperty (te::IDs::programNum, instance->getCurrentProgram(), nullptr);

                juce::MemoryBlock chunk;
                instance->suspendProcessing (true);
                instance->getStateInformation (chunk);
                instance->suspendProcessing (false);

                if (chunk.isEmpty())
                {
                    error = getPluginName (plugin) + " gave no settings to keep";
                    return {};
                }

                settings.setProperty (te::IDs::state, chunk.toBase64Encoding(), nullptr);
                return settings;
            }

            // A built-in plugin's settings are its node's properties (and the synth's modulation).
            auto settings = plugin.state.createCopy();

            for (int i = settings.getNumProperties(); --i >= 0;)
                if (const auto name = settings.getPropertyName (i); ! isSetting (name))
                    settings.removeProperty (name, nullptr);

            // And the drum machine's own samples.
            for (int i = settings.getNumChildren(); --i >= 0;)
                if (! settings.getChild (i).hasType (te::IDs::MODMATRIX) && ! settings.getChild (i).hasType (drum_kit::getSamplesTag()))
                    settings.removeChild (i, nullptr);

            return settings;
        }

        /** Puts settings into a plugin that keeps them itself (a VST3), not in the song: undo
            alone can't take them back. Finds the plugin by its ID, as it may be a new object by
            the time of an undo. */
        class SetPluginState final : public juce::UndoableAction
        {
        public:
            SetPluginState (te::ExternalPlugin& plugin, juce::ValueTree settingsBefore, juce::ValueTree settingsAfter, bool done)
                : edit (plugin.edit), pluginID (plugin.itemID),
                  before (std::move (settingsBefore)), after (std::move (settingsAfter)), alreadyDone (done)
            {
            }

            bool perform() override
            {
                // The first time, the change may have been made already (the plugin's own preset).
                if (! std::exchange (alreadyDone, false))
                    apply (after);

                return true;
            }

            bool undo() override
            {
                apply (before);
                return true;
            }

            int getSizeInUnits() override
            {
                // Settings can be large (a sampler's): an undo step of them counts for their size.
                return 10 + (int) ((sizeOf (before) + sizeOf (after)) / 1024);
            }

        private:
            te::Edit& edit;
            const te::EditItemID pluginID;
            const juce::ValueTree before, after;
            bool alreadyDone;

            static size_t sizeOf (const juce::ValueTree& settings)
            {
                return (size_t) settings[te::IDs::state].toString().length();
            }

            void apply (const juce::ValueTree& settings)
            {
                auto plugin = te::findPluginForID (edit, pluginID);
                auto* external = plugin != nullptr ? asExternal (*plugin) : nullptr;

                if (external == nullptr || ! settings.isValid())
                    return;

                external->restorePluginStateFromValueTree (settings);

                // The song's copy follows: a plugin set up again (another sample rate) starts from it.
                // Not as an undo step of its own: this one is.
                for (const auto& id : { te::IDs::state, te::IDs::programNum })
                    if (settings.hasProperty (id))
                        external->state.setProperty (id, settings[id], nullptr);
            }
        };

        /** Puts settings into the plugin, into the current undo step. */
        void apply (te::Plugin& plugin, const juce::ValueTree& settings)
        {
            if (auto* external = asExternal (plugin))
            {
                juce::String ignored;
                auto before = capture (*external, ignored);
                plugin.edit.getUndoManager().perform (new SetPluginState (*external, std::move (before), settings, false));
                return;
            }

            // The built-in plugins take their settings as properties of their node, which undo puts back.
            plugin.restorePluginStateFromValueTree (settings);

            for (auto* parameter : plugin.getAutomatableParameters())
                parameter->updateFromAttachedValue();
        }

        bool isBuiltIn (te::Plugin& plugin)
        {
            const auto type = plugin.getPluginType();

            if (type == te::FourOscPlugin::xmlTypeName || type == drum_kit::Plugin::xmlTypeName)
                return true;

            for (const auto& effect : effects::getBuiltIn())
                if (effect.builtInType == type)
                    return true;

            return false;
        }

        juce::String legalName (const juce::String& name)
        {
            return juce::File::createLegalFileName (name.trim()).trim();
        }
    }

    juce::File getDefaultFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Harmonia").getChildFile ("Presets");
    }

    bool canHavePresets (te::Plugin& plugin)
    {
        return asExternal (plugin) != nullptr || isBuiltIn (plugin);
    }

    juce::String getPluginName (te::Plugin& plugin)
    {
        if (auto* external = asExternal (plugin))
            return external->desc.name;

        const auto type = plugin.getPluginType();

        if (type == te::FourOscPlugin::xmlTypeName)
            return "4OSC";

        // The names the effects have in the mixer's list (the filter keeps one name both ways).
        for (const auto& effect : effects::getBuiltIn())
            if (effect.builtInType == type)
                return effect.name;

        return plugin.getName();
    }

    juce::File getFolderFor (te::Plugin& plugin, const juce::File& presetsFolder)
    {
        const auto name = legalName (getPluginName (plugin));
        return presetsFolder.getChildFile (name.isNotEmpty() ? name : juce::String ("Plugin"));
    }

    juce::String getName (const juce::File& preset)
    {
        return preset.getFileNameWithoutExtension();
    }

    juce::File getFileFor (te::Plugin& plugin, const juce::String& name, const juce::File& presetsFolder)
    {
        return getFolderFor (plugin, presetsFolder).getChildFile (legalName (name) + fileExtension);
    }

    juce::Array<juce::File> list (te::Plugin& plugin, const juce::File& presetsFolder)
    {
        juce::Array<juce::File> result;
        const auto identity = getIdentity (plugin);

        for (const auto& file : getFolderFor (plugin, presetsFolder).findChildFiles (juce::File::findFiles, false,
                                                                                    juce::String ("*") + fileExtension))
        {
            // Only the root tag is needed: a quick look, not the whole file parsed.
            juce::XmlDocument document (file);

            if (const auto root = document.getDocumentElementIfTagMatches (presetTag.toString());
                root != nullptr && root->getStringAttribute (forPluginId) == identity)
                result.add (file);
        }

        std::sort (result.begin(), result.end(), [] (const juce::File& a, const juce::File& b)
        {
            return getName (a).compareNatural (getName (b)) < 0;
        });

        return result;
    }

    juce::String save (te::Plugin& plugin, const juce::String& name, const juce::File& presetsFolder)
    {
        if (! canHavePresets (plugin))
            return getPluginName (plugin) + " can't keep presets";

        if (legalName (name).isEmpty())
            return "Type a name for the preset";

        const auto file = getFileFor (plugin, name, presetsFolder);
        const auto identity = getIdentity (plugin);

        // Another plugin of the same name has a preset called that: it stays.
        if (file.existsAsFile())
        {
            juce::XmlDocument document (file);

            if (const auto root = document.getDocumentElementIfTagMatches (presetTag.toString());
                root == nullptr || root->getStringAttribute (forPluginId) != identity)
                return "A preset called \"" + getName (file) + "\" is another plugin's (also called "
                     + getPluginName (plugin) + "): choose another name";
        }

        juce::String error;
        auto settings = capture (plugin, error);

        if (! settings.isValid())
            return error;

        juce::ValueTree preset (presetTag);
        preset.setProperty (formatVersionId, formatVersion, nullptr);
        preset.setProperty (forPluginId, identity, nullptr);
        preset.setProperty (pluginNameId, getPluginName (plugin), nullptr);
        preset.appendChild (settings, nullptr);

        if (! file.getParentDirectory().createDirectory())
            return "Cannot create folder: " + file.getParentDirectory().getFullPathName();

        const auto xml = preset.createXml();
        juce::TemporaryFile temp (file);

        if (xml == nullptr || ! xml->writeTo (temp.getFile()) || ! temp.overwriteTargetFileWithTemporary())
            return "Cannot write preset file: " + file.getFullPathName();

        // Only a reminder for the next save, not a change to the song worth an undo step.
        plugin.state.setProperty (lastUsedId, getName (file), nullptr);
        return {};
    }

    juce::String load (te::Plugin& plugin, const juce::File& file)
    {
        if (! canHavePresets (plugin))
            return getPluginName (plugin) + " can't take presets";

        const auto xml = juce::parseXML (file);

        if (xml == nullptr || ! xml->hasTagName (presetTag.toString()))
            return "Not a Harmonia preset: " + file.getFileName();

        const auto preset = juce::ValueTree::fromXml (*xml);

        if ((int) preset[formatVersionId] > formatVersion)
            return "This preset was saved by a newer version of Harmonia";

        if (preset[forPluginId].toString() != getIdentity (plugin))
            return "\"" + getName (file) + "\" is a preset of " + preset[pluginNameId].toString() + ", not of " + getPluginName (plugin);

        const auto settings = preset.getChildWithName (te::IDs::PLUGIN);

        if (! settings.isValid())
            return "The preset has no settings in it: " + file.getFileName();

        auto& undoManager = plugin.edit.getUndoManager();
        undoManager.beginNewTransaction ("Preset " + getName (file));
        apply (plugin, settings);
        plugin.state.setProperty (lastUsedId, getName (file), &undoManager);
        return {};
    }

    juce::String getLastUsed (te::Plugin& plugin)
    {
        return plugin.state[lastUsedId].toString();
    }

    bool canReset (te::Plugin& plugin)
    {
        return isBuiltIn (plugin);
    }

    void reset (te::Plugin& plugin)
    {
        if (! canReset (plugin))
            return;

        auto& undoManager = plugin.edit.getUndoManager();
        undoManager.beginNewTransaction ("Reset " + getPluginName (plugin));

        // No property: every setting goes back to what it is in a new plugin.
        apply (plugin, juce::ValueTree (te::IDs::PLUGIN));
        plugin.state.removeProperty (lastUsedId, &undoManager);
    }

    juce::StringArray getOwnPresets (te::Plugin& plugin)
    {
        auto* external = asExternal (plugin);
        auto* instance = external != nullptr ? external->getAudioPluginInstance() : nullptr;

        if (instance == nullptr)
            return {};

        juce::StringArray names;
        juce::StringArray named;

        for (int i = 0; i < instance->getNumPrograms(); ++i)
        {
            const auto name = instance->getProgramName (i).trim();
            names.add (name.isNotEmpty() ? name : "Preset " + juce::String (i + 1));

            if (name.isNotEmpty())
                named.addIfNotAlreadyThere (name);
        }

        // Some plugins have a program list of one name over and over ("Default", "---"): not presets.
        return named.size() >= 2 ? names : juce::StringArray();
    }

    int getCurrentOwnPreset (te::Plugin& plugin)
    {
        auto* external = asExternal (plugin);
        auto* instance = external != nullptr ? external->getAudioPluginInstance() : nullptr;
        return instance != nullptr && instance->getNumPrograms() > 0 ? instance->getCurrentProgram() : -1;
    }

    void useOwnPreset (te::Plugin& plugin, int index)
    {
        auto* external = asExternal (plugin);
        auto* instance = external != nullptr ? external->getAudioPluginInstance() : nullptr;

        if (instance == nullptr || ! juce::isPositiveAndBelow (index, instance->getNumPrograms()))
            return;

        juce::String ignored;
        auto before = capture (plugin, ignored);
        external->setCurrentProgram (index, true);
        auto after = capture (plugin, ignored);

        auto& undoManager = plugin.edit.getUndoManager();
        undoManager.beginNewTransaction ("Preset " + instance->getProgramName (index).trim());
        undoManager.perform (new SetPluginState (*external, std::move (before), std::move (after), true));
        plugin.state.removeProperty (lastUsedId, &undoManager);
    }
}
