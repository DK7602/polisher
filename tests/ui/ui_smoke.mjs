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

check(errors.length === 0, `no script errors (${errors.join(' | ')})`);
await browser.close();
if (failures.length) { console.log(`\n${failures.length} UI check(s) failed`); process.exit(1); }
console.log('\nAll UI checks passed');
