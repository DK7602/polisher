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
                 'Bass Control': 'bassOn', 'Glue': 'glueOn', 'Dynamics': 'dynOn', 'Stereo Imager': 'imgOn', 'Saturation': 'satOn',
                 'Soft Clipper': 'clipOn', 'Maximizer': 'bypass' };
const toggles = () => page.evaluate(async (ids) => {
  const juce = await import('./juce/index.js');
  return Object.fromEntries(ids.map((id) => [id, !!juce.getToggleState(id).getValue()]));
}, Object.values(SWITCH));
// Waits (up to ms) until fn() in the page is true: for checks of what Learn shows after a change (it follows live
// changes within about a tenth of a second, so a fixed short wait can be too short on a busy machine).
const until = async (fn, arg, ms = 3000) => {
  for (let t = 0; t < ms; t += 50) { if (await page.evaluate(fn, arg)) return true; await page.waitForTimeout(50); }
  return false;
};
// A press on a Learn button the way a person makes it: down, a moment, up, at the button's centre. Learn may
// redraw its numbers meanwhile (the listening view does, several times a second here); the press must still land.
// (Playwright's click() re-checks the element before it presses and can keep missing a panel that redraws.)
const press = async (selector) => {
  let box = null;
  for (let t = 0; t < 5000 && !box; t += 50) {
    const ready = await page.evaluate((sel) => { const el = document.querySelector(sel); return !!el && !el.disabled; }, selector);
    box = ready ? await page.locator(selector).first().boundingBox().catch(() => null) : null;
    if (!box) await page.waitForTimeout(50);
  }
  if (!box) throw new Error(`${selector} not found (or never enabled)`);
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down();
  await page.waitForTimeout(60);
  await page.mouse.up();
};
const names = await page.evaluate(() => [...document.querySelectorAll('.row-btn .row-name')].map((x) => x.textContent));
check(names.length === 12, `signal chain lists 12 modules (${names.length})`);

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

// Saturation's Focus: Full by default; BASS flips satFocus only, and Learn explains it with its TRY THIS; HIGHS and BASS+HI too.
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
check(f0.found && f0.buttons.join('/') === 'FULL/BASS/HIGHS/BASS+HI', `Saturation shows a Focus choice (${f0.buttons.join(' / ')})`);
check(f0.focus === 0 && f0.pressed === 'FULL' && f0.note === 'whole mix', `Focus starts on Full ("${f0.pressed}", "${f0.note}")`);
await clickFocus('BASS');
await until(() => /Bass only, Tube, Drive \+9/.test(document.getElementById('learn-body').textContent));   // (its own TRY THIS: "Focus Bass only" is in Full's too)
const f1 = await satFocusState();
check(f1.focus === 1 && f1.mode === f0.mode && f1.pressed === 'BASS', `BASS sets satFocus only ("${f1.pressed}", mode ${f1.mode})`);
check(/Focus\s*Bass only/.test(f1.learn) && /phone/.test(f1.learn), 'Learn explains Bass only');
check(/Bass only, Tube, Drive \+9/.test(f1.learn) && /MATCH/.test(f1.learn), 'Learn gives the Bass only TRY THIS (phone speakers, MATCH)');
for (const [label, index, words] of [['HIGHS', 2, /Highs only/], ['BASS+HI', 3, /Bass \+ Highs/]])
{
  await clickFocus(label);
  await until((v) => document.getElementById('learn-body').textContent.includes(`Focus${v}`), index === 2 ? 'Highs only' : 'Bass + Highs');
  const fx = await satFocusState();
  check(fx.focus === index && fx.pressed === label && words.test(fx.learn), `${label} sets satFocus ${index} and Learn explains it ("${fx.pressed}")`);
}
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

// Auto-Master with the reference: the report says so and shows the comparison table. (mock.js listens to
// its 2:30 song 30x faster than real time and finishes by itself when the song ends.)
const learnText = () => page.evaluate(() => document.getElementById('learn-body').textContent);
const waitForLearn = async (re, ms = 10000) => {
  for (let t = 0; t < ms; t += 100) { if (re.test(await learnText())) return true; await page.waitForTimeout(100); }
  return false;
};
await page.click('#btn-automaster');
check(await waitForLearn(/Listened to \d:\d\d of your song/), 'Auto-Master finishes by itself when the song ends and opens its report');
r = await ref();
check(/Matched to your reference/.test(r.learn) && /REFERENCE vs YOUR MIX vs MASTER/.test(r.learn), 'report names the reference and compares it');
check(/Listened to 2:30 of your song; the loudest part \(0:54–1:06, usually the hook or drop\) set the levels\./.test(r.learn),
      'report says how much of the song it heard and where the loudest part was');
check(/Whole song/.test(r.learn) && /Tone Steady learned your song's overall tone from these 2:30/.test(r.learn), 'report shows the whole song and the learned tone');
check(await page.evaluate(() => document.querySelectorAll('#learn-body table.l-cmp').length === 2), 'comparison tables (levels and tone) are shown');
check(/Loudness, loudest 12 s \(LUFS\)\s*−7\.3/.test(r.learn), 'the loudness row compares the reference\'s loudest 12 s (its hook) with the master\'s');

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
check(f3.focus === 1 && f3.pressed === 'BASS', `after Auto-Master, Focus shows Bass only ("${f3.pressed}")`);
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
  await until(() => /WHAT YOU'RE HEARING/.test(document.getElementById('learn-body').textContent));
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
check(m.listen === 9 + 2 && /HI-MID band/.test(m.text), `Dynamics LISTEN plays what the picked band loses (mode ${m.listen}, "${m.text}")`);
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
check(gc.refLoaded && gc.shown && gc.legend === 'Hip-Hop target', `genre line has a legend entry ("${gc.legend}")`);
check(gc.pressed === 'false' && gc.d === '', 'with a reference loaded the genre line starts hidden (the reference is what Auto-Master aims for)');
await page.click('#legend-genre');
await page.waitForTimeout(200);
gc = await genre();
check(gc.pressed === 'true' && gc.points === 96 && gc.inside > 60, `clicking its legend entry draws the genre line (${gc.points} points, ${gc.inside} inside the plot)`);
const trapD = gc.d;
// The song's style (Mixology's list, shared with Voxology and Mixology) sets the genre: pick one in the picker.
const pickSongStyle = async (genreName, styleName) => {
  await page.click('#genre-name');
  await page.waitForTimeout(150);
  await page.locator('.stp-genre', { hasText: genreName }).first().click();
  await page.locator('.stp-style', { hasText: styleName }).first().click();
  await page.waitForTimeout(300);
};
check(await page.evaluate(() => document.querySelectorAll('.stp-genre').length === 0), 'the style picker opens only when asked');
check((await page.evaluate(() => document.getElementById('genre-name').textContent)) === 'Song styleChoose Style' && (await genre()).legend === 'Hip-Hop target',
      'with no style chosen the pill asks for one (no "Trap / Hip-Hop"), and the line is the hip-hop target');
await pickSongStyle('R&B & Soul', 'Smooth R&B');
gc = await genre();
check(gc.legend === 'R&B target' && gc.points === 96 && gc.d !== trapD, `the line and its legend follow the song's style ("${gc.legend}")`);
const pill = await page.evaluate(() => ({ text: document.getElementById('genre-name').textContent, open: !document.getElementById('style-picker').hidden,
                                           genres: document.querySelectorAll('.stp-genre').length }));
check(pill.text === 'R&B & SoulSmooth R&B' && !pill.open && pill.genres === 14, `the pill shows the style under its genre, the picker closes (${JSON.stringify(pill)})`);
await page.click('#genre-next');   // the arrows step the styles of the song's genre
await page.waitForTimeout(300);
check((await page.evaluate(() => document.getElementById('genre-name').textContent)) === 'R&B & Soul90s R&B' && (await genre()).legend === 'R&B target',
      'the arrows step to the next style of the genre');
await pickSongStyle('Trap & Drill', 'Hard 808');
check((await genre()).legend === 'Trap target', 'a trap style names the line Trap');
await pickSongStyle('Hip Hop', 'Modern Clean');
check((await genre()).legend === 'Hip-Hop target', 'and a hip-hop style Hip-Hop (the same curve)');
await pick('Tonal EQ');
await page.waitForTimeout(200);
const eqLearn = await page.evaluate(() => document.getElementById('learn-body').textContent);
check(/Dotted line · Hip-Hop target/.test(eqLearn) && /above the dotted line/.test(eqLearn), 'EQ Learn explains how to read the dotted line');
// Everything under the spectrum fits on one line, even with the EQ curve's entry and the longest genre name.
await pickSongStyle('Folk & Acoustic', 'Singer-Songwriter');   // -> Acoustic
const fit = await page.evaluate(() => {
  const lg = document.querySelector('.legend'), t = document.querySelector('.view-toggle').getBoundingClientRect();
  const screen = document.getElementById('spectrum-screen').getBoundingClientRect();
  return { over: lg.scrollWidth - lg.clientWidth, inside: t.right <= screen.right, name: document.getElementById('legend-genre-name').textContent,
           oneLine: lg.getBoundingClientRect().height < 26 };
});
check(fit.over <= 0 && fit.inside && fit.oneLine, `legend fits on one line with "${fit.name}" and the EQ entry (overflow ${fit.over} px)`);
await pickSongStyle('Hip Hop', 'Modern Clean');   // -> Trap again
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
const fmt = (v) => `${v < 0 ? '−' : ''}${Math.abs(v).toFixed(1)}`;   // a dB value as the undo labels write it

await pick('Maximizer'); await page.waitForTimeout(150);
h = await hist();
const pos0 = h.pos, drive0 = h.drive;
await typeInto('Drive', '8');
h = await hist();
check(h.pos === pos0 + 1 && h.steps === h.pos && h.drive === 8, `a typed value is one undo step (${h.pos - pos0} new, Drive ${h.drive})`);
check(h.undoTitle === `Undo: Drive ${fmt(drive0)} → 8.0 dB (Ctrl+Z)` && !h.undoOff, `the undo arrow says what it will undo ("${h.undoTitle}")`);
check(h.redoOff && h.redoTitle === 'Nothing to redo', `redo is off until something is undone ("${h.redoTitle}")`);

await page.keyboard.press('Control+z'); await page.waitForTimeout(200);
h = await hist();
check(h.drive === drive0 && !h.redoOff, `Ctrl+Z undoes it (Drive ${h.drive})`);
check(h.redoTitle === `Redo: Drive ${fmt(drive0)} → 8.0 dB (Ctrl+Shift+Z or Ctrl+Y)`, `the redo arrow says what it will redo ("${h.redoTitle}")`);
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

// While typing a value, Ctrl+Z belongs to the text box, not to Masterology's undo.
h = await hist();
await cellOf('Drive').locator('.cell-value').dblclick();
await page.keyboard.press('Control+z');
await page.keyboard.press('Escape');
await page.waitForTimeout(200);
h2 = await hist();
check(h2.pos === h.pos && h2.drive === h.drive, 'Ctrl+Z in a value box leaves the settings alone');

// Auto-Master is one step that undoes everything it set; redo puts it back.
const preAm = await hist();
const amVersion = await page.evaluate(() => window.__mockAm.version);
await page.click('#btn-automaster');
// (mock.js listens to its whole 2:30 song, 30x faster than real time, then thinks for 0.9 s)
await page.waitForFunction((v) => window.__mockAm.version > v && window.__mockAm.state === 0, amVersion, { timeout: 12000 });
await page.waitForTimeout(300);
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
  await press('#learn-body .stem-apply'); await page.waitForTimeout(250);
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

// The header still fits on one row with the new controls (the page scales the 1600 x 900 design). The
// logo may hang a little past the row's left edge (it overlaps the frame on purpose), by at most 16 px.
const fits = await page.evaluate(() => {
  const row = document.querySelector('.header-row'), r = row.getBoundingClientRect();
  return row.scrollWidth <= row.clientWidth && [...row.children].every((c) => { const b = c.getBoundingClientRect();
    return b.left >= r.left - (c.classList.contains('logo') ? 16 : 0.5) && b.right <= r.right + 0.5 && b.width > 0; });
});
check(fits, 'header controls fit on one row');
// Whole-song listen: the button and Learn say what it has heard; FINISH NOW works from 12 s of music on.
const amView = () => page.evaluate(() => ({
  title: document.getElementById('am-title').textContent,
  sub: document.getElementById('am-sub').textContent,
  learn: document.getElementById('learn-body').textContent,
  finish: document.getElementById('am-finish') ? { disabled: document.getElementById('am-finish').disabled, text: document.getElementById('am-finish').textContent } : null,
  state: window.__mockAm.state, mode: window.__mockAm.mode, heard: window.__mockAm.heard,
}));
await pick('Maximizer'); await page.waitForTimeout(120);
await page.click('#btn-automaster');
await page.waitForTimeout(150);
let a = await amView();
check(a.state === 1 && a.mode === 0 && a.title === 'LISTENING', `Auto-Master starts listening ("${a.title}")`);
check(/LISTENING TO YOUR SONG/.test(a.learn) && /finishes by itself/.test(a.learn) && /FINISH NOW/.test(a.learn), 'Learn shows the listening view with FINISH NOW');
check(a.finish && a.finish.disabled && a.heard < 12, `FINISH NOW waits for 12 s of music ("${a.finish && a.finish.text}" at ${a.heard} s)`);
await page.waitForFunction(() => window.__mockAm.heard >= 24, null, { timeout: 5000 });
await page.waitForTimeout(120);
a = await amView();
check(a.finish && !a.finish.disabled, 'FINISH NOW is ready after 12 s of music');
check(/heard/.test(a.sub) && /click to finish/.test(a.sub), `the button says what it heard ("${a.sub}")`);
check(/Loudest part so far/.test(a.learn), 'Learn shows the loudest part so far');
await press('#am-finish');
check(await waitForLearn(/Listened to \d:\d\d of your song/, 5000), 'FINISH NOW uses what it heard and opens the report');
a = await amView();
check(/Listened to 0:\d\d of your song; the loudest part \(0:\d\d–0:\d\d/.test(a.learn), 'the report quotes the part it heard');

// While it chooses the settings: an honest duration and CANCEL, which changes nothing. (The mock thinks for 4 s here,
// so a busy machine still finds CANCEL there.)
const versionBefore = await page.evaluate(() => { window.__mockAm.thinkMs = 4000; return window.__mockAm.version; });
await page.click('#btn-automaster');
await page.waitForFunction(() => window.__mockAm.heard >= 13, null, { timeout: 5000 });
await press('#am-finish');
await page.waitForTimeout(250);
a = await amView();
check(a.state === 2 && a.title === 'THINKING' && /usually 5–15 seconds/.test(a.sub), `the button says how long choosing takes ("${a.sub}")`);
check(/choosing every setting/.test(a.learn) && await page.locator('#am-cancel').count() === 1, 'Learn offers CANCEL while it chooses');
await press('#am-cancel');
await page.waitForTimeout(1200);
a = await amView();
check(a.state === 0 && a.title === 'AUTO-MASTER' && await page.evaluate(() => window.__mockAm.version) === versionBefore, 'CANCEL stops it and changes nothing');
await page.evaluate(() => { window.__mockAm.thinkMs = 900; });

// Tone Steady's target: LEARN (a tone-only listen), CLEAR, and Learn says which reference it uses.
await pick('Tone Steady'); await page.waitForTimeout(150);
const tsView = () => page.evaluate(() => ({
  val: document.getElementById('ts-target-val')?.textContent || '',
  learnBtn: document.getElementById('ts-learn')?.textContent || '',
  clearDisabled: !!document.getElementById('ts-clear')?.disabled,
  learn: document.getElementById('learn-body').textContent,
  read: document.getElementById('read-ts')?.textContent || '',
  target: window.__mockAm.toneTarget,
}));
let tv = await tsView();
check(/^your song · \d:\d\d$/.test(tv.val) && !tv.clearDisabled, `Tone Steady shows the target learned by Auto-Master ("${tv.val}")`);
check(/Tone Steady now holds your song to its own overall tone/.test(tv.learn) && /what you hear is what you export/.test(tv.learn), 'Learn explains the stored target');
check(/song$/.test(tv.read), `the chain row says it holds to the song ("${tv.read}")`);
await page.click('#ts-clear'); await page.waitForTimeout(200);
tv = await tsView();
check(tv.target === 0 && tv.val === 'following the music' && tv.clearDisabled, `CLEAR goes back to following the music ("${tv.val}")`);
check(/following the music/.test(tv.learn) && /LEARN holds it to your whole song's tone/.test(tv.learn), 'Learn explains following the music and LEARN');
await page.click('#ts-learn'); await page.waitForTimeout(150);
a = await amView();
check(a.state === 1 && a.mode === 1 && a.title === 'LEARNING TONE', `LEARN starts a tone-only listen ("${a.title}")`);
check(/LEARNING YOUR SONG'S TONE/.test(a.learn) && /Nothing else changes/.test(a.learn), 'Learn shows the tone-learning view');
check((await tsView()).learnBtn === 'LEARNING', 'the LEARN button shows it is learning');
const driveBefore = await page.evaluate(async () => (await import('./juce/index.js')).getSliderState('drive').getScaledValue());
await page.waitForFunction(() => window.__mockAm.heard >= 12, null, { timeout: 5000 });
await page.waitForTimeout(150);
await press('#am-finish');
await page.waitForTimeout(400);
tv = await tsView();
check(tv.target >= 12 && /^your song · \d:\d\d$/.test(tv.val), `LEARN stores the song's tone ("${tv.val}")`);
check(await page.evaluate(async () => (await import('./juce/index.js')).getSliderState('drive').getScaledValue()) === driveBefore, 'LEARN changes no other setting');
check(/Tone Steady now holds your song to its own overall tone/.test(tv.learn), 'Learn is back on Tone Steady with the target');
// LEARN is an undo step of its own: undo goes back to following the music, redo brings the tone back.
const undoTitle = () => page.evaluate(() => document.getElementById('btn-undo').title);
check(/Tone Steady: learn the song's tone/.test(await undoTitle()), `the undo arrow offers to undo LEARN ("${await undoTitle()}")`);
const learnedTarget = tv.target;
await page.click('#btn-undo'); await page.waitForTimeout(250);
tv = await tsView();
check(tv.target === 0 && tv.val === 'following the music', `undo of LEARN goes back to following the music ("${tv.val}")`);
await page.click('#btn-redo'); await page.waitForTimeout(250);
tv = await tsView();
check(tv.target === learnedTarget, `redo brings the learned tone back ("${tv.val}")`);
// A snapshot keeps the target: store S4 with it, CLEAR, recall S4.
await page.locator('#snaps .snap').nth(3).click({ modifiers: ['Shift'] }); await page.waitForTimeout(200);   // store (replace)
await page.click('#ts-clear'); await page.waitForTimeout(200);
check((await tsView()).target === 0, 'CLEAR forgets the tone');
await page.evaluate(() => document.querySelectorAll('#snaps .snap')[3].click()); await page.waitForTimeout(250);
tv = await tsView();
check(tv.target === learnedTarget, `recalling the snapshot brings Tone Steady's target back ("${tv.val}")`);
// The music sits far from the stored tone (a preset from another song): the cell and Learn say so, with a plan.
await page.evaluate(async () => { window.__mockAm.toneFar = true; (await import('./juce/index.js')).getSliderState('tsAmount').setNormalisedValue(0.4); });
await page.waitForTimeout(300);
tv = await tsView();
check(/^another song\? · \d:\d\d$/.test(tv.val), `the Target cell asks whether the tone is another song's ("${tv.val}")`);
check(/far from the tone Tone Steady is holding it to/.test(tv.learn) && /Do I need to fix it\?/.test(tv.learn) && /Press LEARN in this panel/.test(tv.learn),
      'Learn warns about a tone from another song, with a plan');
await page.evaluate(() => { window.__mockAm.toneFar = false; });
await page.waitForTimeout(300);
tv = await tsView();
check(/^your song · \d:\d\d$/.test(tv.val) && !/far from the tone/.test(tv.learn), `the warning goes when the music matches again ("${tv.val}")`);

// ---------------------------------------------------------------------------------------------
// Re-audit fixes.
// Space / Enter after a mouse click never press that button again (Space is Cubase's play key); with the
// keyboard (Tab) they still press buttons, and a value box still takes Enter.
await pick('Maximizer'); await page.waitForTimeout(150);
const pressed = (id) => page.evaluate((i) => document.getElementById(i).getAttribute('aria-pressed'), id);
await page.click('#btn-match'); await page.waitForTimeout(150);
const matchOn = await pressed('btn-match');
await page.keyboard.press('Space'); await page.waitForTimeout(150);
await page.keyboard.press('Enter'); await page.waitForTimeout(150);
check(matchOn === 'true' && (await pressed('btn-match')) === 'true', `Space / Enter after clicking MATCH leave it on (${matchOn} -> ${await pressed('btn-match')})`);
check(await page.evaluate(() => document.activeElement === document.body), 'a clicked button does not keep the keyboard');
await page.click('#btn-match'); await page.waitForTimeout(150);
await page.click('#btn-automaster'); await page.waitForTimeout(150);
await page.keyboard.press('Space'); await page.waitForTimeout(150);
a = await amView();
check(a.state === 1 && a.title === 'LISTENING', `Space after clicking Auto-Master keeps it listening ("${a.title}", state ${a.state})`);
await page.evaluate(() => { window.__mockAm.state = 0; });   // (stop the mock's listen)
await page.waitForTimeout(250);
const recalls = () => page.evaluate(() => window.__mockSnaps.recalls || 0);
const r0 = await recalls();
await page.click('#snaps .snap[data-slot="1"]'); await page.waitForTimeout(200);   // recall S2 with the mouse
const r1 = await recalls();
await page.keyboard.press('Space'); await page.keyboard.press('Enter'); await page.waitForTimeout(250);
check(r1 === r0 + 1 && (await recalls()) === r1, `Space / Enter after clicking a snapshot don't recall it again (${r0} -> ${r1} -> ${await recalls()})`);
await page.click('#btn-a'); await page.waitForTimeout(100);
await page.keyboard.press('Space'); await page.waitForTimeout(150);
check((await pressed('btn-a')) === 'true', 'Space after clicking A stays on A');
await page.click('#btn-b'); await page.waitForTimeout(100);
// Keyboard users: Tab onto a button, then Space presses it.
await page.focus('#btn-match');
await page.keyboard.press('Shift+Tab'); await page.keyboard.press('Tab');
const tabbed = await page.evaluate(() => document.activeElement && document.activeElement.id);
await page.keyboard.press('Space'); await page.waitForTimeout(150);
check(tabbed === 'btn-match' && (await pressed('btn-match')) === 'true', `with Tab, Space presses the focused button (${tabbed})`);
await page.keyboard.press('Enter'); await page.waitForTimeout(150);
check((await pressed('btn-match')) === 'false', 'with Tab, Enter presses it too');
await page.mouse.click(5, 5); await page.waitForTimeout(100);
await typeInto('Drive', '6');
check((await hist()).drive === 6, 'a value box still takes Enter');
check(await page.evaluate(() => ![...document.querySelectorAll('[title]')].some((e) => /never in your export/i.test(e.title))),
      'no tooltip claims "never in your export" (a Realtime Export plays what you hear)');

// EQ point: double-click (back to flat at its own frequency) is one undo step.
await pick('Tonal EQ'); await page.waitForTimeout(200);
const node = page.locator('.eq-node').nth(2);
const nb = await node.boundingBox();
await page.mouse.move(nb.x + nb.width / 2, nb.y + nb.height / 2);
await page.mouse.down();
for (let i = 1; i <= 6; i++) await page.mouse.move(nb.x + nb.width / 2 + 4 * i, nb.y + nb.height / 2 - 5 * i);
await page.mouse.up();
await page.waitForTimeout(1200);   // (past the merge window of quick repeats)
h = await hist();
const nb2 = await node.boundingBox();
await page.mouse.dblclick(nb2.x + nb2.width / 2, nb2.y + nb2.height / 2);
await page.waitForTimeout(250);
h2 = await hist();
const eqBack = await page.evaluate(async () => { const j = await import('./juce/index.js'); return [j.getSliderState('eqGain3').getScaledValue(), j.getSliderState('eqFreq3').getScaledValue()]; });
check(h2.pos === h.pos + 1 && /^Undo: EQ Low Mid \(2 controls\)/.test(h2.undoTitle) && eqBack[0] === 0 && Math.abs(eqBack[1] - 350) < 0.5,
      `double-clicking an EQ point resets it as one undo step (${h2.pos - h.pos} new, "${h2.undoTitle}", ${eqBack.join(' / ')})`);

// A drag whose pointer capture is lost (the window lost the mouse) still closes its undo step.
await pick('Maximizer'); await page.waitForTimeout(150);
await page.waitForTimeout(1100);
h = await hist();
const kb = await cellOf('Release').locator('.knob').boundingBox();
await page.mouse.move(kb.x + kb.width / 2, kb.y + kb.height / 2);
await page.mouse.down();
for (let i = 1; i <= 4; i++) await page.mouse.move(kb.x + kb.width / 2, kb.y + kb.height / 2 - 6 * i);
await cellOf('Release').locator('.knob').dispatchEvent('lostpointercapture');
await page.waitForTimeout(100);
const openAfterLoss = await page.evaluate(() => window.__mockHistory.open.size);
h2 = await hist();
await page.mouse.up();
check(openAfterLoss === 0 && h2.pos === h.pos + 1 && /^Undo: Release /.test(h2.undoTitle), `lost pointer capture closes the drag as one step (open ${openAfterLoss}, "${h2.undoTitle}")`);

// Integrated restarts (the "Restart loudness after you change a control" setting, on) after undo / redo, a
// snapshot recall, Tone Steady's CLEAR and LEARN, and an Auto-Master run, not only after a knob move.
const statsResets = () => page.evaluate(() => window.__mockResets.stats);
const restarts = async (what, action, wait = 1000) => {
  const before = await statsResets();
  await action();
  await page.waitForTimeout(wait);
  check((await statsResets()) > before, `Integrated restarts after ${what}`);
};
await restarts('undo', () => page.click('#btn-undo'));
await restarts('redo', () => page.click('#btn-redo'));
await restarts('a snapshot recall', () => page.evaluate(() => document.querySelectorAll('#snaps .snap')[0].click()));
await pick('Tone Steady'); await page.waitForTimeout(150);
await restarts('LEARN of the song\'s tone', async () => {
  await page.click('#ts-learn');
  await page.waitForFunction(() => window.__mockAm.heard >= 12, null, { timeout: 5000 });
  await page.waitForTimeout(150);
  await press('#am-finish');
});
await restarts('CLEAR', () => page.click('#ts-clear'));
await restarts('an Auto-Master run', async () => {
  const v = await page.evaluate(() => window.__mockAm.version);
  await page.click('#btn-automaster');
  await page.waitForFunction(() => window.__mockAm.heard >= 12, null, { timeout: 5000 });
  await page.waitForTimeout(150);
  await press('#am-finish');
  await page.waitForFunction((x) => window.__mockAm.version > x && window.__mockAm.state === 0, v, { timeout: 5000 });
}, 1300);

// The suite link: Mixology's hand-off shows in Learn; a different style offers its genre; mono / limiter warn.
await pick('Maximizer');
await page.waitForTimeout(150);
await page.evaluate(() => window.__polisherMock.setMixology({ style: 'R&B & Soul - Modern', genre: 1, peak: -5.8, lufs: -16.2, analysed: true, mono: false, limited: false, tone: [0.4, 2.6, -0.5, -3.1] }));
check(await waitForLearn(/FROM MIXOLOGY/), 'Learn shows Mixology\'s hand-off');
const hand = await page.evaluate(() => ({ learn: document.getElementById('learn-body').textContent,
  button: [...document.querySelectorAll('#learn-body button')].map((b) => b.textContent).find((t) => /MIXOLOGY'S STYLE/.test(t)) || '' }));
check(/mixed this song as R&B & Soul - Modern/.test(hand.learn) && /more low mids \(\+2\.6 dB\) and less air \(−3\.1 dB\)/.test(hand.learn), 'it names the style and the tone Mixology heard');
check(/USE R&B/.test(hand.button), `it offers Mixology's genre ("${hand.button}")`);
await page.evaluate(() => [...document.querySelectorAll('#learn-body button')].find((b) => /MIXOLOGY'S STYLE/.test(b.textContent)).click());
await page.waitForTimeout(200);
const genreNow = await page.evaluate(async () => (await import('./juce/index.js')).getComboBoxState('amGenre').getChoiceIndex());
check(genreNow === 1, `the button sets Auto-Master's genre (now ${genreNow})`);
check(await page.evaluate(() => ![...document.querySelectorAll('#learn-body button')].some((b) => /MIXOLOGY'S STYLE/.test(b.textContent))), 'the offer goes once the genre matches');
await page.evaluate(() => window.__polisherMock.setMixology({ style: 'R&B & Soul - Modern', genre: 1, peak: -0.4, lufs: -9.1, analysed: false, mono: true, limited: true, tone: [0, 0, 0, 0] }));
check(await waitForLearn(/YOUR MIX IS MONO/) && /MIXOLOGY'S LIMITER IS ON/.test(await learnText()) && /little room left/.test(await learnText()), 'a mono mix, Mixology\'s limiter and no headroom are flagged');
// A hand-off that isn't this mix (Mixology on a group, in another project...): said so, without its numbers and tips.
await page.evaluate(() => window.__polisherMock.setMixology({ style: 'R&B & Soul - Modern', genre: 1, peak: -0.4, lufs: -9.1, analysed: false, mono: true, limited: true, tone: [0, 0, 0, 0], notThisMix: 1 }));
check(await waitForLearn(/aren't the mix reaching Masterology/) && !/YOUR MIX IS MONO|MIXOLOGY'S LIMITER IS ON|little room left/.test(await learnText()), 'a hand-off from another signal is called so, without its numbers');
await page.evaluate(() => window.__polisherMock.setMixology({ style: 'R&B & Soul - Modern', genre: 1, peak: -0.4, lufs: -9.1, analysed: false, mono: false, limited: false, tone: [0, 0, 0, 0], notThisMix: 2 }));
check(await waitForLearn(/Mixology hears nothing/), 'a silent Mixology (bypassed, another project) is called so');
await page.evaluate(() => window.__polisherMock.setMixology(null));
check(await waitForLearn(/^(?![\s\S]*FROM MIXOLOGY)/), 'without Mixology the note goes');
await page.evaluate(async () => (await import('./juce/index.js')).getComboBoxState('amGenre').setChoiceIndex(0));

// Auto Analyze: after an Auto-Master run, Learn has its switch; on, it listens and offers edits with APPLY / KEEP MINE.
await pick('Maximizer');
await page.waitForTimeout(200);
check(await page.evaluate(() => !!document.getElementById('btn-autoanalyze')), 'Learn shows the AUTO ANALYZE switch once Auto-Master has run');
await press('#btn-autoanalyze');
check(await waitForLearn(/HOW IT WORKS/), 'switching it on opens its Learn page');
check(await page.evaluate(() => window.__mockAa.on), 'the switch reaches the plug-in');
await page.evaluate(() => window.__polisherMock.aaCheck());
check(await waitForLearn(/NEW EDITS/), 'the edits show when the mix changed');
const aaCards = await page.evaluate(() => [...document.querySelectorAll('#learn-body .aa-card')].map((c) => c.textContent));
check(aaCards.length === 2 && /TONAL EQ/.test(aaCards[0]) && /→/.test(aaCards[0]) && /MAXIMIZER/.test(aaCards[1]), `one card per module, now → new (${aaCards.length} cards)`);
check(/arrives 1\.8 dB louder on the hook/.test(await learnText()) && /more low end \(\+2\.1 dB\)/.test(await learnText()), 'it says what changed in the mix');
check(/Auto Analyze: 2 new edits/.test(await page.evaluate(() => document.getElementById('am-sub').textContent)), 'the Auto-Master button says edits are waiting');
const aaDriveBefore = await page.evaluate(async () => (await import('./juce/index.js')).getSliderState('drive').getScaledValue());
const aaHistBefore = await hist();
await page.evaluate(() => [...document.querySelectorAll('#learn-body .aa-card')].find((c) => /MAXIMIZER/.test(c.textContent)).querySelector('button').click());
await page.waitForTimeout(400);
const aaDriveAfter = await page.evaluate(async () => (await import('./juce/index.js')).getSliderState('drive').getScaledValue());
const aaHistAfter = await hist();
check(Math.abs(aaDriveAfter - (aaDriveBefore - 1.8)) < 0.02, `APPLY moves the control (Drive ${aaDriveBefore.toFixed(2)} → ${aaDriveAfter.toFixed(2)})`);
check(aaHistAfter.pos === aaHistBefore.pos + 1 && /Auto Analyze: Maximizer/.test(aaHistAfter.undoTitle), `as one undo step ("${aaHistAfter.undoTitle}")`);
check(await page.evaluate(() => document.querySelectorAll('#learn-body .aa-card').length === 1), 'the applied card goes away');
await page.evaluate(() => [...document.querySelectorAll('#learn-body .aa-card button')].find((b) => b.textContent === 'KEEP MINE').click());
check(await waitForLearn(/YOUR SETTINGS STILL FIT/), 'KEEP MINE clears the last card');
await pick('Tonal EQ');
await page.waitForTimeout(200);
check(await page.evaluate(() => /WHAT IT HEARS/.test(document.querySelector('#learn-body .aa-strip').textContent)), 'module pages keep the switch at the top');
await press('#btn-autoanalyze');
await page.waitForTimeout(300);
check(await page.evaluate(() => !window.__mockAa.on), 'the switch turns it off again');
await pick('Maximizer');
await page.waitForTimeout(150);

// ---------------------------------------------------------------------------------------------
// Interface audit fixes.
const sliderVal = (id) => page.evaluate(async (i) => (await import('./juce/index.js')).getSliderState(i).getScaledValue(), id);
const knobOf = (label) => cellOf(label).locator('.knob');
const wheelAt = (label, init) => knobOf(label).evaluate((el, i) => el.dispatchEvent(new WheelEvent('wheel', { bubbles: true, cancelable: true, ...i })), init);
await page.mouse.click(5, 5);

// Learn is redrawn only when it changes, and a press that spans a redraw still lands.
await pick('Glue'); await page.waitForTimeout(300);
const learnSwaps = await page.evaluate(() => new Promise((res) => {
  let n = 0; const o = new MutationObserver((ms) => { n += ms.length; });
  o.observe(document.getElementById('learn-body'), { childList: true }); setTimeout(() => { o.disconnect(); res(n); }, 2500);
}));
check(learnSwaps === 0, `an idle module page's Learn isn't redrawn (${learnSwaps} redraws in 2.5 s)`);
await pick('Maximizer'); await page.waitForTimeout(150);
await page.evaluate(() => window.__polisherMock.setMixology({ style: 'R&B & Soul - Modern', genre: 1, peak: -5.8, lufs: -16.2, analysed: true, mono: false, limited: false, tone: [0, 0, 0, 0] }));
check(await waitForLearn(/MIXOLOGY'S STYLE/), 'Learn offers Mixology\'s genre');
{
  const b = await page.locator('#learn-body button', { hasText: "MIXOLOGY'S STYLE" }).boundingBox();
  await page.mouse.move(b.x + b.width / 2, b.y + b.height / 2);
  await page.mouse.down();
  await page.evaluate(async () => { for (let k = 0; k < 6; k++) { window.__JUCE__.backend.emitByBackend('__juce__sliderdrive', JSON.stringify({ eventType: 'valueChanged', value: 4 + k / 10 })); await new Promise((r) => setTimeout(r, 30)); } });
  await page.mouse.up();
  await page.waitForTimeout(200);
  const g = await page.evaluate(async () => (await import('./juce/index.js')).getComboBoxState('amGenre').getChoiceIndex());
  check(g === 1, `a press on a Learn button lands although Learn redrew under it (genre ${g})`);
}
await page.evaluate(() => window.__polisherMock.setMixology(null));
await page.evaluate(async () => (await import('./juce/index.js')).getComboBoxState('amGenre').setChoiceIndex(0));
await page.waitForTimeout(200);

// Wheel: Shift+wheel arrives sideways (deltaX only) and follows the wheel; trackpad scrolls add up (~100 px a step).
const d0 = await sliderVal('drive');
await wheelAt('Drive', { deltaX: -100, deltaY: 0, shiftKey: true });
await page.waitForTimeout(450);
const dUp = await sliderVal('drive');
await wheelAt('Drive', { deltaX: 100, deltaY: 0, shiftKey: true });
await page.waitForTimeout(100);
check(dUp > d0 && (await sliderVal('drive')) === d0, `Shift+wheel up turns Drive up, down turns it down (${d0} -> ${dUp} -> ${await sliderVal('drive')})`);
await page.waitForTimeout(450);
const c0 = await sliderVal('ceiling');
await knobOf('Ceiling').evaluate(async (el) => { for (let i = 0; i < 40; i++) { el.dispatchEvent(new WheelEvent('wheel', { deltaY: 3, bubbles: true, cancelable: true })); await new Promise((r) => setTimeout(r, 5)); } });
await page.waitForTimeout(100);
const c1 = await sliderVal('ceiling');
check(Math.abs(c1 - (c0 - 0.03)) < 1e-6, `forty 3-px trackpad scrolls are one step, not forty (Ceiling ${c0} -> ${c1})`);

// Arrow keys and wheel move a Ratio off 1:1 (the bottom of a skewed range), and whole-number controls with Shift.
await pick('Glue'); await page.waitForTimeout(200);
await knobOf('Ratio').focus();
await page.keyboard.press('ArrowUp'); await page.waitForTimeout(100);
const ratio = await sliderVal('glueRatio');
check(ratio > 1, `ArrowUp moves Glue Ratio off 1:1 (${ratio})`);
await page.keyboard.press('ArrowDown'); await page.waitForTimeout(100);
check((await sliderVal('glueRatio')) === 1, 'ArrowDown puts it back to 1:1');
await pick('Maximizer'); await page.waitForTimeout(200);
await knobOf('Bass First').focus();
await page.keyboard.press('Shift+ArrowUp'); await page.waitForTimeout(100);
check((await sliderVal('maxBassFirst')) === 1, `Shift+ArrowUp moves Bass First by one (${await sliderVal('maxBassFirst')})`);
await page.keyboard.press('Shift+ArrowDown'); await page.waitForTimeout(100);

// JUCE's late echoes of values the page sent don't cost steps.
await page.evaluate(() => window.__polisherMock.setEcho(150));
await page.waitForTimeout(1100);
const e0 = await sliderVal('ceiling');
await knobOf('Ceiling').focus();
for (let i = 0; i < 5; i++) { await page.keyboard.press('ArrowUp'); await page.waitForTimeout(20); }
await page.waitForTimeout(400);
const e1 = await sliderVal('ceiling');
check(Math.abs(e1 - (e0 + 0.15)) < 1e-6, `five quick ArrowUp with echoes coming back late are five steps (Ceiling ${e0} -> ${e1})`);
await page.evaluate(() => window.__JUCE__.backend.emitByBackend('__juce__sliderceiling', JSON.stringify({ eventType: 'valueChanged', value: -1 })));
await page.waitForTimeout(150);
check((await sliderVal('ceiling')) === -1, 'a value from the host still comes through');
await page.evaluate(() => window.__polisherMock.setEcho(15));

// A clicked knob takes the keyboard, without a focus ring.
const rk = await knobOf('Release').boundingBox();
await page.mouse.click(rk.x + rk.width / 2, rk.y + rk.height / 2);
await page.waitForTimeout(100);
const kf = await page.evaluate(() => ({ label: document.activeElement.getAttribute('aria-label'), ring: document.activeElement.matches(':focus-visible') }));
const rel0 = await sliderVal('release');
await page.keyboard.press('ArrowUp'); await page.waitForTimeout(100);
check(kf.label === 'Release' && !kf.ring && (await sliderVal('release')) > rel0, `a clicked knob takes the arrow keys, with no ring (${JSON.stringify(kf)})`);
check(await page.evaluate(() => { const k = document.querySelector('.knob[aria-label="Release"]'); return k.getAttribute('aria-valuemin') === '10' && k.getAttribute('aria-valuemax') === '1000' && !!k.getAttribute('aria-valuenow'); }),
      'knobs report aria-valuenow / min / max');

// The type-in box: left unchanged it changes nothing; 1,200 is 1200.
await page.mouse.click(5, 5);
await page.evaluate(() => window.__JUCE__.backend.emitByBackend('__juce__slidereqGain4', JSON.stringify({ eventType: 'valueChanged', value: 2.37 })));
await pick('Tonal EQ'); await page.waitForTimeout(200);
h = await hist();
await cellOf('Mid').locator('.cell-value').dblclick(); await page.waitForTimeout(100);
await page.mouse.click(5, 5); await page.waitForTimeout(200);
h2 = await hist();
check((await sliderVal('eqGain4')) === 2.37 && h2.pos === h.pos, `opening the type-in box and leaving changes nothing (${await sliderVal('eqGain4')}, ${h2.pos - h.pos} new steps)`);
await page.evaluate(() => window.__JUCE__.backend.emitByBackend('__juce__slidereqGain4', JSON.stringify({ eventType: 'valueChanged', value: 0 })));
await pick('Dynamics'); await page.waitForTimeout(200);
await page.evaluate(() => document.querySelectorAll('.band-pick .band-btn')[1].click()); await page.waitForTimeout(150);
await typeInto('Split', '1,200');
check((await sliderVal('dynX2')) === 1200, `"1,200" types 1200 (Low Mid Split ${await sliderVal('dynX2')})`);
await page.evaluate(() => document.querySelectorAll('.band-pick .band-btn')[0].click()); await page.waitForTimeout(100);

// EQ points: no jump when grabbed off-centre; the keyboard moves frequency and gain.
await pick('Tonal EQ'); await page.waitForTimeout(250);
{
  const nb4 = await page.locator('.eq-node').nth(3).boundingBox();
  const f0 = await sliderVal('eqFreq4');
  await page.mouse.move(nb4.x + nb4.width / 2 + 5, nb4.y + nb4.height / 2 + 5);
  await page.mouse.down();
  await page.mouse.move(nb4.x + nb4.width / 2 + 6, nb4.y + nb4.height / 2 + 5);
  await page.mouse.up();
  await page.waitForTimeout(150);
  const f1 = await sliderVal('eqFreq4'), g1 = await sliderVal('eqGain4');
  check(g1 === 0 && f1 / f0 < 1.03, `an EQ point grabbed 5 px off-centre doesn't jump (gain ${g1}, ${f0} -> ${f1} Hz)`);
  await page.locator('.eq-node').nth(3).focus();
  await page.keyboard.press('ArrowRight'); await page.keyboard.press('ArrowUp'); await page.waitForTimeout(150);
  check((await sliderVal('eqFreq4')) > f1 && (await sliderVal('eqGain4')) === 0.5, 'an EQ point takes the arrow keys (frequency and gain)');
  await page.locator('.eq-node').nth(3).dblclick(); await page.waitForTimeout(150);
}

// The browser's own menu and keys stay out of the plug-in window.
const prevented = (type, init, sel) => page.evaluate(([t, i, sl]) => {
  const ev = t === 'contextmenu' ? new MouseEvent(t, { bubbles: true, cancelable: true, ...i }) : new KeyboardEvent(t, { bubbles: true, cancelable: true, ...i });
  (sl ? document.querySelector(sl) : document.body).dispatchEvent(ev); return ev.defaultPrevented; }, [type, init, sel]);
check(await prevented('contextmenu', {}, '.knob') && !(await prevented('contextmenu', {}, '#manual-body')), 'no browser right-click menu (except over the manual\'s text)');
check(await prevented('keydown', { key: 'F5' }) && await prevented('keydown', { key: 'F3' }) && await prevented('keydown', { key: 'p', ctrlKey: true })
      && await prevented('keydown', { key: 'r', ctrlKey: true }) && !(await prevented('keydown', { key: 'a' })), 'F3, F5, Ctrl+P and Ctrl+R are not the browser\'s');

// Keyboard: Esc closes Settings and Help; the style name opens the picker; the manual's contents are links.
await page.click('#btn-settings'); await page.waitForTimeout(100);
await page.keyboard.press('Escape'); await page.waitForTimeout(100);
await page.click('#btn-help'); await page.waitForTimeout(100);
const helpOpen = await page.evaluate(() => !document.getElementById('pop-help').hidden);
await page.keyboard.press('Escape'); await page.waitForTimeout(100);
check(helpOpen && await page.evaluate(() => document.getElementById('pop-settings').hidden && document.getElementById('pop-help').hidden), 'Esc closes Settings and Quick Help');
await page.focus('#genre-name'); await page.keyboard.press('Tab'); await page.keyboard.press('Shift+Tab');
await page.keyboard.press('Enter'); await page.waitForTimeout(200);
check(await page.evaluate(() => !document.getElementById('style-picker').hidden && document.getElementById('style-picker').contains(document.activeElement)),
      'Enter on the style name opens the picker, with the keyboard in it');
await page.keyboard.press('Escape'); await page.waitForTimeout(100);
check(await page.evaluate(() => document.getElementById('style-picker').hidden && document.activeElement.id === 'genre-name'), 'Esc closes it and gives the keyboard back');
await page.mouse.click(5, 5);
check(await page.evaluate(() => !/v0\.\d/.test(document.querySelector('.pop-foot').textContent)), 'Settings shows no typed-in version (the plug-in hands it over)');
check(await page.evaluate(() => [...document.querySelectorAll('[data-daw], [data-daw-title]')].every((e) => !/\{daw/.test(e.textContent + e.title))), 'the window\'s {daw} texts are filled in');

// One odd field in a frame (JUCE sends NaN as null) doesn't freeze the window.
await page.evaluate(() => window.__polisherMock.setFrame({ range: null, dynGr: null, gr: null, mix: Array(96).fill(null),
  fromMixology: { style: 'X', genre: 9, peak: null, lufs: -14, analysed: true, mono: false, limited: false, tone: [null, 1] } }));
await pick('Dynamics'); await page.waitForTimeout(200);
const sp0 = await page.evaluate(() => document.getElementById('sp-master').getAttribute('d'));
await page.waitForTimeout(500);
const sp1 = await page.evaluate(() => document.getElementById('sp-master').getAttribute('d'));
check(sp0 !== sp1 && /6\.8 LU/.test(await page.evaluate(() => document.getElementById('mv-range').textContent)), 'a frame with null fields keeps the meters and spectrum moving');
await pick('Maximizer');
check(await waitForLearn(/FROM MIXOLOGY/), 'and Learn still shows (an unknown Mixology genre is ignored)');
await page.evaluate(() => { window.__polisherMock.setFrame(null); window.__polisherMock.setMixology(null); });

// The window reopens where it was: module, LEARN, loudness history (mock.js keeps them like the plug-in does).
await pick('Glue'); await page.waitForTimeout(150);
await page.click('#learn-toggle'); await page.waitForTimeout(100);
await page.click('#view-history'); await page.waitForTimeout(6500);
const histBefore = await page.evaluate(() => (document.getElementById('hist-master').getAttribute('d') || '').split(/[ML]/).filter(Boolean).length);
await page.reload(); await page.waitForTimeout(900);
const reopened = await page.evaluate(() => ({ module: document.getElementById('module-name').textContent, learn: document.getElementById('learn-toggle').textContent,
  points: (document.getElementById('hist-master').getAttribute('d') || '').split(/[ML]/).filter(Boolean).length }));
// (a point or two may join between the count and the reload: the window hands over what it has as it goes)
check(reopened.module === 'GLUE' && reopened.learn === 'LEARN OFF' && reopened.points >= Math.min(3, histBefore) && reopened.points <= histBefore + 2,
      `reopened on Glue with LEARN off and the history (${JSON.stringify(reopened)}, ${histBefore} before)`);
await page.click('#learn-toggle'); await page.click('#view-spectrum'); await pick('Maximizer'); await page.waitForTimeout(200);

check(errors.length === 0, `no script errors (${errors.join(' | ')})`);
await browser.close();

// The listening banner at 1440 x 810 (where a new Masterology opens), 1280 x 720, 1120 x 630 and its smallest (800 x 450,
// for 1080p laptops at 150 % and more): the longest description (a Dynamics band on PHONE) is shown whole,
// "— not the master" included.
const browser2 = await chromium.launch();
for (const [w, hgt] of [[1440, 810], [1280, 720], [1120, 630], [800, 450]]) {
  const pg = await browser2.newPage({ viewport: { width: w, height: hgt } });
  pg.on('pageerror', (e) => errors.push(String(e)));
  await pg.goto(url);
  await pg.waitForTimeout(800);
  await pg.evaluate(() => [...document.querySelectorAll('.row-btn')].find((x) => x.querySelector('.row-name').textContent === 'Dynamics').click());
  await pg.waitForTimeout(150);
  await pg.evaluate(() => document.querySelectorAll('.band-pick .band-btn')[2].click());
  await pg.click('#btn-listen'); await pg.waitForTimeout(150);
  await pg.click('#btn-phone'); await pg.waitForTimeout(400);
  const b = await pg.evaluate(() => {
    const t = document.getElementById('monitor-text'), ban = document.getElementById('monitor-banner').getBoundingClientRect();
    const scr = document.getElementById('spectrum').getBoundingClientRect();
    return { text: t.textContent, hidden: t.scrollHeight - t.clientHeight, cut: t.scrollWidth - t.clientWidth, inside: ban.right <= scr.right && ban.bottom <= scr.bottom,
             px: parseFloat(getComputedStyle(t).fontSize) * document.getElementById('stage').getBoundingClientRect().width / 1600 };
  });
  check(/not the master$/.test(b.text) && b.hidden <= 1 && b.cut <= 1 && b.inside, `${w} x ${hgt}: the banner shows its whole text ("${b.text}", hidden ${b.hidden} px)`);
  // 9 points down to 1120 x 630; the smallest size is for screens at 150 % or more, where 6 points are 9+ pixels.
  check(b.px >= (w <= 800 ? 6 : 9), `${w} x ${hgt}: banner text is ${b.px.toFixed(1)} px`);
  await pg.close();
}
await browser2.close();
check(errors.length === 0, `no script errors at the small window sizes (${errors.join(' | ')})`);
if (failures.length) { console.log(`\n${failures.length} UI check(s) failed`); process.exit(1); }
console.log('\nAll UI checks passed');
