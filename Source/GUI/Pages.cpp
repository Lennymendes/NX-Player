// NX Player — MIT License
#include "Pages.h"
#include "../Engine/DeviceProfiles.h"
#include "../Library/Downloads.h"
#include "../Providers/StreamingProviders.h"
#include "../Library/LibraryScanner.h"
#include "../Ext/PluginManager.h"
#include "../Platform/AuraBridge.h"
#include "../Platform/SystemMonitor.h"
#include "../Remote/RemoteServer.h"
#include "../DSP/StudioTools.h"
#include "LibraryActions.h"
#include "Dashboard.h"

namespace nx
{

//==============================================================================
// ListingPage
//==============================================================================
ListingPage::ListingPage (AppContext& c) : ScrollPage (c), grid (c), list (c), pills (c), heroArt (c, 640)
{
    contentComp.addAndMakeVisible (header);
    contentComp.addChildComponent (pills);
    contentComp.addChildComponent (artistHeader);
    pills.onSelect = [this] (const juce::String& id) { ctx.navigate ({ PageId::Library, id }); };
    contentComp.addAndMakeVisible (grid);
    contentComp.addAndMakeVisible (list);
    contentComp.addChildComponent (heroArt);
    contentComp.addChildComponent (playAll);
    contentComp.addChildComponent (shuffleAll);
    contentComp.addChildComponent (downloadAll);
    contentComp.addChildComponent (cta);
    downloadAll.setTooltip ("Copier ces morceaux du serveur dans le dossier de téléchargement, pour l'écoute hors-ligne"_t);
    downloadAll.onClick = [this]
    {
        const int n = ctx.downloads().enqueue (heroTracks);
        ctx.showMessage (n > 0 ? "Téléchargement de "_t + juce::String (n) + " morceau(x)" : juce::String ("Déjà disponible hors-ligne"_t));
    };
    cta.setToggleState (true, juce::dontSendNotification);
    cta.onClick = [this] { if (ctaAction) ctaAction(); };
    playAll.setToggleState (true, juce::dontSendNotification);
    playAll.onClick = [this] { if (! heroTracks.empty()) ctx.playTracks (heroTracks, 0); };
    shuffleAll.onClick = [this]
    {
        auto t = heroTracks;
        juce::Random r;
        for (size_t i = t.size(); i > 1; --i) std::swap (t[i - 1], t[(size_t) r.nextInt ((int) i)]);
        if (! t.empty()) ctx.playTracks (t, 0);
    };
    grid.onOpen = [this] (const AlbumInfo& a) { ctx.navigate ({ PageId::AlbumDetail, a.artist, a.title }); };
    grid.onPlay = [this] (const AlbumInfo& a) { ctx.playTracks (ctx.library().getAlbumTracks (a.artist, a.title), 0); };
    ctx.artwork().addChangeListener (this);
}

ListingPage::~ListingPage() { ctx.artwork().removeChangeListener (this); }

void ListingPage::Header::paint (juce::Graphics& g)
{
    const auto& t = page.ctx.theme();
    auto r = getLocalBounds();
    if (page.hero)
    {
        r.removeFromLeft (page.heroArt.getWidth() + 28);
        r.removeFromBottom (54);
        g.setColour (t.textDim());
        g.setFont (style::font (12.0f, true));
        g.drawText ("ALBUM"_t, r.removeFromTop (r.getHeight() - 96), juce::Justification::bottomLeft);
        g.setColour (t.textCol());
        g.setFont (style::font (34.0f, true));
        g.drawFittedText (page.title, r.removeFromTop (44), juce::Justification::bottomLeft, 1);
        g.setColour (t.accent());
        g.setFont (style::font (18.0f, true));
        g.drawText (page.subtitle.upToFirstOccurrenceOf ("\n", false, false), r.removeFromTop (26), juce::Justification::centredLeft, true);
        g.setColour (t.textDim());
        g.setFont (style::font (13.0f));
        g.drawText (page.subtitle.fromFirstOccurrenceOf ("\n", false, false), r.removeFromTop (22), juce::Justification::centredLeft, true);
        return;
    }
    drawPageTitle (g, t, r, page.title, page.subtitle);
}

void ListingPage::layoutContent (int width)
{
    const int pad = 32;
    const int inner = juce::jmax (100, width - pad * 2);
    int y = 24;

    heroArt.setVisible (hero);
    playAll.setVisible (hero || ! heroTracks.empty());
    shuffleAll.setVisible (hero || ! heroTracks.empty());
    bool remote = false;
    for (auto& t : heroTracks) remote = remote || DownloadManager::canDownload (t);
    downloadAll.setVisible (remote && playAll.isVisible());
    artistHeader.setVisible (artistName.isNotEmpty());
    header.setVisible (artistName.isEmpty());
    if (artistName.isNotEmpty())
    {
        const int h = artistHeader.heightForWidth (inner);
        artistHeader.setBounds (pad, y, inner, h);
        y += h + 12;
        if (! heroTracks.empty())
        {
            playAll.setBounds (pad, y, 120, 36);
            shuffleAll.setBounds (pad + 132, y, 130, 36);
            downloadAll.setBounds (pad + 274, y, 140, 36);
            y += 56;
        }
    }
    else if (hero)
    {
        const int art = juce::jmin (240, inner / 3);
        heroArt.setArtwork (heroArtwork, title);
        heroArt.setBounds (pad, y, art, art);
        header.setBounds (pad, y, inner, art);
        playAll.setBounds (pad + art + 28, y + art - 44, 120, 38);
        shuffleAll.setBounds (pad + art + 160, y + art - 44, 130, 38);
        downloadAll.setBounds (pad + art + 302, y + art - 44, 140, 38);
        y += art + 28;
    }
    else
    {
        header.setBounds (pad, y, inner, subtitle.isNotEmpty() ? 74 : 54);
        y += header.getHeight() + 8;
        pills.setVisible (showPills);
        if (showPills)
        {
            pills.setBounds (pad, y, inner, 38);
            y += 50;
        }
        if (! heroTracks.empty())
        {
            playAll.setBounds (pad, y, 120, 36);
            shuffleAll.setBounds (pad + 132, y, 130, 36);
            downloadAll.setBounds (pad + 274, y, 140, 36);
            y += 52;
        }
    }
    if (hero || artistName.isNotEmpty()) pills.setVisible (false);

    cta.setVisible (ctaText.isNotEmpty() && ! hero);
    if (cta.isVisible())
    {
        cta.setButtonText (ctaText);
        cta.setBounds (pad, y, 260, 40);
        y += 60;
    }

    if (topSlot != nullptr && ! topSlot->isEmpty())
    {
        if (topSlot->getParentComponent() != &contentComp) contentComp.addAndMakeVisible (topSlot);
        const int h = juce::jmin (220, topSlot->getPreferredHeight());
        topSlot->setBounds (pad, y, inner, h);
        y += h + 20;
    }

    if (artistName.isNotEmpty() && grid.getAlbums().size() > 0)
    {
        struct Caption : juce::Component
        {
            ListingPage& page;
            explicit Caption (ListingPage& p) : page (p) { setInterceptsMouseClicks (false, false); }
            void paint (juce::Graphics& g) override
            {
                g.setColour (page.ctx.theme().textCol());
                g.setFont (style::font (20.0f, true));
                g.drawText ("Discographie dans la bibliothèque"_t, getLocalBounds(), juce::Justification::centredLeft);
            }
        };
        if (contentComp.findChildWithID ("disco") == nullptr)
        {
            auto* c = new Caption (*this);
            c->setComponentID ("disco");
            contentComp.addAndMakeVisible (c);   // owned by contentComp's children list via deleter below
            captionOwner.reset (c);
        }
        if (auto* c = contentComp.findChildWithID ("disco")) { c->setVisible (true); c->setBounds (pad, y, inner, 32); }
        y += 40;
    }
    else if (auto* c = contentComp.findChildWithID ("disco")) c->setVisible (false);

    const int gh = grid.heightForWidth (inner);
    grid.setBounds (pad, y, inner, gh);
    y += gh + (gh > 0 ? 16 : 0);

    list.setBounds (pad - 10, y, inner + 20, list.getTotalHeight());
    y += list.getTotalHeight() + 120;
    setContentHeight (y);
}

//==============================================================================
ListingPage::ArtistHeader::ArtistHeader (ListingPage& p) : page (p)
{
    page.ctx.artistInfo().addChangeListener (this);
    page.ctx.artwork().addChangeListener (this);
}

ListingPage::ArtistHeader::~ArtistHeader()
{
    page.ctx.artistInfo().removeChangeListener (this);
    page.ctx.artwork().removeChangeListener (this);
}

void ListingPage::ArtistHeader::setArtist (const juce::String& n, int albums, int tracks)
{
    if (n != name) expanded = false;
    name = n;
    albumCount = albums;
    trackCount = tracks;
    meta = page.ctx.artistInfo().get (name);
    repaint();
}

void ListingPage::ArtistHeader::changeListenerCallback (juce::ChangeBroadcaster* b)
{
    if (b == &page.ctx.artistInfo() && name.isNotEmpty())
    {
        auto m = page.ctx.artistInfo().get (name, false);
        const bool changed = m.has_value() != meta.has_value() || (m && meta && (m->bio != meta->bio || m->imageUrl != meta->imageUrl));
        meta = m;
        if (changed) page.layoutContent (page.getWidth() - page.viewport.getScrollBarThickness());
    }
    repaint();
}

int ListingPage::ArtistHeader::heightForWidth (int width) const
{
    int h = 300;   // banner + name block
    if (hasBio())
    {
        juce::AttributedString as;
        as.append (meta->bio, style::font (14.5f));
        as.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout tl;
        tl.createLayout (as, (float) juce::jmin (width, 900));
        const float full = tl.getHeight();
        h += 18 + (int) (expanded ? full : juce::jmin (full, 4.0f * 22.0f)) + 34;
    }
    return h;
}

void ListingPage::ArtistHeader::mouseUp (const juce::MouseEvent& e)
{
    if (moreButton.contains (e.getPosition()))
    {
        expanded = ! expanded;
        page.layoutContent (page.getWidth() - page.viewport.getScrollBarThickness());
    }
}

void ListingPage::ArtistHeader::paint (juce::Graphics& g)
{
    const auto& t = page.ctx.theme();
    auto r = getLocalBounds();
    auto banner = r.removeFromTop (240).toFloat();
    // banner: the artist's fanart, else the blurred photo / album art, under a dark gradient
    juce::Path clip;
    clip.addRoundedRectangle (banner, t.radius());
    g.saveState();
    g.reduceClipRegion (clip);
    juce::Image img;
    if (meta && meta->bannerUrl.isNotEmpty()) img = page.ctx.artwork().get (meta->bannerUrl, 1280);
    if (! img.isValid() && meta && meta->imageUrl.isNotEmpty()) img = page.ctx.artwork().get (meta->imageUrl, 640);
    if (! img.isValid() && ! page.grid.getAlbums().empty()) img = page.ctx.artwork().get (page.grid.getAlbums().front().artworkKey, 640);
    if (img.isValid())
    {
        const float scale = juce::jmax (banner.getWidth() / (float) img.getWidth(), banner.getHeight() / (float) img.getHeight());
        const float w = (float) img.getWidth() * scale, h = (float) img.getHeight() * scale;
        g.setOpacity (0.9f);
        g.drawImage (img, juce::Rectangle<float> (w, h).withCentre (banner.getCentre()).withY (banner.getY() - (h - banner.getHeight()) * 0.3f));
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (t.accent().withAlpha (0.5f), banner.getX(), banner.getY(), t.accent2().withAlpha (0.3f), banner.getRight(), banner.getBottom(), false));
        g.fillRect (banner);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.05f), 0, banner.getY(), t.bg().withAlpha (0.92f), 0, banner.getBottom(), false));
    g.fillRect (banner);
    g.restoreState();

    // round photo, overlapping the banner
    const float d = 150.0f;
    auto photo = juce::Rectangle<float> (d, d).withPosition (banner.getX() + 28.0f, banner.getBottom() - d * 0.62f);
    juce::Path circle;
    circle.addEllipse (photo);
    juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 20, { 0, 6 }).drawForPath (g, circle);
    juce::Image face;
    if (meta && meta->imageUrl.isNotEmpty()) face = page.ctx.artwork().get (meta->imageUrl, 320);
    if (! face.isValid() && ! page.grid.getAlbums().empty()) face = page.ctx.artwork().get (page.grid.getAlbums().front().artworkKey, 320);
    if (! face.isValid()) face = imaging::placeholder (name, 160, t.accent(), t.accent2());
    g.saveState();
    g.reduceClipRegion (circle);
    g.setOpacity (1.0f);
    const float side = (float) juce::jmin (face.getWidth(), face.getHeight());
    g.drawImage (face, photo.getX(), photo.getY(), photo.getWidth(), photo.getHeight(),
                 (int) (((float) face.getWidth() - side) * 0.5f), (int) (((float) face.getHeight() - side) * 0.2f), (int) side, (int) side);
    g.restoreState();
    g.setColour (t.bg());
    g.drawEllipse (photo, 4.0f);

    // name + meta
    auto text = juce::Rectangle<float> (photo.getRight() + 24.0f, banner.getBottom() - 92.0f, banner.getRight() - photo.getRight() - 40.0f, 92.0f);
    g.setColour (t.textDim());
    g.setFont (style::font (12.0f, true));
    g.drawText ("ARTISTE"_t, text.removeFromTop (18), juce::Justification::centredLeft);
    g.setColour (t.textCol());
    g.setFont (style::font (38.0f, true));
    g.drawFittedText (name, text.removeFromTop (46).toNearestInt(), juce::Justification::centredLeft, 1);
    juce::StringArray facts;
    if (meta && meta->genre.isNotEmpty()) facts.add (meta->genre);
    if (meta && meta->country.isNotEmpty()) facts.add (meta->country);
    facts.add (juce::String (albumCount) + " album(s) · "_t + juce::String (trackCount) + " morceau(x)"_t);
    g.setColour (t.accent());
    g.setFont (style::font (14.0f, true));
    g.drawText (facts.joinIntoString ("  ·  "_t), text, juce::Justification::centredLeft, true);

    r.removeFromTop (60);
    moreButton = {};
    if (hasBio())
    {
        auto bioArea = r.withWidth (juce::jmin (r.getWidth(), 900)).toFloat();
        juce::AttributedString as;
        as.append (meta->bio, style::font (14.5f), t.textCol().withAlpha (0.88f));
        as.setWordWrap (juce::AttributedString::byWord);
        as.setLineSpacing (3.0f);
        juce::TextLayout tl;
        tl.createLayout (as, bioArea.getWidth());
        const float full = tl.getHeight();
        const float shown = expanded ? full : juce::jmin (full, 4.0f * 22.0f);
        g.saveState();
        g.reduceClipRegion (bioArea.withHeight (shown).toNearestInt());
        tl.draw (g, bioArea.withHeight (full));
        g.restoreState();
        if (! expanded && full > shown)
        {
            g.setGradientFill (juce::ColourGradient (t.bg().withAlpha (0.0f), 0, bioArea.getY() + shown - 30.0f, t.bg().withAlpha (0.85f), 0, bioArea.getY() + shown, false));
            g.fillRect (bioArea.withY (bioArea.getY() + shown - 30.0f).withHeight (30.0f));
        }
        auto under = juce::Rectangle<int> ((int) bioArea.getX(), (int) (bioArea.getY() + shown + 8.0f), (int) bioArea.getWidth(), 22);
        if (full > 4.0f * 22.0f)
        {
            moreButton = under.withWidth (140);
            g.setColour (t.accent());
            g.setFont (style::font (13.0f, true));
            g.drawText (expanded ? "Réduire"_t : "Lire la suite"_t, moreButton, juce::Justification::centredLeft);
        }
        g.setColour (t.textDim());
        g.setFont (style::font (11.5f));
        g.drawText ("Biographie : "_t + meta->bioSource, under, juce::Justification::centredRight);
    }
}

//==============================================================================
// SourcePage
//==============================================================================
SourcePage::SourcePage (AppContext& c) : ScrollPage (c) {}

juce::TextEditor* SourcePage::addField (const juce::String& label, const juce::String& key, bool password)
{
    auto* ed = new juce::TextEditor();
    ed->setTextToShowWhenEmpty (label, ctx.theme().textDim());
    ed->setFont (style::font (14.0f));
    ed->setIndents (12, 0);
    ed->setJustification (juce::Justification::centredLeft);
    if (password) ed->setPasswordCharacter ((juce::juce_wchar) 0x2022);
    else ed->setText (ctx.providers().getCredentials().get (sourceId, key), false);
    ed->onTextChange = [this, ed, key, password] { if (! password) ctx.providers().getCredentials().set (sourceId, key, ed->getText().trim()); };
    ed->setName (key);
    rows.add (ed);
    rowHeights.push_back (36);
    contentComp.addAndMakeVisible (ed);
    return ed;
}

juce::TextButton* SourcePage::addButton (const juce::String& text, std::function<void()> fn)
{
    auto* b = new juce::TextButton (text);
    b->onClick = std::move (fn);
    rows.add (b);
    rowHeights.push_back (38);
    contentComp.addAndMakeVisible (b);
    return b;
}

void SourcePage::addText (const juce::String& text, bool dim)
{
    auto* l = new juce::Label ({}, text);
    l->setFont (style::font (14.0f));
    l->setColour (juce::Label::textColourId, dim ? ctx.theme().textDim() : ctx.theme().textCol());
    l->setJustificationType (juce::Justification::topLeft);
    l->setMinimumHorizontalScale (1.0f);
    rows.add (l);
    rowHeights.push_back (juce::jmax (24, 22 * (1 + text.length() / 110 + juce::StringArray::fromLines (text).size() - 1)));
    contentComp.addAndMakeVisible (l);
}

void SourcePage::setSource (const juce::String& id, const juce::String& arg)
{
    sourceId = id;
    sourceArg = arg;
    rows.clear();
    rowHeights.clear();
    tracks.reset();
    albums.reset();
    contentComp.removeAllChildren();

    if (id == "local") buildLocal();
    else if (id == "upnp") buildUpnp();
    else if (auto* p = ctx.providers().getProvider (id)) buildProvider (*p);
    layoutContent (getWidth() - viewport.getScrollBarThickness());
    repaint();
}

void SourcePage::buildLocal()
{
    heading = "Fichiers locaux";
    description = "Disques internes, disques externes et partages réseau montés (SMB / NFS / AFP). Les dossiers sont surveillés : "_t
                  "ajouts, suppressions et modifications apparaissent tout seuls."_t;
    for (auto& f : ctx.library().getFolders())
    {
        auto* row = addButton ("Retirer"_t + "  " + f, nullptr);
        row->onClick = [this, f]
        {
            ctx.library().removeFolder (juce::File (f));
            ctx.library().notifyChanged();
            setSource ("local");
        };
    }
    addButton ("Ajouter un dossier…"_t, [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Dossier de musique", juce::File::getSpecialLocation (juce::File::userMusicDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this, chooser] (const juce::FileChooser& fc)
        {
            const auto dir = fc.getResult();
            if (! dir.isDirectory()) return;
            ctx.library().addFolder (dir);
            ctx.scanner().requestFullScan();
            ctx.showMessage ("Indexation de " + dir.getFileName() + "…"_t);
            setSource ("local");
        });
    });
    addButton ("Réindexer maintenant"_t, [this] { ctx.scanner().requestFullScan(); ctx.showMessage ("Réindexation lancée"_t); });
    const auto& pr = ctx.scanner().getProgress();
    addText (juce::String (ctx.library().countTracks ("local")) + " morceaux indexés"_t + (pr.scanning.load() ? " · indexation en cours…"_t : ""));
    albums = std::make_unique<AlbumGrid> (ctx);
    albums->setAlbums (ctx.library().getAlbums ("artist", 2000, "local"));
    albums->onOpen = [this] (const AlbumInfo& a) { ctx.navigate ({ PageId::AlbumDetail, a.artist, a.title }); };
    albums->onPlay = [this] (const AlbumInfo& a) { ctx.playTracks (ctx.library().getAlbumTracks (a.artist, a.title), 0); };
    contentComp.addAndMakeVisible (*albums);
}

juce::String SourcePage::upnpArg (const juce::String& udn, const juce::String& objectId, const juce::StringArray& path)
{
    return udn + "\x1f" + objectId + "\x1f" + path.joinIntoString ("\x1e");
}

void SourcePage::buildUpnp()
{
    heading = "NAS (UPnP / DLNA)";
    description = "Serveurs de musique du réseau local (MinimServer, Asset, Synology, QNAP, JRiver, Jellyfin…). Les morceaux sont lus en flux HTTP "_t
                  "par le moteur de NX Player, donc en bit-perfect. Pour un partage SMB, ajoutez simplement le dossier monté dans Fichiers locaux."_t;
    auto* upnp = dynamic_cast<UpnpProvider*> (ctx.providers().getProvider ("upnp"));
    if (upnp == nullptr) return;
    juce::Component::SafePointer<SourcePage> safe (this);
    auto* app = &ctx;

    const auto parts = juce::StringArray::fromTokens (sourceArg, "\x1f", {});
    if (sourceArg.isNotEmpty() && parts.size() >= 2)
    {
        // ---- inside a server folder -----------------------------------------------------------
        const auto udn = parts[0], objectId = parts[1];
        const auto path = juce::StringArray::fromTokens (parts[2], "\x1e", {});
        UpnpServer server;
        for (auto& sv : upnp->getServers()) if (sv.udn == udn) server = sv;
        if (server.udn.isEmpty()) { addText ("Ce serveur n'est plus disponible : relancez la recherche."_t); return; }
        heading = path.isEmpty() ? server.name : path[path.size() - 1];
        description = path.size() > 1 ? path.joinIntoString ("  ›  "_t, 0, path.size() - 1) : server.name;

        const auto key = udn + "|" + objectId;
        auto it = upnpCache.find (key);
        if (it == upnpCache.end())
        {
            addText ("Chargement du dossier…"_t);
            if (! upnpLoading.contains (key))
            {
                upnpLoading.add (key);
                const auto arg = sourceArg;
                juce::Thread::launch ([safe, upnp, server, objectId, key, arg]
                {
                    auto entries = upnp->browseAll (server, objectId);
                    const auto err = upnp->lastError;
                    juce::MessageManager::callAsync ([safe, entries, key, arg, err]
                    {
                        if (safe == nullptr) return;
                        safe->upnpLoading.removeString (key);
                        safe->upnpCache[key] = entries;
                        if (entries.empty() && err.isNotEmpty()) safe->ctx.showMessage (err);
                        if (safe->sourceId == "upnp" && safe->sourceArg == arg) safe->setSource ("upnp", arg);
                    });
                });
            }
            return;
        }

        const auto& entries = it->second;
        std::vector<TrackInfo> items;
        std::vector<AlbumInfo> folders;
        for (auto& e : entries)
        {
            if (! e.isContainer) { items.push_back (e.track); continue; }
            AlbumInfo a;
            a.title = e.title;
            a.artist = e.track.artist.isNotEmpty() ? e.track.artist
                     : (e.childCount >= 0 ? juce::String (e.childCount) + " éléments"_t : juce::String ("Dossier"));
            a.artworkKey = e.track.artworkKey;
            a.source = e.id;
            folders.push_back (a);
        }

        addButton (items.empty() ? juce::String ("Tout lire (sous-dossiers compris)"_t) : juce::String ("Lire ce dossier"), [safe, upnp, server, objectId, app]
        {
            app->showMessage ("Préparation de la lecture…"_t);
            juce::Thread::launch ([safe, upnp, server, objectId, app]
            {
                auto all = upnp->collectTracks (server, objectId, 5000);
                juce::MessageManager::callAsync ([safe, all, app]
                {
                    if (safe == nullptr) return;
                    if (all.empty()) app->showMessage ("Aucun morceau dans ce dossier"_t);
                    else app->playTracks (all, 0);
                });
            });
        });
        addButton ("Télécharger pour l'écoute hors-ligne"_t, [safe, upnp, server, objectId, app]
        {
            app->showMessage ("Préparation du téléchargement…"_t);
            juce::Thread::launch ([safe, upnp, server, objectId, app]
            {
                auto all = upnp->collectTracks (server, objectId, 5000);
                juce::MessageManager::callAsync ([safe, all, app]
                {
                    if (safe == nullptr) return;
                    const int n = app->downloads().enqueue (all);
                    app->showMessage (n > 0 ? "Téléchargement de "_t + juce::String (n) + " morceau(x) vers "_t + app->downloads().getFolder().getFullPathName()
                                            : juce::String ("Rien à télécharger (déjà hors-ligne ?)"_t));
                });
            });
        });
        addButton ("Importer dans la bibliothèque"_t, [safe, upnp, server, objectId, app]
        {
            auto* lib = &app->library();
            juce::Thread::launch ([safe, app, lib, upnp, server, objectId]
            {
                const int n = upnp->importContainer (*lib, server, objectId);
                juce::MessageManager::callAsync ([safe, app, n] { if (safe != nullptr) app->showMessage (juce::String (n) + " morceaux importés depuis le NAS"_t); });
            });
        });
        addText (juce::String ((int) folders.size()) + " dossier(s) · "_t + juce::String ((int) items.size()) + " morceau(x)"_t);

        if (! folders.empty())
        {
            albums = std::make_unique<AlbumGrid> (ctx);
            albums->setAlbums (folders);
            albums->onOpen = [this, udn, path] (const AlbumInfo& a)
            {
                auto deeper = path;
                deeper.add (a.title);
                ctx.navigate ({ PageId::Source, "upnp", upnpArg (udn, a.source, deeper) });
            };
            albums->onPlay = [safe, upnp, server, app] (const AlbumInfo& a)
            {
                const auto id = a.source;
                juce::Thread::launch ([safe, upnp, server, id, app]
                {
                    auto all = upnp->collectTracks (server, id, 5000);
                    juce::MessageManager::callAsync ([safe, all, app] { if (safe != nullptr && ! all.empty()) app->playTracks (all, 0); });
                });
            };
            contentComp.addAndMakeVisible (*albums);
        }
        if (! items.empty())
        {
            tracks = std::make_unique<TrackList> (ctx);
            tracks->setTracks (items, true, true, true);
            contentComp.addAndMakeVisible (*tracks);
        }
        return;
    }

    // ---- server list ----------------------------------------------------------------------------
    auto searchNetwork = [safe, upnp, app]
    {
        juce::Thread::launch ([safe, upnp, app]
        {
            const auto found = upnp->discover (3000);
            const auto err = upnp->lastError;
            juce::MessageManager::callAsync ([safe, app, found, err]
            {
                if (safe == nullptr) return;
                app->saveNasServers();
                app->showMessage (found.empty() ? err : juce::String ((int) found.size()) + " serveur(s) trouvé(s)"_t);
                safe->autoDiscoveryRunning = false;
                if (safe->sourceId == "upnp" && safe->sourceArg.isEmpty()) safe->setSource ("upnp");
            });
        });
    };
    addButton ("Rechercher les serveurs du réseau"_t, [this, searchNetwork] { autoDiscoveryRunning = true; searchNetwork(); });
    // First visit of the NAS page in this session: look for servers automatically (only now, not at startup).
    if (! autoDiscoveryDone && upnp->getServers().empty())
    {
        autoDiscoveryDone = autoDiscoveryRunning = true;
        searchNetwork();
    }
    if (autoDiscoveryRunning) addText ("Recherche des serveurs UPnP / DLNA du réseau…"_t);

    auto* address = addField ("Adresse IP du NAS ou URL de description (ex. 192.168.1.20)"_t, "__nas_address", true);
    address->setPasswordCharacter (0);
    auto connect = [safe, upnp, app, address]
    {
        auto text = address->getText().trim();
        if (text.isEmpty()) return;
        app->showMessage ("Connexion à "_t + text + "…"_t);
        juce::Thread::launch ([safe, upnp, app, text]
        {
            const auto before = upnp->getServers().size();
            bool ok;
            if (text.containsChar ('/')) ok = upnp->addServerFromLocation (text.startsWith ("http") ? text : "http://" + text);
            else ok = upnp->discoverHost (text.upToFirstOccurrenceOf (":", false, false)).size() > before;
            const auto err = upnp->lastError;
            juce::MessageManager::callAsync ([safe, app, ok, err]
            {
                if (safe == nullptr) return;
                app->saveNasServers();
                app->showMessage (ok ? juce::String ("Serveur ajouté"_t) : err);
                safe->setSource ("upnp");
            });
        });
    };
    address->onReturnKey = connect;
    addButton ("Ajouter ce serveur"_t, connect);

    const auto servers = upnp->getServers();
    if (servers.empty() && ! autoDiscoveryRunning)
    {
        addText ("Aucun serveur trouvé pour l'instant. Vérifiez que le serveur UPnP / DLNA du NAS est activé (MinimServer, "_t
                 "Synology Media Server, Asset…) ; si votre réseau filtre la découverte automatique, entrez l'adresse IP du NAS ci-dessus."_t);
        return;
    }
    std::vector<AlbumInfo> cards;
    for (auto& sv : servers)
    {
        AlbumInfo a;
        a.title = sv.name;
        a.artist = juce::URL (sv.location).getDomain();
        a.artworkKey = sv.iconUrl;
        a.source = sv.udn;
        cards.push_back (a);
    }
    albums = std::make_unique<AlbumGrid> (ctx);
    albums->setAlbums (cards);
    albums->onOpen = [this] (const AlbumInfo& a) { ctx.navigate ({ PageId::Source, "upnp", upnpArg (a.source, "0", { a.title }) }); };
    albums->onPlay = albums->onOpen;
    contentComp.addAndMakeVisible (*albums);
}

void SourcePage::buildProvider (MediaProvider& p)
{
    heading = p.getDisplayName();
    const auto id = p.getId();
    if (id == "qobuz")
    {
        description = "FLAC jusqu'à 24 bits / 192 kHz, lu par le moteur bit-perfect de NX Player (changement automatique de fréquence du DAC). "_t
                      "L'API Qobuz demande des identifiants d'application partenaire (app_id / app_secret) délivrés par Qobuz."_t;
        if (CredentialStore::builtIn ("qobuz", "app_id").isEmpty())
        {
            addField ("app_id (partenaire Qobuz)", "app_id");
            addField ("app_secret", "app_secret", false);
        }
        auto* email = addField ("E-mail du compte Qobuz", "email");
        auto* password = addField ("Mot de passe", "password_input", true);
        addButton (p.isSignedIn() ? "Se reconnecter" : "Se connecter", [this, &p, email, password]
        {
            juce::String err;
            if (static_cast<QobuzProvider&> (p).signIn (email->getText(), password->getText(), err)) ctx.showMessage ("Qobuz connecté"_t);
            else ctx.showMessage ("Qobuz : " + err);
            setSource ("qobuz");
        });
    }
    else if (id == "tidal")
    {
        description = "FLAC (et FLAC Max jusqu'à 24 / 192 en DASH) lu par le moteur bit-perfect. Connexion par code sur link.tidal.com "_t
                      "avec un client_id délivré par Tidal (programme développeurs). Les flux chiffrés (DRM) ne sont pas pris en charge."_t;
        if (CredentialStore::builtIn ("tidal", "client_id").isEmpty())
        {
            addField ("client_id (Tidal)", "client_id");
            addField ("client_secret (facultatif)", "client_secret");
        }
        addButton (p.isSignedIn() ? "Se reconnecter" : "Se connecter", [this, &p]
        {
            auto& tidal = static_cast<TidalProvider&> (p);
            TidalProvider::DeviceLogin login;
            juce::String err;
            if (! tidal.beginDeviceLogin (login, err)) { ctx.showMessage ("Tidal : " + err); return; }
            juce::URL (login.verificationUri).launchInDefaultBrowser();
            ctx.showMessage ("Tidal : entrez le code " + login.userCode + " sur " + login.verificationUri);
            juce::Component::SafePointer<SourcePage> safe (this);
            juce::Thread::launch ([safe, &tidal, login]
            {
                for (int i = 0; i * login.interval < login.expiresIn; ++i)
                {
                    juce::Thread::sleep (login.interval * 1000);
                    juce::String e;
                    if (tidal.pollDeviceLogin (login, e))
                    {
                        juce::MessageManager::callAsync ([safe] { if (safe) { safe->ctx.showMessage ("Tidal connecté"_t); safe->setSource ("tidal"); } });
                        return;
                    }
                    if (e != "authorization_pending" && e != "slow_down") break;
                }
            });
        });
    }
    else if (id == "spotify")
    {
        description = "Recherche, bibliothèque et playlists via l'API Web officielle de Spotify (connexion PKCE dans votre navigateur, "_t
                      "redirection http://127.0.0.1:43821/callback). La lecture est confiée à votre appareil Spotify Connect : Spotify ne "_t
                      "délivre pas de flux audio aux applications tierces, ces morceaux ne passent donc pas par le moteur bit-perfect."_t;
        if (CredentialStore::builtIn ("spotify", "client_id").isEmpty())
            addField ("client_id (developer.spotify.com, redirection http://127.0.0.1:43821/callback)", "client_id");
        addButton (p.isSignedIn() ? "Se reconnecter" : "Se connecter (navigateur)", [this, &p]
        {
            juce::Component::SafePointer<SourcePage> safe (this);
            juce::Thread::launch ([safe, &p]
            {
                juce::String err;
                const bool ok = static_cast<SpotifyProvider&> (p).signIn (err);
                juce::MessageManager::callAsync ([safe, ok, err] { if (safe) { safe->ctx.showMessage (ok ? juce::String ("Spotify connecté"_t) : "Spotify : " + err); safe->setSource ("spotify"); } });
            });
        });
    }
    else if (id == "youtube")
    {
        description = "Recherche et playlists via l'API YouTube Data v3 (clé d'API Google). La lecture s'ouvre sur music.youtube.com : "_t
                      "les conditions de YouTube interdisent d'extraire l'audio, ces morceaux ne passent donc pas par le moteur."_t;
        addField ("Clé d'API YouTube Data v3"_t, "api_key");
        addField ("Identifiants de playlists publiques (séparés par des virgules)"_t, "playlists");
    }

    addText ("État : "_t + p.getStatusText() + (p.lastError.isNotEmpty() ? "  ·  "_t + p.lastError : juce::String()), false);

    if (p.isSignedIn())
    {
        juce::Component::SafePointer<SourcePage> safe (this);
        auto* provider = &p;
        if (id == "spotify")
        {
            // Spotify browser: search, liked tracks, playlists. Double-click plays through Spotify Connect.
            auto* spotify = static_cast<SpotifyProvider*> (provider);
            auto showTracks = [safe] (std::vector<TrackInfo> list, juce::String error, juce::String what)
            {
                juce::MessageManager::callAsync ([safe, list = std::move (list), error, what]
                {
                    if (safe == nullptr || safe->tracks == nullptr) return;
                    safe->tracks->setTracks (list, true, true);
                    safe->ctx.showMessage (list.empty() ? (error.isNotEmpty() ? error : what + " : aucun morceau"_t)
                                                        : what + " : " + juce::String ((int) list.size()) + " morceaux"_t);
                    safe->layoutContent (safe->getWidth() - safe->viewport.getScrollBarThickness());
                });
            };

            auto* search = addField ("Rechercher sur Spotify (Entrée)"_t, "__search", true);
            search->setPasswordCharacter (0);
            search->onReturnKey = [search, spotify, showTracks]
            {
                const auto q = search->getText().trim();
                if (q.isEmpty()) return;
                juce::Thread::launch ([spotify, showTracks, q] { auto r = spotify->searchTracks (q, 30); showTracks (std::move (r), spotify->lastError, "« "_t + q + " »"_t); });
            };

            auto* lists = new juce::ComboBox();
            lists->setTextWhenNothingSelected ("Chargement de vos playlists…"_t);
            rows.add (lists);
            rowHeights.push_back (36);
            contentComp.addAndMakeVisible (lists);
            auto playlistIds = std::make_shared<juce::StringArray>();
            lists->onChange = [lists, spotify, showTracks, playlistIds]
            {
                const int idx = lists->getSelectedItemIndex();
                if (idx < 0) return;
                const auto name = lists->getText();
                const auto plId = (*playlistIds)[idx];
                juce::Thread::launch ([spotify, showTracks, plId, name]
                {
                    auto r = plId == "__liked" ? spotify->getLikedTracks() : spotify->getPlaylistTracks (plId);
                    showTracks (std::move (r), spotify->lastError, name);
                });
            };
            juce::Component::SafePointer<juce::ComboBox> safeList (lists);
            juce::Thread::launch ([spotify, safeList, playlistIds, showTracks]
            {
                auto pls = spotify->getPlaylists();
                const auto err = spotify->lastError;
                auto liked = spotify->getLikedTracks (200);
                const auto likedErr = spotify->lastError;
                juce::MessageManager::callAsync ([safeList, playlistIds, pls, err]
                {
                    if (safeList == nullptr) return;
                    safeList->clear (juce::dontSendNotification);
                    playlistIds->clear();
                    safeList->addItem ("Titres likés"_t, 1);
                    playlistIds->add ("__liked");
                    for (auto& pl : pls)
                    {
                        safeList->addItem (pl.name + (pl.trackCount > 0 ? "  (" + juce::String (pl.trackCount) + ")" : juce::String()), playlistIds->size() + 1);
                        playlistIds->add (pl.externalId);
                    }
                    safeList->setTextWhenNothingSelected (pls.empty() && err.isNotEmpty() ? err : juce::String ("Choisir une playlist…"_t));
                });
                showTracks (std::move (liked), likedErr, "Titres likés"_t);
            });
            addText ("Double-cliquez sur un morceau : il est lancé sur votre appareil Spotify (application Spotify ouverte, "_t
                     "compte Premium nécessaire pour le pilotage à distance ; sinon le morceau s'ouvre dans Spotify)."_t);
        }
        addButton ("Importer mes playlists dans la bibliothèque"_t, [safe, provider]
        {
            if (safe == nullptr) return;
            juce::Thread::launch ([safe, provider]
            {
                const auto lists = provider->getPlaylists();
                juce::MessageManager::callAsync ([safe, provider, lists]
                {
                    if (safe == nullptr) return;
                    for (auto& pl : lists)
                        safe->ctx.providers().importPlaylist (provider->getId(), pl, [safe, name = pl.name] (int n)
                        {
                            if (safe) safe->ctx.showMessage (name + " : " + juce::String (n) + " morceaux");
                        });
                    if (lists.empty()) safe->ctx.showMessage (provider->lastError.isNotEmpty() ? provider->lastError : juce::String ("Aucune playlist"_t));
                });
            });
        });
        tracks = std::make_unique<TrackList> (ctx);
        if (id != "spotify") tracks->setTracks (ctx.library().getTracks ("artist", 3000, id), true, true);
        contentComp.addAndMakeVisible (*tracks);
    }
}

void SourcePage::layoutContent (int width)
{
    const int pad = 32;
    const int inner = juce::jmax (100, width - pad * 2);
    int y = 24 + 54;
    // description height
    juce::AttributedString as;
    as.append (description, style::font (14.0f));
    as.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout tl;
    tl.createLayout (as, (float) inner);
    y += (int) tl.getHeight() + 30;

    for (int i = 0; i < rows.size(); ++i)
    {
        const int h = rowHeights[(size_t) i];
        const int w = dynamic_cast<juce::TextButton*> (rows[i]) != nullptr ? juce::jmin (inner, 520) : juce::jmin (inner, 620);
        rows[i]->setBounds (pad, y, dynamic_cast<juce::Label*> (rows[i]) != nullptr ? inner : w, h);
        y += h + 10;
    }
    y += 10;
    if (albums != nullptr)
    {
        const int h = albums->heightForWidth (inner);
        albums->setBounds (pad, y, inner, h);
        y += h + 20;
    }
    if (tracks != nullptr)
    {
        tracks->setBounds (pad - 10, y, inner + 20, tracks->getTotalHeight());
        y += tracks->getTotalHeight();
    }
    setContentHeight (y + 120);

    struct Head : juce::Component
    {
        SourcePage& page;
        explicit Head (SourcePage& p) : page (p) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override
        {
            drawPageTitle (g, page.ctx.theme(), getLocalBounds().removeFromTop (54), page.heading);
            juce::AttributedString as;
            as.append (page.description, style::font (14.0f), page.ctx.theme().textDim());
            as.setWordWrap (juce::AttributedString::byWord);
            juce::TextLayout tl;
            tl.createLayout (as, (float) getWidth());
            tl.draw (g, getLocalBounds().toFloat().withTrimmedTop (60.0f));
        }
    };
    if (contentComp.findChildWithID ("head") == nullptr)
    {
        auto* h = new Head (*this);
        h->setComponentID ("head");
        rows.add (h);
        rowHeights.push_back (0);
        contentComp.addAndMakeVisible (h);
    }
    if (auto* h = contentComp.findChildWithID ("head")) h->setBounds (pad, 24, inner, 60 + (int) tl.getHeight() + 10);
}

//==============================================================================
// SettingsPage
//==============================================================================
SettingsPage::SettingsPage (AppContext& c) : ScrollPage (c)
{
    contentComp.addAndMakeVisible (labels);
    refresh();
}

void SettingsPage::section (const juce::String& title)
{
    Row r;
    r.label = title;
    r.isSection = true;
    r.height = 48;
    layoutRows.push_back (r);
}

namespace
{
    /** Device banner (Merging Anubis, RME ADI-2): the interface, its sound profiles (one click) and a tip. */
    class DeviceProfilePanel : public juce::Component
    {
    public:
        DeviceProfilePanel (AppContext& c, DeviceFamily f, const juce::String& device, bool detected)
            : ctx (c), family (f), deviceName (device), autoDetected (detected)
        {
            setRepaintsOnMouseActivity (true);
        }

        static int idealHeight (DeviceFamily) { return 286; }

        void paint (juce::Graphics& g) override
        {
            const auto& t = ctx.theme();
            style::paintGlass (g, *this, ctx, getLocalBounds().toFloat(), t.radius());
            g.setColour (t.accent().withAlpha (0.35f));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), t.radius(), 1.0f);

            auto r = getLocalBounds().reduced (18, 14);
            auto head = r.removeFromTop (44);
            // interface icon: a small rack / desktop unit with its ports
            auto icon = head.removeFromLeft (44).toFloat().reduced (2.0f);
            g.setColour (t.accent().withAlpha (0.16f));
            g.fillRoundedRectangle (icon, 8.0f);
            g.setColour (t.accent());
            g.drawRoundedRectangle (icon.reduced (8.0f, 13.0f), 3.0f, 1.6f);
            for (int k = 0; k < 2; ++k)
                g.fillRoundedRectangle (icon.getX() + 12.0f + (float) k * 9.0f, icon.getCentreY() - 2.5f, 6.0f, 5.0f, 1.0f);
            g.fillEllipse (icon.getRight() - 15.0f, icon.getCentreY() - 2.5f, 5.0f, 5.0f);
            head.removeFromLeft (12);
            g.setColour (t.textCol());
            g.setFont (style::font (16.0f, true));
            g.drawText (familyTitle (family), head.removeFromTop (22), juce::Justification::centredLeft, true);
            g.setColour (t.textDim());
            g.setFont (style::font (12.5f));
            g.drawText ((autoDetected ? "Détecté : "_t : "Profil choisi manuellement · "_t) + deviceName, head, juce::Justification::centredLeft, true);

            r.removeFromTop (10);
            const int count = familyProfileCount (family);
            const int current = familyCurrentProfile (ctx.engine().getSettings(), family);
            tiles.clear();
            const int gap = 10;
            const int w = (r.getWidth() - (count - 1) * gap) / juce::jmax (1, count);
            auto row = r.removeFromTop (112);
            for (int i = 0; i < count; ++i)
            {
                auto tile = row.removeFromLeft (w);
                row.removeFromLeft (gap);
                tiles.push_back (tile);
                const bool on = i == current;
                const bool hover = tile.contains (getMouseXYRelative()) && isMouseOver (true);
                g.setColour (on ? t.accent().withAlpha (0.18f) : juce::Colours::white.withAlpha (hover ? 0.08f : 0.04f));
                g.fillRoundedRectangle (tile.toFloat(), 10.0f);
                g.setColour (on ? t.accent() : t.border());
                g.drawRoundedRectangle (tile.toFloat().reduced (0.5f), 10.0f, on ? 1.6f : 1.0f);
                auto tr = tile.reduced (12, 10);
                g.setColour (on ? t.accent() : t.textCol());
                g.setFont (style::font (13.5f, true));
                g.drawText (profileName (family, i).fromFirstOccurrenceOf (juce::String::fromUTF8 ("\xe2\x80\x94 "), false, false), tr.removeFromTop (20),
                            juce::Justification::centredLeft, true);
                g.setColour (t.textDim());
                g.setFont (style::font (11.5f));
                g.drawFittedText (profileDescription (family, i), tr, juce::Justification::topLeft, 5, 0.9f);
                if (i == 0)
                {
                    g.setColour (t.textDim().withAlpha (0.8f));
                    g.setFont (style::font (10.5f, true));
                    g.drawText ("PAR DÉFAUT"_t, tile.reduced (12, 8), juce::Justification::topRight);
                }
            }

            r.removeFromTop (12);
            g.setColour (t.colour ("--nx-quality-low", juce::Colour (0xffffb547)));
            g.setFont (style::font (12.5f, true));
            g.drawText (family == DeviceFamily::Merging ? "Astuce MT VAD"_t : "Astuce RME"_t, r.removeFromTop (18), juce::Justification::centredLeft);
            g.setColour (t.textCol().withAlpha (0.85f));
            g.setFont (style::font (12.5f));
            const auto tip = familyTip (family).fromFirstOccurrenceOf (" : ", false, false);
            g.drawFittedText (tip.substring (0, 1).toUpperCase() + tip.substring (1), r, juce::Justification::topLeft, 3, 0.95f);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            for (size_t i = 0; i < tiles.size(); ++i)
                if (tiles[i].contains (e.getPosition()))
                {
                    auto s = ctx.engine().getSettings();
                    familySetProfile (s, family, (int) i);
                    ctx.engine().setSettings (s);
                    ctx.applyEngineSettings();
                    ctx.showMessage ("Profil activé : "_t + profileName (family, (int) i));
                    repaint();
                }
        }

    private:
        AppContext& ctx;
        DeviceFamily family;
        juce::String deviceName;
        bool autoDetected;
        std::vector<juce::Rectangle<int>> tiles;
    };
}

void SettingsPage::fullRow (juce::Component* control, int height)
{
    Row r;
    r.control = control;
    r.height = height;
    r.full = true;
    owned.add (control);
    contentComp.addAndMakeVisible (control);
    layoutRows.push_back (r);
}

void SettingsPage::row (const juce::String& label, juce::Component* control, int height, const juce::String& help)
{
    Row r;
    r.label = label;
    r.help = help;
    r.control = control;
    r.height = height;
    if (control != nullptr) { owned.add (control); contentComp.addAndMakeVisible (control); }
    layoutRows.push_back (r);
}

juce::StringArray SettingsPage::tabIds() { return { "audio", "bibliotheque", "interface", "remote", "outils", "apropos" }; }
juce::StringArray SettingsPage::tabLabels()
{
    return { "Audio & Système"_t, "Bibliothèque & NAS"_t, "Interface"_t, "NX Remote", "Outils Studio"_t, "À propos"_t };
}

namespace
{
    juce::ComboBox* makeCombo (const juce::StringArray& items, const juce::String& selected, std::function<void (const juce::String&)> fn)
    {
        auto* cb = new juce::ComboBox();
        cb->addItemList (items, 1);
        cb->setText (selected, juce::dontSendNotification);
        cb->onChange = [cb, fn] { fn (cb->getText()); };
        return cb;
    }
    juce::ToggleButton* makeToggle (bool on, std::function<void (bool)> fn)
    {
        auto* t = new juce::ToggleButton();
        t->setToggleState (on, juce::dontSendNotification);
        t->onClick = [t, fn] { fn (t->getToggleState()); };
        return t;
    }
    juce::TextButton* makeButton (const juce::String& text, std::function<void()> fn)
    {
        auto* b = new juce::TextButton (text);
        b->onClick = std::move (fn);
        return b;
    }
}

void SettingsPage::refresh()
{
    for (auto* c : owned) contentComp.removeChildComponent (c);
    owned.clear();
    layoutRows.clear();
    if (! tabIds().contains (tab)) tab = "audio";
    if (tab == "audio") buildAudio();
    else if (tab == "bibliotheque") buildLibrary();
    else if (tab == "interface") buildInterface();
    else if (tab == "remote") buildRemote();
    else if (tab == "outils") buildTools();
    else buildAbout();
    layoutContent (getWidth() - viewport.getScrollBarThickness());
}

void SettingsPage::buildAudio()
{
    auto& eng = ctx.engine();
    auto s = eng.getSettings();

    fullRow (makeHardwareDashboard (ctx).release(), HardwareDashboardHeight);

    section ("Sortie audio"_t);
    row ("Pilote"_t, makeCombo (ctx.getBackendNames(), ctx.getCurrentBackendName(), [this] (const juce::String& n) { ctx.selectBackend (n); refresh(); }), 36,
         "ASIO et WASAPI exclusif (Windows), CoreAudio + Hog Mode (macOS), ALSA hw (Linux) contournent le mixeur du système."_t);
    row ("Périphérique"_t, makeCombo (eng.getBackend().getDeviceNames(), eng.getDeviceName(), [this] (const juce::String& n) { ctx.selectDevice (n); refresh(); }));
    row ("Accès exclusif (Hog Mode / exclusif)"_t, makeToggle (s.exclusive, [this] (bool v) { auto st = ctx.engine().getSettings(); st.exclusive = v; ctx.engine().setSettings (st); ctx.applyEngineSettings(); }));
    {
        auto* buf = makeCombo ({ "Automatique"_t, "10 ms", "20 ms", "50 ms", "100 ms", "200 ms" },
                               s.bufferMilliseconds == 0 ? "Automatique"_t : juce::String (s.bufferMilliseconds) + " ms",
                               [this] (const juce::String& v) { auto st = ctx.engine().getSettings(); st.bufferMilliseconds = v.getIntValue(); ctx.engine().setSettings (st); ctx.applyEngineSettings(); });
        row ("Tampon du périphérique"_t, buf);
    }
    const auto status = eng.getOutputStatus();
    row (status.open ? "État : "_t + status.backendName + " · "_t + formatRate (status.sampleRate) + " · "_t + status.detail : juce::String ("État : périphérique fermé (s'ouvre à la lecture)"_t), nullptr, 28);

    // Hardware profile (Merging Anubis / RAVENNA, RME ADI-2)
    {
        const juce::StringArray profiles { "Automatique (détection)"_t, "Générique"_t, "Merging Anubis / RAVENNA (AES67)"_t, "RME ADI-2 (USB)" };
        row ("Profil matériel"_t, makeCombo (profiles, profiles[(int) s.hardwareProfile], [this, profiles] (const juce::String& v)
        {
            auto st = ctx.engine().getSettings();
            st.hardwareProfile = (HardwareProfile) juce::jmax (0, profiles.indexOf (v));
            ctx.engine().setSettings (st);
            ctx.applyEngineSettings();
            refresh();
        }), 36, "Un périphérique reconnu reçoit des réglages dédiés (comme dans Roon) : Merging (« Merging », « Anubis », « RAVENNA », « VAD ») et RME (« RME », « ADI-2 »)."_t);

        const auto family = activeFamily (s, eng.getDeviceName());
        if (family != DeviceFamily::None)
        {
            fullRow (new DeviceProfilePanel (ctx, family, eng.getDeviceName(), s.hardwareProfile == HardwareProfile::Auto),
                     DeviceProfilePanel::idealHeight (family));
            if (family == DeviceFamily::Merging)
            {
                const juce::StringArray gains { "0 dB", "+3 dB", "+6 dB" };
                const int gi = s.anubisDsdGainDb >= 5.0 ? 2 : s.anubisDsdGainDb >= 2.0 ? 1 : 0;
                row ("Gain DSD -> DXD (profils Anubis)"_t, makeCombo (gains, gains[gi], [this] (const juce::String& v)
                {
                    auto st = ctx.engine().getSettings();
                    st.anubisDsdGainDb = v.startsWith ("+6") ? 6.0 : v.startsWith ("+3") ? 3.0 : 0.0;
                    ctx.engine().setSettings (st);
                    ctx.applyEngineSettings();
                }), 36, "SACD 0 dB = −6 dBFS : +6 dB retrouve le niveau du PCM. Un limiteur 64 bits (plafond −0,2 dBFS) évite tout écrêtage."_t);
                row ("Modelage harmonique (Analog Warmth)"_t, makeToggle (s.anubisHarmonics, [this] (bool v)
                {
                    auto st = ctx.engine().getSettings();
                    st.anubisHarmonics = v;
                    ctx.engine().setSettings (st);
                    ctx.applyEngineSettings();
                }), 36, "Harmonique paire d'ordre 2 à −90 dB et détente de −0,5 dB au-dessus de 18 kHz. Désactivé : filtre à phase minimale seul."_t);
            }
            else
            {
                row ("Sécurité RME DSD Direct (verrouiller le volume logiciel à 100 %)"_t, makeToggle (s.rmeLockVolume, [this] (bool v)
                {
                    auto st = ctx.engine().getSettings();
                    st.rmeLockVolume = v;
                    ctx.engine().setSettings (st);
                    ctx.applyEngineSettings();
                }), 36, "Attention : en « DSD Direct », l'ADI-2 désactive son volume matériel sur les sorties ligne. Le volume de NX Player reste "_t
                        "à 100 % (bit-perfect) : réglez le niveau avec un contrôle analogique externe."_t);
            }
        }
    }

    section ("DSD");
    const juce::StringArray modes { "Automatique (natif, puis DoP, puis PCM)"_t, "DSD natif (ASIO)"_t, "DoP (DSD over PCM)", "Conversion en PCM (DXD)"_t };
    row ("Mode DSD"_t, makeCombo (modes, modes[(int) s.dsdMode], [this, modes] (const juce::String& v)
    {
        auto st = ctx.engine().getSettings();
        st.dsdMode = (DsdMode) juce::jmax (0, modes.indexOf (v));
        ctx.engine().setSettings (st);
        ctx.applyEngineSettings();
    }));
    row ("Autoriser DoP"_t, makeToggle (s.dopAllowed, [this] (bool v) { auto st = ctx.engine().getSettings(); st.dopAllowed = v; ctx.engine().setSettings (st); ctx.applyEngineSettings(); }), 36,
         "Le DAC doit reconnaître les marqueurs DoP (0x05 / 0xFA) ; sinon il joue du bruit à faible niveau."_t);
    const juce::StringArray gains { "0 dB (sans écrêtage)"_t, "+6 dB (SACD 0 dB = 0 dBFS)" };
    row ("Gain DSD -> PCM"_t, makeCombo (gains, s.dsdToPcmGainDb > 3.0 ? gains[1] : gains[0],
         [this, gains] (const juce::String& v) { auto st = ctx.engine().getSettings(); st.dsdToPcmGainDb = v == gains[1] ? 6.0 : 0.0; ctx.engine().setSettings (st); ctx.applyEngineSettings(); }));

    section ("Moteur & système"_t);
    row ("Memory Play : charger chaque morceau en RAM"_t, makeToggle (s.memoryPlay, [this] (bool v)
    {
        auto st = ctx.engine().getSettings();
        st.memoryPlay = v;
        ctx.engine().setSettings (st, false);
        ctx.applyEngineSettings();
    }), 36, "Le fichier entier est copié en mémoire au début de la lecture : plus aucun accès disque ou réseau pendant l'écoute. Données identiques au fichier (bit-perfect)."_t);
    {
        const juce::StringArray sizes { "512 Mo", "1 Go", "1,5 Go", "2 Go", "4 Go" };
        const int mb[] = { 512, 1024, 1536, 2048, 4096 };
        int sel = 2;
        for (int i = 0; i < 5; ++i) if (s.memoryPlayMaxMb == mb[i]) sel = i;
        row ("Memory Play : taille maximale d'un fichier"_t, makeCombo (sizes, sizes[sel], [this, sizes] (const juce::String& v)
        {
            const int values[] = { 512, 1024, 1536, 2048, 4096 };
            auto st = ctx.engine().getSettings();
            st.memoryPlayMaxMb = values[juce::jmax (0, sizes.indexOf (v))];
            ctx.engine().setSettings (st, false);
            ctx.applyEngineSettings();
        }), 36, "Un fichier plus gros (DSD512, DXD de plusieurs heures) est lu depuis le disque."_t);
    }
    row ("Empêcher la mise en veille pendant la lecture"_t, makeToggle ((bool) ctx.settings().get ("preventSleep", true), [this] (bool v) { ctx.settings().set ("preventSleep", v); }), 36,
         SleepBlocker::isSupported() ? "L'ordinateur reste éveillé tant que la musique joue (l'écran peut s'éteindre)."_t
                                     : "Non disponible sur ce système."_t);
    row ("Suréchantillonnage sinc vers DXD (352,8 / 384 kHz)"_t, makeToggle (s.upsampleToDxd, [this] (bool v) { auto st = ctx.engine().getSettings(); st.upsampleToDxd = v; ctx.engine().setSettings (st); ctx.applyEngineSettings(); }), 36,
         "Filtre polyphase 64 bits, 160 dB. Rapport entier (×2, ×4, ×8) dans la même famille de fréquences. Désactive le bit-perfect."_t);
    row ("Volume numérique : 100 % = bit-perfect"_t, nullptr, 28);
}

void SettingsPage::buildLibrary()
{
    section ("Dossiers de musique"_t);
    for (auto& f : ctx.library().getFolders())
        row (f, makeButton ("Retirer"_t, [this, f]
        {
            ctx.library().removeFolder (juce::File (f));
            ctx.library().notifyChanged();
            refresh();
        }));
    row ("Ajouter un dossier (disque interne, externe, partage SMB / NFS monté)"_t, makeButton ("Ajouter un dossier…"_t, [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Dossier de musique"_t, juce::File::getSpecialLocation (juce::File::userMusicDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this, chooser] (const juce::FileChooser& fc)
        {
            const auto dir = fc.getResult();
            if (! dir.isDirectory()) return;
            ctx.library().addFolder (dir);
            ctx.scanner().requestFullScan();
            ctx.showMessage ("Indexation de "_t + dir.getFileName() + "…"_t);
            refresh();
        });
    }));
    row (juce::String (ctx.library().countTracks ("local")) + " morceaux locaux indexés"_t, makeButton ("Réindexer"_t, [this] { ctx.scanner().requestFullScan(); ctx.showMessage ("Réindexation lancée"_t); }));

    section ("NAS / Jellyfin"_t);
    const int nasAlbums = ctx.library().countAlbumsFromSourcePrefix ("upnp");
    row (juce::String (nasAlbums) + " album(s) du NAS dans la bibliothèque"_t, nullptr, 28);
    row ("Retirer les albums des serveurs indisponibles / hors-ligne"_t, makeButton ("Retirer les indisponibles"_t, [this]
    {
        const int n = actions::removeUnavailableNasAlbums (ctx);
        ctx.showMessage (juce::String (n) + " morceau(x) de serveurs indisponibles retiré(s)"_t);
        refresh();
    }), 36, "Garde les serveurs connectés pendant cette session, retire les autres. Les fichiers restent sur le NAS."_t);
    row ("Purger tous les éléments du NAS de la bibliothèque"_t, makeButton ("Purger le NAS"_t, [this]
    {
        actions::confirm (ctx, "Purger le NAS"_t, "Tous les albums importés d'un NAS / Jellyfin seront retirés de la bibliothèque (les fichiers restent sur le serveur)."_t,
                          "Purger"_t, [this] { const int n = actions::purgeNas (ctx); ctx.showMessage (juce::String (n) + " morceau(x) retiré(s)"_t); refresh(); });
    }));

    section ("Téléchargements"_t);
    {
        const auto folder = ctx.downloads().getFolder();
        row ("Dossier de téléchargement (NAS / Jellyfin)"_t, makeButton (folder.getFullPathName(), [this]
        {
            auto chooser = std::make_shared<juce::FileChooser> ("Dossier de téléchargement"_t, ctx.downloads().getFolder());
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this, chooser] (const juce::FileChooser& fc)
            {
                const auto dir = fc.getResult();
                if (! dir.isDirectory()) return;
                ctx.downloads().setFolder (dir);
                ctx.settings().set ("downloadFolder", dir.getFullPathName());
                ctx.showMessage ("Téléchargements : "_t + dir.getFullPathName());
                refresh();
            });
        }), 36, "Les morceaux et albums téléchargés depuis un serveur sont copiés ici (Artiste / Album / fichier original) "_t
                "puis ajoutés à la bibliothèque, disponibles hors-ligne."_t);
    }

    section ("Artistes & paroles"_t);
    row ("Photos et biographies d'artistes en ligne (TheAudioDB, Wikipédia)"_t, makeToggle (ctx.artistInfo().isEnabled(), [this] (bool v)
    {
        ctx.artistInfo().setEnabled (v);
        ctx.settings().set ("artistInfoOnline", v);
    }), 36, "Seul le nom de l'artiste est envoyé. Les réponses sont gardées 30 jours dans la bibliothèque."_t);
    row ("Chercher les paroles en ligne (LRCLIB, puis lyrics.ovh)"_t, makeToggle ((bool) ctx.settings().get ("lyricsOnline", true), [this] (bool v) { ctx.settings().set ("lyricsOnline", v); }), 36,
         "Fichier .lrc et tags d'abord, puis LRCLIB (paroles synchronisées), puis lyrics.ovh en secours (texte seul). Artiste, titre, album et durée sont envoyés à ces services."_t);
}

void SettingsPage::buildInterface()
{
    section ("Langue & thème"_t);
    const juce::StringArray langs { juce::String::fromUTF8 ("Fran\xc3\xa7" "ais"), "English" };
    row ("Langue / Language"_t, makeCombo (langs, i18n::isEnglish() ? juce::String ("English") : langs[0], [this] (const juce::String& v)
    {
        ctx.setLanguage (v == "English" ? "en" : "fr");
    }));
    {
        juce::StringArray names, ids;
        for (auto& th : ctx.themes().getThemes()) { names.add (th.name); ids.add (th.id); }
        row ("Thème"_t, makeCombo (names, ctx.theme().name, [this, names, ids] (const juce::String& v)
        {
            const int i = names.indexOf (v);
            if (i >= 0) ctx.applyTheme (ids[i]);
        }), 36, "Plus de thèmes et plug-ins : barre latérale › Extensions & Thèmes."_t);
    }

    section ("Disposition"_t);
    const auto layout = ctx.settings().get ("layout");
    const juce::StringArray sidebarModes { "Étendue (icônes et libellés)"_t, "Compacte (icônes)"_t };
    row ("Barre latérale"_t, makeCombo (sidebarModes, layout.getProperty ("sidebar", "wide").toString() == "compact" ? sidebarModes[1] : sidebarModes[0],
                                        [this, sidebarModes] (const juce::String& v) { ctx.setLayoutOption ("sidebar", v == sidebarModes[1] ? "compact" : "wide"); }));
    const juce::StringArray playerModes { "En bas (Cider 2)"_t, "En haut"_t };
    row ("Barre de lecture"_t, makeCombo (playerModes, layout.getProperty ("player", "bottom").toString() == "top" ? playerModes[1] : playerModes[0],
                                          [this, playerModes] (const juce::String& v) { ctx.setLayoutOption ("player", v == playerModes[1] ? "top" : "bottom"); }));

    section ("Vue immersive"_t);
    const auto imm = ctx.settings().get ("immersive");
    const juce::StringArray layouts { "Partagée : pochette + paroles"_t, "Centrée : grande pochette"_t, "Minimale"_t };
    const auto current = imm.getProperty ("layout", "split").toString();
    row ("Disposition de la vue immersive"_t, makeCombo (layouts, current == "centered" ? layouts[1] : current == "minimal" ? layouts[2] : layouts[0],
         [this, layouts] (const juce::String& v)
    {
        auto o = ctx.settings().get ("immersive");
        if (! o.isObject()) o = juce::var (new juce::DynamicObject());
        o.getDynamicObject()->setProperty ("layout", v == layouts[1] ? "centered" : v == layouts[2] ? "minimal" : "split");
        ctx.settings().set ("immersive", o);
    }), 36, "Les thèmes (.nxtheme : immersive.json, --nx-immersive-*) et les plug-ins peuvent aussi la modifier."_t);
    const std::pair<const char*, const char*> widgets[] = { { "lyrics", "Paroles synchronisées" }, { "waveform", "Barre de progression en forme d'onde" },
                                                            { "vu", "VU-mètres" }, { "spectrum", "Spectre" }, { "tech", "Infos techniques (DSD / DXD, chemin du signal)" } };
    for (auto& w : widgets)
    {
        const juce::String key (w.first);
        const bool def = key == "lyrics" || key == "waveform" || key == "tech";
        row (tr (juce::String::fromUTF8 (w.second)), makeToggle ((bool) imm.getProperty (key, def), [this, key] (bool v)
        {
            auto o = ctx.settings().get ("immersive");
            if (! o.isObject()) o = juce::var (new juce::DynamicObject());
            o.getDynamicObject()->setProperty (key, v);
            ctx.settings().set ("immersive", o);
        }));
    }

    section ("Assistant"_t);
    row ("Assistant de premier démarrage (langue, disposition, DAC, sources)"_t, makeButton ("Relancer l'assistant"_t, [this] { ctx.showOnboarding(); }));
}

void SettingsPage::buildRemote()
{
    section ("NX Remote");
    row ("Activer NX Remote (Wi-Fi, à la demande)"_t, makeToggle (ctx.remote().isRunning(), [this] (bool v) { ctx.setRemoteEnabled (v); refresh(); }), 36,
         "Un serveur web local démarre sur cet ordinateur : scannez le QR code avec votre téléphone (même réseau Wi-Fi). Arrêté à la fermeture de NX Player."_t);
    if (ctx.remote().isRunning())
    {
        fullRow (makeRemotePanel (ctx).release(), RemotePanelHeight);
        row ("Révoquer l'accès des appareils déjà connectés"_t, makeButton ("Nouveau code"_t, [this] { ctx.remote().regenerateToken(); refresh(); }));
    }
}

void SettingsPage::buildTools()
{
    section ("Outils Studio"_t);
    auto& st = ctx.studioTools();
    const juce::StringArray monitors { "Stéréo"_t, "Mono (L+R)", "Solo Mid (M)", "Solo Side (S)" };
    row ("Écoute"_t, makeCombo (monitors, monitors[(int) st.monitor.load()], [this, monitors] (const juce::String& v)
    {
        ctx.studioTools().monitor = (StudioToolsState::Monitor) juce::jmax (0, monitors.indexOf (v));
        ctx.applyStudioTools();
    }), 36, "Contrôles de mastering, en 64 bits. Tout outil actif fait sortir du bit-perfect (indiqué dans le chemin du signal) ; le DSD passe alors en DXD."_t);
    row (juce::String::fromUTF8 ("Inversion de phase (\xc3\x98)"), makeToggle (st.invertPhase.load(), [this] (bool v) { ctx.studioTools().invertPhase = v; ctx.applyStudioTools(); }));
    row ("Smart Crossfeed (casque, Bauer 700 Hz / 4,5 dB)"_t, makeToggle (st.crossfeed.load(), [this] (bool v) { ctx.studioTools().crossfeed = v; ctx.applyStudioTools(); }), 36,
         "Mélange une part filtrée de chaque voie dans l'autre, comme des enceintes ; inactif en mono."_t);

    section ("AuraEngine X");
    const auto configured = ctx.settings().get ("auraEnginePath").toString();
    const auto app = aura::findApplication (configured);
    row (app.exists() ? "Application : "_t + app.getFullPathName() : juce::String ("AuraEngine X non trouvé"_t),
         makeButton ("Choisir…"_t, [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("AuraEngine X"_t, juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories,
                              [this, chooser] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (! f.exists()) return;
            ctx.settings().set ("auraEnginePath", f.getFullPathName());
            refresh();
        });
    }), 36, "Clic droit sur un morceau ou un album › « Analyser / Convertir dans AuraEngine X » : les fichiers s'ouvrent dans l'application (analyse, puis conversion DSD / PCM)."_t);
}

void SettingsPage::buildAbout()
{
    section ("À propos"_t);
    row ("NX Player " NX_VERSION " · NX Audio Suite · licence MIT"_t, nullptr, 26);
    row ("Utilise JUCE (AGPLv3 / commercial), SQLite (domaine public), Apple ALAC (Apache 2.0), QR Code generator (Project Nayuki, MIT). Voir THIRD_PARTY_NOTICES.md."_t, nullptr, 40);
    row ("Infos d'artistes : TheAudioDB et Wikipédia (CC BY-SA). Paroles : LRCLIB, lyrics.ovh."_t, nullptr, 26);
}

void SettingsPage::layoutContent (int width)
{
    const int pad = 32;
    const int inner = juce::jmax (200, width - pad * 2);
    int y = embedded ? 12 : 24 + 60;
    painted.clear();
    for (auto& r : layoutRows)
    {
        if (r.isSection)
        {
            painted.push_back ({ { pad, y, inner, r.height }, r });
            y += r.height;
            continue;
        }
        if (r.full)
        {
            r.control->setBounds (pad, y, juce::jmin (inner, 980), r.height);
            y += r.height + 12;
            continue;
        }
        const int controlW = juce::jmin (380, inner / 2);
        auto rowArea = juce::Rectangle<int> (pad, y, inner, r.height + (r.help.isNotEmpty() ? 20 : 0));
        if (r.control != nullptr)
            r.control->setBounds (rowArea.getRight() - controlW, y, controlW, r.height);
        painted.push_back ({ rowArea.withTrimmedRight (r.control != nullptr ? controlW + 20 : 0), r });
        y += rowArea.getHeight() + 10;
    }
    labels.setBounds (0, 0, width, y + 80);
    setContentHeight (y + 80);
}

void SettingsPage::Labels::paint (juce::Graphics& g)
{
    const auto& t = p.ctx.theme();
    if (! p.embedded) drawPageTitle (g, t, { 32, 24, getWidth() - 64, 54 }, "Réglages"_t);
    for (auto& [area, r] : p.painted)
    {
        if (r.isSection)
        {
            g.setColour (t.accent());
            g.setFont (style::font (12.5f, true));
            g.drawText (style::upper (r.label), area.withTrimmedTop (18), juce::Justification::centredLeft);
            g.setColour (t.border());
            g.fillRect (area.getX(), area.getBottom() - 1, area.getWidth(), 1);
            continue;
        }
        auto a = area;
        g.setColour (t.textCol());
        g.setFont (style::font (14.0f));
        g.drawFittedText (r.label, a.removeFromTop (r.height), juce::Justification::centredLeft, 2);
        if (r.help.isNotEmpty())
        {
            g.setColour (t.textDim());
            g.setFont (style::font (12.0f));
            g.drawFittedText (r.help, a.withHeight (20), juce::Justification::centredLeft, 2);
        }
    }
}

} // namespace nx
