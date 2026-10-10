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
// The parameter each module's header button must switch (read from JUCE's own state, so a wrong id
// in the UI's table fails here even if the button's label looks right).
const SWITCH = { 'Unlimiter': 'ulOn', 'Tonal EQ': 'eqOn', 'Stem EQ': 'stemOn', 'Tone Steady': 'tsOn', 'Resonance Tamer': 'rtOn',
                 'Bass Control': 'bassOn', 'Dynamics': 'dynOn', 'Stereo Imager': 'imgOn', 'Saturation': 'satOn',
                 'Soft Clipper': 'clipOn', 'Maximizer': 'bypass' };
const toggles = () => page.evaluate(async (ids) => {
  const juce = await import('./juce/index.js');
  return Object.fromEntries(ids.map((id) => [id, !!juce.getToggleState(id).getValue()]));
}, Object.values(SWITCH));
const names = await page.evaluate(() => [...document.querySelectorAll('.row-btn .row-name')].map((x) => x.textContent));
check(names.length === 11, `signal chain lists 11 modules (${names.length})`);

for (const name of names) {
  await pick(name);
  await page.waitForTimeout(120);
  const before = await state();
  const t0 = await toggles();
  check(before.module === name.toUpperCase(), `${name}: opens its own screen`);
  await page.click('#btn-bypass');
  await page.waitForTimeout(120);
  const after = await state();
  const t1 = await toggles();
  const flipped = Object.keys(t0).filter((id) => t0[id] !== t1[id]);
  check(flipped.length === 1 && flipped[0] === SWITCH[name], `${name}: header button switches ${SWITCH[name]} and nothing else (${flipped.join(', ')})`);
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

// Stem EQ's Keep Loudness switch flips stemKeep only (on by default) and says what it is doing.
await pick('Stem EQ');
await page.waitForTimeout(120);
const keepState = () => page.evaluate(async () => {
  const juce = await import('./juce/index.js');
  const btn = [...document.querySelectorAll('#knob-row .cell')].find((c) => c.querySelector('.cell-label').textContent === 'Keep Loudness');
  return { keep: !!juce.getToggleState('stemKeep').getValue(), on: !!juce.getToggleState('stemOn').getValue(),
           found: !!btn, label: btn ? btn.querySelector('button').textContent : '', text: document.getElementById('stemkeep-val')?.textContent || '' };
});
const k0 = await keepState();
check(k0.found && k0.keep && k0.label === 'ON', `Stem EQ shows Keep Loudness, on by default ("${k0.label}")`);
await page.evaluate(() => [...document.querySelectorAll('#knob-row .cell')].find((c) => c.querySelector('.cell-label').textContent === 'Keep Loudness').querySelector('button').click());
await page.waitForTimeout(150);
const k1 = await keepState();
check(!k1.keep && k1.on && k1.label === 'OFF' && k1.text === 'level moves with you', `Keep Loudness switch flips stemKeep only ("${k1.text}")`);
await page.evaluate(() => [...document.querySelectorAll('#knob-row .cell')].find((c) => c.querySelector('.cell-label').textContent === 'Keep Loudness').querySelector('button').click());
await page.waitForTimeout(150);
check((await keepState()).keep, 'second click turns Keep Loudness back on');

// Saturation's Focus: Full by default; BASS ONLY flips satFocus only, and Learn explains it with its TRY THIS.
await pick('Saturation');
await page.waitForTimeout(120);
const satFocusState = () => page.evaluate(async () => {
  const juce = await import('./juce/index.js');
  const cell = [...document.querySelectorAll('#knob-row .cell')].find((c) => c.querySelector('.cell-label').textContent === 'Focus');
  const pressed = cell ? [...cell.querySelectorAll('button')].find((b) => b.getAttribute('aria-pressed') === 'true') : null;
  return { found: !!cell, buttons: cell ? [...cell.querySelectorAll('button')].map((b) => b.textContent) : [],
           focus: juce.getComboBoxState('satFocus').getChoiceIndex(), mode: juce.getComboBoxState('satMode').getChoiceIndex(),
           pressed: pressed ? pressed.textContent : '', note: cell ? cell.querySelector('.os-note').textContent : '',
           learn: document.getElementById('learn-body').textContent };
});
const clickFocus = (label) => page.evaluate((l) => [...document.querySelectorAll('#knob-row .cell')]
  .find((c) => c.querySelector('.cell-label').textContent === 'Focus').querySelectorAll('button').forEach((b) => { if (b.textContent === l) b.click(); }), label);
const f0 = await satFocusState();
check(f0.found && f0.buttons.join('/') === 'FULL/BASS ONLY', `Saturation shows a Focus choice (${f0.buttons.join(' / ')})`);
check(f0.focus === 0 && f0.pressed === 'FULL' && f0.note === 'whole mix', `Focus starts on Full ("${f0.pressed}", "${f0.note}")`);
await clickFocus('BASS ONLY');
await page.waitForTimeout(150);
const f1 = await satFocusState();
check(f1.focus === 1 && f1.mode === f0.mode && f1.pressed === 'BASS ONLY', `BASS ONLY sets satFocus only ("${f1.pressed}", mode ${f1.mode})`);
check(/Focus\s*Bass only/.test(f1.learn) && /phone/.test(f1.learn), 'Learn explains Bass only');
check(/Bass only, Tube, Drive \+9/.test(f1.learn) && /MATCH/.test(f1.learn), 'Learn gives the Bass only TRY THIS (phone speakers, MATCH)');
await clickFocus('FULL');
await page.waitForTimeout(150);
const f2 = await satFocusState();
check(f2.focus === 0 && f2.pressed === 'FULL', 'FULL puts Focus back');

// The A / B buttons drive the global bypass.
await page.click('#btn-a'); await page.waitForTimeout(100);
check((await state()).globalA, 'A selects the original mix');
await page.click('#btn-b'); await page.waitForTimeout(100);
check(!(await state()).globalA, 'B selects the master');

// Match ref: mock.js starts with a reference loaded. The pill shows its name, the dashed curve and its
// legend are on the spectrum, the target pills step back; × clears it; clicking the pill loads another.
const ref = () => page.evaluate(() => ({
  pill: document.getElementById('btn-ref').textContent,
  red: document.getElementById('btn-ref').classList.contains('pill-red'),
  clearShown: !document.getElementById('btn-ref-clear').hidden,
  curve: (document.getElementById('sp-ref').getAttribute('d') || '').length,
  legend: !document.getElementById('legend-ref').hidden,
  litTargets: [...document.querySelectorAll('#row-target button[data-i]')].filter((b) => b.classList.contains('pill-red')).length,
  learn: document.getElementById('learn-body').textContent,
}));
let r = await ref();
check(r.pill === 'Mock Reference (Trap)' && r.red, `reference pill shows the loaded song ("${r.pill}")`);
check(r.clearShown, 'reference pill has its clear button');
check(r.curve > 100 && r.legend, `reference curve is drawn on the spectrum with its legend (${r.curve} chars)`);
check(r.litTargets === 0, 'no target pill is lit while a reference sets the loudness');
// The curve sits within the spectrum's range (its points are inside the plot, not pinned to an edge).
const ys = await page.evaluate(() => (document.getElementById('sp-ref').getAttribute('d') || '').split(/[ML]/).filter(Boolean).map((p) => Number(p.split(',')[1])));
check(ys.length === 96 && ys.filter((y) => y > 0.5 && y < 199.5).length > 60, `reference curve lies inside the plot (${ys.filter((y) => y > 0.5 && y < 199.5).length}/96 points)`);

// Auto-Master with the reference: the report says so and shows the comparison table.
await page.click('#btn-automaster');
await page.waitForTimeout(6500);
r = await ref();
check(/Matched to your reference/.test(r.learn) && /REFERENCE vs YOUR MIX vs MASTER/.test(r.learn), 'report names the reference and compares it');
check(await page.evaluate(() => document.querySelectorAll('#learn-body table.l-cmp').length === 2), 'comparison tables (levels and tone) are shown');

await page.click('#btn-ref-clear');
await page.waitForTimeout(300);
r = await ref();
check(r.pill === 'Match ref' && !r.red && !r.clearShown, `clearing the reference resets the pill ("${r.pill}")`);
check(r.curve === 0 && !r.legend, 'clearing the reference removes its curve and legend');
check(r.litTargets === 1, 'the chosen target pill is lit again');

await page.click('#btn-ref');
await page.waitForTimeout(150);
r = await ref();
check(r.pill.startsWith('Loading'), `pill says it's loading ("${r.pill}")`);
await page.waitForTimeout(1000);
r = await ref();
check(r.pill === 'Second Reference' && r.curve > 100, `loading a reference shows the new song ("${r.pill}")`);
check(/MATCH A REFERENCE/.test(r.learn) && /HOW TO USE IT/.test(r.learn), 'Learn explains the reference and how to use it');

// Auto-Master (mock: Trap) put Saturation on Tube, Bass only: the Focus button, the chain readout and
// Learn's Auto-Master section show it, and undo puts Full back.
await pick('Saturation');
await page.waitForTimeout(150);
const f3 = await satFocusState();
const readSat = await page.evaluate(() => document.getElementById('read-sat').textContent);
check(f3.focus === 1 && f3.pressed === 'BASS ONLY', `after Auto-Master, Focus shows Bass only ("${f3.pressed}")`);
check(/bass/.test(readSat), `the chain row says it works on the bass ("${readSat}")`);
check(/Harmonics go on the 808/.test(f3.learn), 'Learn shows Auto-Master\'s reason for Bass only');
await page.click('#btn-undo');
await page.waitForTimeout(300);
check((await satFocusState()).focus === 0, 'undo puts Focus back to Full');

check(errors.length === 0, `no script errors (${errors.join(' | ')})`);
await browser.close();
if (failures.length) { console.log(`\n${failures.length} UI check(s) failed`); process.exit(1); }
console.log('\nAll UI checks passed');
