// NX Player — MIT License
#include "LibraryActions.h"
#include "../Providers/MediaProvider.h"
#include "../Providers/UpnpProvider.h"

namespace nx::actions
{

std::vector<juce::int64> trackIds (const std::vector<TrackInfo>& tracks)
{
    std::vector<juce::int64> ids;
    for (auto& t : tracks) if (t.id > 0) ids.push_back (t.id);
    return ids;
}

std::vector<TrackInfo> albumTracks (AppContext& ctx, const std::vector<AlbumInfo>& albums)
{
    std::vector<TrackInfo> out;
    for (auto& a : albums)
        for (auto& t : ctx.library().getAlbumTracks (a.artist, a.title)) out.push_back (t);
    return out;
}

int addToPlaylist (AppContext& ctx, juce::int64 playlistId, const std::vector<juce::int64>& ids)
{
    const int n = ctx.library().appendToPlaylist (playlistId, ids);
    juce::String name;
    for (auto& pl : ctx.library().getPlaylists()) if (pl.id == playlistId) name = pl.name;
    ctx.showMessage (juce::String (n) + " morceau(x) ajouté(s) à « "_t + name + " »"_t);
    ctx.library().notifyChanged();
    return n;
}

juce::int64 createPlaylistWith (AppContext& ctx, const juce::String& name, const std::vector<juce::int64>& ids)
{
    const auto clean = name.trim().isNotEmpty() ? name.trim() : juce::String ("Nouvelle playlist"_t);
    const auto id = ctx.library().createPlaylist (clean, "local");
    ctx.library().appendToPlaylist (id, ids);
    ctx.library().notifyChanged();
    ctx.showMessage ("Playlist « "_t + clean + " » créée ("_t + juce::String ((int) ids.size()) + " morceaux)");
    return id;
}

void promptNewPlaylist (AppContext& ctx, std::vector<juce::int64> ids)
{
    auto* w = new juce::AlertWindow ("Nouvelle playlist"_t, juce::String ((int) ids.size()) + " morceau(x) à ajouter"_t, juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", "Ma playlist"_t, "Nom"_t);
    w->addButton ("Créer"_t, 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Annuler"_t, 0, juce::KeyPress (juce::KeyPress::escapeKey));
    auto* app = &ctx;
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, app, ids] (int result)
    {
        if (result == 1) createPlaylistWith (*app, w->getTextEditorContents ("name"), ids);
    }), true);
}

juce::PopupMenu playlistSubmenu (AppContext& ctx, std::vector<juce::int64> ids)
{
    juce::PopupMenu m;
    auto* app = &ctx;
    m.addItem ("Nouvelle playlist…"_t, ! ids.empty(), false, [app, ids] { promptNewPlaylist (*app, ids); });
    const auto lists = ctx.library().getPlaylists();
    if (! lists.empty()) m.addSeparator();
    for (auto& pl : lists)
        m.addItem (pl.name + "  (" + juce::String (pl.trackCount) + ")", ! ids.empty(), false, [app, id = pl.id, ids] { addToPlaylist (*app, id, ids); });
    return m;
}

void confirm (AppContext& ctx, const juce::String& title, const juce::String& message, const juce::String& button, std::function<void()> action)
{
    juce::ignoreUnused (ctx);
    auto options = juce::MessageBoxOptions().withTitle (title).withMessage (message).withButton (button).withButton ("Annuler"_t)
                                            .withIconType (juce::MessageBoxIconType::QuestionIcon);
    juce::AlertWindow::showAsync (options, [action] (int result) { if (result == 1 && action) action(); });
}

int removeNasAlbums (AppContext& ctx, const std::vector<AlbumInfo>& albums)
{
    int n = 0;
    for (auto& a : albums) if (a.fromNas()) n += ctx.library().removeAlbum (a.artist, a.title, "upnp");
    ctx.library().notifyChanged();
    return n;
}

int removeUnavailableNasAlbums (AppContext& ctx)
{
    juce::StringArray available;
    if (auto* upnp = dynamic_cast<UpnpProvider*> (ctx.providers().getProvider ("upnp")))
        for (auto& sv : upnp->getServers()) available.add ("upnp:" + sv.udn);
    const int n = ctx.library().removeNasSourcesExcept (available);
    ctx.library().notifyChanged();
    return n;
}

int purgeNas (AppContext& ctx)
{
    const int n = ctx.library().removeSourcePrefix ("upnp");
    ctx.library().notifyChanged();
    return n;
}

void showAlbumMenu (AppContext& ctx, const std::vector<AlbumInfo>& albums, juce::Component* target)
{
    if (albums.empty()) return;
    auto* app = &ctx;
    const bool single = albums.size() == 1;
    const auto& first = albums.front();
    juce::PopupMenu m;
    if (single) m.addSectionHeader (first.title + " — "_t + first.artist);
    else m.addSectionHeader (juce::String ((int) albums.size()) + " albums sélectionnés"_t);

    m.addItem ("Lire"_t, [app, albums] { app->playTracks (albumTracks (*app, albums), 0); });
    m.addItem ("Ajouter à la file d'attente"_t, [app, albums] { app->enqueue (albumTracks (*app, albums)); });
    m.addSubMenu ("Ajouter à une playlist"_t, playlistSubmenu (ctx, trackIds (albumTracks (ctx, albums))));
    if (single)
    {
        const bool fav = ctx.library().isAlbumFavourite (first.artist, first.title);
        m.addItem (fav ? "Retirer des favoris"_t : "Ajouter aux favoris"_t, [app, first, fav]
        {
            app->library().setAlbumFavourite (first.artist, first.title, ! fav);
            app->showMessage (fav ? "Album retiré des favoris"_t : "Album ajouté aux favoris"_t);
            app->library().notifyChanged();
        });
        m.addItem ("Voir l'artiste"_t, [app, first] { app->navigate ({ PageId::ArtistDetail, first.artist }); });
    }
    m.addSeparator();
    m.addItem ("Analyser / Convertir dans AuraEngine X"_t, [app, albums] { app->sendToAuraEngine (albumTracks (*app, albums)); });

    std::vector<AlbumInfo> nas;
    for (auto& a : albums) if (a.fromNas()) nas.push_back (a);
    m.addSeparator();
    const auto label = nas.empty() ? juce::String ("NAS") : ctx.networkSourceLabel (nas.front().nasSource);
    m.addItem (nas.size() > 1 ? "Retirer les "_t + juce::String ((int) nas.size()) + " albums " + label + " de la bibliothèque"_t
                              : "Retirer de la bibliothèque (album "_t + label + ")",
               ! nas.empty(), false, [app, nas]
    {
        confirm (*app, "Retirer de la bibliothèque"_t,
                 juce::String ((int) nas.size()) + " album(s) du NAS seront retirés de la bibliothèque. Les fichiers restent sur le serveur."_t,
                 "Retirer"_t, [app, nas]
        {
            const int n = removeNasAlbums (*app, nas);
            app->showMessage (juce::String (n) + " morceau(x) du NAS retiré(s) de la bibliothèque"_t);
        });
    });
    if (! nas.empty())
    {
        const auto server = nas.front().nasSource;
        const int count = ctx.library().countAlbumsFromSourcePrefix (server);
        m.addItem ("Retirer tous les albums de ce serveur ("_t + juce::String (count) + ")", [app, server, count]
        {
            confirm (*app, "Retirer le serveur de la bibliothèque"_t,
                     juce::String (count) + " album(s) de ce serveur seront retirés de la bibliothèque (les fichiers restent sur le NAS)."_t,
                     "Retirer"_t, [app, server]
            {
                const int n = app->library().removeSourcePrefix (server);
                app->library().notifyChanged();
                app->showMessage (juce::String (n) + " morceau(x) retiré(s)"_t);
            });
        });
    }
    else if (std::any_of (albums.begin(), albums.end(), [] (const AlbumInfo& a) { return a.source == "local"; }))
    {
        m.addItem ("Fichiers locaux : retirez leur dossier dans Réglages › Bibliothèque"_t, false, false, nullptr);
    }

    auto opts = juce::PopupMenu::Options();
    if (target != nullptr) opts = opts.withTargetComponent (target).withMousePosition();
    m.showMenuAsync (opts);
}

juce::var dragPayload (const std::vector<juce::int64>& ids, const juce::String& label)
{
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> list;
    for (auto id : ids) list.add (id);
    o->setProperty ("nxTracks", list);
    o->setProperty ("label", label);
    return juce::var (o);
}

std::vector<juce::int64> idsFromPayload (const juce::var& payload)
{
    std::vector<juce::int64> ids;
    if (auto* a = payload.getProperty ("nxTracks", {}).getArray())
        for (auto& v : *a) ids.push_back ((juce::int64) v);
    return ids;
}

} // namespace nx::actions
