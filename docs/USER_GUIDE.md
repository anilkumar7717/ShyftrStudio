# User guide

## Quick Editor (Ctrl/⌘+1)
1. **Import:** drop a recording onto the viewer, or click *Import recording…*. In an empty project the clip is placed on the timeline automatically.
2. **Auto-caption:** pick the language (Hinglish = Roman script, Hindi = Devanagari, Punjabi = Gurmukhi) and click **Generate**. Transcription runs on your computer, and the progress bar shows each stage.
3. **Pick a caption style:** click any template. Changing styles never re-transcribes and never loses your corrections.
4. **Add music:** goes onto its own track at −14 dB, with fades, trimmed to your video.
5. **Format & look:** 16:9 / 9:16 / 1:1 / 4:5 (clips fill the new frame), plus a colour look.
6. **Export:** pick a preset and click *Export video…*.

## Advanced Editor (Ctrl/⌘+2)
Dockable **Media**, **Effects**, **Inspector**, **AI Subtitles** and **Timeline** panels. Rearrange them freely; each mode remembers its own layout.

### Timeline
- Drag clips to move them, including between tracks of the same kind. Linked audio follows; hold **Alt** to move one side only. Hold **Ctrl** to disable snapping temporarily.
- Drag clip edges to trim. Drag the small **dots at the top corners** to set fades.
- Click the ruler to scrub. Use Ctrl/⌘+wheel to zoom and Shift+wheel to scroll horizontally.
- Track buttons: **M** mute, **L** lock, **H** hide, **CC** burn captions into exports.
- Right-click clips, tracks or empty space for more commands (speed, fades, unlink, rename, add title, and so on).
- Caption lane: click a caption to jump to it, drag its edges to retime it, and double-click to edit its text. An orange underline marks words that need review.

### Moving and resizing things on the video
Click anything on the video to select it: captions, titles, shapes or a video layer. A blue box with corner handles appears.
- **Move:** drag it. Pink guide lines show when it snaps to the centre, the frame edges or the caption-safe area. Hold **Shift** to move in a straight line, or **Ctrl/⌘** to turn snapping off.
- **Resize:** drag a corner handle. Shapes stretch freely (hold **Shift** to keep their proportions). Captions, titles and video scale evenly.
- **Captions:** dragging moves every caption on that track. Hold **Alt/Option** while dragging to move only the caption you grabbed. Right-click → *Reset caption position* puts them back.
- **Video:** the main video usually fills the frame, so the first click only selects it. Drag after that to make picture-in-picture layouts.
- **Add shapes and titles by dropping:** drag an item from *Effects → Shapes* or *Titles* onto the video to place it exactly where you drop it. The shapes include an arrow, highlight box, circle, star, pill tag and triangle.
- **Edit text in place:** double-click a title or caption on the video.
- **Right-click** for Reset position, Reset size or Delete. Every move and resize can be undone with ⌘Z.

### Editing captions (three ways)
1. **On the video:** double-click the caption in the viewer, type, then press **Enter** to save or **Esc** to cancel. Double-clicking a caption on the timeline does the same.
2. **In the caption list:** *AI Subtitles → Edit captions*. Click a caption and edit it in the text box, then press **Apply text** or ⌘/Ctrl+Enter. Use **Prev/Next** to move through the captions.
3. **In the list cell:** double-click (or click again on a selected caption) to edit it in place.

After generation, the *Edit captions* tab opens automatically, in Quick mode as well. Every edit can be undone with ⌘Z.

### AI Subtitles panel
- **Generate:** source, language, model, optional vocabulary (names, brands), speech detection, timing refinement. Includes *Details* (local log), *Retry* and *Cancel*.
- **Transcript:** synced list (follows playback), search, edit text inline, ◀In/In▶/◀Out/Out▶ frame nudges, Split (before the selected word), Merge, Delete, speaker per caption, *Edit speakers…*, a word-timing table, *Re-flow captions*, *Shift all*, and export to SRT/VTT/ASS/TXT.
- **Style:** template gallery (32 built-in templates; the newest are listed first: Pop Pill, Glass Card, Neon and Sunset Gradient, Tilt Punch, Hollow Outline, Mint Karaoke, Editorial Italic, Red Alert, Comic Bubble, Shadow Stack, Story Clean, Gamer Glow and Streaming Classic) (or *apply to selected caption only*), plus every style property: font, weight, size, case, colours, outline, shadow, background box (line/caption/spoken word/full bar), word effect (highlight, karaoke fill, pop, one word at a time, reveal, typewriter), entry/exit animation, safe position, offset, alignment, words per line, lines, speaker names and colours. *Save as brand preset…* stores the style in `User_Templates` for every project.

## Keyboard shortcuts (defaults, remappable in Preferences → Shortcuts)
| Key | Action | Key | Action |
|---|---|---|---|
| Space | Play / pause | J / K / L | Reverse / stop / forward (repeat for faster) |
| ← / → | Previous / next frame | Shift+← / → | 10 frames |
| ↑ / ↓ | Previous / next edit | Home / End | Start / end |
| S or ⌘B | Split | Shift+S | Split all tracks |
| Delete | Delete | Shift+Delete | Ripple delete |
| ⌘C / ⌘X / ⌘V / ⌘D | Copy / cut / paste at playhead / duplicate | ⌘L | Link / unlink |
| I / O / Alt+X | In / out / clear | ⇧⌘L | Loop in/out |
| = / − / Shift+Z | Zoom in / out / fit | N | Snapping |
| T / Shift+T | Title / lower third | ' | Caption-safe guides |
| ⌘I | Import | ⇧⌘R | Record voice-over |
| ⇧⌘C | Generate captions | ⌘E | Export |
| ⌘S | Save | ⌘Z / ⇧⌘Z | Undo / redo |

## Saving and recovery
- **⌘S** saves atomically and keeps rotating backups in `backups/`.
- Autosave runs every 60 s by default (configurable), writing to `autosaves/`.
- If the app or computer stops unexpectedly, reopening the project offers to recover the newer autosave.
- **File → Relink Media** finds moved files. **Collect Media** copies everything into the project, and **Save Portable Package** writes a self-contained copy.
