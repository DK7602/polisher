// Browser preview only. Inside the plug-in, JUCE provides window.__JUCE__ before this runs and
// this file does nothing. Opened in a normal browser, it simulates the plug-in so the UI can be
// designed and tested without a DAW.
(function () {
  if (typeof window.__JUCE__ !== "undefined") return;

  const sliders = {
    drive:   { start: 0,  end: 24,   skew: 1,   interval: 0.01, value: 4.5, label: "dB" },
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
  const toggles = { adaptive: true, bypass: false, autoreset: true, clipOn: true, resetOnChange: true, eqOn: true };
  const combos = { clipOversampling: { choices: ["1x", "2x", "4x", "8x"], value: 2 / 3 } };

  const send = (id, obj) => setTimeout(() => window.__JUCE__.backend.emitByBackend(id, JSON.stringify(obj)), 0);

  window.__JUCE__ = {
    initialisationData: {
      __juce__platform: [],
      __juce__functions: ["resetMeter"],
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
        if (payload.name === "resetMeter") integratedAcc = [];   // (the mock has no separate short-term history)
        send("__juce__complete", { promiseId: payload.resultId, result: true });
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
      clip: toggles.clipOn && !bypass ? +(-Math.max(0, drive - 1.5 + sliders.clipLevel.value * -0.5) * 0.5 * (0.3 + 0.7 * beat)).toFixed(1) : 0,
      mix, master,
    };
    window.__JUCE__.backend && window.__JUCE__.backend.emitByBackend("polisherMeters", JSON.stringify(frame));
  }, 1000 / 30);
})();
