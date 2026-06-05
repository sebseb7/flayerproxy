import './style.css';

// DOM Elements
const container = document.getElementById('map-container');
const viewport = document.getElementById('map-viewport');
const dimensionSelect = document.getElementById('dimension-select');
const coordsDisplay = document.getElementById('coords-display');
const chunkCoordsDisplay = document.getElementById('chunk-coords-display');
const zoomDisplay = document.getElementById('zoom-display');
const tilesCount = document.getElementById('tiles-count');
const centerBtn = document.getElementById('center-btn');
const connStatus = document.getElementById('conn-status');

// API Base configuration (CORS support for Vite dev server)
const API_BASE = window.location.port === '5173'
  ? `${window.location.protocol}//${window.location.hostname}:3000`
  : '';

// Map State
let offsetX = 0;
let offsetY = 0;
let scale = 1.0;
let isDragging = false;
let startX = 0;
let startY = 0;
let animId = null;
let lastKnownCenterWorldX = 0;
let lastKnownCenterWorldZ = 0;
let followPlayer = true;

const ZOOM_SPEED = 0.15;
const MIN_SCALE = 0.02;
const MAX_SCALE = 16.0;

function setFollowPlayer(value) {
  followPlayer = value;
  if (followPlayer) {
    centerBtn.classList.add('active');
  } else {
    centerBtn.classList.remove('active');
  }
}

// Data State
let allBigchunks = [];
let allTiles = [];
let currentDimension = 'minecraft:overworld';
let currentPlayer = null;
const renderedTiles = new Map(); // Key: "worldX,worldZ" -> img element

let currentLayer = null; // 'bigchunks' or 'tiles'

function getActiveLayer() {
  const hasTiles = allTiles.some(t => t.dimension === currentDimension);
  const hasBigchunks = allBigchunks.some(c => c.dimension === currentDimension);

  if (scale <= 0.25) {
    return hasTiles ? 'tiles' : (hasBigchunks ? 'bigchunks' : 'tiles');
  } else {
    return hasBigchunks ? 'bigchunks' : (hasTiles ? 'tiles' : 'bigchunks');
  }
}

function updateLayerState() {
  const targetLayer = getActiveLayer();
  if (currentLayer !== targetLayer) {
    currentLayer = targetLayer;
    renderDimension(currentDimension);
  }
}
const viewDistanceGrid = document.createElement('div');
viewDistanceGrid.className = 'view-distance-grid';
viewDistanceGrid.style.display = 'none';
viewport.appendChild(viewDistanceGrid);

const playerMarker = document.createElement('div');
playerMarker.className = 'player-marker';
playerMarker.innerHTML = `
  <svg class="player-marker-arrow" viewBox="0 0 24 24" width="24" height="24">
    <defs>
      <linearGradient id="arrow-grad" x1="0%" y1="0%" x2="100%" y2="100%">
        <stop offset="0%" stop-color="#00f2fe" />
        <stop offset="100%" stop-color="#4facfe" />
      </linearGradient>
      <filter id="arrow-glow" x="-20%" y="-20%" width="140%" height="140%">
        <feGaussianBlur stdDeviation="2" result="blur" />
        <feComposite in="SourceGraphic" in2="blur" operator="over" />
      </filter>
    </defs>
    <path d="M12 2 L3 22 L12 17 L21 22 Z" fill="url(#arrow-grad)" filter="url(#arrow-glow)" />
  </svg>
`;
playerMarker.style.display = 'none';
viewport.appendChild(playerMarker);

// Initial state updates
connStatus.className = 'status-badge connecting';
connStatus.querySelector('.text').textContent = 'CONNECTING';

// Apply CSS Transform
function applyTransform() {
  const width = container.clientWidth || window.innerWidth;
  const height = container.clientHeight || window.innerHeight;
  const containerCenterX = width / 2;
  const containerCenterY = height / 2;
  
  // Calculate map coordinates at the center of the screen
  const viewportCenterX = (containerCenterX - offsetX) / scale;
  const viewportCenterY = (containerCenterY - offsetY) / scale;
  
  lastKnownCenterWorldX = viewportCenterX / 2;
  lastKnownCenterWorldZ = viewportCenterY / 2;

  updateLayerState();

  viewport.style.transform = `translate(${offsetX}px, ${offsetY}px) scale(${scale})`;
  viewport.style.setProperty('--map-scale', scale);
  updateCenterReadout();
}

function setViewportCenter() {
  offsetX = container.clientWidth / 2;
  offsetY = container.clientHeight / 2;
  lastKnownCenterWorldX = 0;
  lastKnownCenterWorldZ = 0;
}

function updateCenterReadout() {
  const centerChunkX = Math.floor(lastKnownCenterWorldX / 16);
  const centerChunkZ = Math.floor(lastKnownCenterWorldZ / 16);

  coordsDisplay.textContent = `Center X: ${lastKnownCenterWorldX.toFixed(2)}, Z: ${lastKnownCenterWorldZ.toFixed(2)}`;
  chunkCoordsDisplay.textContent = `Center CX: ${centerChunkX}, CZ: ${centerChunkZ}`;
}

function getPlayerWorldX() {
  return currentPlayer?.x ?? 0;
}

function getPlayerWorldZ() {
  return currentPlayer?.z ?? 0;
}

function getCurrentPlayer() {
  return currentPlayer;
}

function renderPlayerMarker() {
  const player = getCurrentPlayer();
  if (!player || !Number.isFinite(player.x) || !Number.isFinite(player.z)) {
    playerMarker.style.display = 'none';
    viewDistanceGrid.style.display = 'none';
    return;
  }

  const isCurrentDimension = player.dimension === currentDimension;
  playerMarker.style.display = isCurrentDimension ? 'block' : 'none';
  viewDistanceGrid.style.display = isCurrentDimension ? 'block' : 'none';

  // player coordinates are in block space; tiles are positioned at 2px/block
  playerMarker.style.left = `${player.x * 2}px`;
  playerMarker.style.top = `${player.z * 2}px`;
  const yaw = Number(player.yaw);
  if (Number.isFinite(yaw)) {
    playerMarker.style.setProperty('--yaw', `${yaw}deg`);
  }

  if (isCurrentDimension) {
    const playerChunkX = Math.floor(player.x / 16);
    const playerChunkZ = Math.floor(player.z / 16);
    
    // View distance = 8 chunks. Grid is 17x17 chunks (centered at playerChunkX, playerChunkZ)
    // Left/top position of the grid in viewport pixel space (2px/block, so 32px/chunk)
    const gridLeft = (playerChunkX - 8) * 32;
    const gridTop = (playerChunkZ - 8) * 32;
    
    viewDistanceGrid.style.left = `${gridLeft}px`;
    viewDistanceGrid.style.top = `${gridTop}px`;
  }
}

// Update Zoom Display
function updateZoomDisplay() {
  zoomDisplay.textContent = `${Math.round(scale * 100)}%`;
}

// Show Toast message
function showToast(title, message) {
  const toastContainer = document.getElementById('toast-container');
  if (!toastContainer) return;

  const toast = document.createElement('div');
  toast.className = 'toast';
  toast.innerHTML = `
    <div class="toast-title">${title}</div>
    <div class="toast-msg">${message}</div>
  `;
  toastContainer.appendChild(toast);

  setTimeout(() => {
    toast.classList.add('toast-fadeout');
    toast.addEventListener('animationend', () => {
      toast.remove();
    });
  }, 4000);
}

// Smoothly pan viewport to a target coordinate
function animatePanTo(targetX, targetY, duration = 800) {
  if (animId) {
    cancelAnimationFrame(animId);
    animId = null;
  }

  const startOffsetX = offsetX;
  const startOffsetY = offsetY;
  const destOffsetX = container.clientWidth / 2 - targetX * scale;
  const destOffsetY = container.clientHeight / 2 - targetY * scale;

  const startTime = performance.now();

  function step(now) {
    const elapsed = now - startTime;
    const progress = Math.min(elapsed / duration, 1);
    
    // Easing: easeInOutQuad
    const ease = progress < 0.5 
      ? 2 * progress * progress 
      : 1 - Math.pow(-2 * progress + 2, 2) / 2;

    offsetX = startOffsetX + (destOffsetX - startOffsetX) * ease;
    offsetY = startOffsetY + (destOffsetY - startOffsetY) * ease;
    applyTransform();

    if (progress < 1) {
      animId = requestAnimationFrame(step);
    } else {
      animId = null;
    }
  }

  animId = requestAnimationFrame(step);
}

// Center map view on current player location
function centerMap(smooth = false) {
  const player = getCurrentPlayer();
  if (!player || !Number.isFinite(player.x) || !Number.isFinite(player.z)) {
    console.log('[centerMap] no player position yet', { currentPlayer: player });
    setViewportCenter();
    applyTransform();
    return;
  }

  const playerX = player.x * 2;
  const playerY = player.z * 2;
  console.log('[centerMap] centering on player', { playerX, playerY, scale, smooth, dimension: player.dimension });

  if (smooth) {
    animatePanTo(playerX, playerY);
  } else {
    if (animId) {
      cancelAnimationFrame(animId);
      animId = null;
    }
    const width = container.clientWidth || window.innerWidth;
    const height = container.clientHeight || window.innerHeight;
    offsetX = width / 2 - playerX * scale;
    offsetY = height / 2 - playerY * scale;
    applyTransform();
  }

  updateCenterReadout();
}

// Create or update a bigchunk or overview tile image in DOM
function createOrUpdateTile(chunk) {
  if (!chunk || chunk.url === undefined || chunk.url === null) return null;

  const key = `${chunk.worldX},${chunk.worldZ}`;
  let img = renderedTiles.get(key);

  if (!img) {
    img = document.createElement('img');
    img.className = chunk.kind === 'tile' ? 'overview-tile' : 'bigchunk';
    img.style.left = `${chunk.worldX * 2}px`;
    img.style.top = `${chunk.worldZ * 2}px`;
    if (chunk.kind === 'tile') {
      img.style.width = '8192px';
      img.style.height = '8192px';
    } else {
      img.style.width = '512px';
      img.style.height = '512px';
    }
    img.setAttribute('loading', 'lazy');
    viewport.appendChild(img);
    renderedTiles.set(key, img);
  }

  const url = chunk.url.startsWith('http') ? chunk.url : `${API_BASE}${chunk.url}`;
  if (renderedTiles.size < 5) {
    console.log('[tile src]', key, url);
  }
  img.src = url;
  return img;
}

// Render all bigchunks or overview tiles for selected dimension
function renderDimension(dim) {
  viewport.innerHTML = '';
  renderedTiles.clear();

  if (!currentLayer) {
    currentLayer = getActiveLayer();
  }

  const items = currentLayer === 'tiles'
    ? allTiles.filter(c => c.dimension === dim)
    : allBigchunks.filter(c => c.dimension === dim);

  tilesCount.textContent = items.length;

  console.log('[renderDimension]', dim, { layer: currentLayer, count: items.length });

  for (const item of items) {
    createOrUpdateTile(item);
  }

  viewport.appendChild(viewDistanceGrid);
  viewport.appendChild(playerMarker);
  renderPlayerMarker();
}

// Event Listeners for Draggability and Coordinate display
container.addEventListener('pointerdown', (e) => {
  if (e.button !== 0) return;
  if (e.target.closest('.glass') || e.target.closest('#toast-container')) return;

  isDragging = true;
  startX = e.clientX - offsetX;
  startY = e.clientY - offsetY;
  container.setPointerCapture(e.pointerId);

  if (animId) {
    cancelAnimationFrame(animId);
    animId = null;
  }

  setFollowPlayer(false);
});

container.addEventListener('pointermove', (e) => {
  if (isDragging) {
    offsetX = e.clientX - startX;
    offsetY = e.clientY - startY;
    applyTransform();
  }
});

container.addEventListener('pointerup', (e) => {
  isDragging = false;
  try { container.releasePointerCapture(e.pointerId); } catch {}
});

container.addEventListener('pointercancel', () => {
  isDragging = false;
});

// Event Listener for Zoom via Wheel
container.addEventListener('wheel', (e) => {
  e.preventDefault();
  e.stopPropagation();
  isDragging = false;

  setFollowPlayer(false);

  const rect = container.getBoundingClientRect();
  const mouseX = e.clientX - rect.left;
  const mouseY = e.clientY - rect.top;

  const mapX = (mouseX - offsetX) / scale;
  const mapY = (mouseY - offsetY) / scale;

  const factor = e.deltaY > 0 ? 1 / 1.15 : 1.15;
  const newScale = Math.min(Math.max(scale * factor, MIN_SCALE), MAX_SCALE);

  offsetX = mouseX - mapX * newScale;
  offsetY = mouseY - mapY * newScale;
  scale = newScale;

  applyTransform();
  updateZoomDisplay();
}, { passive: false });

// Control event listeners
dimensionSelect.addEventListener('change', (e) => {
  currentDimension = e.target.value;
  renderDimension(currentDimension);
  renderPlayerMarker();
  centerMap(false);
});

centerBtn.addEventListener('click', () => {
  setFollowPlayer(true);
  centerMap(true);
});

// Fetch Bigchunks list
async function fetchBigchunks() {
  try {
    const [bigchunksRes, tilesRes] = await Promise.all([
      fetch(`${API_BASE}/api/bigchunks`),
      fetch(`${API_BASE}/api/tiles`),
    ]);

    if (!bigchunksRes.ok) throw new Error(`Bigchunks request failed: ${bigchunksRes.status}`);
    if (!tilesRes.ok) throw new Error(`Tiles request failed: ${tilesRes.status}`);

    allBigchunks = await bigchunksRes.json();
    allTiles = await tilesRes.json();
    console.log('[fetchBigchunks]', allBigchunks.length, 'bigchunks', allTiles.length, 'tiles');
    renderDimension(currentDimension);
    centerMap(false);
  } catch (error) {
    console.error('Failed to fetch map data:', error);
    showToast('Fetch Error', 'Failed to retrieve existing map tiles.');
  }
}

// Setup EventSource for SSE
function setupSSE() {
  const eventSource = new EventSource(`${API_BASE}/api/sse`);

  const handlePlayerEvent = (raw) => {
    try {
      console.log('[SSE player raw]', raw);
      const data = JSON.parse(raw);
      const x = Number(data.x);
      const y = Number(data.y);
      const z = Number(data.z);
      const yaw = Number(data.yaw);
      const pitch = Number(data.pitch);

      // Look-only packets do not carry position. Keep the last known position.
      if (!Number.isFinite(x) || !Number.isFinite(z)) {
        if (Number.isFinite(yaw) || Number.isFinite(pitch)) {
          if (!currentPlayer) return;
          currentPlayer = {
            ...currentPlayer,
            ...data,
            yaw: Number.isFinite(yaw) ? yaw : currentPlayer.yaw,
            pitch: Number.isFinite(pitch) ? pitch : currentPlayer.pitch,
          };
          console.log('[SSE player look-only]', currentPlayer);
          renderPlayerMarker();
        } else {
          console.error('[SSE player] invalid coordinates', data);
        }
        return;
      }

      currentPlayer = {
        ...data,
        x,
        y: Number.isFinite(y) ? y : undefined,
        z,
        yaw: Number.isFinite(yaw) ? yaw : undefined,
        pitch: Number.isFinite(pitch) ? pitch : undefined,
      };

      console.log('[SSE player]', currentPlayer);
      renderPlayerMarker();
      if (currentPlayer.dimension === currentDimension) {
        if (followPlayer) {
          centerMap(false);
        }
      }
    } catch (err) {
      console.error('Error parsing player SSE data:', err, raw);
    }
  };

  eventSource.addEventListener('player', (e) => handlePlayerEvent(e.data));

  eventSource.onopen = () => {
    connStatus.className = 'status-badge connected';
    connStatus.querySelector('.text').textContent = 'CONNECTED';
    showToast('Connected', 'Established live map stream.');
  };

  eventSource.onerror = () => {
    connStatus.className = 'status-badge disconnected';
    connStatus.querySelector('.text').textContent = 'DISCONNECTED';
  };

  eventSource.onmessage = (e) => {
    try {
      const data = JSON.parse(e.data);
      console.log('[SSE message]', data);
      if (!data) return;
      if (data.kind === 'player') {
        handlePlayerEvent(e.data);
        return;
      }

      if (data.kind === 'tile') {
        const idx = allTiles.findIndex(
          c => c.dimension === data.dimension && c.worldX === data.worldX && c.worldZ === data.worldZ
        );
        if (idx !== -1) {
          allTiles[idx] = data;
        } else {
          allTiles.push(data);
        }

        if (currentLayer === 'tiles') {
          const dimTiles = allTiles.filter(c => c.dimension === currentDimension);
          tilesCount.textContent = dimTiles.length;
        }

        if (data.dimension === currentDimension && currentLayer === 'tiles') {
          const img = createOrUpdateTile(data);
          if (img) {
            img.classList.remove('bigchunk-new');
            void img.offsetWidth; // Force CSS reflow
            img.classList.add('bigchunk-new');
          }
          showToast('Overview Updated', `New overview tile at block coordinates X: ${data.worldX}, Z: ${data.worldZ}`);
        } else if (data.dimension !== currentDimension) {
          showToast('Overview Updated', `[${data.dimension}] New overview tile at X: ${data.worldX}, Z: ${data.worldZ}`);
        }
        return;
      }

      // Add or update in list
      const idx = allBigchunks.findIndex(
        c => c.dimension === data.dimension && c.worldX === data.worldX && c.worldZ === data.worldZ
      );
      if (idx !== -1) {
        allBigchunks[idx] = data;
      } else {
        allBigchunks.push(data);
      }

      if (currentLayer === 'bigchunks') {
        const dimChunks = allBigchunks.filter(c => c.dimension === currentDimension);
        tilesCount.textContent = dimChunks.length;
      }

      if (data.dimension === currentDimension && currentLayer === 'bigchunks') {
        const img = createOrUpdateTile(data);
        if (img) {
          // Apply new pulse animation
          img.classList.remove('bigchunk-new');
          void img.offsetWidth; // Force CSS reflow
          img.classList.add('bigchunk-new');
        }

        showToast('Map Updated', `New tile at block coordinates X: ${data.worldX}, Z: ${data.worldZ}`);
      } else if (data.dimension !== currentDimension) {
        // Just show toast if it was in another dimension
        showToast('Map Updated', `[${data.dimension}] New tile at X: ${data.worldX}, Z: ${data.worldZ}`);
      }
    } catch (err) {
      console.error('Error parsing SSE data:', err);
    }
  };
}

// Initialize Application
async function init() {
  console.log('[init] starting map viewer');
  // Set initial viewport translation to center (0,0) before fetch
  setViewportCenter();
  applyTransform();
  updateZoomDisplay();

  setFollowPlayer(true);

  await fetchBigchunks();
  setupSSE();
}

// Window resizing
window.addEventListener('resize', () => {
  // Simple transform re-applying to prevent clipping
  applyTransform();
});

// Run
init();
