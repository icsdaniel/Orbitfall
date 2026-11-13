#!/usr/bin/env node
// Smoke test for a web export: serves the folder, loads it in headless Chromium
// (Playwright, software WebGL) and fails when the engine does not start or
// logs an error. Saves a screenshot for a human to check.
//
//   node tools/web_smoke.js <export-dir> [screenshot.png] [--wait-ms 12000]
//
// Needs `playwright` resolvable by node (npm i playwright && npx playwright install chromium).
const http = require('http');
const fs = require('fs');
const path = require('path');

const args = process.argv.slice(2);
const waitIdx = args.indexOf('--wait-ms');
const waitMs = waitIdx >= 0 ? parseInt(args.splice(waitIdx, 2)[1], 10) : 12000;
const root = path.resolve(args[0] || 'build/web');
const shot = args[1] || path.join(root, '..', 'web_smoke.png');

const TYPES = {
  '.html': 'text/html', '.js': 'application/javascript', '.wasm': 'application/wasm',
  '.pck': 'application/octet-stream', '.png': 'image/png', '.json': 'application/json',
};

function serve() {
  const server = http.createServer((req, res) => {
    let rel = decodeURIComponent(req.url.split('?')[0]);
    if (rel.endsWith('/')) rel += 'index.html';
    const file = path.join(root, path.normalize(rel));
    if (!file.startsWith(root) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) {
      res.writeHead(404); res.end('not found'); return;
    }
    res.writeHead(200, { 'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream' });
    fs.createReadStream(file).pipe(res);
  });
  return new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve(server)));
}

(async () => {
  const { chromium } = require('playwright');
  const server = await serve();
  const url = `http://127.0.0.1:${server.address().port}/index.html`;
  const browser = await chromium.launch({
    args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'],
  });
  const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
  const logs = [];
  const problems = [];
  page.on('console', (m) => {
    const line = `[${m.type()}] ${m.text()}`;
    logs.push(line);
    // WebGL driver chatter from software rendering is not a game failure.
    if (m.type() === 'error' && !/GL Driver Message|WebGL: INVALID_ENUM|GPU stall/i.test(m.text())) problems.push(line);
  });
  page.on('pageerror', (e) => problems.push(`[pageerror] ${e.message}`));
  page.on('requestfailed', (r) => problems.push(`[requestfailed] ${r.url()}`));
  page.on('response', (r) => { if (r.status() >= 400 && !r.url().endsWith('favicon.ico')) problems.push(`[http ${r.status()}] ${r.url()}`); });

  await page.goto(url, { waitUntil: 'load' });
  await page.waitForTimeout(waitMs);
  const state = await page.evaluate(() => {
    const status = document.getElementById('status');
    const canvas = document.getElementById('canvas');
    const tag = document.getElementById('orbitfall-version');
    return {
      statusVisible: !!status && getComputedStyle(status).visibility !== 'hidden' && getComputedStyle(status).display !== 'none',
      statusText: status ? status.innerText.trim() : '',
      canvas: canvas ? `${canvas.width}x${canvas.height}` : 'missing',
      versionTag: tag ? tag.textContent : null,
    };
  });
  await page.screenshot({ path: shot });
  await browser.close();
  server.close();

  if (state.statusVisible) problems.push(`loading/status overlay still visible: "${state.statusText}"`);
  if (state.canvas === 'missing') problems.push('canvas missing');
  console.log(logs.join('\n'));
  console.log(`canvas: ${state.canvas}`);
  console.log(`version tag: ${state.versionTag}`);
  console.log(`screenshot: ${shot}`);
  if (problems.length) {
    console.error('SMOKE FAILED:\n' + problems.join('\n'));
    process.exit(1);
  }
  console.log('SMOKE OK');
})().catch((e) => { console.error(e); process.exit(1); });
