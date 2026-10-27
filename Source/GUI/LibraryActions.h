// NX Player — MIT License
// Library actions shared by the lists, grids, sidebar and drag & drop: playlists, favourites, NAS clean-up,
// AuraEngine X. Menus are asynchronous; the functions that change the library return what they did (tests).
#pragma once

#include "AppContext.h"

namespace nx::actions
{
std::vector<juce::int64> trackIds (const std::vector<TrackInfo>& tracks);
std::vector<TrackInfo> albumTracks (AppContext& ctx, const std::vector<AlbumInfo>& albums);

/** "Ajouter à une playlist" submenu: "Nouvelle playlist…" then every playlist. */
juce::PopupMenu playlistSubmenu (AppContext& ctx, std::vector<juce::int64> ids);
int addToPlaylist (AppContext& ctx, juce::int64 playlistId, const std::vector<juce::int64>& ids);
/** Asks for a name, creates the playlist with the tracks. */
void promptNewPlaylist (AppContext& ctx, std::vector<juce::int64> ids);
juce::int64 createPlaylistWith (AppContext& ctx, const juce::String& name, const std::vector<juce::int64>& ids);

/** Right click on album cards (one or a selection). */
void showAlbumMenu (AppContext& ctx, const std::vector<AlbumInfo>& albums, juce::Component* target);

/** NAS clean-up. Return the number of tracks removed. */
int removeNasAlbums (AppContext& ctx, const std::vector<AlbumInfo>& albums);
int removeUnavailableNasAlbums (AppContext& ctx);
int purgeNas (AppContext& ctx);
/** Asks before a removal (async), then runs it. */
void confirm (AppContext& ctx, const juce::String& title, const juce::String& message, const juce::String& button, std::function<void()> action);

/** Drag & drop payload: { "nxTracks": [ids], "label": "…" }. */
juce::var dragPayload (const std::vector<juce::int64>& ids, const juce::String& label);
std::vector<juce::int64> idsFromPayload (const juce::var& payload);
}
