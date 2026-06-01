import * as fs from 'fs';
import * as vscode from 'vscode';
import * as path from 'path';

const C2S_PATH_PROBE =
  'raw/client/c2s_position/rx0/rz0/cx0/cz0/x0_y0_z0.c2s_position.wire';
const REF_CAPTURE_PROBE = '0290_play_51_entity_head_rotation.wire';

/** Path + packet name probes (mc_reference_client / out2 capture filenames). */
const CAPTURE_PATH_PROBES: ReadonlyArray<readonly [string, string]> = [
  [REF_CAPTURE_PROBE, 'entity_head_rotation'],
  ['0062_play_10_declare_commands.wire', 'declare_commands'],
  ['0047_play_77_system_chat.wire', 'system_chat'],
  ['0058_play_7d_set_ticking_state.wire', 'set_ticking_state'],
  ['0031_play_83_update_recipes.wire', 'update_recipes'],
];

type LibchunkModule = typeof import('@flayerproxy/libchunk') & { __libchunkRoot?: string };
type WireMeta = ReturnType<LibchunkModule['parseWirePath']>;

function libchunkProbeFailure(mod: LibchunkModule): string | null {
  if (mod.packetNameFromPath(C2S_PATH_PROBE) !== 'c2s_position') {
    return 'c2s_position path inference';
  }
  for (const [capturePath, packet] of CAPTURE_PATH_PROBES) {
    if (mod.packetNameFromPath(capturePath) !== packet) {
      return `${packet} capture path (${capturePath})`;
    }
    if (!mod.isPacketSupported(packet)) {
      return `${packet} decoder`;
    }
  }
  return null;
}

/** Prefer a build that recognizes c2s_* sniffer paths (vendor, then repo libchunk/js). */
function loadLibchunk(): LibchunkModule {
  const candidates = [
    path.join(__dirname, '..', 'vendor', 'libchunk'),
    path.join(__dirname, '..', '..', '..', 'libchunk', 'js'),
  ];
  const errors: string[] = [];
  for (const root of candidates) {
    try {
      const resolved = require.resolve(root);
      delete require.cache[resolved];
      // eslint-disable-next-line @typescript-eslint/no-require-imports
      const mod = require(root) as LibchunkModule;
      const fail = libchunkProbeFailure(mod);
      if (!fail) {
        mod.__libchunkRoot = root;
        return mod;
      }
      errors.push(`${root}: missing ${fail}`);
    } catch (err) {
      errors.push(`${root}: ${err instanceof Error ? err.message : String(err)}`);
    }
  }
  throw new Error(`libchunk not loaded (${errors.join('; ')})`);
}

const libchunk = loadLibchunk();

const WIRE_EDITOR_VIEW = 'minecraftWireViewer.wireDecode';
const HEX_MAX_BYTES = 4096;
const CACHE_MAX = 64;

class WireCustomDocument implements vscode.CustomDocument {
  constructor(readonly uri: vscode.Uri) {}

  dispose(): void {}
}

let lastWireUri: vscode.Uri | undefined;

interface DecodeCacheEntry {
  mtimeMs: number;
  text: string;
}

const decodeCache = new Map<string, DecodeCacheEntry>();

function getConfig() {
  return vscode.workspace.getConfiguration('minecraftWireViewer');
}

function readWireBuffer(filePath: string): Buffer {
  return fs.readFileSync(filePath);
}

function fileMtimeMs(filePath: string): number {
  return fs.statSync(filePath).mtimeMs;
}

function appendHexDump(lines: string[], wire: Buffer): void {
  lines.push('## hex');
  if (wire.length <= HEX_MAX_BYTES) {
    lines.push(libchunk.hexDump(wire));
  } else {
    lines.push(
      libchunk.hexDump(wire.subarray(0, HEX_MAX_BYTES)) +
        `\n… (${wire.length - HEX_MAX_BYTES} more bytes; enable smaller captures or raise limit in settings)`
    );
  }
  lines.push('');
}

function appendMetaHeader(lines: string[], filePath: string, meta: WireMeta): void {
  lines.push(`# ${path.basename(filePath)}`);
  lines.push(`# packet: ${meta.packet ?? '(unknown)'}`);
  if (libchunk.__libchunkRoot) lines.push(`# libchunk: ${libchunk.__libchunkRoot}`);
  if (meta.captureSeq != null) {
    const id =
      meta.protocolId != null
        ? `0x${meta.protocolId.toString(16).padStart(2, '0')}`
        : '?';
    lines.push(
      `# capture: [${String(meta.captureSeq).padStart(4, '0')}] ${meta.capturePhase ?? '?'} ${id}`
    );
  }
  if (meta.category && meta.category !== meta.capturePhase) {
    lines.push(`# archive: ${meta.category}/`);
  }
  if (meta.worldX != null) {
    lines.push(
      `# world: ${meta.worldX}${meta.worldY != null ? `, ${meta.worldY}` : ''}, ${meta.worldZ}`
    );
  }
  if (meta.entityId != null) lines.push(`# entityId: ${meta.entityId}`);
  if (meta.rx != null) lines.push(`# region rx${meta.rx} rz${meta.rz} cx${meta.cx} cz${meta.cz}`);
}

function formatDecode(filePath: string): string {
  const mtime = fileMtimeMs(filePath);
  const cached = decodeCache.get(filePath);
  if (cached && cached.mtimeMs === mtime) {
    return cached.text;
  }

  const lines: string[] = [];
  const meta = libchunk.parseWirePath(filePath);
  appendMetaHeader(lines, filePath, meta);
  lines.push('');

  try {
    const wire = readWireBuffer(filePath);
    lines.push(`# wire bytes: ${wire.length}`);
    lines.push('');

    const showHexAlways = getConfig().get<boolean>('showHexDump', false);
    const showHexWhenUndecoded = getConfig().get<boolean>('showHexWhenUndecoded', true);

    if (showHexAlways) {
      appendHexDump(lines, wire);
    }

    lines.push('## decoded');

    const result = libchunk.decodeWireFile(filePath, wire);
    const packet = result.packet ?? meta.packet ?? libchunk.packetNameFromPath(filePath);

    if (!packet) {
      if (meta.protocolId != null) {
        lines.push(
          `(unknown packet name; protocol id 0x${meta.protocolId.toString(16).padStart(2, '0')})`
        );
      } else {
        lines.push('(cannot infer packet from path)');
      }
      if (!showHexAlways && showHexWhenUndecoded) {
        lines.push('');
        appendHexDump(lines, wire);
      }
    } else if (result.unsupported) {
      lines.push(`(libchunk has no structured decoder for ${packet})`);
      if (!showHexAlways && showHexWhenUndecoded) {
        lines.push('');
        appendHexDump(lines, wire);
      }
    } else if (result.ok && result.text) {
      lines.push(result.text);
    } else {
      lines.push(`error: ${result.error ?? 'parse failed'}`);
      if (!showHexAlways && showHexWhenUndecoded) {
        lines.push('');
        appendHexDump(lines, wire);
      }
    }
  } catch (e) {
    lines.push(`error: ${e instanceof Error ? e.message : String(e)}`);
  }

  const text = lines.join('\n');
  decodeCache.set(filePath, { mtimeMs: mtime, text });
  if (decodeCache.size > CACHE_MAX) {
    const first = decodeCache.keys().next().value;
    if (first) decodeCache.delete(first);
  }
  return text;
}

function buildWebviewHtml(filePath: string, body?: string): string {
  const content = (body ?? formatDecode(filePath))
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');
  return `<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<style>
  body { font-family: var(--vscode-editor-font-family); font-size: 13px; padding: 12px; color: var(--vscode-editor-foreground); background: var(--vscode-editor-background); }
  pre { white-space: pre-wrap; word-break: break-all; margin: 0; }
</style>
</head><body><pre>${content}</pre></body></html>`;
}

function getActiveWireUri(): vscode.Uri | undefined {
  if (lastWireUri?.fsPath.endsWith('.wire')) return lastWireUri;
  const doc = vscode.window.activeTextEditor?.document;
  if (doc?.uri.fsPath.endsWith('.wire')) return doc.uri;
  return undefined;
}

class WireDecodeEditorProvider implements vscode.CustomReadonlyEditorProvider<WireCustomDocument> {
  private readonly refreshTimers = new Map<string, ReturnType<typeof setTimeout>>();

  openCustomDocument(
    uri: vscode.Uri,
    _openContext: vscode.CustomDocumentOpenContext,
    _token: vscode.CancellationToken
  ): WireCustomDocument {
    return new WireCustomDocument(uri);
  }

  resolveCustomEditor(
    document: WireCustomDocument,
    webviewPanel: vscode.WebviewPanel,
    _token: vscode.CancellationToken
  ): void {
    lastWireUri = document.uri;
    const filePath = document.uri.fsPath;

    const refreshSoon = () => {
      const existing = this.refreshTimers.get(filePath);
      if (existing) clearTimeout(existing);
      this.refreshTimers.set(
        filePath,
        setTimeout(() => {
          this.refreshTimers.delete(filePath);
          this.refreshEditor(filePath, webviewPanel);
        }, getConfig().get<number>('refreshDebounceMs', 250))
      );
    };

    this.refreshEditor(filePath, webviewPanel);

    const watchLive = getConfig().get<boolean>('watchFileChanges', true);
    if (watchLive) {
      const watcher = vscode.workspace.createFileSystemWatcher(
        new vscode.RelativePattern(path.dirname(filePath), path.basename(filePath))
      );
      watcher.onDidChange(refreshSoon);
      webviewPanel.onDidDispose(() => {
        watcher.dispose();
        const t = this.refreshTimers.get(filePath);
        if (t) clearTimeout(t);
        this.refreshTimers.delete(filePath);
      });
    }
  }

  private refreshEditor(filePath: string, panel: vscode.WebviewPanel): void {
    panel.webview.html = buildWebviewHtml(filePath, 'Decoding…');
    setImmediate(() => {
      try {
        panel.webview.html = buildWebviewHtml(filePath, formatDecode(filePath));
      } catch {
        /* panel disposed */
      }
    });
  }
}

async function decodeActiveWire() {
  const uri = getActiveWireUri();
  if (!uri) {
    vscode.window.showWarningMessage('Open a .wire file first.');
    return;
  }
  await vscode.commands.executeCommand(
    'vscode.openWith',
    uri,
    WIRE_EDITOR_VIEW,
    vscode.ViewColumn.Active
  );
}

async function decodeMapChunkJson() {
  const uri = getActiveWireUri();
  if (!uri) {
    vscode.window.showWarningMessage('Open a .wire file first.');
    return;
  }
  const filePath = uri.fsPath;
  const packet = libchunk.packetNameFromPath(filePath);
  if (packet !== 'map_chunk') {
    vscode.window.showWarningMessage('Not a map_chunk wire file.');
    return;
  }
  const result = libchunk.decodeMapChunkJson(
    path.basename(filePath),
    readWireBuffer(filePath)
  );
  if (!result.ok) {
    vscode.window.showErrorMessage(result.error ?? 'JSON decode failed');
    return;
  }
  const doc = await vscode.workspace.openTextDocument({
    language: 'json',
    content: result.json ?? JSON.stringify(result.parsed, null, 2),
  });
  await vscode.window.showTextDocument(doc, vscode.ViewColumn.Beside);
}

async function openPairedPng() {
  const uri = getActiveWireUri();
  if (!uri) return;
  const filePath = uri.fsPath;
  const pngRoot = getConfig().get<string>('pngRoot', '') || undefined;
  const pngPath = libchunk.pngPathForWire(filePath, pngRoot || undefined);
  if (!pngPath) {
    vscode.window.showWarningMessage('No paired PNG for this wire path.');
    return;
  }
  if (!fs.existsSync(pngPath)) {
    vscode.window.showErrorMessage(`PNG not found: ${pngPath}`);
    return;
  }
  await vscode.commands.executeCommand('vscode.open', vscode.Uri.file(pngPath));
}

function refreshActiveWireEditor() {
  const uri = getActiveWireUri();
  if (!uri) return;
  decodeCache.delete(uri.fsPath);
  lastWireUri = uri;
  vscode.commands.executeCommand('vscode.openWith', uri, WIRE_EDITOR_VIEW, vscode.ViewColumn.Active);
}

export function activate(context: vscode.ExtensionContext) {
  const libchunkGap = libchunkProbeFailure(libchunk);
  if (libchunkGap) {
    void vscode.window.showWarningMessage(
      `Minecraft Wire Viewer: libchunk is outdated (missing ${libchunkGap}). Run: cd extensions/minecraft-wire-viewer && npm run bundle:libchunk`
    );
  }
  context.subscriptions.push(
    vscode.window.registerCustomEditorProvider(
      WIRE_EDITOR_VIEW,
      new WireDecodeEditorProvider(),
      { webviewOptions: { retainContextWhenHidden: true } }
    ),
    vscode.commands.registerCommand('minecraftWireViewer.decode', decodeActiveWire),
    vscode.commands.registerCommand(
      'minecraftWireViewer.decodeMapChunkJson',
      decodeMapChunkJson
    ),
    vscode.commands.registerCommand('minecraftWireViewer.openPng', openPairedPng),
    vscode.commands.registerCommand('minecraftWireViewer.refresh', refreshActiveWireEditor),
    vscode.workspace.onDidChangeConfiguration((e) => {
      if (e.affectsConfiguration('minecraftWireViewer')) {
        decodeCache.clear();
      }
    })
  );
}

export function deactivate() {}
