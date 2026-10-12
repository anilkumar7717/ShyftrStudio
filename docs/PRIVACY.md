# Privacy statement

Shyftr Studio is local-first by design.

- **No network use.** The app makes no outbound connections: no telemetry, analytics, update checks, crash uploads, licence server or sign-in. Editing, transcription and export all work with the network disabled.
- **Your media stays put.** Imported files are referenced in place, or copied into your project folder only when you choose *Collect Media* or *Portable Package*.
- **Speech recognition is local.** Audio is extracted to a temporary WAV inside the project (`subtitles/work/`) and processed by whisper.cpp on this computer. Model files live in `AI_Models/`, each with a recorded SHA-256 and source.
- **Logs stay local.** Export and transcription logs are written inside the project (`exports/logs/`) and are shared only if you send them yourself.
- **Clearing data.** Preferences → Privacy & Storage clears caches. Deleting a project folder removes everything related to it.
- **Administrators** can deploy `User_Preferences/policy.json`:
```json
{ "cacheRetentionDays": 30, "allowUserModels": false,
  "defaultExport": { "presetId": "youtube-1080p", "width": 1920, "height": 1080, "fps": "30/1", "crf": 20 } }
```
