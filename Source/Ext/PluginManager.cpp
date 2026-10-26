// NX Player — MIT License
#include "PluginManager.h"
#include "ThemeEngine.h"
#include "../Core/Text.h"

namespace nx
{

//==============================================================================
// DSP plug-in wrapper
//==============================================================================
namespace
{
class DynamicDspProcessor final : public DspProcessor
{
public:
    DynamicDspProcessor (const nx_dsp_descriptor* d, std::vector<double> v) : desc (d), values (std::move (v)) {}
    ~DynamicDspProcessor() override { if (instance) desc->destroy (instance); }

    juce::String getName() const override        { return juce::String::fromUTF8 (desc->name); }
    juce::String getDescription() const override { return juce::String::fromUTF8 (desc->description); }

    bool prepare (double rate, int channels, int maxBlock) override
    {
        if (instance) desc->destroy (instance);
        instance = desc->create (rate, channels, maxBlock);
        if (instance == nullptr) return false;
        for (int i = 0; i < desc->num_params && i < (int) values.size(); ++i) desc->set_param (instance, i, values[(size_t) i]);
        if (desc->reset) desc->reset (instance);
        return true;
    }

    void process (double* const* ch, int numCh, int n) noexcept override
    {
        if (instance) desc->process (instance, ch, numCh, n);
    }

    void reset() noexcept override { if (instance && desc->reset) desc->reset (instance); }

private:
    const nx_dsp_descriptor* desc;
    void* instance = nullptr;
    std::vector<double> values;
};

juce::File findLibrary (const juce::File& folder, const juce::String& baseName)
{
   #if JUCE_WINDOWS
    const juce::StringArray exts { ".dll" };
   #elif JUCE_MAC
    const juce::StringArray exts { ".dylib", ".so" };
   #else
    const juce::StringArray exts { ".so" };
   #endif
    for (auto sub : { "", "bin" })
        for (auto& ext : exts)
        {
            auto f = folder.getChildFile (sub).getChildFile (baseName + ext);
            if (f.existsAsFile()) return f;
            f = folder.getChildFile (sub).getChildFile ("lib" + baseName + ext);
            if (f.existsAsFile()) return f;
        }
    return {};
}
}

//==============================================================================
// UI plug-in runtime
//==============================================================================
struct PluginManager::UiRuntime
{
    juce::String id;
    juce::File script;
    std::unique_ptr<juce::JavascriptEngine> engine;
    juce::DynamicObject::Ptr g;
    juce::Graphics* graphics = nullptr;
    juce::Colour colour { juce::Colours::white };
    std::optional<juce::ColourGradient> gradient;
    juce::String error;
    bool hasInit = false, initDone = false;

    juce::Colour parseColour (const juce::var& v, float alpha = -1.0f) const
    {
        juce::Colour c = juce::Colours::white;
        if (v.isString()) parseCssColour (v.toString(), c);
        else if (auto* a = v.getArray(); a != nullptr && a->size() >= 3)
            c = juce::Colour::fromFloatRGBA ((float) (*a)[0], (float) (*a)[1], (float) (*a)[2], a->size() > 3 ? (float) (*a)[3] : 1.0f);
        return alpha >= 0.0f ? c.withMultipliedAlpha (alpha) : c;
    }

    void applyFill()
    {
        if (gradient) graphics->setGradientFill (*gradient);
        else graphics->setColour (colour);
    }

    void buildGraphicsObject()
    {
        g = new juce::DynamicObject();
        auto num = [] (const juce::var::NativeFunctionArgs& a, int i, float def = 0.0f) { return i < a.numArguments ? (float) (double) a.arguments[i] : def; };

        g->setMethod ("color", [this] (const juce::var::NativeFunctionArgs& a)
        {
            colour = parseColour (a.numArguments > 0 ? a.arguments[0] : juce::var(), a.numArguments > 1 ? (float) (double) a.arguments[1] : -1.0f);
            gradient.reset();
            return juce::var();
        });
        g->setMethod ("gradient", [this, num] (const juce::var::NativeFunctionArgs& a)
        {
            if (a.numArguments >= 6)
                gradient = juce::ColourGradient (parseColour (a.arguments[2]), num (a, 0), num (a, 1), parseColour (a.arguments[5]), num (a, 3), num (a, 4), false);
            return juce::var();
        });
        g->setMethod ("alpha", [this, num] (const juce::var::NativeFunctionArgs& a) { if (graphics) graphics->setOpacity (juce::jlimit (0.0f, 1.0f, num (a, 0, 1.0f))); return juce::var(); });
        g->setMethod ("rect", [this, num] (const juce::var::NativeFunctionArgs& a) { if (graphics) { applyFill(); graphics->fillRect (num (a, 0), num (a, 1), num (a, 2), num (a, 3)); } return juce::var(); });
        g->setMethod ("roundRect", [this, num] (const juce::var::NativeFunctionArgs& a) { if (graphics) { applyFill(); graphics->fillRoundedRectangle (num (a, 0), num (a, 1), num (a, 2), num (a, 3), num (a, 4)); } return juce::var(); });
        g->setMethod ("strokeRect", [this, num] (const juce::var::NativeFunctionArgs& a) { if (graphics) { applyFill(); graphics->drawRect (num (a, 0), num (a, 1), num (a, 2), num (a, 3), num (a, 4, 1.0f)); } return juce::var(); });
        g->setMethod ("ellipse", [this, num] (const juce::var::NativeFunctionArgs& a) { if (graphics) { applyFill(); graphics->fillEllipse (num (a, 0), num (a, 1), num (a, 2), num (a, 3)); } return juce::var(); });
        g->setMethod ("line", [this, num] (const juce::var::NativeFunctionArgs& a) { if (graphics) { applyFill(); graphics->drawLine (num (a, 0), num (a, 1), num (a, 2), num (a, 3), num (a, 4, 1.0f)); } return juce::var(); });
        g->setMethod ("arc", [this, num] (const juce::var::NativeFunctionArgs& a)
        {
            if (graphics == nullptr) return juce::var();
            juce::Path p;
            p.addCentredArc (num (a, 0), num (a, 1), num (a, 2), num (a, 2), 0.0f, num (a, 3), num (a, 4), true);
            applyFill();
            graphics->strokePath (p, juce::PathStrokeType (num (a, 5, 2.0f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            return juce::var();
        });
        g->setMethod ("path", [this, num] (const juce::var::NativeFunctionArgs& a)
        {
            if (graphics == nullptr || a.numArguments < 1) return juce::var();
            auto* pts = a.arguments[0].getArray();
            if (pts == nullptr || pts->size() < 4) return juce::var();
            juce::Path p;
            p.startNewSubPath ((float) (*pts)[0], (float) (*pts)[1]);
            for (int i = 2; i + 1 < pts->size(); i += 2) p.lineTo ((float) (*pts)[i], (float) (*pts)[i + 1]);
            const bool closed = a.numArguments > 1 && (bool) a.arguments[1];
            if (closed) p.closeSubPath();
            applyFill();
            const auto mode = a.numArguments > 2 ? a.arguments[2].toString() : juce::String ("stroke");
            if (mode == "fill") graphics->fillPath (p);
            else graphics->strokePath (p, juce::PathStrokeType (num (a, 3, 1.5f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            return juce::var();
        });
        g->setMethod ("text", [this, num] (const juce::var::NativeFunctionArgs& a)
        {
            if (graphics == nullptr || a.numArguments < 5) return juce::var();
            const auto align = a.numArguments > 5 ? a.arguments[5].toString() : juce::String ("left");
            const float size = num (a, 6, 13.0f);
            const bool bold = a.numArguments > 7 && (bool) a.arguments[7];
            graphics->setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
            applyFill();
            const auto j = align == "center" ? juce::Justification::centred : align == "right" ? juce::Justification::centredRight : juce::Justification::centredLeft;
            graphics->drawFittedText (a.arguments[0].toString(), juce::Rectangle<float> (num (a, 1), num (a, 2), num (a, 3), num (a, 4)).toNearestInt(), j, 1);
            return juce::var();
        });
        g->setMethod ("save", [this] (const juce::var::NativeFunctionArgs&) { if (graphics) graphics->saveState(); return juce::var(); });
        g->setMethod ("restore", [this] (const juce::var::NativeFunctionArgs&) { if (graphics) graphics->restoreState(); return juce::var(); });
        g->setMethod ("translate", [this, num] (const juce::var::NativeFunctionArgs& a) { if (graphics) graphics->addTransform (juce::AffineTransform::translation (num (a, 0), num (a, 1))); return juce::var(); });
        g->setMethod ("rotate", [this, num] (const juce::var::NativeFunctionArgs& a) { if (graphics) graphics->addTransform (juce::AffineTransform::rotation (num (a, 0), num (a, 1), num (a, 2))); return juce::var(); });
    }
};

//==============================================================================
PluginManager::PluginManager()
{
    state = new juce::DynamicObject();
    startTimer (700);
}

PluginManager::~PluginManager()
{
    stopTimer();
    runtimes.clear();
}

void PluginManager::setSearchFolders (juce::Array<juce::File> f)
{
    folders = std::move (f);
    rescan();
}

void PluginManager::rescan()
{
    std::map<juce::String, bool> uiEnabled, dspEnabled;
    for (auto& p : uiPlugins) uiEnabled[p.id] = p.enabled;
    for (auto& p : dspPlugins) dspEnabled[p.id] = p.enabled;
    uiPlugins.clear();
    dspPlugins.clear();

    for (auto& root : folders)
    {
        // Plug-ins removed while their library was loaded (Windows): finish the job now.
        for (auto kind : { "UI", "DSP" })
            for (const auto& e : juce::RangedDirectoryIterator (root.getChildFile (kind), false, "*", juce::File::findDirectories))
                if (e.getFile().getChildFile (".nx-uninstalled").exists() && ! e.getFile().deleteRecursively())
                    e.getFile().getChildFile (".nx-uninstalled").create();   // still locked: try again next time

        for (const auto& e : juce::RangedDirectoryIterator (root.getChildFile ("UI"), false, "*", juce::File::findDirectories))
        {
            const auto m = juce::JSON::parse (e.getFile().getChildFile ("manifest.json"));
            if (! m.isObject()) continue;
            UiPluginInfo p;
            p.folder = e.getFile();
            p.id = m.getProperty ("id", p.folder.getFileName()).toString();
            p.name = m.getProperty ("name", p.id).toString();
            p.version = m.getProperty ("version", "1.0").toString();
            p.author = m.getProperty ("author", {}).toString();
            p.description = m.getProperty ("description", {}).toString();
            p.script = p.folder.getChildFile (m.getProperty ("main", "plugin.js").toString());
            p.fps = juce::jlimit (1, 120, (int) m.getProperty ("fps", 30));
            p.preferredHeight = juce::jlimit (24, 600, (int) m.getProperty ("preferredHeight", 120));
            p.layout = m.getProperty ("layout", {});
            if (auto* s = m.getProperty ("slots", {}).getArray()) for (auto& v : *s) p.slots.add (v.toString());
            p.enabled = uiEnabled.count (p.id) ? uiEnabled[p.id] : (bool) m.getProperty ("enabledByDefault", false);
            uiPlugins.erase (std::remove_if (uiPlugins.begin(), uiPlugins.end(), [&] (auto& x) { return x.id == p.id; }), uiPlugins.end());
            uiPlugins.push_back (p);
        }

        for (const auto& e : juce::RangedDirectoryIterator (root.getChildFile ("DSP"), false, "*", juce::File::findDirectories))
        {
            const auto m = juce::JSON::parse (e.getFile().getChildFile ("manifest.json"));
            if (! m.isObject()) continue;
            DspPluginInfo p;
            p.folder = e.getFile();
            p.id = m.getProperty ("id", p.folder.getFileName()).toString();
            p.name = m.getProperty ("name", p.id).toString();
            p.author = m.getProperty ("author", {}).toString();
            p.description = m.getProperty ("description", {}).toString();
            p.library = findLibrary (p.folder, m.getProperty ("library", p.folder.getFileName()).toString());
            p.enabled = dspEnabled.count (p.id) ? dspEnabled[p.id] : false;
            if (p.library == juce::File()) p.error = "bibliothèque non compilée"_t;
            else
            {
                auto lib = std::make_unique<juce::DynamicLibrary>();
                if (! lib->open (p.library.getFullPathName())) p.error = "chargement impossible"_t;
                else if (auto fn = (nx_dsp_get_descriptor_fn) lib->getFunction ("nx_dsp_get_descriptor"))
                {
                    const auto* d = fn();
                    if (d == nullptr || d->api_version != NX_DSP_API_VERSION) p.error = "version d'API incompatible"_t;
                    else
                    {
                        p.name = juce::String::fromUTF8 (d->name);
                        p.description = juce::String::fromUTF8 (d->description);
                        for (int i = 0; i < d->num_params; ++i) p.params.push_back (d->params[i]);
                        if (! dspValues.count (p.id))
                            for (auto& prm : p.params) dspValues[p.id].push_back (prm.default_value);
                        // Never unload a library a running DSP chain may still use: keep the old handle alive.
                        if (auto old = libraries.find (p.id); old != libraries.end() && old->second != nullptr)
                            retiredLibraries.push_back (std::move (old->second));
                        libraries[p.id] = std::move (lib);
                    }
                }
                else p.error = "symbole nx_dsp_get_descriptor absent"_t;
            }
            dspPlugins.erase (std::remove_if (dspPlugins.begin(), dspPlugins.end(), [&] (auto& x) { return x.id == p.id; }), dspPlugins.end());
            dspPlugins.push_back (p);
        }
    }

    for (auto& p : uiPlugins) fingerprints[p.id] = fingerprint (p.folder);
    if (onPluginsChanged) onPluginsChanged();
}

std::vector<UiPluginInfo> PluginManager::getUiPlugins() const { return uiPlugins; }
std::vector<DspPluginInfo> PluginManager::getDspPlugins() const { return dspPlugins; }

void PluginManager::setUiPluginEnabled (const juce::String& id, bool enabled)
{
    for (auto& p : uiPlugins)
        if (p.id == id)
        {
            p.enabled = enabled;
            if (! enabled) runtimes.erase (id);
            else if (p.layout.isObject() && onLayoutRequest) onLayoutRequest (p.layout);
        }
    if (onPluginsChanged) onPluginsChanged();
}

juce::StringArray PluginManager::getEnabledForSlot (const juce::String& slot) const
{
    juce::StringArray ids;
    for (auto& p : uiPlugins)
        if (p.enabled && p.slots.contains (slot)) ids.add (p.id);
    return ids;
}

int PluginManager::getPreferredHeight (const juce::String& id) const
{
    for (auto& p : uiPlugins) if (p.id == id) return p.preferredHeight;
    return 120;
}

int PluginManager::getFps (const juce::String& id) const
{
    for (auto& p : uiPlugins) if (p.id == id) return p.fps;
    return 30;
}

juce::String PluginManager::getUiError (const juce::String& id) const
{
    auto it = runtimes.find (id);
    return it != runtimes.end() && it->second != nullptr ? it->second->error : juce::String();
}

void PluginManager::setState (const juce::String& key, const juce::var& value)
{
    state->setProperty (key, value);
}

bool PluginManager::loadScript (UiRuntime& rt)
{
    rt.engine = std::make_unique<juce::JavascriptEngine>();
    rt.engine->maximumExecutionTime = juce::RelativeTime::milliseconds (60);
    rt.buildGraphicsObject();

    auto* ui = new juce::DynamicObject();
    std::weak_ptr<int> token = alive;
    ui->setMethod ("setLayout", [this, token] (const juce::var::NativeFunctionArgs& a)
    {
        if (a.numArguments > 0 && onLayoutRequest)
        {
            const auto layout = a.arguments[0];
            juce::MessageManager::callAsync ([this, layout, token] { if (! token.expired() && onLayoutRequest) onLayoutRequest (layout); });
        }
        return juce::var();
    });
    auto* logger = new juce::DynamicObject();
    const auto id = rt.id;
    logger->setMethod ("log", [id] (const juce::var::NativeFunctionArgs& a)
    {
        juce::String s;
        for (int i = 0; i < a.numArguments; ++i) s << a.arguments[i].toString() << " ";
        juce::Logger::writeToLog ("[" + id + "] " + s);
        return juce::var();
    });
    if (ownsSharedObjects || ! state->hasProperty ("ui"))
    {
        state->setProperty ("ui", juce::var (ui));
        state->setProperty ("console", juce::var (logger));
    }
    rt.engine->registerNativeObject ("nx", state.get());

    const auto res = rt.engine->execute (rt.script.loadFileAsString());
    if (res.failed()) { rt.error = res.getErrorMessage(); return false; }
    rt.error.clear();
    rt.initDone = false;
    return true;
}

juce::File PluginManager::getPluginFolder (const juce::String& id) const
{
    for (auto& p : uiPlugins) if (p.id == id) return p.folder;
    for (auto& p : dspPlugins) if (p.id == id) return p.folder;
    return {};
}

bool PluginManager::render (const juce::String& id, const juce::String& slot, juce::Graphics& g, juce::Rectangle<float> area, bool preview)
{
    const UiPluginInfo* info = nullptr;
    for (auto& p : uiPlugins) if (p.id == id) info = &p;
    if (info == nullptr || (! info->enabled && ! preview)) return false;

    auto& rt = runtimes[preview ? "preview|" + id : id];
    if (rt == nullptr)
    {
        rt = std::make_unique<UiRuntime>();
        rt->id = id;   // the preview instance reports the same id
        rt->script = info->script;
        loadScript (*rt);
    }

    auto showError = [&] (const juce::String& e)
    {
        g.setColour (juce::Colour (0xffff5c6c).withAlpha (0.9f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText (info->name + " : " + e, area.toNearestInt().reduced (6), juce::Justification::centredLeft, 3);
        return false;
    };
    if (rt->error.isNotEmpty()) return showError (rt->error);

    g.saveState();
    g.reduceClipRegion (area.toNearestInt());
    g.setOrigin (area.getTopLeft().toInt());
    rt->graphics = &g;
    rt->colour = juce::Colours::white;
    rt->gradient.reset();

    juce::Result r = juce::Result::ok();
    if (! rt->initDone)
    {
        rt->initDone = true;
        const juce::var initArgs[] = { juce::var (state.get()) };
        rt->engine->callFunction ("init", juce::var::NativeFunctionArgs (juce::var(), initArgs, 1), &r);
        r = juce::Result::ok();   // init() is optional
    }
    const juce::var args[] = { juce::var (rt->g.get()), area.getWidth(), area.getHeight(), juce::var (state.get()), slot };
    rt->engine->callFunction ("render", juce::var::NativeFunctionArgs (juce::var(), args, 5), &r);
    rt->graphics = nullptr;
    g.restoreState();

    if (r.failed())
    {
        rt->error = r.getErrorMessage();
        return showError (rt->error);
    }
    return true;
}

juce::int64 PluginManager::fingerprint (const juce::File& folder) const
{
    juce::int64 f = 0;
    for (const auto& e : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findFiles))
        f ^= e.getModificationTime().toMilliseconds() * 131 + e.getFileSize();
    return f;
}

void PluginManager::timerCallback()
{
    // Hot reload of UI plug-ins: a changed script restarts its engine on the next frame.
    for (auto& p : uiPlugins)
    {
        const auto fp = fingerprint (p.folder);
        if (fingerprints[p.id] == fp) continue;
        fingerprints[p.id] = fp;
        const auto m = juce::JSON::parse (p.folder.getChildFile ("manifest.json"));
        if (m.isObject())
        {
            p.fps = juce::jlimit (1, 120, (int) m.getProperty ("fps", p.fps));
            p.preferredHeight = juce::jlimit (24, 600, (int) m.getProperty ("preferredHeight", p.preferredHeight));
        }
        runtimes.erase (p.id);
        runtimes.erase ("preview|" + p.id);
        if (onPluginsChanged) onPluginsChanged();
    }
}

//==============================================================================
void PluginManager::setDspPluginEnabled (const juce::String& id, bool enabled)
{
    for (auto& p : dspPlugins) if (p.id == id) p.enabled = enabled && p.error.isEmpty();
    if (onPluginsChanged) onPluginsChanged();
}

void PluginManager::setDspParameter (const juce::String& id, int index, double value)
{
    auto& v = dspValues[id];
    if (index >= 0 && index < (int) v.size()) v[(size_t) index] = value;
    if (onPluginsChanged) onPluginsChanged();
}

double PluginManager::getDspParameter (const juce::String& id, int index) const
{
    auto it = dspValues.find (id);
    return it != dspValues.end() && index >= 0 && index < (int) it->second.size() ? it->second[(size_t) index] : 0.0;
}

std::unique_ptr<DspChain> PluginManager::buildDspChain()
{
    auto chain = std::make_unique<DspChain>();
    for (auto& p : dspPlugins)
    {
        if (! p.enabled) continue;
        auto lib = libraries.find (p.id);
        if (lib == libraries.end()) continue;
        if (auto fn = (nx_dsp_get_descriptor_fn) lib->second->getFunction ("nx_dsp_get_descriptor"))
            chain->processors.push_back (std::make_unique<DynamicDspProcessor> (fn(), dspValues[p.id]));
    }
    return chain;
}

} // namespace nx
