#include "ui/SampleRateBox.h"

namespace harmonia::ui
{
    SampleRateBox::SampleRateBox (double deviceRate)
    {
        addItem (deviceRate > 0.0 ? "As the audio device plays (" + describe (deviceRate) + ")"
                                  : juce::String ("As the audio device plays"), 1);

        // What the usual two are for.
        for (size_t i = 0; i < audio::exportSampleRates.size(); ++i)
        {
            const auto rate = audio::exportSampleRates[i];
            addItem (describe (rate) + (rate == 44100.0 ? " (CD, streaming)" : rate == 48000.0 ? " (video)" : ""), (int) i + 2);
        }

        setSelectedId (1, juce::dontSendNotification);
    }

    double SampleRateBox::getRate() const
    {
        const auto index = getSelectedId() - 2;
        return index >= 0 && index < (int) audio::exportSampleRates.size() ? audio::exportSampleRates[(size_t) index] : 0.0;
    }

    void SampleRateBox::setRate (double rate)
    {
        const auto found = std::find (audio::exportSampleRates.begin(), audio::exportSampleRates.end(), rate);
        setSelectedId (found != audio::exportSampleRates.end() ? (int) (found - audio::exportSampleRates.begin()) + 2 : 1,
                       juce::dontSendNotification);
    }

    juce::String SampleRateBox::describe (double rate)
    {
        return juce::String (juce::roundToInt (rate)) + " Hz";
    }
}
