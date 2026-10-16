# User guide

## Quick Editor (Ctrl/⌘+1)
1. **Import:** drop a recording onto the viewer, or click *Import recording…*. In an empty project the clip is placed on the timeline automatically.
2. **Auto-caption:** pick the language (Hinglish = Roman script, Hindi = Devanagari, Punjabi = Gurmukhi) and click **Generate**. Transcription runs on your computer, and the progress bar shows each stage.
3. **Pick a caption style:** click any template. Changing styles never re-transcribes and never loses your corrections.
4. **Add music:** goes onto its own track at −14 dB, with fades, trimmed to your video.
5. **Format & look:** 16:9 / 9:16 / 1:1 / 4:5 (clips fill the new frame), plus a colour look.
6. **Export:** pick a preset and click *Export video…*. Choose MP4, MOV, GIF or audio only (MP3/WAV/M4A), and tick *Normalise loudness* for social platforms.

## Advanced Editor (Ctrl/⌘+2)
Dockable **Media**, **Effects**, **Inspector**, **AI Subtitles** and **Timeline** panels. Rearrange them freely; each mode remembers its own layout.

### Timeline
- **Drag and drop everything:** drag media from the Media bin, files from Finder/Explorer, or Library items (text, stickers, shapes, effects, filters, transitions) straight onto the timeline. A ghost shows exactly where it will land before you let go.
- Drag clips to move them, including between tracks. Linked audio follows; hold **Alt** to move one side only. Hold **Shift** while dragging to turn snapping off for a moment.
- If the spot you drop on is already taken, the item goes on a **new track** right next to that one (the ghost shows "new track"), so nothing is pushed off-screen.
- Hold **Ctrl/⌘** while dropping or moving to **insert**: a yellow marker shows the cut where it goes, and everything after it moves later (linked audio too). This is also how you reorder clips on a track.
- Drop onto the dashed **+ New track** lanes above the top track or below the last one to put things on a fresh track.
- Drop an effect, filter, speed curve or voice effect **onto a clip** to apply it to that clip (it lights up); drop a transition **near a cut**. Dropped on empty space, effects and filters become an effect layer.
- Drag a **track header** up or down to reorder tracks. Drag a **caption block** to retime it. While dragging near the edges the timeline scrolls; press **Esc** to cancel.
- Drag clip edges to trim. Drag the small **dots at the top corners** to set fades.
- **Room to work:** the timeline gets about a third of the window and keeps that share when you resize or maximise the window. Drag the divider above it to change the share, and your choice is remembered. On small screens the tracks switch to a compact size so they all stay visible, and the side panels scroll instead of squeezing the timeline.
- Click the ruler to scrub. Use Ctrl/⌘+wheel to zoom and Shift+wheel to scroll horizontally.
- Track buttons: **M** mute, **L** lock, **H** hide, **CC** burn captions into exports.
- Right-click clips, tracks or empty space for more commands (speed, fades, unlink, rename, add title, and so on).
- Caption lane: click a caption to jump to it, drag it (or its edges) to retime it, and double-click to edit its text. An orange underline marks words that need review.

### Library (text, stickers, effects, transitions, filters…)
The **Library** panel on the left has live previews of everything you can add. Search at the top, or pick a tab:
- **Text**: 18 ready-made styles (bold pop, neon, gradient headline, lower third, breaking news, subscribe button, quote, chapter, price tag, typewriter, speech bubble…). Double-click to add at the playhead, or drag onto the video to place it.
- **Stickers**: emoji stickers that pop in and float. Drag them onto the video.
- **Effects**: drag onto a clip to apply it to that clip, or onto empty timeline space to make an **effect layer**: a pink clip whose effect applies to everything on the tracks below while it plays. Trim it like any clip.
- **Transitions**: drag onto the cut between two clips on the same track (or select the second clip and double-click). The blue bow tie on the timeline shows it; change the type and length in Inspector → Basic.
- **Filters**: colour looks. Applied to the selected clips, or as a filter layer when nothing is selected.
- **Shapes**, **Speed** curves, **Audio** voice effects and **Basics** (fades, framing).

### Inspector tabs
Select a clip and use the tabs on the right:
- **Basic**: position, scale, rotation, opacity, mirror/flip, transition in, fades, crop, colour.
- **Animate**: keyframes. Move the playhead, set the clip how you want it, press **◆ Add keyframe** (or ⌘/Ctrl+K); move and change it again. Once a clip has keyframes, dragging it on the video or changing Transform sets the keyframe at the playhead. Pick the curve (smooth, linear, ease in/out, bounce, hold) or a **Quick animation** preset (zoom in/out, pan, pop in, fade, spin, rise). Diamonds on the timeline clip show where the keyframes are.
- **Effects**: filter look, adjustments (hue, vibrance, highlights, shadows, fade, vignette, grain, sharpen), your own **.cube LUT**, the clip's effects list (intensity, speed, order) and blend mode.
- **Cutout**: **chroma key** (green/blue screen with strength, softness and spill removal), **masks** (linear, mirror, circle, rectangle, with feather and invert) and **canvas background** (blurred video or a colour behind the clip, great for landscape video in a 9:16 project).
- **Speed**: speed, **speed curves**, **Reverse**, **Freeze frame**, **Stabilise**, **Extract audio**.
- Titles have **Text**, **Style** (gradient, shadow, letter spacing, box), and **Animate** (in, out and loop animations plus keyframes). Audio clips have **Audio** (volume, auto-duck) and **Voice** (voice changers, noise reduction, voice enhance).

### Smart tools (Tools menu)
- **Remove Silences…** cuts the quiet parts and closes the gaps (choose the level, shortest pause and how much breathing room to keep).
- **Remove Filler Words** cuts “um”, “uh”, “erm”… using the captions' word timings (generate captions first).
- **Split by Scenes** cuts a clip wherever the shot changes.
- **Find Beats** adds markers on the beats of the music; clips snap to markers while you drag. **M** adds a marker at the playhead.
- **Text to Speech…** turns text (or your captions) into a voice-over with the voices installed on your computer.
- **Save Frame as Image…** exports the current frame as PNG/JPEG.
- **Auto-duck**: tick it on the music track (Inspector → Audio) and the music dips whenever someone speaks.
- **Translate to English**: an option in AI Subtitles → Generate.

Everything above runs on this computer with FFmpeg and Qt. Heavy jobs (reverse, stabilise) show progress and can be cancelled; their results are saved in the project's `media/generated` folder.

### Moving and resizing things on the video
Click anything on the video to select it: captions, titles, shapes or a video layer. A blue box with corner handles appears.
- **Move:** drag it. Pink guide lines show when it snaps to the centre, the frame edges or the caption-safe area. Hold **Shift** to move in a straight line, or **Ctrl/⌘** to turn snapping off.
- **Resize:** drag a corner handle. Shapes stretch freely (hold **Shift** to keep their proportions). Captions, titles and video scale evenly.
- **Captions:** dragging a caption moves **only that caption**; everything else stays put. Hold **Alt/Option** while dragging to move **all** captions together (captions you placed on their own move along by the same amount). Prefer it the other way round? Right-click a caption → *Dragging a caption moves* → *All captions together*.
  Right-click also offers *Reset this caption's position*, *Put all captions at this position*, *Reset all caption positions* and the same for size. A caption you placed keeps its spot when captions are re-split (e.g. after changing words per line).
- **Video:** the main video usually fills the frame, so the first click only selects it. Drag after that to make picture-in-picture layouts.
- **Add shapes and titles by dropping:** drag an item from *Effects → Shapes* or *Titles* onto the video to place it exactly where you drop it. The shapes include an arrow, highlight box, circle, star, pill tag and triangle.
- **Edit text in place:** double-click a title or caption on the video.
- **Right-click** for Reset position, Reset size or Delete. Every move and resize can be undone with ⌘Z.

### Text
- **Add text:** press **T**, click **T Add Text** in the toolbar, **+ Text** under the video, or **+ Add text** in Quick mode. The text appears at the playhead with an editor right on the video, so just type. **Enter** finishes, **Shift+Enter** starts a new line, **Esc** cancels. The video updates as you type, and the whole edit is one undo step. Double-click any text to edit it again.
- **Colour single words:** put stars around them, e.g. `Hello *world*`. Starred words use the text's highlight colour (the stars aren't shown).
- **Format bar:** when one text is selected, a bar appears above the video with Styles, font, size, **B**old, *I*talic, U̲nderline, letter case (Aa), alignment, text colour, highlight colour and ✎ (edit words). It only ever changes **that** text.
- **Professional styles:** 28 looks in 6 groups: clean, bold, boxed/labels, glow/neon, elegant and fun. Pick one from the format bar's *Styles*, the Inspector's *Text styles* grid, or the Library's *Text styles* tab. You can also drag a style onto a text on the video, or onto empty space for a new styled text. A style changes only the look (font, colours, outline, shadow, box, glow, spacing, case). Your words, position, size and animation stay.
- **Inspector → Text** has every setting too (font, weight, italic, underline, case, size, colours, outline, box, alignment, line spacing, highlight colour), plus glow, gradient, shadow, letter spacing and box shape under **Style**. If several texts are selected, a banner says so; click **Edit only this one** to change just one.
- **Library → Text** has ready-made layouts, including a pro lower third, name tag, chapter title, quote card, breaking-news bar, YouTube title, neon and luxury titles, a subscribe button and a key-point highlight.
- **One caption, its own look:** in *AI Subtitles → Style*, tick **Change only the selected caption**. Templates and every style setting (font, size, colours…) then change just the selected caption.

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
| ' | Caption-safe guides | | |
| ⌘I | Import | ⇧⌘R | Record voice-over |
| ⇧⌘C | Generate captions | ⌘E | Export |
| ⌘S | Save | ⌘Z / ⇧⌘Z | Undo / redo |
| T | Add text (type on the video) | ⇧T | Lower third |
| ⌘K | Add keyframe at playhead | ⇧F | Freeze frame |
| M | Add marker | ⇧M / ⌥M | Previous / next marker |
| ⇧⌘S | Snapshot (save frame as PNG) | | |

## Saving and recovery
- **⌘S** saves atomically and keeps rotating backups in `backups/`.
- Autosave runs every 60 s by default (configurable), writing to `autosaves/`.
- If the app or computer stops unexpectedly, reopening the project offers to recover the newer autosave.
- **File → Relink Media** finds moved files. **Collect Media** copies everything into the project, and **Save Portable Package** writes a self-contained copy.
