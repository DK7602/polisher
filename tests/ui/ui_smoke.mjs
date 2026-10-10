// UI smoke test: opens the plug-in's web UI in headless Chromium with mock.js standing in for the
// plug-in, visits every module, and checks that each module-header switch controls that module only
// (the Maximizer's is the global bypass, A), that host changes show on the button, and that nothing
// throws.
//   Needs plugin/ui/juce/*.js (JUCE's web-view bridge) and the UI served at UI_URL.
//   node tests/ui/ui_smoke.mjs        (default UI_URL http://127.0.0.1:8765/index.html)
import { chromium } from 'playwright';

const url = process.env.UI_URL || 'http://127.0.0.1:8765/index.html';
const failures = [];
const check = (ok, what) => { if (!ok) failures.push(what); console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); };

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1600, height: 900 } });
const errors = [];
page.on('pageerror', (e) => errors.push(String(e)));
page.on('console', (m) => { if (m.type() === 'error' && !/favicon|404/.test(m.text())) errors.push(m.text()); });
await page.goto(url);
await page.waitForTimeout(800);

const state = () => page.evaluate(() => ({
  module: document.getElementById('module-name').textContent,
  button: document.getElementById('btn-bypass').textContent,
  globalA: document.getElementById('btn-a').classList.contains('on'),
  dimmed: document.getElementById('knob-row').classList.contains('dimmed'),
}));
const pick = (name) => page.evaluate((n) => [...document.querySelectorAll('.row-btn')]
  .find((x) => x.querySelector('.row-name').textContent === n).click(), name);
const names = await page.evaluate(() => [...document.querySelectorAll('.row-btn .row-name')].map((x) => x.textContent));
check(names.length === 11, `signal chain lists 11 modules (${names.length})`);

for (const name of names) {
  await pick(name);
  await page.waitForTimeout(120);
  const before = await state();
  check(before.module === name.toUpperCase(), `${name}: opens its own screen`);
  await page.click('#btn-bypass');
  await page.waitForTimeout(120);
  const after = await state();
  if (name === 'Maximizer') {
    check(after.globalA && after.button === 'BYPASSED', `${name}: header button is the global bypass (A)`);
  } else {
    check(!after.globalA, `${name}: header switch leaves the rest of the chain playing (B stays on)`);
    check(after.button.endsWith(' OFF') && after.dimmed, `${name}: header switch turns the module off ("${after.button}")`);
  }
  await page.click('#btn-bypass');
  await page.waitForTimeout(120);
  const back = await state();
  check(back.button === before.button && back.globalA === before.globalA, `${name}: second click restores it ("${back.button}")`);
}

// A host change (automation, undo) to a module switch shows on the button without a click.
await pick('Unlimiter');
await page.evaluate(() => window.__JUCE__.backend.emitByBackend('__juce__toggleulOn', JSON.stringify({ eventType: 'valueChanged', value: false })));
await page.waitForTimeout(150);
check((await state()).button === 'UNLIMITER OFF', 'host turning the Unlimiter off updates its button');
await page.evaluate(() => window.__JUCE__.backend.emitByBackend('__juce__toggleulOn', JSON.stringify({ eventType: 'valueChanged', value: true })));

// The A / B buttons drive the global bypass.
await page.click('#btn-a'); await page.waitForTimeout(100);
check((await state()).globalA, 'A selects the original mix');
await page.click('#btn-b'); await page.waitForTimeout(100);
check(!(await state()).globalA, 'B selects the master');

check(errors.length === 0, `no script errors (${errors.join(' | ')})`);
await browser.close();
if (failures.length) { console.log(`\n${failures.length} UI check(s) failed`); process.exit(1); }
console.log('\nAll UI checks passed');
