#pragma once

#include "audio/PluginLoad.h"

namespace harmonia::ui
{
    /** Which plugin takes how much of the sound's time: the song's instruments and effects, the
        heaviest first, read anew a few times a second. A click on one opens its window. */
    class PluginLoadPanel final : public juce::Component,
                                  private juce::ListBoxModel,
                                  private juce::Timer
    {
    public:
        /** `read` is for tests (audio::plugin_load::measure). */
        explicit PluginLoadPanel (te::Edit&, audio::plugin_load::Reader read = {});
        ~PluginLoadPanel() override;

        /** In a bubble pointing at `target` (the CPU meter). */
        static void show (te::Edit&, juce::Component& target);

        /** Reads the loads now (the panel also does so by itself). */
        void refresh();

        int getNumShown() const noexcept                { return (int) entries.size(); }
        juce::String getShownName (int row) const;      ///< "Lead: Surge XT"
        juce::String getShownLoad (int row) const;      ///< "12%"
        juce::String getSummary() const;                ///< the line above the list

        /** As a click on the row: the plugin's window opens. */
        void choose (int row);

        /** What a click does (opens the plugin's window, unless a test set something else). */
        std::function<void (te::Plugin&)> onChosen;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        te::SafeSelectable<te::Edit> edit;
        audio::plugin_load::Reader read;
        std::vector<audio::plugin_load::Entry> entries;
        juce::Label summary, hint;
        juce::ListBox list;

        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE (PluginLoadPanel)
    };
}
