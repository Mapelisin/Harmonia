#include "audio/ExportFormat.h"
#include "audio/Mp3Encoder.h"
#include "core/Session.h"

namespace harmonia::audio
{
    namespace
    {
        const juce::String lastFormatKey ("exportFormat");

        const char* getTypeName (ExportFormat::Type type)
        {
            switch (type)
            {
                case ExportFormat::Type::flac:  return "flac";
                case ExportFormat::Type::mp3:   return "mp3";
                case ExportFormat::Type::wav:   break;
            }

            return "wav";
        }
    }

    juce::String ExportFormat::getExtension() const
    {
        return "." + juce::String (getTypeName (type));
    }

    juce::String ExportFormat::describe() const
    {
        switch (type)
        {
            case Type::flac:    return "FLAC, " + juce::String (bitDepth) + "-bit (lossless, about half the size)";
            case Type::mp3:     return "MP3, " + juce::String (kbps) + " kbps" + (kbps >= 320 ? " (best)" : juce::String());
            case Type::wav:     break;
        }

        if (bitDepth == 32)
            return "WAV, 32-bit float (for mastering)";

        return "WAV, " + juce::String (bitDepth) + "-bit" + (bitDepth == 16 ? " (CD)" : juce::String());
    }

    juce::Array<ExportFormat> ExportFormat::getChoices()
    {
        juce::Array<ExportFormat> choices;

        for (const auto bits : { 24, 16, 32 })
            choices.add ({ Type::wav, bits });

        for (const auto bits : { 24, 16 })
            choices.add ({ Type::flac, bits });

        for (const auto kbps : { 320, 256, 192, 128 })
            if (mp3::isAvailable (kbps))
                choices.add ({ Type::mp3, 16, kbps });

        return choices;
    }

    juce::String ExportFormat::toString() const
    {
        return juce::String (getTypeName (type)) + "-" + juce::String (type == Type::mp3 ? kbps : bitDepth);
    }

    ExportFormat ExportFormat::fromString (const juce::String& text)
    {
        for (const auto& choice : getChoices())
            if (choice.toString() == text)
                return choice;

        return {};
    }

    ExportFormat getLastExportFormat (core::Session& session)
    {
        return ExportFormat::fromString (session.getEngine().getPropertyStorage().getPropertiesFile().getValue (lastFormatKey));
    }

    void setLastExportFormat (core::Session& session, const ExportFormat& format)
    {
        auto& settings = session.getEngine().getPropertyStorage().getPropertiesFile();
        settings.setValue (lastFormatKey, format.toString());
        settings.saveIfNeeded();
    }

    bool ExportFormat::operator== (const ExportFormat& other) const noexcept
    {
        if (type != other.type)
            return false;

        return type == Type::mp3 ? kbps == other.kbps : bitDepth == other.bitDepth;
    }
}
