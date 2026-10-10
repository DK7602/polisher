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
await page.click('#am-finish');
check(await waitForLearn(/Listened to \d:\d\d of your song/, 5000), 'FINISH NOW uses what it heard and opens the report');
a = await amView();
check(/Listened to 0:\d\d of your song; the loudest part \(0:\d\d–0:\d\d/.test(a.learn), 'the report quotes the part it heard');

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
await page.click('#am-finish');
await page.waitForTimeout(400);
tv = await tsView();
check(tv.target >= 12 && /^your song · \d:\d\d$/.test(tv.val), `LEARN stores the song's tone ("${tv.val}")`);
check(await page.evaluate(async () => (await import('./juce/index.js')).getSliderState('drive').getScaledValue()) === driveBefore, 'LEARN changes no other setting');
check(/Tone Steady now holds your song to its own overall tone/.test(tv.learn), 'Learn is back on Tone Steady with the target');

check(errors.length === 0, `no script errors (${errors.join(' | ')})`);
await browser.close();
if (failures.length) { console.log(`\n${failures.length} UI check(s) failed`); process.exit(1); }
console.log('\nAll UI checks passed');
