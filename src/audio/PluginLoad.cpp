#include "audio/PluginLoad.h"
#include "audio/Effects.h"

namespace harmonia::audio::plugin_load
{
    namespace
    {
        void add (std::vector<Entry>& entries, te::Plugin& plugin, const Reader& read)
        {
            const auto load = read != nullptr ? read (plugin) : plugin.getCpuUsage();
            entries.push_back ({ &plugin, effects::getDisplayName (plugin), effects::getOwnerName (plugin),
                                 juce::jmax (0.0, load) });
        }
    }

    std::vector<Entry> measure (te::Edit& edit, const Reader& read)
    {
        std::vector<Entry> entries;

        for (auto* plugin : effects::getAllOfSong (edit))
            add (entries, *plugin, read);

        std::stable_sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b) { return a.load > b.load; });
        return entries;
    }

    double getTotal (const std::vector<Entry>& entries)
    {
        auto total = 0.0;

        for (const auto& entry : entries)
            total += entry.load;

        return total;
    }

    juce::String describe (double load)
    {
        const auto percent = juce::jmax (0.0, load) * 100.0;

        if (percent < 0.05)
            return "0%";

        // Small ones with a decimal: "0.4%" says more than "0%" about ten of them.
        return (percent < 9.95 ? juce::String (percent, 1) : juce::String (juce::roundToInt (percent))) + "%";
    }

    juce::String describe (const Entry& entry)
    {
        return entry.owner.isNotEmpty() ? entry.owner + ": " + entry.name : entry.name;
    }
}
