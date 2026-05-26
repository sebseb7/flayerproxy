export interface DecodeResult {
  ok: boolean;
  text?: string;
  json?: string;
  parsed?: unknown;
  error?: string;
  unsupported?: boolean;
}

export interface WireMeta {
  packet: string | null;
  category?: 'player' | 'config' | 'misc';
  worldX?: number;
  worldY?: number;
  worldZ?: number;
  entityId?: number;
  rx?: number;
  rz?: number;
  cx?: number;
  cz?: number;
}

export function supportedPackets(): string[];
export function isPacketSupported(name: string): boolean;
export function decodeWire(packetName: string, buffer: Buffer): DecodeResult;
export function decodeMapChunkJson(basename: string, buffer: Buffer): DecodeResult;
export function decodeWireFile(filePath: string, buffer?: Buffer): DecodeResult & { packet?: string; meta?: WireMeta };
export function hexDump(buffer: Buffer): string;
export function packetNameFromPath(filePath: string): string | null;
export function parseWirePath(filePath: string): WireMeta;
export function archiveCategoryFromPath(filePath: string): 'player' | 'config' | 'misc' | null;
export function pngPathForWire(filePath: string, pngRoot?: string): string | null;
