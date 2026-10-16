// THE DAWS: THIS FILE IS THE SAME IN ALL THREE REPOSITORIES (Mixology plugin/ui, Voxology plugin/ui, Masterology
// plugin/ui). Change it in Mixology, copy it to the other two. The DAW is a choice the three share through the suite
// link (suite/SuiteLink.h: Choice::daw), so the keys never change.
//
// How to do the everyday jobs in each DAW, in the words its own manual uses. Every tip and report line that tells you
// to do something in your DAW takes its words from here: in the interface with say("clipGain"), and in text made by
// the plug-in's engine with {daw:clipGain} tokens that fillDaw() replaces ({daw} alone = the DAW's name).
// Where a DAW can't do something (Logic can't send MIDI to an audio effect), the text says so.

export const DAWS = [
  { key: "cubase", name: "Cubase", also: "and Nuendo" },
  { key: "studioone", name: "Studio One" },
  { key: "fl", name: "FL Studio" },
  { key: "ableton", name: "Ableton Live" },
  { key: "reaper", name: "REAPER" },
  { key: "bitwig", name: "Bitwig Studio" },
  { key: "logic", name: "Logic Pro", also: "on a Mac" },
  { key: "cakewalk", name: "Cakewalk", also: "by BandLab, and Sonar" },   // BandLab's web studio can't load plug-ins
  { key: "other", name: "Other DAW" },
];

const P = {
  // What a piece of audio on a track is called.
  clip: { cakewalk: "clip", cubase: "audio event", studioone: "audio event", fl: "audio clip", ableton: "clip", reaper: "item", bitwig: "clip", logic: "region", other: "audio clip" },
  // The mixer window.
  mixer: { cakewalk: "the Console view (Alt+2)", cubase: "the MixConsole (F3)", studioone: "the Console (F3)", fl: "the Mixer (F9)", ableton: "the Mixer (Ctrl+Alt+M)", reaper: "the Mixer (Ctrl+M)",
    bitwig: "the Mix view", logic: "the Mixer (X)", other: "the mixer" },
  // The channel every track flows into.
  master: { cakewalk: "Master", cubase: "Stereo Out", studioone: "Main", fl: "Master", ableton: "Main", reaper: "Master", bitwig: "Master", logic: "Stereo Out", other: "master" },
  // Putting a plug-in on that channel (the text around it says where in the chain).
  loadOnMaster: {
    cakewalk: "open the Console view (Alt+2), find the Master bus on the right, and add it to its FX rack",
    cubase: "open the MixConsole (F3), find Stereo Out on the far right, and load it in one of its insert slots",
    studioone: "open the Console (F3), find the Main channel on the right, and add it to its Inserts",
    fl: "open the Mixer (F9), select the Master insert, and load it in one of its effect slots",
    ableton: "click the Main track and drop it into its device chain",
    reaper: "click the FX button on the Master track (View > Master track if you can't see it) and add it to the chain",
    bitwig: "select the Master track and add it to its device chain",
    logic: "open the Mixer (X), find the Stereo Out channel and load it in one of its Audio FX slots",
    other: "load it in the effects of the master channel (the one every track flows into)" },
  // A mono group / bus folds everything to the middle: how to spot and fix it.
  monoMeter: {
    cakewalk: "In Cakewalk's Console view a mono bus shows one meter instead of two: check your buses (and the Master)",
    cubase: "In Cubase's MixConsole a mono channel shows one meter instead of two: check your groups (and the Stereo Out)",
    studioone: "In Studio One's Console a mono channel shows one meter instead of two: check your buses (and the Main out)",
    fl: "In FL Studio the Mixer inserts are stereo: look for an insert with its stereo separation turned to mono, or a mono effect on it",
    ableton: "In Live, look for a Utility with Mono on, on a group or return track",
    reaper: "In REAPER, check the routing of your folder and bus tracks (2 channels to the parent) and any mono effect on them",
    bitwig: "In Bitwig, look for a utility set to mono on a group track",
    logic: "In Logic's Mixer a mono channel shows one meter instead of two: check your summing stacks and buses (and the Stereo Out)",
    other: "Look for a mono group or bus on the way (one meter instead of two, or a mono effect on it)" },
  monoBusFix: {
    cakewalk: "click its Interleave button (the mono / stereo switch on the strip) so it shows stereo",
    cubase: "find the small button at the bottom of its channel (next to the channel number): one circle = mono, two linked circles = stereo. Stop playback and click it once: it switches to stereo",
    studioone: "a mono channel has the single-circle button at the top of its strip: click it to make the channel stereo",
    fl: "Mixer inserts are always stereo: turn its stereo separation back to the middle and remove any effect that sums to mono",
    ableton: "tracks are always stereo in Live: look in its devices for a Utility with Mono on, and switch it off",
    reaper: "click its Routing button: it should send 2 channels (1/2) to its parent, with no mono effect on it",
    bitwig: "look in its devices for a utility set to mono, and switch it off",
    logic: "the small button at the bottom of its channel strip shows one circle for mono, two for stereo: click it to make it stereo",
    other: "make that channel stereo (a mono / stereo switch on it, or a mono effect to switch off)" },
  // The audio outputs of the project are a stereo pair.
  outputsStereo: {
    cakewalk: "the Master bus going to a stereo pair of outputs (its Output menu)",
    cubase: "Studio > Audio Connections > Outputs: a Stereo bus with Left and Right both connected",
    studioone: "Song > Song Setup > Audio I/O Setup > Outputs: the Main out with both Left and Right",
    fl: "the Master insert's output set to a stereo pair (for example Out 1 - Out 2)",
    ableton: "Settings > Audio > Output Config: a stereo pair switched on, and the Main track going to it",
    reaper: "the Master track going to a stereo pair of outputs (its Routing button)",
    bitwig: "Settings > Audio: a stereo output for the Master",
    logic: "the Stereo Out going to a stereo pair of outputs (Output 1-2)",
    other: "the master going out on a stereo pair (Left and Right)" },
  // A track sending to an FX channel.
  sendHow: {
    cakewalk: "the track's Sends: Insert Send to that bus, at 0 dB", cubase: "MixConsole, Sends, at 0 dB", studioone: "in the Console, the channel's Sends: add one to that FX channel, at 0 dB",
    fl: "the track's Mixer insert: turn up the send knob on the route arrow to that insert, at 0 dB", ableton: "the track's Send knob for that return track",
    reaper: "the track's Routing button: add a send to that track, at 0 dB", bitwig: "the track's Sends in the Mix view",
    logic: "the channel strip's Sends: pick that bus, at 0 dB", other: "a send from the track to that FX channel, at 0 dB" },
  // Adding Mixology Link right after Voxology on a vocal.
  linkAfterVoxology: {
    cakewalk: "right after Voxology in the track's FX rack", cubase: "in the insert slot right under Voxology (after it, still above the fader divider line)", studioone: "in the Insert right under Voxology",
    fl: "in the effect slot right under Voxology on the vocal's Mixer insert", ableton: "right after Voxology in the track's device chain",
    reaper: "right after Voxology in the track's FX chain", bitwig: "right after Voxology in the track's device chain",
    logic: "in the Audio FX slot right under Voxology", other: "right after Voxology on the same track" },
  manyAtOnce: {
    cubase: "Several at once: select the channels, hold Shift + Alt and pick Mixology Link in one empty slot (or use Q-Link). Pick a slot that is empty on all of them",
    other: "Do the same on each vocal track" },
  // Keeping the low end in the middle.
  monoLows: { cubase: "Cubase's MultiPanner or a stereo-width plug-in on those tracks", ableton: "Live's Utility with Bass Mono on, on those tracks",
    other: "a stereo-width plug-in on those tracks (width 0 below about 120 Hz)" },
  // When Mixology can't move the DAW's faders (bridges: Cubase, FL Studio, Ableton Live, for now).
  noFaderLink: {
    cubase: "Not connected to Cubase, so faders and pans can't move (Settings > Cubase Bridge).",
    fl: "Not connected to FL Studio, so faders and pans can't move (Settings > FL Studio Bridge).",
    ableton: "Not connected to Ableton Live, so faders and pans can't move (Settings > Ableton Live Bridge).",
    other: "Mixology moves faders in Cubase, FL Studio and Ableton Live (for now): untick the fader and pan rows to apply the rest, and set those by hand." },
  // Turning a recording down at the source, before any plug-in.
  clipGain: {
    cakewalk: "select the clip and lower its Clip Gain in the Inspector (Clip properties)",
    cubase: "drag the event's volume handle (the small square at the top middle of the event) down, or lower Pre-Gain in the channel's Pre section",
    studioone: "drag the event's gain handle (the small square at the top middle of the event) down, or lower Event Gain in the Inspector",
    fl: "double-click the audio clip and turn its volume down in the channel settings",
    ableton: "double-click the clip and turn its Gain down in Clip View",
    reaper: "drag the item's volume handle (the line along the top of the item) down, or lower Item volume in Item Properties (F2)",
    bitwig: "select the clip and lower its Gain in the Inspector",
    logic: "select the region and lower its Gain in the Region inspector",
    other: "lower the clip's own gain (the volume of the audio itself, before any plug-in)" },
  // Automating a control.
  automate: {
    cakewalk: "with automation: the track's W (Write) button, or draw it in an automation lane",
    cubase: "with automation: the track's W button, or draw it in an automation lane",
    studioone: "with automation: show the lanes with A, then draw it",
    fl: "with automation: right-click the control > Create automation clip",
    ableton: "with automation: show the lanes with A in Arrangement View, then draw it",
    reaper: "with an envelope: the track's Envelope button, then draw it",
    bitwig: "with automation: show the track's automation lane, then draw it",
    logic: "with automation: show the lanes with A, then draw it",
    other: "with your DAW's automation" },
  // Sending the beat to a plug-in's side-chain input (Voxology hears the beat's key through it).
  sidechain: {
    cakewalk: "on the beat track add a Send and pick \"Voxology (Sidechain)\" as its destination, at 0 dB",
    cubase: "click the side-chain button in Voxology's top bar (it lights up), then on the beat track open Sends, pick \"Voxology - Side-Chain\" and switch the send on, at 0 dB",
    studioone: "open the Side-Chain menu at the top of Voxology's window and tick the beat track",
    fl: "select the beat's Mixer insert, right-click the arrow at the bottom of the vocal's insert and pick \"Sidechain to this track\", then set Voxology's side-chain input in its wrapper settings (the gear) > Processing",
    ableton: "open Voxology's side-chain panel (the triangle in the device's title bar), switch it on and set Audio From to the beat track",
    reaper: "click the vocal track's Routing button: set it to 4 channels and add a Receive from the beat track into channels 3/4",
    bitwig: "pick the beat track in the Sidechain menu on the Voxology device",
    logic: "pick the beat track in the Side Chain menu at the top right of Voxology's window",
    other: "send the beat to Voxology's side-chain input (your DAW's manual: \"side-chain\")" },
  // Playing MIDI into Voxology (an audio effect).
  midiToVoxology: {
    cakewalk: "in Voxology's VST menu (top of its window) switch on Enable MIDI Input, then make a MIDI track and set its output to Voxology",
    cubase: "make a MIDI track and set its output to Voxology (the routing at the top of the Inspector)",
    studioone: "make an Instrument track and set its output to Voxology",
    fl: "add a MIDI Out channel and set its port to Voxology's MIDI input port (Voxology's wrapper settings, the gear)",
    ableton: "make a MIDI track and set MIDI To your vocal track, then Voxology",
    reaper: "make a track for the MIDI and add a send from it to the vocal track (MIDI only)",
    bitwig: "on the vocal track, add a Note Receiver before Voxology and pick the MIDI track as its source",
    logic: "Logic doesn't send MIDI to audio effects, so use Key AUTO (Voxology hears the key) instead",
    other: "route a MIDI track's output to Voxology" },
  // Recording a vocal in mono.
  monoTrack: {
    cakewalk: "record from one input: set the track's Input to a mono input (Left or Right), not Stereo",
    cubase: "record vocals on a mono track (Add Track > Audio > Configuration: Mono)",
    studioone: "record vocals on a mono track (Add Tracks > Format: Mono)",
    fl: "record from one input: pick a single input (for example In 1) on the vocal's Mixer insert, not a stereo pair",
    ableton: "record from one input: set the track's Audio From to a single channel (for example 1), not 1/2",
    reaper: "set the track's record input to Mono (Input: Mono > your mic's input)",
    bitwig: "set the track's input to your mic's single (mono) input",
    logic: "set the track's input format to Mono (the circle button on the channel strip)",
    other: "record vocals on a mono track (one input, not a stereo pair)" },
  // Folding a stereo recording to mono before a plug-in.
  toMono: {
    cubase: "put Cubase's MixConvert (or the Stereo Combined Panner) before Voxology, or re-import the file to a mono track",
    studioone: "bounce it to a mono track, or record it again on one",
    fl: "convert the file to mono, or record it again from one input",
    ableton: "put Live's Utility before Voxology and switch on its Mono button",
    reaper: "render it to a mono take (Item > Apply track/take FX to items as new take (mono output))",
    bitwig: "convert the file to mono, or record it again from one input",
    logic: "put Logic's Gain plug-in before Voxology and switch on its Mono button",
    other: "convert the file to mono, or record it again from one input" },
  // Repairing clipped audio.
  declipper: { cubase: "SpectraLayers (in Cubase Pro) or iZotope RX De-clip", other: "a declipper such as iZotope RX De-clip" },
  // Letting the song play straight through (no loop).
  loopOff: {
    cakewalk: "turn off Loop on the Control Bar (the loop button)",
    cubase: "turn off Cycle on the transport bar (the loop button)",
    studioone: "turn off Loop on the transport (the loop button)",
    fl: "play in Song mode (SONG on the transport), not Pattern mode",
    ableton: "turn off the Loop switch in Arrangement View (Ctrl+L)",
    reaper: "turn off Repeat on the transport (R)",
    bitwig: "turn off the loop on the transport",
    logic: "turn off Cycle in the control bar (C)",
    other: "turn off the loop on the transport" },
  // A plug-in doesn't show up.
  rescan: {
    cakewalk: "open Edit > Preferences > File > VST Settings and click Scan",
    cubase: "open Studio > VST Plug-in Manager and click Update Plug-in Information",
    studioone: "open Studio One > Options > Locations > VST Plug-ins and click Rescan",
    fl: "open Options > Manage plugins and click Find installed plugins",
    ableton: "open Settings > Plug-Ins, switch on Use VST3 Plug-In System Folders and click Rescan",
    reaper: "open Options > Preferences > Plug-ins > VST and click Re-scan",
    bitwig: "open Settings > Locations and let it rescan the VST3 folder",
    logic: "open Logic Pro > Settings > Plug-in Manager and click Reset & Rescan Selection",
    other: "rescan your plug-ins in your DAW's settings" },
  // The exported file's bit depth.
  exportDepth: {
    cakewalk: "File > Export > Audio: Bit Depth", cubase: "File > Export > Audio Mixdown: Bit Depth", studioone: "Song > Export Mixdown: Format", fl: "File > Export: Depth",
    ableton: "File > Export Audio/Video: Bit Depth", reaper: "File > Render: Format", bitwig: "File > Export Audio: Bit depth",
    logic: "File > Bounce > Project or Section: Bit Depth", other: "your DAW's export settings" },
  // A track's routing / output menu.
  trackOutput: {
    cakewalk: "the track's Output menu (Track view or Inspector)", cubase: "the track's output menu (the routing at the top of the Inspector)", studioone: "the channel's output menu (bottom of its strip in the Console)",
    fl: "the Mixer insert's route arrows", ableton: "the track's Audio To menu", reaper: "the track's Routing button",
    bitwig: "the track's output chooser (bottom of the track in the Mix view)", logic: "the channel strip's Output slot", other: "the track's output" },
};

/** The DAW's entry (the "other" one for an unknown key). */
// The DAWs Mixology can move faders in (a bridge script: Cubase's MIDI Remote, FL Studio's and Live's Python scripts).
export const BRIDGED = ["cubase", "fl", "ableton"];
export function hasBridge(key) { return BRIDGED.includes(key); }

export function dawOf(key) { return DAWS.find((d) => d.key === key) || DAWS[DAWS.length - 1]; }

/** The DAW's name as it reads in a sentence ("Cubase", "your DAW" for Other). */
export function dawName(key) { const d = dawOf(key); return d.key === "other" ? "your DAW" : d.name; }

/** How to do one job in that DAW (see P above). */
export function say(key, phrase) {
  const p = P[phrase];
  if (!p) return phrase;
  return p[dawOf(key).key] ?? p.other;
}

/** Text from the plug-in's engine with {daw} / {daw:phrase} tokens, in the words of that DAW. */
export function fillDaw(text, key) {
  if (typeof text !== "string" || text.indexOf("{daw") < 0) return text;
  return text.replace(/\{daw(?::([a-zA-Z]+))?\}/g, (m, phrase) => (phrase ? say(key, phrase) : dawName(key)));
}

/** Every phrase key (the tests check each DAW has its words). */
export const PHRASES = Object.keys(P);
