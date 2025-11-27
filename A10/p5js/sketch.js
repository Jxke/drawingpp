let rippleClock = 0;
const ringSpacing = 30;
const blockSteps = 16;
const snapSize = 8;
const ringMargin = 24; // keep rings inside canvas bounds

function setup() {
  const size = determineSize();
  const canvas = createCanvas(size, size);
  const holder = document.getElementById("sketch-holder");
  if (holder) {
    canvas.parent(holder);
  }
  pixelDensity(1);
  noSmooth();
  strokeCap(SQUARE);
  attachExportButton();
}

function windowResized() {
  const size = determineSize();
  resizeCanvas(size, size);
}

function determineSize() {
  return Math.min(window.innerWidth * 0.9, window.innerHeight * 0.9, 760);
}

function draw() {
  renderRipple(this, rippleClock);
  rippleClock += 0.02;
}

function renderRipple(pg, t) {
  pg.push();
  pg.background(255);
  pg.translate(pg.width / 2, pg.height / 2);

  const maxR = maxRadius(pg.width, pg.height);

  drawRippleBands(pg, maxR, t);
  drawCenter(pg, t);

  pg.pop();
}

function drawRippleBands(pg, maxR, t) {
  pg.noFill();
  const offset = (t * 50) % ringSpacing;

  for (let r = offset; r < maxR; r += ringSpacing) {
    const wobble = Math.sin(t * 1.5 - r * 0.12) * 5;
    const radius = r + wobble;
    const strokeW = map(r, 0, maxR, 8, 2);
    const alpha = map(r, 0, maxR, 255, 50);

    drawBlockyRing(pg, radius, strokeW, alpha, t);
  }
}

function drawBlockyRing(pg, radius, strokeW, alpha, t) {
  const points = getRingPoints(radius, t);
  pg.stroke(0, alpha);
  pg.strokeWeight(strokeW);
  pg.beginShape();
  points.forEach((pt) => pg.vertex(pt.x, pt.y));
  pg.endShape(CLOSE);
}

function drawCenter(pg, t) {
  const pulse = (Math.sin(t * 3) + 1) * 0.5;

  drawBlockyRing(pg, 60 + pulse * 6, 4, 255, t * 0.8);
  drawBlockyRing(pg, 110 + Math.sin(t * 2) * 6, 3, 200, t * 0.6);

  pg.noStroke();
  pg.fill(0);
  const core = 12 + pulse * 4;
  pg.rectMode(CENTER);
  pg.rect(0, 0, core, core);
  pg.rectMode(CORNER);

  pg.fill(255);
  pg.rect(-6, -6 - core * 0.2, 4, 4);
  pg.rect(2, -12, 4, 4);
}

function keyPressed() {
  if (key === "s" || key === "S") {
    saveRipplesSVG();
  }
}

function snap(value) {
  return Math.round(value / snapSize) * snapSize;
}

function saveRipplesSVG() {
  const size = determineSize();
  const center = size / 2;
  const maxR = maxRadius(size, size);
  const t = rippleClock;
  const svgLines = [];

  svgLines.push(`<svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 ${size} ${size}" shape-rendering="crispEdges">`);
  svgLines.push(`<rect width="${size}" height="${size}" fill="#ffffff"/>`);

  const offset = (t * 50) % ringSpacing;
  for (let r = offset; r < maxR; r += ringSpacing) {
    const wobble = Math.sin(t * 1.5 - r * 0.12) * 5;
    const radius = r + wobble;
    const strokeW = map(r, 0, maxR, 8, 2);
    const alpha = map(r, 0, maxR, 255, 50);
    const opacity = (alpha / 255).toFixed(2);
    const polygon = polygonString(getRingPoints(radius, t), center);
    svgLines.push(`<polygon points="${polygon}" fill="none" stroke="#000" stroke-width="${strokeW}" stroke-opacity="${opacity}" stroke-linejoin="round" />`);
  }

  const pulse = (Math.sin(t * 3) + 1) * 0.5;
  const innerPoly = polygonString(getRingPoints(60 + pulse * 6, t * 0.8), center);
  svgLines.push(`<polygon points="${innerPoly}" fill="none" stroke="#000" stroke-width="4" />`);
  const outerPoly = polygonString(getRingPoints(110 + Math.sin(t * 2) * 6, t * 0.6), center);
  svgLines.push(`<polygon points="${outerPoly}" fill="none" stroke="#000" stroke-width="3" stroke-opacity="0.8" />`);

  const core = 12 + pulse * 4;
  svgLines.push(`<rect x="${(center - core / 2).toFixed(2)}" y="${(center - core / 2).toFixed(2)}" width="${core.toFixed(2)}" height="${core.toFixed(2)}" fill="#000" />`);

  const hl1 = { x: center - 6, y: center - 6 - core * 0.2, size: 4 };
  const hl2 = { x: center + 2, y: center - 12, size: 4 };
  svgLines.push(`<rect x="${hl1.x.toFixed(2)}" y="${hl1.y.toFixed(2)}" width="${hl1.size}" height="${hl1.size}" fill="#fff" />`);
  svgLines.push(`<rect x="${hl2.x.toFixed(2)}" y="${hl2.y.toFixed(2)}" width="${hl2.size}" height="${hl2.size}" fill="#fff" />`);

  svgLines.push(`</svg>`);

  saveStrings(svgLines, "ripple-layer", "svg");
}

function maxRadius(w, h) {
  return Math.min(w, h) / 2 - ringMargin;
}

function polygonString(points, center) {
  return points
    .map((pt) => `${(pt.x + center).toFixed(2)},${(pt.y + center).toFixed(2)}`)
    .join(" ");
}

function getRingPoints(radius, t) {
  const pts = [];
  const step = TWO_PI / blockSteps;
  for (let angle = 0; angle <= TWO_PI; angle += step) {
    const warble = Math.sin(angle * 3 + t * 1.1) * 3;
    const x = Math.cos(angle) * (radius + warble);
    const y = Math.sin(angle) * (radius + warble);
    pts.push({ x: snap(x), y: snap(y) });
  }
  return pts;
}

function attachExportButton() {
  const button = document.getElementById("export-btn");
  if (button && !button.dataset.bound) {
    button.addEventListener("click", saveRipplesSVG);
    button.dataset.bound = "true";
  }
}

if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", attachExportButton);
} else {
  attachExportButton();
}
