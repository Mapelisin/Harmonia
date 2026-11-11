#include "ui/PluginLoadPanel.h"
#include "audio/Effects.h"
#include "ui/CpuMeter.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    namespace
    {
        constexpr int rowHeight = 24, panelWidth = 400, maxRowsShown = 14;
    }

    PluginLoadPanel::PluginLoadPanel (te::Edit& e, audio::plugin_load::Reader r)
        : edit (e), read (std::move (r))
    {
        onChosen = [] (te::Plugin& plugin) { audio::effects::showEditor (plugin); };

        summary.setFont (juce::FontOptions (theme::size::fontSize));
        summary.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        addAndMakeVisible (summary);

        hint.setFont (juce::FontOptions (theme::size::smallFontSize));
        hint.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        hint.setText ("The share of the time between two audio buffers each plugin takes. Click one to open its window.",
                      juce::dontSendNotification);
        hint.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (hint);

        list.setModel (this);
        list.setRowHeight (rowHeight);
        list.setColour (juce::ListBox::backgroundColourId, theme::colour::surface);
        addAndMakeVisible (list);

        refresh();
        const auto rows = juce::jlimit (1, maxRowsShown, getNumShown());
        setSize (panelWidth, 16 + 22 + 34 + rows * rowHeight + 8);
        startTimerHz (3);
    }

    PluginLoadPanel::~PluginLoadPanel()
    {
        stopTimer();
        list.setModel (nullptr);
    }

    void PluginLoadPanel::show (te::Edit& edit, juce::Component& target)
    {
        juce::CallOutBox::launchAsynchronously (std::make_unique<PluginLoadPanel> (edit), target.getScreenBounds(), nullptr);
    }

    void PluginLoadPanel::refresh()
    {
        entries = edit != nullptr ? audio::plugin_load::measure (*edit, read) : std::vector<audio::plugin_load::Entry>();
        summary.setText (getSummary(), juce::dontSendNotification);
        list.updateContent();
        list.repaint();
    }

    juce::String PluginLoadPanel::getShownName (int row) const
    {
        return juce::isPositiveAndBelow (row, getNumShown()) ? audio::plugin_load::describe (entries[(size_t) row]) : juce::String();
    }

    juce::String PluginLoadPanel::getShownLoad (int row) const
    {
        return juce::isPositiveAndBelow (row, getNumShown()) ? audio::plugin_load::describe (entries[(size_t) row].load) : juce::String();
    }

    juce::String PluginLoadPanel::getSummary() const
    {
        if (entries.empty())
            return "No instruments or effects in this song yet";

        return "Plugins take " + audio::plugin_load::describe (audio::plugin_load::getTotal (entries)) + " of the audio time";
    }

    void PluginLoadPanel::choose (int row)
    {
        if (! juce::isPositiveAndBelow (row, getNumShown()))
            return;

        // The plugin's window may take the bubble, and this with it, away.
        const auto plugin = entries[(size_t) row].plugin;
        const auto chosen = onChosen;

        if (chosen != nullptr && plugin != nullptr)
            chosen (*plugin);
    }

    //==============================================================================
    void PluginLoadPanel::paint (juce::Graphics& g)
    {
        g.fillAll (theme::colour::background);
    }

    void PluginLoadPanel::resized()
    {
        auto area = getLocalBounds().reduced (8);
        summary.setBounds (area.removeFromTop (22));
        hint.setBounds (area.removeFromTop (34));
        list.setBounds (area);
    }

    int PluginLoadPanel::getNumRows()
    {
        return getNumShown();
    }

    void PluginLoadPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool)
    {
        if (! juce::isPositiveAndBelow (row, getNumShown()))
            return;

        const auto& entry = entries[(size_t) row];
        auto area = juce::Rectangle<int> (width, height).reduced (6, 0);

        g.setFont (juce::FontOptions (theme::size::smallFontSize));
        g.setColour (theme::colour::textSecondary);
        g.drawText (audio::plugin_load::describe (entry.load), area.removeFromRight (44), juce::Justification::centredRight, false);

        auto bar = area.removeFromRight (90).withSizeKeepingCentre (84, 8).toFloat();
        g.setColour (theme::mixer::meterBackground);
        g.fillRoundedRectangle (bar, 2.0f);

        if (entry.load > 0.0)
        {
            const auto share = (float) juce::jlimit (0.0, 1.0, entry.load);
            g.setColour (CpuMeter::getColour (share));
            g.fillRoundedRectangle (bar.withWidth (juce::jmax (2.0f, bar.getWidth() * share)), 2.0f);
        }

        g.setColour (theme::colour::textPrimary);
        g.drawText (audio::plugin_load::describe (entry), area.withTrimmedRight (6), juce::Justification::centredLeft, true);
    }

    void PluginLoadPanel::listBoxItemClicked (int row, const juce::MouseEvent&)
    {
        choose (row);
    }

    void PluginLoadPanel::timerCallback()
    {
        refresh();
    }
}
