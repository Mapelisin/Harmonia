#include "ui/ComputerKeyboard.h"
#include "ui/PhysicalKeys.h"

namespace harmonia::ui
{
    namespace keys
    {
        juce::juce_wchar getLetter (const juce::KeyPress& key)
        {
            // The Russian ЙЦУКЕН layout, key by key over the US QWERTY letters.
            static const juce::String russian = juce::String::fromUTF8 (
                "\xd0\xb9\xd1\x86\xd1\x83\xd0\xba\xd0\xb5\xd0\xbd\xd0\xb3\xd1\x88\xd1\x89\xd0\xb7"   // йцукенгшщз
                "\xd1\x84\xd1\x8b\xd0\xb2\xd0\xb0\xd0\xbf\xd1\x80\xd0\xbe\xd0\xbb\xd0\xb4"           // фывапролд
                "\xd1\x8f\xd1\x87\xd1\x81\xd0\xbc\xd0\xb8\xd1\x82\xd1\x8c");                          // ячсмить
            static const juce::String latin = "QWERTYUIOPASDFGHJKLZXCVBNM";

            const auto toLetter = [] (juce::juce_wchar c) -> juce::juce_wchar
            {
                if (c >= 'a' && c <= 'z')
                    return c - 'a' + 'A';

                if (c >= 'A' && c <= 'Z')
                    return c;

                // Capitals (with Shift or Caps Lock) by hand: the C library's towlower doesn't know
                // Cyrillic in the default locale. А..Я are 0x20 below а..я.
                if (c >= 0x0410 && c <= 0x042f)
                    c += 0x20;

                if (const auto index = russian.indexOfChar (c); index >= 0)
                    return latin[index];

                return 0;
            };

            // On Windows JUCE already reports the Latin letter of the key; on other systems the
            // key code can be the typed (Cyrillic) letter.
            if (const auto letter = toLetter ((juce::juce_wchar) key.getKeyCode()); letter != 0)
                return letter;

            return toLetter (key.getTextCharacter());
        }

        bool isTypingKey (const juce::KeyPress& key)
        {
            const auto mods = key.getModifiers();

            if (mods.isCtrlDown() || mods.isAltDown() || mods.isCommandDown())
                return false;

            // Special keys (arrows, Delete, F1...) carry JUCE's extended-key flag; Space, Tab,
            // Return and Escape are below '!'.
            const auto code = key.getKeyCode();
            return code > ' ' && code < 0x10000 && code != juce::KeyPress::deleteKey && code != juce::KeyPress::backspaceKey;
        }
    }

    //==============================================================================
    const std::vector<ComputerKeyboard::Key>& ComputerKeyboard::getKeys()
    {
        static const std::vector<Key> keys {
            // Lower row: C of the chosen octave
            { 0x2C, 'z', 0 },  { 0x1F, 's', 1 },  { 0x2D, 'x', 2 },  { 0x20, 'd', 3 },  { 0x2E, 'c', 4 },
            { 0x2F, 'v', 5 },  { 0x22, 'g', 6 },  { 0x30, 'b', 7 },  { 0x23, 'h', 8 },  { 0x31, 'n', 9 },
            { 0x24, 'j', 10 }, { 0x32, 'm', 11 }, { 0x33, ',', 12 }, { 0x26, 'l', 13 }, { 0x34, '.', 14 },
            { 0x27, ';', 15 }, { 0x35, '/', 16 },

            // Upper row: an octave higher
            { 0x10, 'q', 12 }, { 0x03, '2', 13 }, { 0x11, 'w', 14 }, { 0x04, '3', 15 }, { 0x12, 'e', 16 },
            { 0x13, 'r', 17 }, { 0x06, '5', 18 }, { 0x14, 't', 19 }, { 0x07, '6', 20 }, { 0x15, 'y', 21 },
            { 0x08, '7', 22 }, { 0x16, 'u', 23 }, { 0x17, 'i', 24 }, { 0x0A, '9', 25 }, { 0x18, 'o', 26 },
            { 0x0B, '0', 27 }, { 0x19, 'p', 28 }, { 0x1A, '[', 29 }, { 0x0D, '=', 30 }, { 0x1B, ']', 31 },
        };

        return keys;
    }

    ComputerKeyboard::ComputerKeyboard (KeyReader r)
        : reader (std::move (r))
    {
        if (reader == nullptr)
        {
            reader = [] (const Key& key)
            {
                if (physical_keys::isSupported())
                    return physical_keys::isDown (key.scanCode);

                return juce::KeyPress::isKeyCurrentlyDown ((int) key.character);
            };
        }

        pitchOfKey.assign (getKeys().size(), -1);
    }

    ComputerKeyboard::~ComputerKeyboard()
    {
        stopTimer();
        releaseAll();
    }

    void ComputerKeyboard::setEnabled (bool shouldBeEnabled)
    {
        if (enabled == shouldBeEnabled)
            return;

        if (! shouldBeEnabled)
            releaseAll();

        enabled = shouldBeEnabled;
        settingsChanged();
    }

    void ComputerKeyboard::setOctave (int newOctave)
    {
        newOctave = juce::jlimit (minOctave, maxOctave, newOctave);

        if (octave == newOctave)
            return;

        releaseAll();
        octave = newOctave;
        settingsChanged();
    }

    void ComputerKeyboard::setVelocity (int newVelocity)
    {
        newVelocity = juce::jlimit (1, 127, newVelocity);

        if (velocity != newVelocity)
        {
            velocity = newVelocity;
            settingsChanged();
        }
    }

    void ComputerKeyboard::update (bool canStartNotes)
    {
        if (! enabled)
            return;

        const auto& keys = getKeys();
        const auto basePitch = (octave + 1) * 12;

        for (size_t i = 0; i < keys.size(); ++i)
        {
            const auto down = reader (keys[i]);

            if (! down && pitchOfKey[i] >= 0)
                release (i);
            else if (down && pitchOfKey[i] < 0 && canStartNotes)
                if (const auto pitch = basePitch + keys[i].semitone; pitch <= 127)
                    press (i, pitch);
        }

        if (isPlayingAnything())
            startTimer (30);
        else
            stopTimer();
    }

    void ComputerKeyboard::releaseAll()
    {
        for (size_t i = 0; i < pitchOfKey.size(); ++i)
            if (pitchOfKey[i] >= 0)
                release (i);

        stopTimer();
    }

    bool ComputerKeyboard::isPlayingAnything() const
    {
        return std::any_of (pitchOfKey.begin(), pitchOfKey.end(), [] (int p) { return p >= 0; });
    }

    void ComputerKeyboard::press (size_t keyIndex, int pitch)
    {
        pitchOfKey[keyIndex] = pitch;

        if (keysOnPitch[(size_t) pitch]++ == 0 && onNoteOn != nullptr)
            onNoteOn (pitch, velocity);
    }

    void ComputerKeyboard::release (size_t keyIndex)
    {
        const auto pitch = pitchOfKey[keyIndex];
        pitchOfKey[keyIndex] = -1;

        if (--keysOnPitch[(size_t) pitch] == 0 && onNoteOff != nullptr)
            onNoteOff (pitch);
    }

    void ComputerKeyboard::settingsChanged()
    {
        if (onSettingsChanged != nullptr)
            onSettingsChanged();
    }

    void ComputerKeyboard::timerCallback()
    {
        // Another app came to the front: its keys are none of our business.
        if (! juce::Process::isForegroundProcess())
            releaseAll();
        else
            update (false);
    }
}
