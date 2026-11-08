#include "ui/TrackColours.h"
#include "core/Clips.h"
#include "core/Tracks.h"
#include "ui/Theme.h"

namespace harmonia::ui::track_colours
{
    const std::vector<NamedColour>& getPalette()
    {
        static const std::vector<NamedColour> palette {
            { "Red",    juce::Colour (0xffe0605a) },
            { "Orange", juce::Colour (0xffe0875a) },
            { "Amber",  juce::Colour (0xffe6a85a) },
            { "Yellow", juce::Colour (0xffd9c45a) },
            { "Lime",   juce::Colour (0xff9ccf5c) },
            { "Green",  juce::Colour (0xff62c79a) },
            { "Teal",   juce::Colour (0xff4fc2c9) },
            { "Sky",    juce::Colour (0xff6cc4ee) },
            { "Blue",   juce::Colour (0xff5aa9e6) },
            { "Indigo", juce::Colour (0xff6f8be0) },
            { "Purple", juce::Colour (0xffb17ad9) },
            { "Pink",   juce::Colour (0xffe06f9f) },
            { "Rose",   juce::Colour (0xffd9677a) },
            { "Brown",  juce::Colour (0xffa8805e) },
            { "Grey",   juce::Colour (0xff8d95a8) },
            { "White",  juce::Colour (0xffd8dce6) }
        };

        return palette;
    }

    juce::Colour getAutomatic (int trackIndex)
    {
        return theme::arrangement::getTrackColour (trackIndex);
    }

    juce::Colour getTrackColour (const te::Track& track, int trackIndex)
    {
        return core::tracks::getColour (track).value_or (getAutomatic (trackIndex));
    }

    juce::Colour getClipColour (const te::Clip& clip, int trackIndex)
    {
        if (const auto own = core::clips::getColour (clip))
            return *own;

        if (auto* track = clip.getTrack())
            return getTrackColour (*track, trackIndex);

        return getAutomatic (trackIndex);
    }

    juce::PopupMenu createMenu (std::optional<juce::Colour> current, const juce::String& noneText,
                                std::function<void (std::optional<juce::Colour>)> choose)
    {
        juce::PopupMenu menu;
        menu.addItem (noneText, true, ! current.has_value(), [choose] { choose (std::nullopt); });
        menu.addSeparator();

        for (const auto& [name, colour] : getPalette())
        {
            auto swatch = std::make_unique<juce::DrawablePath>();
            juce::Path shape;
            shape.addRoundedRectangle (0.0f, 0.0f, 14.0f, 14.0f, 3.0f);
            swatch->setPath (shape);
            swatch->setFill (colour);

            juce::PopupMenu::Item item (name);
            item.setTicked (current.has_value() && *current == colour)
                .setImage (std::move (swatch))
                .setAction ([choose, c = colour] { choose (c); });
            menu.addItem (std::move (item));
        }

        return menu;
    }
}
