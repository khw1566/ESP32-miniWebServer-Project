// Song search for the board's YouTube Music app, from music.youtube.com itself.
// Signed out, YouTube Music offers no "Songs" filter, so the catalogue is reached through
// what a search returns: songs and albums/singles first (expanded with the InnerTube
// browse call), then YouTube Music's own playlists, then user playlists (read from the
// YouTube web browse call, with yt-dlp as fallback). Talk shows, fan cams and other
// uploaded videos never enter the list.
import { sanitize, validVideoId, MUSIC_MAX_SECONDS } from './lib.mjs';

const ORIGIN = 'https://music.youtube.com';
const UA = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/141.0 Safari/537.36';
const ALBUMS = 'EgWKAQIYAWoQEAUQCRAEEAoQEBAOEBUQEQ==';
const MAX_SOURCES = 12, PLAYLIST_ITEMS = 50;
// Lower sorts first (ties keep search order): top-result songs, albums and YouTube Music's
// own playlists, then albums from the album filter (genre words match obscure album
// titles), then user playlists (often teasers and clips).
const RANK = source => source.song || source.official || (source.album && !source.filtered) ? 0 : source.album ? 1 : 2;

// "3:45" / "1:02:03" -> seconds
export function parseDuration(text) {
  if (!/^\d+(:\d{1,2}){1,2}$/.test(text || '')) return 0;
  return text.split(':').reduce((total, part) => total * 60 + Number(part), 0);
}
const runsText = runs => (runs || []).map(run => run.text).join('');
const columns = item => (item.flexColumns || []).map(c => c.musicResponsiveListItemFlexColumnRenderer?.text);
const collect = (root, key) => {
  const found = [];
  (function walk(node) {
    if (!node || typeof node !== 'object') return;
    if (node[key]) found.push(node[key]);
    for (const k in node) walk(node[k]);
  })(root);
  return found;
};
// Subtitle runs look like "Single • Artist • 2026" or "Song • Artist • Album • 3:45".
function subtitleParts(text) { return (text?.runs ? runsText(text.runs) : '').split(' • ').map(s => s.trim()); }
function artistOf(text) {
  const artists = (text?.runs || []).filter(run => run.navigationEndpoint?.browseEndpoint?.browseId?.startsWith('UC')).map(run => run.text);
  return artists.length ? artists.join(', ') : subtitleParts(text)[1] || '';
}
function track(id, title, artist, duration) {
  if (!validVideoId(id) || !title || duration > MUSIC_MAX_SECONDS) return null; /* untitled = deleted/private */
  return { id, title: sanitize(title, 150) || id, channel: sanitize(artist, 60), channel_id: '', duration, views: 0, live: false };
}

export class YouTubeMusic {
  constructor({ ytdlp, log = () => {} }) { this.ytdlp = ytdlp; this.log = log; this.session = null; this.queries = new Map(); }

  // Client version and visitor id from the web app; refreshed hourly.
  async context() {
    if (this.session && this.session.expires > Date.now()) return this.session;
    const page = await (await fetch(`${ORIGIN}/`, { headers: { 'user-agent': UA, 'accept-language': 'ko-KR,ko;q=0.9' } })).text();
    const version = /"INNERTUBE_CLIENT_VERSION":"([^"]+)"/.exec(page)?.[1], visitor = /"VISITOR_DATA":"([^"]+)"/.exec(page)?.[1];
    if (!version) throw new Error('YouTube Music page changed (no client version)');
    this.session = { version, visitor: visitor || '', expires: Date.now() + 3600000 };
    return this.session;
  }
  async api(endpoint, body) {
    const { version, visitor } = await this.context();
    const response = await fetch(`${ORIGIN}/youtubei/v1/${endpoint}?prettyPrint=false`, {
      method: 'POST',
      headers: { 'content-type': 'application/json', origin: ORIGIN, 'user-agent': UA, 'x-goog-visitor-id': visitor },
      body: JSON.stringify({ context: { client: { clientName: 'WEB_REMIX', clientVersion: version, hl: 'ko', gl: 'KR', visitorData: visitor } }, ...body }),
    });
    if (!response.ok) throw new Error(`YouTube Music ${endpoint} HTTP ${response.status}`);
    return response.json();
  }

  // Search results in relevance order -> songs and expandable album/playlist sources.
  async sources(query) {
    const found = [];
    let filtered = false;
    const add = item => {
      const [titleText, subtitle] = columns(item);
      const title = runsText(titleText?.runs), browseId = item.navigationEndpoint?.browseEndpoint?.browseId || '';
      const videoId = item.playlistItemData?.videoId;
      const kind = subtitleParts(subtitle)[0];
      if (videoId) {
        // Songs only: episodes, podcasts and uploaded videos are not catalogue music.
        const type = item.overlay?.musicItemThumbnailOverlayRenderer?.content?.musicPlayButtonRenderer?.playNavigationEndpoint
          ?.watchEndpoint?.watchEndpointMusicSupportedConfigs?.watchEndpointMusicConfig?.musicVideoType;
        if (type === 'MUSIC_VIDEO_TYPE_ATV' || /^(노래|Song)$/.test(kind)) {
          const parts = subtitleParts(subtitle);
          found.push({ song: track(videoId, title, artistOf(subtitle), parseDuration(parts[parts.length - 1])) });
        }
      } else if (browseId.startsWith('MPREb_')) found.push({ album: browseId, artist: artistOf(subtitle), filtered });
      else if (/^VL(RDCLAK|PL|OLAK)/.test(browseId)) {
        found.push({ playlist: browseId.slice(2), official: /^VL(RDCLAK|OLAK)/.test(browseId) || /YouTube Music/.test(runsText(subtitle?.runs)) });
      }
    };
    collect(await this.api('search', { query }), 'musicResponsiveListItemRenderer').forEach(add);
    // Always offer the matching albums after the top results (an artist search shows one album at most).
    filtered = true;
    collect(await this.api('search', { query, params: ALBUMS }), 'musicResponsiveListItemRenderer').forEach(add);
    const seen = new Set();
    return found.filter(source => {
      const key = source.album || source.playlist || source.song?.id;
      if (!key || seen.has(key)) return false;
      seen.add(key); return true;
    }).map((source, order) => ({ source, order })).sort((a, b) => RANK(a.source) - RANK(b.source) || a.order - b.order)
      .map(({ source }) => source).slice(0, MAX_SOURCES);
  }
  async albumTracks(browseId, albumArtist) {
    return collect(await this.api('browse', { browseId }), 'musicResponsiveListItemRenderer').map(item => {
      const [titleText, artistText] = columns(item);
      const length = item.fixedColumns?.[0]?.musicResponsiveListItemFixedColumnRenderer?.text?.runs?.[0]?.text;
      return track(item.playlistItemData?.videoId, runsText(titleText?.runs), runsText(artistText?.runs) || albumArtist, parseDuration(length));
    }).filter(Boolean);
  }
  // Signed-out YouTube Music returns playlists empty; the YouTube web client lists them.
  async playlistTracks(list) {
    try {
      const tracks = await this.webPlaylist(list);
      if (tracks.length) return tracks;
    } catch (error) { this.log(`ytmusic web playlist failed: ${error.message}`); }
    const json = JSON.parse(await this.ytdlp(['--flat-playlist', '-J', '--playlist-items', `1:${PLAYLIST_ITEMS}`,
      `https://www.youtube.com/playlist?list=${list}`], { timeout: 40000 }));
    return (json.entries || []).map(entry => track(entry.id, entry.title,
      String(entry.channel || entry.uploader || '').replace(/ - Topic$/, ''), Number.isFinite(entry.duration) ? Math.round(entry.duration) : 0)).filter(Boolean);
  }

  async webPlaylist(list) {
    if (!this.web || this.web.expires < Date.now()) {
      const page = await (await fetch('https://www.youtube.com/', { headers: { 'user-agent': UA, 'accept-language': 'ko-KR' } })).text();
      const version = /"INNERTUBE_CLIENT_VERSION":"([^"]+)"/.exec(page)?.[1];
      if (!version) throw new Error('YouTube page changed (no client version)');
      this.web = { version, expires: Date.now() + 3600000 };
    }
    const response = await fetch('https://www.youtube.com/youtubei/v1/browse?prettyPrint=false', {
      method: 'POST', headers: { 'content-type': 'application/json', 'user-agent': UA },
      body: JSON.stringify({ context: { client: { clientName: 'WEB', clientVersion: this.web.version, hl: 'ko', gl: 'KR' } }, browseId: `VL${list}` }),
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return collect(await response.json(), 'lockupViewModel').filter(item => item.contentType === 'LOCKUP_CONTENT_TYPE_VIDEO').map(item => {
      const meta = item.metadata?.lockupMetadataViewModel;
      const artist = meta?.metadata?.contentMetadataViewModel?.metadataRows?.[0]?.metadataParts?.[0]?.text?.content || '';
      const badge = collect(item.contentImage, 'thumbnailBadgeViewModel')[0]?.text;
      return track(item.contentId, meta?.title?.content, artist.replace(/ - Topic$/, ''), parseDuration(badge));
    }).filter(Boolean).slice(0, PLAYLIST_ITEMS);
  }

  // Page of 10 songs; sources are expanded only as far as the requested page needs.
  async search(query, page, size = 10) {
    let entry = this.queries.get(query);
    if (!entry || entry.expires < Date.now()) entry = { sources: null, next: 0, tracks: [], ids: new Set(), expires: Date.now() + 600000 };
    this.queries.delete(query); this.queries.set(query, entry);
    if (this.queries.size > 50) this.queries.delete(this.queries.keys().next().value);
    entry.sources ||= await this.sources(query);
    const need = page * size + 1;
    while (entry.tracks.length < need && entry.next < entry.sources.length) {
      const source = entry.sources[entry.next++];
      let tracks = [];
      try {
        tracks = source.song ? [source.song] : source.album ? await this.albumTracks(source.album, source.artist) : await this.playlistTracks(source.playlist);
      } catch (error) { this.log(`ytmusic source failed: ${error.message}`); }
      for (const item of tracks) if (item && !entry.ids.has(item.id)) { entry.ids.add(item.id); entry.tracks.push(item); }
    }
    const items = entry.tracks.slice((page - 1) * size, page * size);
    return { page, more: entry.tracks.length > page * size || entry.next < entry.sources.length, title: '', items };
  }
}
