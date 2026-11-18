#include "PluginLibrary.h"

#include <algorithm>

namespace carthage::engine
{
    int fuzzyScore (const juce::String& query, const juce::String& text)
    {
        if (query.isEmpty())
            return 0;

        const auto q = query.toLowerCase();
        const auto t = text.toLowerCase();

        if (t.startsWith (q))
            return 1000 + q.length() * 10 - (t.length() - q.length());   // exact prefix wins

        // Walk the text once, matching the query's letters in order.
        int score = 0, qi = 0, run = 0, lastMatch = -2;
        const auto qn = q.length(), tn = t.length();

        for (int ti = 0; ti < tn && qi < qn; ++ti)
        {
            const auto tc = t[ti];

            if (tc != q[qi])
                continue;

            const bool wordStart = ti == 0 || ! juce::CharacterFunctions::isLetterOrDigit (t[ti - 1])
                                   || (juce::CharacterFunctions::isUpperCase (text[ti]) && juce::CharacterFunctions::isLowerCase (text[ti - 1]));

            run = (lastMatch == ti - 1) ? run + 1 : 0;
            score += 10 + (wordStart ? 25 : 0) + run * 15;

            if (lastMatch >= 0)
                score -= std::min (ti - lastMatch - 1, 10);   // gaps cost a little

            lastMatch = ti;
            ++qi;
        }

        return qi == qn ? score : -1;
    }

    namespace
    {
        /** Several words must each match the name or the vendor; the best word scores count. */
        int matchWords (const juce::StringArray& words, const juce::String& name, const juce::String& vendor)
        {
            int total = 0;

            for (const auto& w : words)
            {
                const auto inName = fuzzyScore (w, name);
                const auto inVendor = fuzzyScore (w, vendor);
                const auto s = std::max (inName, inVendor < 0 ? -1 : inVendor / 2);   // a vendor match counts half

                if (s < 0)
                    return -1;

                total += s;
            }

            return total;
        }
    }

    PluginLibrary::PluginLibrary (juce::File cacheFolder)
        : file (cacheFolder.getChildFile ("Library.xml"))
    {
        load();
    }

    void PluginLibrary::setPlugins (const juce::Array<juce::PluginDescription>& plugins)
    {
        entries.clear();
        lowerNames.clear();
        lowerVendors.clear();

        for (const auto& d : plugins)
        {
            Entry e;
            e.description = d;
            e.key = d.createIdentifierString();
            e.favourite = favourites.contains (e.key);

            if (const auto it = lastUsed.find (e.key); it != lastUsed.end())
                e.lastUsedMs = it->second;

            entries.push_back (std::move (e));
        }

        std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b)
        {
            const auto c = a.description.name.compareNatural (b.description.name);
            return c != 0 ? c < 0 : a.description.pluginFormatName < b.description.pluginFormatName;
        });

        for (const auto& e : entries)
        {
            lowerNames.push_back (e.description.name);
            lowerVendors.push_back (e.description.manufacturerName);
        }
    }

    std::vector<size_t> PluginLibrary::find (const Filter& f) const
    {
        const auto words = juce::StringArray::fromTokens (f.query.trim(), " ", {});
        std::vector<std::pair<int, size_t>> hits;
        hits.reserve (entries.size());

        for (size_t i = 0; i < entries.size(); ++i)
        {
            const auto& e = entries[i];
            const auto& d = e.description;

            if (f.favouritesOnly && ! e.favourite)                          continue;
            if (f.format.isNotEmpty() && d.pluginFormatName != f.format)    continue;
            if (f.vendor.isNotEmpty() && d.manufacturerName != f.vendor)    continue;
            if (f.kind == Kind::instruments && ! d.isInstrument)            continue;
            if (f.kind == Kind::effects && d.isInstrument)                  continue;

            const auto score = words.isEmpty() ? 0 : matchWords (words, lowerNames[i], lowerVendors[i]);

            if (score >= 0)
                hits.emplace_back (score, i);
        }

        // Best match first; equal scores keep name order (entries are sorted by name).
        std::stable_sort (hits.begin(), hits.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });

        std::vector<size_t> result;
        result.reserve (hits.size());

        for (const auto& h : hits)
            result.push_back (h.second);

        return result;
    }

    std::vector<size_t> PluginLibrary::recent (size_t count) const
    {
        std::vector<size_t> used;

        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].lastUsedMs > 0)
                used.push_back (i);

        std::sort (used.begin(), used.end(), [this] (size_t a, size_t b) { return entries[a].lastUsedMs > entries[b].lastUsedMs; });

        if (used.size() > count)
            used.resize (count);

        return used;
    }

    juce::StringArray PluginLibrary::getVendors() const
    {
        juce::StringArray vendors;

        for (const auto& e : entries)
            if (e.description.manufacturerName.isNotEmpty())
                vendors.addIfNotAlreadyThere (e.description.manufacturerName);

        vendors.sortNatural();
        return vendors;
    }

    const PluginLibrary::Entry* PluginLibrary::findEntry (const juce::String& key) const
    {
        for (const auto& e : entries)
            if (e.key == key)
                return &e;

        return nullptr;
    }

    void PluginLibrary::setFavourite (const juce::String& key, bool on)
    {
        if (on) favourites.addIfNotAlreadyThere (key);
        else    favourites.removeString (key);

        for (auto& e : entries)
            if (e.key == key)
                e.favourite = on;

        save();
    }

    void PluginLibrary::markUsed (const juce::String& key)
    {
        const auto now = juce::Time::currentTimeMillis();
        lastUsed[key] = now;

        for (auto& e : entries)
            if (e.key == key)
                e.lastUsedMs = now;

        save();
    }

    void PluginLibrary::load()
    {
        if (auto xml = juce::parseXML (file))
        {
            if (xml->getIntAttribute ("version") > fileVersion)
                return;   // written by a newer Carthage: keep it untouched until saved over

            for (auto* e : xml->getChildWithTagNameIterator ("PLUGIN"))
            {
                const auto key = e->getStringAttribute ("key");

                if (e->getBoolAttribute ("favourite"))
                    favourites.addIfNotAlreadyThere (key);

                if (const auto used = e->getStringAttribute ("lastUsed").getLargeIntValue(); used > 0)
                    lastUsed[key] = used;
            }
        }
    }

    void PluginLibrary::save() const
    {
        juce::XmlElement root ("CARTHAGEPLUGINLIBRARY");
        root.setAttribute ("version", fileVersion);
        juce::StringArray keys (favourites);

        for (const auto& [key, used] : lastUsed)
            keys.addIfNotAlreadyThere (key);

        for (const auto& key : keys)
        {
            auto* e = root.createNewChildElement ("PLUGIN");
            e->setAttribute ("key", key);
            e->setAttribute ("favourite", favourites.contains (key));

            if (const auto it = lastUsed.find (key); it != lastUsed.end())
                e->setAttribute ("lastUsed", juce::String (it->second));
        }

        file.getParentDirectory().createDirectory();
        root.writeTo (file);
    }
}
