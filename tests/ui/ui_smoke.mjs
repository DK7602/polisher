// UI smoke test: opens the plug-in's web UI in headless Chromium with mock.js standing in for the
// plug-in, visits every module, and checks that each module-header switch controls that module only
// (the Maximizer's is the global bypass, A), that host changes show on the button, the Match ref pill,
// the listening aids (MONO / PHONE, each module's Listen button, the Dynamics S buttons, the banner), and
// that nothing throws.
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
  legend: getComputedStyle(document.getElementById('legend-ref')).display !== 'none',   // what you see, not just the attribute
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

// Listening aids (monitoring only). mock.js records what the UI asked the "plug-in" for in __mockMonitor.
const mon = () => page.evaluate(() => ({
  ...window.__mockMonitor,
  banner: !document.getElementById('monitor-banner').hidden,
  text: document.getElementById('monitor-text').textContent,
  level: document.getElementById('monitor-level').textContent,
  listenShown: !document.getElementById('btn-listen').hidden,
  listenLabel: document.getElementById('btn-listen-text').textContent,
  listenOn: document.getElementById('btn-listen').getAttribute('aria-pressed') === 'true',
  monoOn: document.getElementById('btn-mono').getAttribute('aria-pressed') === 'true',
  phoneOn: document.getElementById('btn-phone').getAttribute('aria-pressed') === 'true',
  learn: document.getElementById('learn-body').textContent,
}));
let m = await mon();
check(!m.banner && m.listen === 0 && !m.mono && !m.phone, 'no listening aid is on at start, and no banner');

// MONO and PHONE next to MATCH: each flips only its own check, and the banner says it isn't the master.
await pick('Maximizer'); await page.waitForTimeout(120);
await page.click('#btn-mono'); await page.waitForTimeout(150);
m = await mon();
check(m.mono && !m.phone && m.listen === 0 && m.monoOn, 'MONO turns the mono check on (and nothing else)');
check(m.banner && /MONO CHECK/.test(m.text) && /not the master/.test(m.text), `banner shows the mono check ("${m.text}")`);
await page.click('#btn-phone'); await page.waitForTimeout(150);
m = await mon();
check(m.mono && m.phone && m.phoneOn && /PHONE/.test(m.text), `PHONE turns the phone check on ("${m.text}")`);
check(/PHONE CHECK/.test(m.learn) && /808/.test(m.learn), 'Learn explains what to listen for on PHONE');
await page.click('#monitor-stop'); await page.waitForTimeout(150);
m = await mon();
check(!m.mono && !m.phone && m.listen === 0 && !m.banner, 'BACK TO MASTER turns every check off and hides the banner');

// Listen buttons in the module headers: Saturation (added), Soft Clipper, Resonance Tamer, Maximizer (removed).
const LISTEN = { 'Saturation': [1, 'LISTEN', /what Saturation adds/], 'Soft Clipper': [2, 'LISTEN', /Soft Clipper shaves off/],
                 'Resonance Tamer': [3, 'LISTEN', /Resonance Tamer cuts/], 'Maximizer': [4, 'LISTEN', /Maximizer takes off/] };
for (const [name, [mode, label, what]] of Object.entries(LISTEN)) {
  await pick(name); await page.waitForTimeout(120);
  m = await mon();
  check(m.listenShown && m.listenLabel === label && !m.listenOn, `${name}: shows its ${label} button`);
  check(/HEAR WHAT IT DOES/.test(m.learn), `${name}: Learn points to the Listen button`);
  await page.click('#btn-listen'); await page.waitForTimeout(200);
  m = await mon();
  check(m.listen === mode && m.listenOn, `${name}: Listen asks for mode ${mode} (${m.listen})`);
  check(m.banner && what.test(m.text) && /not the master/.test(m.text), `${name}: banner says what you hear ("${m.text}")`);
  check(/so you can hear it/.test(m.level), `${name}: banner says how much it is turned up ("${m.level}")`);
  check(/WHAT YOU'RE HEARING/.test(m.learn) && /Do I need to fix it\?/.test(m.learn), `${name}: Learn says what to listen for, with a plan`);
}
// One at a time: the Maximizer's Listen replaced the Tamer's; moving to another module keeps the banner.
await pick('Tonal EQ'); await page.waitForTimeout(150);
m = await mon();
check(!m.listenShown, 'a module without a listen mode has no Listen button');
check(m.banner && m.listen === 4, 'the banner stays while listening, whatever module is open');
await pick('Maximizer'); await page.waitForTimeout(120);
await page.click('#btn-listen'); await page.waitForTimeout(150);
m = await mon();
check(m.listen === 0 && !m.banner, 'pressing Listen again goes back to the master');

// Dynamics: an S (solo) button per band, one at a time; LISTEN plays what the picked band's compression removes.
await pick('Dynamics'); await page.waitForTimeout(150);
const solos = () => page.evaluate(() => [...document.querySelectorAll('.band-pick .solo')].map((b) => b.getAttribute('aria-pressed') === 'true'));
check((await solos()).length === 4, 'Dynamics shows an S button for each of its four bands');
await page.evaluate(() => document.querySelectorAll('.band-pick .solo')[0].click()); await page.waitForTimeout(150);
m = await mon();
check(m.listen === 5 && (await solos()).join() === 'true,false,false,false' && /SOLO: only the LOW band/.test(m.text), `S on LOW solos it ("${m.text}")`);
await page.evaluate(() => document.querySelectorAll('.band-pick .solo')[2].click()); await page.waitForTimeout(150);
m = await mon();
check(m.listen === 7 && (await solos()).join() === 'false,false,true,false', 'S on HI-MID replaces the LOW solo (one at a time)');
await page.evaluate(() => document.querySelectorAll('.band-pick .solo')[2].click()); await page.waitForTimeout(150);
m = await mon();
check(m.listen === 0 && !m.banner, 'S again un-solos');
await page.click('#btn-listen'); await page.waitForTimeout(150);
m = await mon();
check(m.listen === 9 + 2 && /HIGH MID band/.test(m.text), `Dynamics LISTEN plays what the picked band loses (mode ${m.listen}, "${m.text}")`);
await page.evaluate(() => document.querySelectorAll('.band-pick .band-btn')[0].click()); await page.waitForTimeout(150);
m = await mon();
check(m.listen === 9, 'picking another band moves the removed-listen to it');
await page.click('#monitor-stop'); await page.waitForTimeout(150);

// The plug-in switching them off itself (a new window) shows: the frame's state wins.
await page.click('#btn-mono'); await page.waitForTimeout(700);
await page.evaluate(() => { window.__mockMonitor.mono = false; });
await page.waitForTimeout(300);
m = await mon();
check(!m.monoOn && !m.banner, 'the UI follows the plug-in when it switches a check off');

// Genre target line (dotted): the plug-in computes it (mock: getGenreCurve) for the chosen genre. A
// reference is loaded here, so it starts hidden (Auto-Master aims for the reference); its legend entry
// shows it, it follows the genre selector, and without a reference it shows by default.
const genre = () => page.evaluate(() => {
  const d = document.getElementById('sp-genre').getAttribute('d') || '';
  const ys = d.split(/[ML]/).filter(Boolean).map((p) => Number(p.split(',')[1]));
  const btn = document.getElementById('legend-genre');
  return { d, points: ys.length, inside: ys.filter((y) => y > 0.5 && y < 199.5).length, legend: btn.textContent.trim(),
           shown: getComputedStyle(btn).display !== 'none', pressed: btn.getAttribute('aria-pressed'),
           refLoaded: !document.getElementById('btn-ref-clear').hidden };
});
let gc = await genre();
check(gc.refLoaded && gc.shown && gc.legend === 'Trap target', `genre line has a legend entry ("${gc.legend}")`);
check(gc.pressed === 'false' && gc.d === '', 'with a reference loaded the genre line starts hidden (the reference is what Auto-Master aims for)');
await page.click('#legend-genre');
await page.waitForTimeout(200);
gc = await genre();
check(gc.pressed === 'true' && gc.points === 96 && gc.inside > 60, `clicking its legend entry draws the genre line (${gc.points} points, ${gc.inside} inside the plot)`);
const trapD = gc.d;
await page.click('#genre-next');
await page.waitForTimeout(300);
gc = await genre();
check(gc.legend === 'R&B target' && gc.points === 96 && gc.d !== trapD, `the line and its legend follow the genre ("${gc.legend}")`);
await page.click('#genre-prev');
await page.waitForTimeout(300);
check((await genre()).legend === 'Trap target', 'and back to Trap');
await pick('Tonal EQ');
await page.waitForTimeout(200);
const eqLearn = await page.evaluate(() => document.getElementById('learn-body').textContent);
check(/Dotted line · Trap target/.test(eqLearn) && /above the dotted line/.test(eqLearn), 'EQ Learn explains how to read the dotted line');
// Everything under the spectrum fits on one line, even with the EQ curve's entry and the longest genre name.
for (let i = 0; i < 5; i++) await page.click('#genre-next');   // -> Acoustic
await page.waitForTimeout(300);
const fit = await page.evaluate(() => {
  const lg = document.querySelector('.legend'), t = document.querySelector('.view-toggle').getBoundingClientRect();
  const screen = document.getElementById('spectrum-screen').getBoundingClientRect();
  return { over: lg.scrollWidth - lg.clientWidth, inside: t.right <= screen.right, name: document.getElementById('legend-genre-name').textContent,
           oneLine: lg.getBoundingClientRect().height < 26 };
});
check(fit.over <= 0 && fit.inside && fit.oneLine, `legend fits on one line with "${fit.name}" and the EQ entry (overflow ${fit.over} px)`);
await page.click('#genre-next');   // -> Trap again
await page.click('#btn-ref-clear');
await page.waitForTimeout(400);
gc = await genre();
check(!gc.refLoaded && gc.pressed === 'true' && gc.points === 96, 'without a reference the genre line shows by default');

// Loudness history: the same screen switches to the master's short-term loudness over time, with the
// Maximizer's gain reduction and the target. The mock has been "playing" all along, so it has data.
const hv = () => page.evaluate(() => {
  const pts = (id) => (document.getElementById(id).getAttribute('d') || '').split(/[ML]/).filter(Boolean).length;
  const vis = (id) => getComputedStyle(document.getElementById(id)).display !== 'none';
  return { on: document.getElementById('spectrum-screen').classList.contains('view-history'),
           master: pts('hist-master'), mix: pts('hist-in'), gr: (document.getElementById('hist-gr').getAttribute('d') || '').length,
           gaps: document.querySelectorAll('#hist-gaps line').length, specVisible: vis('spec-layer'), histVisible: vis('hist-layer'),
           empty: !document.getElementById('hist-empty').hidden, stats: document.getElementById('hist-stats').textContent,
           times: document.querySelectorAll('#time-labels span').length, targetY: Number(document.getElementById('hist-target').getAttribute('y1')),
           target: document.getElementById('hist-target-label').textContent, learn: document.getElementById('learn-body').textContent,
           pressed: document.getElementById('view-history').getAttribute('aria-pressed') };
});
await page.click('#view-history');
await page.waitForTimeout(700);
let h = await hv();
check(h.on && h.pressed === 'true' && !h.specVisible && h.histVisible, 'HISTORY switches the spectrum screen to the loudness history');
check(h.master > 20 && h.mix > 20 && h.gr > 50 && !h.empty, `history draws the master, the mix and the Maximizer GR from the meter frames (${h.master} points)`);
check(h.times === 5 && /^LOUD .* LUFS · GR UP TO/.test(h.stats), `history has its time axis and readout ("${h.stats}")`);
check(h.target === 'TARGET −9.0' && Math.abs(h.targetY - ((-4 - -9) / 24) * 200) < 0.6, `target line at the Loud −9 target ("${h.target}", y ${h.targetY})`);
check(/LOUDNESS HISTORY/.test(h.learn) && /Blue shading/.test(h.learn) && /turns orange/.test(h.learn), 'Learn explains how to read the history');
// Stopping the transport pauses it (no new points); playing again continues after a gap.
await page.evaluate(() => window.__polisherMock.setPlaying(false));
await page.waitForTimeout(400);
const paused = (await hv()).master;
await page.waitForTimeout(1500);
check((await hv()).master === paused, `stopping playback pauses the history (${paused} points)`);
await page.evaluate(() => window.__polisherMock.setPlaying(true));
await page.waitForTimeout(4600);
h = await hv();
check(h.master > paused && h.gaps === 1, `playing again adds points after a gap (${h.master} points, ${h.gaps} gap)`);
await page.click('#btn-reset');
await page.waitForTimeout(300);
h = await hv();
check(h.master === 0 && h.empty, 'RESET clears the history');
await page.click('#view-spectrum');
await page.waitForTimeout(300);
h = await hv();
check(!h.on && h.specVisible && !h.histVisible, 'SPECTRUM switches back');

// ---------------------------------------------------------------------------------------------
// Undo / redo and snapshots. mock.js keeps a history and snapshot bank like the plug-in's
// (window.__mockHistory, window.__mockSnaps): one step per gesture, groups for Auto-Master and recall.
const hist = () => page.evaluate(async () => {
  const juce = await import('./juce/index.js');
  const h = window.__mockHistory, u = document.getElementById('btn-undo'), r = document.getElementById('btn-redo');
  return { steps: h.steps.length, pos: h.pos, undoTitle: u.title, redoTitle: r.title, undoOff: u.disabled, redoOff: r.disabled,
           drive: juce.getSliderState('drive').getScaledValue(), release: juce.getSliderState('release').getScaledValue(),
           satFocus: juce.getComboBoxState('satFocus').getChoiceIndex(), adaptive: !!juce.getToggleState('adaptive').getValue(),
           bypass: !!juce.getToggleState('bypass').getValue(), match: !!juce.getToggleState('levelMatch').getValue(),
           toast: document.getElementById('toast').textContent, learn: document.getElementById('learn-body').textContent };
});
const cellOf = (label) => page.locator('#knob-row .cell', { has: page.locator('.cell-label', { hasText: new RegExp(`^${label}$`) }) });
const typeInto = async (label, text) => {
  await cellOf(label).locator('.cell-value').dblclick();
  await page.keyboard.press('Control+a');
  await page.keyboard.type(text);
  await page.keyboard.press('Enter');
  await page.waitForTimeout(200);
};
const fmt = (v) => `${v < 0 ? '−' : '+'}${Math.abs(v).toFixed(1)} dB`;

await pick('Maximizer'); await page.waitForTimeout(150);
h = await hist();
const pos0 = h.pos, drive0 = h.drive;
await typeInto('Drive', '8');
h = await hist();
check(h.pos === pos0 + 1 && h.steps === h.pos && h.drive === 8, `a typed value is one undo step (${h.pos - pos0} new, Drive ${h.drive})`);
check(h.undoTitle === `Undo: Drive ${fmt(8 - drive0)} (Ctrl+Z)` && !h.undoOff, `the undo arrow says what it will undo ("${h.undoTitle}")`);
check(h.redoOff && h.redoTitle === 'Nothing to redo', `redo is off until something is undone ("${h.redoTitle}")`);

await page.keyboard.press('Control+z'); await page.waitForTimeout(200);
h = await hist();
check(h.drive === drive0 && !h.redoOff, `Ctrl+Z undoes it (Drive ${h.drive})`);
check(h.redoTitle === `Redo: Drive ${fmt(8 - drive0)} (Ctrl+Shift+Z or Ctrl+Y)`, `the redo arrow says what it will redo ("${h.redoTitle}")`);
check(/^Undone: Drive/.test(h.toast), `undo says what it undid ("${h.toast}")`);
await page.keyboard.press('Control+Shift+Z'); await page.waitForTimeout(200);
check((await hist()).drive === 8, 'Ctrl+Shift+Z redoes it');
await page.keyboard.press('Control+z'); await page.waitForTimeout(150);
await page.keyboard.press('Control+y'); await page.waitForTimeout(200);
check((await hist()).drive === 8, 'Ctrl+Y redoes too');

// A knob drag, press to release, is one step however far it moves.
h = await hist();
const box = await cellOf('Drive').locator('.knob').boundingBox();
await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
await page.mouse.down();
for (let i = 1; i <= 10; i++) await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2 - 6 * i);
await page.mouse.up();
await page.waitForTimeout(200);
let h2 = await hist();
check(h2.pos === h.pos + 1 && h2.drive > h.drive, `a knob drag is one step (${h2.pos - h.pos} new, Drive ${h.drive} -> ${h2.drive})`);

// Arrow keys on a knob: quick presses merge into one step.
await page.waitForTimeout(1100);
h = await hist();
await cellOf('Release').locator('.knob').focus();
for (let i = 0; i < 5; i++) await page.keyboard.press('ArrowUp');
await page.waitForTimeout(200);
h2 = await hist();
check(h2.pos === h.pos + 1 && h2.release > h.release && /^Undo: Release /.test(h2.undoTitle), `five arrow presses are one step ("${h2.undoTitle}")`);

// A new change after an undo drops the redo (one line of history).
await page.click('#btn-undo'); await page.waitForTimeout(200);
check(!(await hist()).redoOff, 'the undo arrow undoes, and redo is offered');
await cellOf('Release Mode').locator('button').click(); await page.waitForTimeout(200);
h = await hist();
check(h.redoOff && /^Undo: Adaptive Release (on|off)/.test(h.undoTitle), `a new change after undo clears redo ("${h.undoTitle}")`);
await cellOf('Release Mode').locator('button').click(); await page.waitForTimeout(150);

// Listening controls are not settings: A / B, MATCH and MONO add no step.
h = await hist();
for (const id of ['#btn-a', '#btn-b', '#btn-match', '#btn-match', '#btn-mono', '#btn-mono']) { await page.click(id); await page.waitForTimeout(80); }
await page.waitForTimeout(150);
h2 = await hist();
check(h2.steps === h.steps && h2.pos === h.pos, 'A / B, MATCH and MONO are never undo steps');

// While typing a value, Ctrl+Z belongs to the text box, not to Polisher's undo.
h = await hist();
await cellOf('Drive').locator('.cell-value').dblclick();
await page.keyboard.press('Control+z');
await page.keyboard.press('Escape');
await page.waitForTimeout(200);
h2 = await hist();
check(h2.pos === h.pos && h2.drive === h.drive, 'Ctrl+Z in a value box leaves the settings alone');

// Auto-Master is one step that undoes everything it set; redo puts it back.
const preAm = await hist();
await page.click('#btn-automaster');
await page.waitForTimeout(6500);
h = await hist();
check(h.undoTitle === 'Undo: Auto-Master (Trap / Hip-Hop, Balanced) (Ctrl+Z)' && h.pos === preAm.pos + 1, `Auto-Master is one undo step ("${h.undoTitle}")`);
check(h.satFocus === 1 && h.drive === 9.4, 'Auto-Master set its values');
await page.click('#btn-undo'); await page.waitForTimeout(250);
h = await hist();
check(h.satFocus === 0 && h.drive === preAm.drive && /^Undone: Auto-Master/.test(h.toast), `undo puts back everything Auto-Master set (Drive ${h.drive})`);
await page.click('#btn-redo'); await page.waitForTimeout(250);
h = await hist();
check(h.satFocus === 1 && h.drive === 9.4, 'redo brings Auto-Master\'s settings back');

// Apply of the Stem EQ suggestion (Vocals +2, and Stem EQ on) is one step.
await pick('Stem EQ'); await page.waitForTimeout(200);
const stemVoc = () => page.evaluate(async () => (await import('./juce/index.js')).getSliderState('stemVoc').getScaledValue());
const preApply = await hist();
const applyBtn = page.locator('#learn-body .stem-apply');
check(await applyBtn.count() === 1, 'Learn offers the Stem EQ suggestion\'s APPLY');
if (await applyBtn.count()) {
  await applyBtn.click(); await page.waitForTimeout(250);
  h = await hist();
  check(h.pos === preApply.pos + 1 && h.undoTitle === 'Undo: Apply Stem EQ suggestion (Ctrl+Z)' && (await stemVoc()) === 2, `APPLY is one undo step ("${h.undoTitle}")`);
  await page.click('#btn-undo'); await page.waitForTimeout(250);
  check((await stemVoc()) === 0, 'undoing APPLY puts Vocals back');
}
await pick('Maximizer'); await page.waitForTimeout(150);

// Snapshots S1..S4.
const snapsUi = () => page.evaluate(() => [...document.querySelectorAll('#snaps .snap')].map((b) => ({
  text: b.textContent, lit: b.classList.contains('pill-red'), empty: b.classList.contains('empty'), pressed: b.getAttribute('aria-pressed') === 'true', title: b.title })));
let sn = await snapsUi();
check(sn.length === 4 && sn.map((s) => s.text).join() === 'S1,S2,S3,S4', `four snapshot slots S1..S4 (${sn.map((s) => s.text).join(' ')})`);
check(sn.every((s) => s.empty && !s.lit && /empty/.test(s.title)), 'all slots start empty and say so');
await page.click('#snaps .snap[data-slot="0"]'); await page.waitForTimeout(250);
sn = await snapsUi();
h = await hist();
check(!sn[0].empty && sn[0].lit && sn[0].pressed && /Snapshot 1, playing/.test(sn[0].title), `clicking an empty slot stores the settings in it ("${sn[0].title}")`);
check(/SNAPSHOTS S1/.test(h.learn) && /TRY THIS/.test(h.learn) && /Balanced vs Punchy/.test(h.learn) && /MATCH/.test(h.learn), 'Learn explains snapshots, MATCH and the Balanced vs Punchy compare');
const s1Drive = h.drive;
await pick('Maximizer'); await page.waitForTimeout(150);
await typeInto('Drive', '5');
sn = await snapsUi();
check(sn[0].text === 'S1•' && sn[0].lit, `a change after storing marks the slot as changed ("${sn[0].text}")`);
await page.click('#snaps .snap[data-slot="1"]'); await page.waitForTimeout(250);
sn = await snapsUi();
check(sn[1].lit && !sn[0].lit && sn[0].text === 'S1', 'S2 stores the new settings and is the one lit');
// Recall S1 while on A with MATCH on: settings come back, A and MATCH stay as they are, and it's one undo step.
await page.click('#btn-a'); await page.click('#btn-match'); await page.waitForTimeout(150);
const preRecall = await hist();
await page.click('#snaps .snap[data-slot="0"]'); await page.waitForTimeout(250);
h = await hist();
sn = await snapsUi();
check(h.drive === s1Drive && sn[0].lit && !sn[1].lit, `recalling S1 brings its settings back (Drive ${h.drive})`);
check(h.bypass && h.match, 'recall leaves A / B and MATCH as they were');
check(h.pos === preRecall.pos + 1 && h.undoTitle === 'Undo: Recall Snapshot 1 (Ctrl+Z)', `a recall is one undo step ("${h.undoTitle}")`);
await page.keyboard.press('Control+z'); await page.waitForTimeout(250);
h = await hist();
sn = await snapsUi();
check(h.drive === 5 && sn[1].lit && !sn[0].lit, 'undoing the recall goes back to S2 (and lights S2)');
// STORE then a slot replaces it; Shift-click stores too.
await page.click('#btn-store'); await page.waitForTimeout(100);
check(await page.evaluate(() => document.getElementById('btn-store').textContent === 'PICK SLOT'), 'STORE asks for a slot');
await page.click('#snaps .snap[data-slot="0"]'); await page.waitForTimeout(250);
check(await page.evaluate(() => Math.abs(window.__mockSnaps.slots[0].values.drive - 5) < 1e-9), 'STORE then S1 replaces S1 with what plays now');
await page.click('#snaps .snap[data-slot="2"]', { modifiers: ['Shift'] }); await page.waitForTimeout(250);
sn = await snapsUi();
check(!sn[2].empty && sn[2].lit, 'Shift-click stores in a slot');
await page.click('#btn-b'); await page.click('#btn-match'); await page.waitForTimeout(150);

// The header still fits on one row with the new controls (the page scales the 1600 x 900 design).
const fits = await page.evaluate(() => {
  const row = document.querySelector('.header-row'), r = row.getBoundingClientRect();
  return row.scrollWidth <= row.clientWidth && [...row.children].every((c) => { const b = c.getBoundingClientRect(); return b.left >= r.left - 0.5 && b.right <= r.right + 0.5 && b.width > 0; });
});
check(fits, 'header controls fit on one row');

check(errors.length === 0, `no script errors (${errors.join(' | ')})`);
await browser.close();
if (failures.length) { console.log(`\n${failures.length} UI check(s) failed`); process.exit(1); }
console.log('\nAll UI checks passed');
