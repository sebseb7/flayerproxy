import * as fs from 'fs';
import * as vscode from 'vscode';
import * as path from 'path';

// Bundled at vendor/libchunk (VSIX) or ../../libchunk/js (extension dev host)
// eslint-disable-next-line @typescript-eslint/no-require-imports
const libchunk: typeof import('@flayerproxy/libchunk') = (() => {
  const vendored = path.join(__dirname, '..', 'vendor', 'libchunk');
  try {
    return require(vendored);
  } catch {
    return require(path.join(__dirname, '..', '..', '..', 'libchunk', 'js'));
  }
})();

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

function formatDecode(filePath: string): string {
  const mtime = fileMtimeMs(filePath);
  const cached = decodeCache.get(filePath);
  if (cached && cached.mtimeMs === mtime) {
    return cached.text;
  }

  const lines: string[] = [];
  const meta = libchunk.parseWirePath(filePath);
  lines.push(`# ${path.basename(filePath)}`);
  lines.push(`# packet: ${meta.packet ?? '(unknown)'}`);
  if (meta.category) lines.push(`# archive: ${meta.category}/`);
  if (meta.worldX != null) {
    lines.push(
      `# world: ${meta.worldX}${meta.worldY != null ? `, ${meta.worldY}` : ''}, ${meta.worldZ}`
    );
  }
  if (meta.entityId != null) lines.push(`# entityId: ${meta.entityId}`);
  if (meta.rx != null) lines.push(`# region rx${meta.rx} rz${meta.rz} cx${meta.cx} cz${meta.cz}`);
  lines.push('');

  try {
    const wire = readWireBuffer(filePath);
    lines.push(`# wire bytes: ${wire.length}`);
    lines.push('');

    const showHex = getConfig().get<boolean>('showHexDump', false);
    if (showHex) {
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

    lines.push('## decoded');

    const packet = meta.packet ?? libchunk.packetNameFromPath(filePath);
    if (!packet) {
      lines.push('(cannot infer packet from path)');
    } else if (!libchunk.isPacketSupported(packet)) {
      lines.push(`(libchunk does not decode: ${packet})`);
    } else {
      const result = libchunk.decodeWire(packet, wire);
      if (result.ok && result.text) {
        lines.push(result.text);
      } else {
        lines.push(`error: ${result.error ?? 'parse failed'}`);
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
