# Minecraft Wire Viewer

VS Code extension that decodes flayerproxy sniffer **`.wire`** captures using **libchunk** (bundled native decoder).

## Setup

```bash
# Build libchunk + Node addon
cd libchunk/js && npm install

# Build extension
cd extensions/minecraft-wire-viewer && npm install && npm run compile
```

Launch: **Run Extension** (F5), or install the VSIX:

```bash
npm run package:vsix
# → minecraft-wire-viewer-0.1.0.vsix in this folder
```

## Usage

1. Open a workspace that contains sniffer captures, e.g. `logs/sniffer/chunks/png/raw/**/*.wire`.
2. Open any `.wire` file — it opens in the **Wire Decode** custom editor (hex + libchunk summary). Raw bytes are not shown in the text editor.
3. Commands (Command Palette):
   - **Wire: Decode with libchunk** — refresh decode panel
   - **Wire: Decode map_chunk as JSON** — full `map_chunk` JSON dump
   - **Wire: Open paired chunk PNG** — `x*_z*.png` for `map_chunk` paths

## Path conventions

Packet type is inferred from the path (see user docs): `raw/<packet>/rx…/…`, `x{wx}_z{wz}.wire` or `x{wx}_y{wy}_z{wz}.wire`, entity dirs `eu…/eu…/e{id}/`, basenames `{id}.{packet}.wire` or `{packet}.wire`.

## Settings

| Setting | Description |
|---------|-------------|
| `minecraftWireViewer.autoDecode` | Open decode webview when focusing `.wire` (default: true) |
| `minecraftWireViewer.pngRoot` | Override PNG root (default: parent of `raw/` in wire path) |
