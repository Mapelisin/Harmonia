#pragma once

#include "core/Tracktion.h"

namespace harmonia::ui
{
    namespace keys
    {
        /** The Latin letter printed on the key, in upper case, whatever layout is active: the
            shortcut letter of a key press. With the Russian layout the key labelled P also says
            З, and it is still P here, so shortcuts stay on the same keys. 0 for other keys. */
        juce::juce_wchar getLetter (const juce::KeyPress&);

        /** True for keys that type something (letters, digits, punctuation) without Ctrl, Alt or
            Cmd: the ones the computer keyboard piano takes over. */
        bool isTypingKey (const juce::KeyPress&);
    }

    /** The computer keyboard as a piano (FL Studio's "typing keyboard to piano", Ctrl+T).

        Two rows, like a piano's white and black keys:
            lower: Z S X D C V G B H N J M , L . ; /   from C of the chosen octave
            upper: Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P [ = ]   an octave higher
        Keys are found by their place on the keyboard, not by the letter they type, so it plays
        the same with the English and the Russian layout (Z is Я there).

        It polls which keys are down whenever a key goes up or down (update()), which also
        means auto-repeat never retriggers a note and several keys play a chord. While notes
        sound a timer keeps polling, so a key released while another window had the focus still
        stops its note. */
    class ComputerKeyboard final : private juce::Timer
    {
    public:
        struct Key
        {
            int scanCode = 0;               // place on the keyboard (PC set-1 scan code)
            juce::juce_wchar character = 0; // what it types on a US layout (fallback without scan codes)
            int semitone = 0;               // from C of the chosen octave
        };

        static const std::vector<Key>& getKeys();

        /** Tells whether a key is held; the default reads the real keyboard. */
        using KeyReader = std::function<bool (const Key&)>;

        explicit ComputerKeyboard (KeyReader = {});
        ~ComputerKeyboard() override;

        void setEnabled (bool);
        bool isEnabled() const noexcept                 { return enabled; }

        static constexpr int minOctave = 0, maxOctave = 8;

        /** The Z key plays C of this octave (C4 = MIDI note 60). Changing it stops held notes. */
        void setOctave (int);
        int getOctave() const noexcept                  { return octave; }

        void setVelocity (int);
        int getVelocity() const noexcept                { return velocity; }

        /** Reads the keys and plays what changed. With `canStartNotes` false only releases
            (e.g. Ctrl is held: Ctrl+C is a shortcut, not a note). */
        void update (bool canStartNotes);

        void releaseAll();
        bool isPlayingAnything() const;

        std::function<void (int pitch, int velocity)> onNoteOn;
        std::function<void (int pitch)> onNoteOff;

        /** Enabled, octave or velocity changed. */
        std::function<void()> onSettingsChanged;

    private:
        KeyReader reader;
        bool enabled = false;
        int octave = 4;
        int velocity = 100;
        std::vector<int> pitchOfKey;            // per key: the pitch it is playing, or -1
        std::array<int, 128> keysOnPitch {};    // two keys can play the same pitch (, and Q)

        void press (size_t keyIndex, int pitch);
        void release (size_t keyIndex);
        void settingsChanged();
        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE (ComputerKeyboard)
    };
}
