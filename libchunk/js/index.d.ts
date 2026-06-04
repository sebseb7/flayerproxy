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
  category?: 'player' | 'config' | 'misc' | 'client' | string;
  captureSeq?: number;
  capturePhase?: string;
  protocolId?: number;
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
export function decodePayload(packetName: string, buffer: Buffer): DecodeResult;
export function decodeWire(packetName: string, buffer: Buffer): DecodeResult;
export function decodeMapChunkJson(basename: string, buffer: Buffer): DecodeResult;
export function decodeWireFile(filePath: string, buffer?: Buffer): DecodeResult & { packet?: string; meta?: WireMeta };
export function hexDump(buffer: Buffer): string;
export function packetNameFromPath(filePath: string): string | null;
export function parseWirePath(filePath: string): WireMeta;
export function archiveCategoryFromPath(filePath: string): 'player' | 'config' | 'misc' | null;
export function pngPathForWire(filePath: string, pngRoot?: string): string | null;

export interface PlayLoginResult {
  entityId: number;
  viewDistance: number;
  simulationDistance: number;
  hasDeath: boolean;
}

export interface PositionResult {
  teleportId: number;
  x: number;
  y: number;
  z: number;
  yaw: number;
  pitch: number;
}

export interface UpdateTimeResult {
  gameTime: bigint;
  dayTime: bigint;
  tickDayTime: boolean;
}

export interface GameEventResult {
  event: number;
  value: number;
}

export interface SetTickingStateResult {
  tickRate: number;
  isFrozen: boolean;
}

export interface UpdateHealthResult {
  health: number;
  food: number;
  saturation: number;
}

export interface UpdateViewPositionResult {
  chunkX: number;
  chunkZ: number;
}

export function parsePlayLogin(buffer: Buffer): PlayLoginResult | null;
export function parsePosition(buffer: Buffer): PositionResult | null;
export function parseUpdateTime(buffer: Buffer): UpdateTimeResult | null;
export function parseGameEvent(buffer: Buffer): GameEventResult | null;
export function parseSetTickingState(buffer: Buffer): SetTickingStateResult | null;
export function parseUpdateHealth(buffer: Buffer): UpdateHealthResult | null;
export function parseUpdateViewPosition(buffer: Buffer): UpdateViewPositionResult | null;

export interface EntityDestroyResult {
  entityIds: number[];
}

export interface SetPassengersResult {
  entityId: number;
  passengers: number[];
}

export interface EntityAttributeModifier {
  uuid?: string;
  amount: number;
  operation: number;
  operationName: string;
}

export interface EntityAttributeProperty {
  key: number;
  keyName?: string;
  value: number;
  modifiers: EntityAttributeModifier[];
}

export interface EntityUpdateAttributesResult {
  entityId: number;
  properties: EntityAttributeProperty[];
}

export interface EntityStatusResult {
  entityId: number;
  status: number;
}

export interface EntityEffectResult {
  entityId: number;
  effectId: number;
  amplifier: number;
  duration: number;
  flags: number;
}

export interface RemoveEntityEffectResult {
  entityId: number;
  effectId: number;
}

export interface AttachEntityResult {
  attachedId: number;
  holdingId: number;
}

export function parseEntityDestroy(buffer: Buffer): EntityDestroyResult | null;
export function parseSetPassengers(buffer: Buffer): SetPassengersResult | null;
export function parseEntityUpdateAttributes(buffer: Buffer): EntityUpdateAttributesResult | null;
export function parseEntityStatus(buffer: Buffer): EntityStatusResult | null;
export function parseEntityEffect(buffer: Buffer): EntityEffectResult | null;
export function parseRemoveEntityEffect(buffer: Buffer): RemoveEntityEffectResult | null;
export function parseAttachEntity(buffer: Buffer): AttachEntityResult | null;


