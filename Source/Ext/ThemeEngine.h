// NX Player — MIT License
// Themes (.nxtheme): a folder (or a .zip renamed .nxtheme) with
//   manifest.json   { "id", "name", "author", "version", "description", "stylesheet": "theme.css",
//                     "background": { "type": "artwork" | "gradient" | "mesh" | "shader", "shader": "background.frag", ... } }
//   theme.css       :root { --nx-bg: #0A0C0E; --nx-accent: #00F5A0; ... }   (CSS custom properties, var() allowed)
//   background.frag optional GLSL fragment shader (OpenGL builds)
// Themes are hot-reloaded: saving theme.css or manifest.json updates the running app.
#pragma once

#include <juce_graphics/juce_graphics.h>
#include <juce_events/juce_events.h>
#include <functional>
#include <map>

namespace nx
{

struct Theme
{
    juce::String id, name, author, version, description;
    juce::File folder;
    std::map<juce::String, juce::String> vars;     // "--nx-accent" -> "#00F5A0" (var() resolved)
    juce::var background;                          // manifest "background" object
    juce::String shaderCode;

    juce::Colour colour (const juce::String& name, juce::Colour fallback = juce::Colours::magenta) const;
    float number (const juce::String& name, float fallback = 0.0f) const;
    juce::String text (const juce::String& name, const juce::String& fallback = {}) const;

    // Shortcuts used everywhere in the UI
    juce::Colour bg() const        { return colour ("--nx-bg", juce::Colour (0xff0a0c0e)); }
    juce::Colour surface() const   { return colour ("--nx-surface", juce::Colour (0xff12161a)); }
    juce::Colour glassTint() const { return colour ("--nx-glass-tint", juce::Colour (0x8c0e1216)); }
    juce::Colour border() const    { return colour ("--nx-border", juce::Colours::white.withAlpha (0.08f)); }
    juce::Colour textCol() const   { return colour ("--nx-text", juce::Colour (0xffeef3f1)); }
    juce::Colour textDim() const   { return colour ("--nx-text-dim", juce::Colour (0xff8a9792)); }
    juce::Colour accent() const    { return colour ("--nx-accent", juce::Colour (0xff00f5a0)); }
    juce::Colour accent2() const   { return colour ("--nx-accent-2", juce::Colour (0xff00d9f5)); }
    float radius() const           { return number ("--nx-radius", 14.0f); }
};

/** CSS parsing (exposed for tests). */
std::map<juce::String, juce::String> parseCssVariables (const juce::String& css);
bool parseCssColour (const juce::String& value, juce::Colour& out);

class ThemeManager : private juce::Timer
{
public:
    ThemeManager();
    ~ThemeManager() override;

    /** Folders searched for themes: bundled first, then the user's folder (which wins on equal ids). */
    void setSearchFolders (juce::Array<juce::File> folders);
    void rescan();
    juce::StringArray getThemeIds() const;
    const Theme* findTheme (const juce::String& id) const;
    std::vector<Theme> getThemes() const { return themes; }

    bool apply (const juce::String& id);
    const Theme& current() const noexcept { return active; }

    /** Copies a .nxtheme folder or zip into the user folder and returns its id. */
    juce::String importTheme (const juce::File& source, juce::String& error);
    /** Zips a theme for sharing. */
    bool exportTheme (const juce::String& id, const juce::File& destination);

    static bool loadTheme (const juce::File& folder, Theme& out, juce::String& error);

    /** Called after apply() and after every hot reload. */
    std::function<void (const Theme&)> onThemeChanged;
    void checkForChanges() { timerCallback(); }   // tests

private:
    void timerCallback() override;
    juce::int64 fingerprint (const juce::File& folder) const;

    juce::Array<juce::File> searchFolders;
    std::vector<Theme> themes;
    Theme active;
    juce::int64 activeFingerprint = 0;
};

} // namespace nx
