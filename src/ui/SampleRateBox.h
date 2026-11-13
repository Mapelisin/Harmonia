#pragma once

#include "audio/SongExport.h"

namespace harmonia::ui
{
    /** The list of sample rates an export can have: "as the audio device plays" first, then the
        usual ones (audio::exportSampleRates). */
    class SampleRateBox final : public juce::ComboBox
    {
    public:
        /** `deviceRate`: what the audio device plays at now, said in the first item (0: not known). */
        explicit SampleRateBox (double deviceRate = 0.0);

        /** The rate chosen; 0: the audio device's. */
        double getRate() const;

        /** A rate the list doesn't have chooses the audio device's. Tells nobody. */
        void setRate (double);

        /** "48000 Hz". */
        static juce::String describe (double rate);

    private:
        JUCE_DECLARE_NON_COPYABLE (SampleRateBox)
    };
}
