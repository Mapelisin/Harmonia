#include "ui/ParameterPanel.h"
#include "audio/Presets.h"
#include "ui/PresetMenu.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    namespace
    {
        constexpr int nameWidth = 150, valueWidth = 90, margin = 10;
    }

    //==============================================================================
    class ParameterPanel::Row final : public juce::Component
    {
    public:
        explicit Row (audio::PluginControl controlToShow)
            : control (std::move (controlToShow))
        {
            name.setText (control.getName(), juce::dontSendNotification);
            name.setColour (juce::Label::textColourId, theme::colour::textPrimary);
            name.setFont (juce::FontOptions (theme::size::smallFontSize));
            name.setTooltip (control.getName());

            value.setColour (juce::Label::textColourId, theme::colour::textSecondary);
            value.setFont (juce::FontOptions (theme::size::smallFontSize));
            value.setJustificationType (juce::Justification::centredRight);

            if (! control.getChoices().isEmpty())
            {
                choice.addItemList (control.getChoices(), 1);
                choice.onChange = [this]
                {
                    control.beginChange();
                    control.setValue ((float) (choice.getSelectedItemIndex()));
                    control.endChange();
                    refresh();
                };
                addAndMakeVisible (choice);
            }
            else
            {
                // The slider works in 0..1 of the control's range, so skewed ranges feel right.
                const auto range = control.getRange();
                slider.setSliderStyle (juce::Slider::LinearHorizontal);
                slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                slider.setRange (0.0, 1.0);
                slider.setScrollWheelEnabled (false);   // the wheel scrolls the list
                slider.setDoubleClickReturnValue (true, (double) range.convertTo0to1 (control.getDefaultValue()));
                slider.onDragStart = [this] { control.beginChange(); };
                slider.onDragEnd = [this] { control.endChange(); };
                slider.onValueChange = [this]
                {
                    // A double-click reset arrives without a drag: still its own undo step.
                    const auto isDrag = slider.isMouseButtonDown();

                    if (! isDrag)
                        control.beginChange();

                    control.setValue (control.getRange().convertFrom0to1 ((float) slider.getValue()));

                    if (! isDrag)
                        control.endChange();

                    showValue();
                };
                addAndMakeVisible (slider);
            }

            for (auto* c : std::initializer_list<juce::Component*> { &name, &value, &slider, &choice })
                c->setWantsKeyboardFocus (false);

            addAndMakeVisible (name);
            addAndMakeVisible (value);
            refresh();
        }

        void refresh()
        {
            if (! control.getChoices().isEmpty())
                choice.setSelectedItemIndex (juce::roundToInt (control.getValue()), juce::dontSendNotification);
            else if (! slider.isMouseButtonDown())
                slider.setValue ((double) control.getRange().convertTo0to1 (control.getValue()), juce::dontSendNotification);

            showValue();
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (margin, 2);
            name.setBounds (area.removeFromLeft (nameWidth));
            value.setBounds (area.removeFromRight (valueWidth));
            area.removeFromRight (6);
            slider.setBounds (area);
            choice.setBounds (area.withSizeKeepingCentre (std::min (area.getWidth(), 160), area.getHeight()));
        }

    private:
        audio::PluginControl control;
        juce::Label name, value;
        juce::Slider slider;
        juce::ComboBox choice;

        void showValue()
        {
            value.setText (control.getChoices().isEmpty() ? control.getText (control.getValue()) : juce::String(),
                           juce::dontSendNotification);
        }
    };

    //==============================================================================
    ParameterPanel::ParameterPanel (te::Plugin& p)
        : plugin (p)
    {
        presetsButton.setWantsKeyboardFocus (false);
        presetsButton.setEnabled (audio::presets::canHavePresets (plugin));
        presetsButton.setTooltip ("Save these settings as a preset, or load one");
        presetsButton.onClick = [this]
        {
            preset_menu::make (plugin, [safe = juce::Component::SafePointer<ParameterPanel> (this)] (const juce::String& text, bool isProblem)
            {
                if (safe == nullptr)
                    return;

                safe->message.setColour (juce::Label::textColourId, isProblem ? theme::colour::warning : theme::colour::textSecondary);
                safe->message.setText (text, juce::dontSendNotification);
            }).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetsButton));
        };
        addAndMakeVisible (presetsButton);

        message.setFont (juce::FontOptions (theme::size::smallFontSize));
        message.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        addAndMakeVisible (message);

        for (auto& control : audio::PluginControl::getAll (plugin))
            content.addAndMakeVisible (rows.add (std::make_unique<Row> (std::move (control))));

        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (10);
        addAndMakeVisible (viewport);

        setSize (520, std::min (getIdealHeight(), 640));
        startTimerHz (10);
    }

    ParameterPanel::~ParameterPanel()
    {
        stopTimer();
    }

    int ParameterPanel::getIdealHeight() const
    {
        return barHeight + std::max (1, rows.size()) * rowHeight + 2 * margin;
    }

    void ParameterPanel::paint (juce::Graphics& g)
    {
        g.fillAll (theme::colour::background);

        if (rows.isEmpty())
        {
            g.setColour (theme::colour::textSecondary);
            g.setFont (juce::FontOptions (theme::size::fontSize));
            g.drawText ("This plugin has no settings.", getLocalBounds().withTrimmedTop (barHeight), juce::Justification::centred);
        }
    }

    void ParameterPanel::resized()
    {
        auto area = getLocalBounds();
        auto bar = area.removeFromTop (barHeight).reduced (margin, 4);
        presetsButton.setBounds (bar.removeFromLeft (90));
        bar.removeFromLeft (margin);
        message.setBounds (bar);

        viewport.setBounds (area);
        const auto rowsHeight = getIdealHeight() - barHeight;
        const auto width = getWidth() - (rowsHeight > area.getHeight() ? viewport.getScrollBarThickness() : 0);
        content.setSize (width, rowsHeight);

        for (int i = 0; i < rows.size(); ++i)
            rows[i]->setBounds (0, margin + i * rowHeight, width, rowHeight);
    }

    void ParameterPanel::timerCallback()
    {
        for (auto* row : rows)
            row->refresh();
    }
}
