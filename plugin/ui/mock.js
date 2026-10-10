// Browser preview only. Inside the plug-in, JUCE provides window.__JUCE__ before this runs and
// this file does nothing. Opened in a normal browser, it simulates the plug-in so the UI can be
// designed and tested without a DAW.
(function () {
  if (typeof window.__JUCE__ !== "undefined") return;

  const sliders = {
    drive:   { start: 0,  end: 24,   skew: 1,   interval: 0.01, value: 4.5, label: "dB" },
    inTrim:  { start: -24, end: 0,   skew: 1,   interval: 0.1,  value: 0,   label: "dB" },
    ceiling: { start: -3, end: 0,    skew: 1,   interval: 0.01, value: -1,  label: "dBTP" },
    release: { start: 10, end: 1000, skew: 0.4, interval: 0.1,  value: 120, label: "ms" },
    clipLevel: { start: -9, end: 3, skew: 1, interval: 0.01, value: 0, label: "dB" },
    clipSoftness:  { start: 0, end: 100, skew: 1, interval: 0.1, value: 50, label: "%" },
  };
  // Tonal EQ: gain -9..+9 dB; frequency ranges with JUCE's setSkewForCentre(geometric centre).
  [[45, 20, 120], [100, 50, 250], [350, 150, 800], [1000, 500, 2500], [2500, 1200, 5000], [5000, 2500, 10000], [12000, 6000, 20000]]
    .forEach(([def, lo, hi], i) => {
      sliders["eqGain" + (i + 1)] = { start: -9, end: 9, skew: 1, interval: 0.01, value: 0, label: "dB" };
      const skew = Math.log(0.5) / Math.log((Math.sqrt(lo * hi) - lo) / (hi - lo));
      sliders["eqFreq" + (i + 1)] = { start: lo, end: hi, skew, interval: 0.1, value: def, label: "Hz" };
    });
  for (const k of ["imgLow", "imgMid", "imgHigh"]) sliders[k] = { start: 0, end: 200, skew: 1, interval: 0.1, value: 100, label: "%" };
  [[120, 40, 300], [1000, 300, 3000], [6000, 3000, 12000]].forEach(([def, lo, hi], i) => {
    const skew = Math.log(0.5) / Math.log((Math.sqrt(lo * hi) - lo) / (hi - lo));
    sliders["dynX" + (i + 1)] = { start: lo, end: hi, skew, interval: 1, value: def, label: "Hz" };
  });
  [30, 20, 10, 5].forEach((atk, i) => {
    const b = i + 1;
    sliders["dynThr" + b] = { start: -48, end: 0, skew: 1, interval: 0.1, value: -20, label: "dB" };
    sliders["dynRatio" + b] = { start: 1, end: 10, skew: 0.5, interval: 0.01, value: 1, label: "" };
    sliders["dynAtk" + b] = { start: 0.5, end: 100, skew: 0.4, interval: 0.1, value: atk, label: "ms" };
    sliders["dynRel" + b] = { start: 20, end: 1000, skew: 0.4, interval: 1, value: [200, 150, 120, 100][i], label: "ms" };
    sliders["dynGain" + b] = { start: -12, end: 12, skew: 1, interval: 0.1, value: 0, label: "dB" };
  });
  sliders.satDrive = { start: 0, end: 18, skew: 1, interval: 0.1, value: 0, label: "dB" };
  sliders.satMix = { start: 0, end: 100, skew: 1, interval: 0.1, value: 100, label: "%" };
  sliders.bassMono = { start: 0, end: 250, skew: 1, interval: 1, value: 0, label: "Hz" };
  sliders.bassTame = { start: 0, end: 100, skew: 1, interval: 0.1, value: 0, label: "%" };
  sliders.bassPunch = { start: 0, end: 100, skew: 1, interval: 0.1, value: 0, label: "%" };
  sliders.ulAmount = { start: 0, end: 100, skew: 1, interval: 0.1, value: 75, label: "%" };
  sliders.ulLength = { start: 5, end: 50, skew: 0.6, interval: 0.1, value: 15, label: "ms" };
  sliders.tsAmount = { start: 0, end: 100, skew: 1, interval: 0.1, value: 40, label: "%" };
  sliders.tsRange = { start: 1, end: 6, skew: 1, interval: 0.1, value: 3, label: "dB" };
  sliders.rtAmount = { start: 0, end: 100, skew: 1, interval: 0.1, value: 50, label: "%" };
  sliders.rtSens = { start: 0, end: 100, skew: 1, interval: 0.1, value: 50, label: "%" };
  sliders.rtRelease = { start: 20, end: 300, skew: 0.5, interval: 1, value: 80, label: "ms" };
  for (const k of ["stemVoc", "stemDrums", "stemBass", "stemOther"]) sliders[k] = { start: -12, end: 12, skew: 1, interval: 0.1, value: 0, label: "dB" };
  const toggles = { adaptive: true, bypass: false, autoreset: true, clipOn: true, resetOnChange: true, eqOn: true, bassOn: true, levelMatch: false, imgOn: true, dynOn: true, dynAdaptive: true, satOn: true, stemOn: true, tsOn: true, rtOn: true, ulOn: true };
  const combos = {
    clipOversampling: { choices: ["1x", "2x", "4x", "8x"], value: 2 / 3 },
    satMode: { choices: ["Tape", "Tube", "Clip"], value: 0 },
    ulFocus: { choices: ["Full", "Kick", "Snap"], value: 0 },
    tsSpeed: { choices: ["Slow", "Medium", "Fast"], value: 0.5 },
    rtFocus: { choices: ["Full", "Boxiness", "Harshness"], value: 0 },
    amGenre: { choices: ["Trap / Hip-Hop", "R&B", "Pop", "EDM", "Rock", "Acoustic"], value: 0 },
    amIntensity: { choices: ["Gentle", "Balanced", "Punchy"], value: 0.5 },
    amTarget: { choices: ["Stream -14", "Loud -9", "Club -7"], value: 0.5 },
  };
  // Simulated Auto-Master (listening runs 3x faster than the real 12 s).
  const am = { state: 0, progress: 0, undo: false, version: 0, report: "", snapshot: null };
  const setSlider = (name, v) => { sliders[name].value = v; send("__juce__slider" + name, { eventType: "valueChanged", value: v }); };
  const setToggle = (name, v) => { toggles[name] = v; send("__juce__toggle" + name, { eventType: "valueChanged", value: v }); };
  function amFinish() {
    am.snapshot = { sliders: JSON.parse(JSON.stringify(sliders)), toggles: { ...toggles } };
    const values = { eqGain1: 0, eqGain2: 0, eqGain3: -1.4, eqGain4: 0, eqGain5: 0, eqGain6: 0.8, eqGain7: 1.6,
                     bassMono: 100, bassTame: 40, bassPunch: 25, clipOn: 1, clipLevel: -1, clipSoftness: 50, drive: 9.4, ceiling: -1, release: 120 };
    for (const [k, v] of Object.entries(values)) (k === "clipOn" ? setToggle(k, !!v) : setSlider(k, v));
    const R = (module, control, value, why) => ({ module, control, value, why });
    am.report = JSON.stringify({
      ok: true, stemSuggest: [2, 0, 0, 0], genre: "Trap / Hip-Hop", intensity: "Balanced", target: "Loud \u22129", values,
      summary: "Listened to 12 s of the loudest part. Your mix measured \u221217.8 LUFS with peaks at \u22120.4 dBTP. The master now measures \u22129.0 LUFS with true peak \u22121.0 dBTP. The full song will read about 1 LU lower than this loud section, which is normal.",
      notes: ["FIX IT IN THE MIX: your mix is 7.4 dB higher in the low mids (180\u2013600 Hz) than typical Trap / Hip-Hop masters. The master EQ only fixes part of that; adjusting the main source in the mix (often guitars, pads or a boomy vocal) will sound better."],
      reasons: [
        R("eq", "Low Mid", "\u22121.4 dB", "Your mix has about 7.4 dB more low mids (180\u2013600 Hz) than typical Trap / Hip-Hop masters, so a 1.4 dB cut closes part of the gap. Less mud and boxiness, so the mix reads clearer."),
        R("eq", "Presence", "+0.8 dB", "Your mix has about 3.1 dB less presence (3.5\u20138 kHz) than typical Trap / Hip-Hop masters, so a 0.8 dB boost closes part of the gap. More edge and definition on vocals and hi-hats."),
        R("eq", "Air", "+1.6 dB", "Your mix has about 6.2 dB less air (above 8 kHz) than typical Trap / Hip-Hop masters, so a 1.6 dB boost closes part of the gap. More sheen and openness on top."),
        R("eq", "Other bands", "0 dB", "Low End, Bass, Mid and Upper Mid are already close to the Trap / Hip-Hop balance, so they stay flat."),
        R("stem", "Vocals", "try +2.0 dB", "The model hears your vocal at \u22125.2 dB against the whole mix; in Trap / Hip-Hop masters it usually sits around \u22123.0 dB, so yours sits a little low under the beat. This is a suggestion only: Auto-Master never moves Stem EQ. Press Apply in Stem EQ's Learn to try it, then compare with MATCH. Vocal level is taste, so trust your ears over this number."),
        R("bass", "Mono Below", "100 Hz", "Your bass is 42 % stereo below 150 Hz. Club systems and phones play bass in mono, so stereo bass partly cancels there. Making it mono below 100 Hz keeps it solid everywhere."),
        R("bass", "Sub Tame", "40 %", "Your loudest bass moments rise about 6.3 dB above the typical bass level, so some 808 notes leap out. Sub Tame evens those notes out."),
        R("bass", "Punch", "25 %", "Your low end is fairly sustained, so kicks can get lost under it. Punch adds thump to the start of each kick (it spots kicks by their click), so 808 notes and the sub aren't turned up."),
        R("clip", "Soft Clipper", "On", "Your mix has 17.4 dB between its peaks and its average loudness (spiky drums). Shaving the sharpest drum peaks before the Maximizer gets you loud with less limiting."),
        R("clip", "Clip Level", "\u22121.0 dB", "Clipping starts 1.0 dB below the ceiling for the Loud \u22129 target; the level comes back up so peaks still use the full headroom."),
        R("max", "Drive", "+9.4 dB", "+9.4 dB of Drive lands this part at \u22129.0 LUFS, your Loud \u22129 target. Holding the peaks down costs about 2.1 dB of loudness, within the Balanced limit of 3.5 dB, so the drums keep their punch."),
        R("max", "Ceiling", "\u22121.0 dBTP", "Leaves room for the MP3/AAC conversion streaming services do, so the streamed version won't clip."),
      ],
    });
    am.undo = true; am.version++; am.state = 0;
  }
  setInterval(() => {
    if (am.state === 1) { am.progress = Math.min(1, am.progress + 0.1 / 4); if (am.progress >= 1) { am.state = 2; setTimeout(amFinish, 900); } }
  }, 100);

  const send = (id, obj) => setTimeout(() => window.__JUCE__.backend.emitByBackend(id, JSON.stringify(obj)), 0);

  window.__JUCE__ = {
    initialisationData: {
      __juce__platform: [],
      __juce__functions: ["resetMeter", "startAutoMaster", "cancelAutoMaster", "undoAutoMaster", "getAutoMasterReport"],
      __juce__registeredGlobalEventIds: [],
      __juce__sliders: Object.keys(sliders),
      __juce__toggles: Object.keys(toggles),
      __juce__comboBoxes: Object.keys(combos),
    },
    postMessage(message) {
      const { eventId, payload } = JSON.parse(message);
      if (eventId.startsWith("__juce__slider")) {
        const name = eventId.slice("__juce__slider".length), s = sliders[name];
        if (payload.eventType === "requestInitialUpdate") {
          send(eventId, { eventType: "propertiesChanged", start: s.start, end: s.end, skew: s.skew, name, label: s.label,
                          numSteps: 1000, interval: s.interval, parameterIndex: 0 });
          send(eventId, { eventType: "valueChanged", value: s.value });
        } else if (payload.eventType === "valueChanged") s.value = payload.value;
      } else if (eventId.startsWith("__juce__toggle")) {
        const name = eventId.slice("__juce__toggle".length);
        if (payload.eventType === "requestInitialUpdate") send(eventId, { eventType: "valueChanged", value: toggles[name] });
        else if (payload.eventType === "valueChanged") toggles[name] = payload.value;
      } else if (eventId.startsWith("__juce__comboBox")) {
        const name = eventId.slice("__juce__comboBox".length), c = combos[name];
        if (payload.eventType === "requestInitialUpdate") {
          send(eventId, { eventType: "propertiesChanged", name, parameterIndex: 0, choices: c.choices });
          send(eventId, { eventType: "valueChanged", value: c.value });
        } else if (payload.eventType === "valueChanged") c.value = payload.value;
      } else if (eventId === "__juce__invoke") {
        let result = true;
        if (payload.name === "resetMeter") integratedAcc = [];   // (the mock has no separate short-term history)
        if (payload.name === "startAutoMaster") { am.state = 1; am.progress = 0; }
        if (payload.name === "cancelAutoMaster" && am.state === 1) am.state = 0;
        if (payload.name === "getAutoMasterReport") result = am.report;
        if (payload.name === "undoAutoMaster") {
          result = am.undo;
          if (am.snapshot) {
            for (const [k, v] of Object.entries(am.snapshot.sliders)) setSlider(k, v.value);
            for (const [k, v] of Object.entries(am.snapshot.toggles)) setToggle(k, v);
          }
          am.undo = false; am.report = ""; am.version++;
        }
        send("__juce__complete", { promiseId: payload.resultId, result });
      }
    },
  };

  // --- fake audio -------------------------------------------------------------------------
  let t = 0, integratedAcc = [];
  const shape = (i, n) => {           // a plausible tilted mix spectrum
    const f = 20 * Math.pow(1000, i / (n - 1));
    const lo = f < 40 ? -10 * Math.log2(40 / f) : 0;
    const hi = f > 12000 ? -14 * Math.log2(f / 12000) : 0;
    return -38 + lo + hi + 3 * Math.sin(i / 7) + (f > 60 && f < 120 ? 5 : 0);
  };
  setInterval(() => {
    t += 1 / 30;
    const bypass = toggles.bypass;
    const drive = bypass ? 0 : sliders.drive.value;
    const beat = Math.pow(Math.max(0, Math.sin(t * Math.PI * 2 * 1.2)), 8);   // kick every ~0.8 s
    const gr = bypass ? 0 : -Math.max(0, drive - 2.5) * (0.35 + 0.65 * beat);
    const base = -16.5 + 1.5 * Math.sin(t / 6);
    const loud = base + 11 * (1 - Math.exp(-drive / 11));   // diminishing returns, like a real limiter
    integratedAcc.push(loud); if (integratedAcc.length > 3000) integratedAcc.shift();
    const mix = [], master = [];
    for (let i = 0; i < 96; i++) {
      const v = shape(i, 96) + (Math.random() - 0.5) * 2.5 + 4 * beat * (i < 20 ? 1 : 0.2);
      mix.push(+v.toFixed(1));
      master.push(+(v + drive * 0.72).toFixed(1));
    }
    const frame = {
      momentary: +(loud + 2 * beat).toFixed(1),
      shortTerm: +loud.toFixed(1),
      integrated: +(integratedAcc.reduce((a, b) => a + b, 0) / integratedAcc.length).toFixed(1),
      range: 6.8,
      truePeak: bypass ? -0.4 : +Math.min(sliders.ceiling.value, -3 + drive).toFixed(1),
      peakRecent: bypass ? -0.6 : +Math.min(sliders.ceiling.value, -4 + drive + 2 * beat).toFixed(1),
      peakL: +(-7 + drive * 0.5 + 5 * beat).toFixed(1),
      peakR: +(-7.5 + drive * 0.5 + 5 * beat).toFixed(1),
      gr: +(toggles.clipOn ? gr * 0.55 : gr).toFixed(1),
      inShort: +base.toFixed(1), inIntegrated: -16.5, sr: 48000,
      tame: toggles.bassOn && sliders.bassTame.value > 0 ? +(-sliders.bassTame.value / 100 * 5 * beat).toFixed(1) : 0,
      punch: toggles.bassOn && sliders.bassPunch.value > 0 ? +(sliders.bassPunch.value / 100 * 4 * beat).toFixed(1) : 0,
      bassWidth: 42, corr: +(0.55 - (sliders.imgMid.value + sliders.imgHigh.value - 200) / 400).toFixed(2), matchDb: +(-(11 * (1 - Math.exp(-drive / 11)))).toFixed(1),
      amState: am.state, amProgress: +am.progress.toFixed(2), amHearing: true, amUndo: am.undo, amReport: am.version,
      clip: toggles.clipOn && !bypass ? +(-Math.max(0, drive - 1.5 + sliders.clipLevel.value * -0.5) * 0.5 * (0.3 + 0.7 * beat)).toFixed(1) : 0,
      dynGr: [1, 2, 3, 4].map((b) => {
        const r = sliders["dynRatio" + b].value;
        const over = Math.max(0, base + 8 + 6 * beat - sliders["dynThr" + b].value - 6 * b);
        return toggles.dynOn && !bypass && r > 1.005 ? +(-over * (1 - 1 / r) * 0.5).toFixed(1) : 0;
      }),
      rtCut: toggles.rtOn && !bypass && sliders.rtAmount.value > 0.5 ? +(-(1.5 + 3 * beat) * sliders.rtAmount.value / 50).toFixed(1) : 0, rtHz: 2870,
      tsDb: toggles.tsOn && !bypass && sliders.tsAmount.value > 0.5 ? +(-(0.8 + 1.4 * beat) * sliders.tsAmount.value / 40).toFixed(1) : 0, tsHz: 310,
      ulBoost: toggles.ulOn && !bypass && sliders.ulAmount.value > 0.5 ? +(5 * beat * sliders.ulAmount.value / 75).toFixed(1) : 0, ulSquash: 7.8,
      stemShare: [38 + Math.round(4 * beat), 27 - Math.round(3 * beat), 14, 21 - Math.round(beat)],
      satHarm: toggles.satOn && !bypass && sliders.satDrive.value > 0.05 && sliders.satMix.value > 0.05
        ? +(-62 + sliders.satDrive.value * 2.4 + 20 * Math.log10(sliders.satMix.value / 100 + 1e-3) + 3 * beat).toFixed(1) : -100,
      mix, master,
    };
    window.__JUCE__.backend && window.__JUCE__.backend.emitByBackend("polisherMeters", JSON.stringify(frame));
  }, 1000 / 30);
})();
