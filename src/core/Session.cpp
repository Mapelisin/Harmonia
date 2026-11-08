#include "core/Session.h"

namespace harmonia::core
{
    namespace
    {
        class HarmoniaEngineBehaviour final : public te::EngineBehaviour
        {
        public:
            HarmoniaEngineBehaviour (bool shouldOpenDevices, bool shouldScanOutOfProcess, juce::File recordings)
                : openDevices (shouldOpenDevices), scanOutOfProcess (shouldScanOutOfProcess),
                  recordingsFolder (std::move (recordings)) {}

            juce::File getFileForNewAudioRecording (te::Track& track, const juce::String& extension) override
            {
                return Session::getFileForNewRecording (track, extension, recordingsFolder);
            }

            // A muted track (or one silenced by another's solo) sends nothing to the return tracks:
            // muting a track takes its reverb away too.
            bool shouldProcessAuxSendWhenTrackIsMuted (te::AuxSendPlugin&) override
            {
                return false;
            }

            bool autoInitialiseDeviceManager() override  { return openDevices; }
            bool canScanPluginsOutOfProcess() override    { return scanOutOfProcess; }

            te::EditLimits getEditLimits() override
            {
                // Tracktion allows only 4 master effects by default; a mastering chain needs more.
                te::EditLimits limits;
                limits.maxNumMasterPlugins = 16;
                return limits;
            }

        private:
            bool openDevices, scanOutOfProcess;
            juce::File recordingsFolder;
        };
    }

    Session::Session (Options options)
        : openDevices (options.openDevices)
    {
        auto ui = options.uiBehaviour != nullptr ? std::move (options.uiBehaviour)
                                                 : std::make_unique<te::UIBehaviour>();

        engine = std::make_unique<te::Engine> (std::make_unique<te::PropertyStorage> (options.settingsName),
                                               std::move (ui),
                                               std::make_unique<HarmoniaEngineBehaviour> (openDevices,
                                                                                          options.scanPluginsOutOfProcess,
                                                                                          options.recordingsFolder));
    }

    juce::File Session::getFileForNewRecording (const te::Track& track, const juce::String& extension,
                                                const juce::File& recordingsFolder)
    {
        auto folder = recordingsFolder != juce::File() ? recordingsFolder
                                                       : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                             .getChildFile ("Harmonia").getChildFile ("Recordings");

        if (track.edit.editFileRetriever != nullptr)
            if (const auto projectFile = track.edit.editFileRetriever(); projectFile != juce::File())
                folder = projectFile.getSiblingFile (projectFile.getFileNameWithoutExtension() + " Audio");

        // Without the characters Windows doesn't allow in file names.
        const auto trackName = juce::File::createLegalFileName (track.getName()).trim();
        const auto time = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
        const auto name = (trackName.isNotEmpty() ? trackName : juce::String ("Recording")) + " " + time;

        return folder.getNonexistentChildFile (name, extension.startsWithChar ('.') ? extension : "." + extension, false);
    }

    Session::~Session() = default;
}
