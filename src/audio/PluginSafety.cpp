#include "audio/PluginSafety.h"
#include "audio/Instruments.h"
#include "sandbox/RemotePlugin.h"

namespace harmonia::audio::plugin_safety
{
    namespace
    {
        /** The plugin of an open song that `remote` stands in for. */
        te::Plugin* find (core::Session& session, sandbox::RemotePlugin& remote)
        {
            for (auto* edit : session.getEngine().getActiveEdits().getEdits())
                if (edit != nullptr)
                    for (auto* plugin : te::getAllPlugins (*edit, false))
                        if (getRemote (*plugin) == &remote)
                            return plugin;

            return nullptr;
        }

        juce::String describeOwner (te::Plugin& plugin)
        {
            if (auto* track = plugin.getOwnerTrack())
                return " on " + track->getName();

            return {};
        }
    }

    sandbox::RemotePlugin* getRemote (te::Plugin& plugin)
    {
        if (auto* external = dynamic_cast<te::ExternalPlugin*> (&plugin))
            return dynamic_cast<sandbox::RemotePlugin*> (external->getAudioPluginInstance());

        return nullptr;
    }

    Status getStatus (te::Plugin& plugin)
    {
        if (auto* remote = getRemote (plugin))
            return remote->hasCrashed() ? Status::crashed : Status::running;

        return Status::notSandboxed;
    }

    bool wasHung (te::Plugin& plugin)
    {
        auto* remote = getRemote (plugin);
        return remote != nullptr && remote->hasCrashed() && remote->wasHung();
    }

    juce::String reload (te::Plugin& plugin)
    {
        auto* remote = getRemote (plugin);

        if (remote == nullptr || ! remote->hasCrashed())
            return {};

        const auto error = remote->reload();

        // Its latency may differ now: the song's timing is worked out again.
        if (error.isEmpty())
            plugin.edit.restartPlayback();

        return error;
    }

    juce::String describeCrash (te::Plugin& plugin)
    {
        auto* track = dynamic_cast<te::AudioTrack*> (plugin.getOwnerTrack());
        const auto isInstrument = track != nullptr && instruments::get (*track) == &plugin;

        const auto what = plugin.getName() + describeOwner (plugin);
        const auto happened = wasHung (plugin) ? " stopped answering and was stopped" : " crashed";
        const auto now = isInstrument ? juce::String ("the track is silent") : juce::String ("its sound goes through unchanged");
        const auto fix = isInstrument ? juce::String ("Reload it with the button next to the instrument (piano roll)")
                                      : juce::String ("Click its slot in the mixer to reload it");

        return what + happened + "; Harmonia plays on, " + now + ". " + fix;
    }

    void bringWindowToFront (te::Plugin& plugin)
    {
        if (auto* remote = getRemote (plugin); remote != nullptr && remote->isEditorShowing())
            remote->showEditor ({}, plugin.getName(), true);
    }

    //==============================================================================
    class Watcher::Impl final : private sandbox::Sandbox::Listener
    {
    public:
        Impl (core::Session& s, std::function<void (te::Plugin&)> callback)
            : session (s), onChanged (std::move (callback))
        {
            if (auto* sandbox = session.getPluginSandbox())
                sandbox->addListener (this);
        }

        ~Impl() override
        {
            if (auto* sandbox = session.getPluginSandbox())
                sandbox->removeListener (this);
        }

    private:
        core::Session& session;
        std::function<void (te::Plugin&)> onChanged;

        void pluginCrashed (sandbox::RemotePlugin& remote) override     { tell (remote); }
        void pluginReloaded (sandbox::RemotePlugin& remote) override    { tell (remote); }

        void tell (sandbox::RemotePlugin& remote)
        {
            if (auto* plugin = find (session, remote); plugin != nullptr && onChanged != nullptr)
                onChanged (*plugin);
        }
    };

    Watcher::Watcher (core::Session& session, std::function<void (te::Plugin&)> onChanged)
        : impl (std::make_unique<Impl> (session, std::move (onChanged)))
    {
    }

    Watcher::~Watcher() = default;
}
