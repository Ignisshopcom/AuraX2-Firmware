# Ignis Studio Modernization Notes

## Current state

This project has been moved from the old Electron 11 / Forge beta setup toward the current Electron Forge flow. Electron is pinned to `42.2.0`, the current stable release listed by Electron Releases on May 20, 2026.

The renderer still uses Node APIs directly (`fs`, `path`, `child_process`, `ipcRenderer`). Because of that, `nodeIntegration` is still enabled for compatibility. The next security milestone is to move those APIs behind a preload script with `contextBridge`, then switch the renderer to:

- `nodeIntegration: false`
- `contextIsolation: true`
- `sandbox: true`

## Build setup

Install a current Node.js LTS with npm, then run from `resources/app`:

```powershell
npm install
```

Windows installer:

```powershell
npm run make:win
```

macOS DMG/ZIP:

```bash
npm run make:mac
```

Build macOS artifacts on macOS. Electron Forge can package only some targets cross-platform, and DMG/signing/notarization should be done on a Mac.

## Important follow-up work

1. Replace direct renderer filesystem access with preload APIs.
2. Replace synchronous IPC (`sendSync`) with `ipcRenderer.invoke` / `ipcMain.handle`.
3. Replace `node-zip` with a maintained ZIP library.
4. Replace browser-side Less compilation with compiled CSS during build.
5. Move remote config from `http://petr.holly.darkyork.com/...` to HTTPS.
6. Add automated smoke tests for import, split-to-timelines, save/load, and export.
7. Add macOS code signing and notarization settings before public distribution.

## Notes

The `package-lock.json` is still from the old dependency tree until `npm install` is run. After installing dependencies, commit the refreshed lockfile so builds are reproducible.
