// THE SONG'S STYLES: THIS FILE IS THE SAME IN ALL THREE REPOSITORIES (Mixology plugin/ui, Voxology plugin/ui,
// Masterology plugin/ui). Change it in Mixology, copy it to the other two. The style picked in any of them is shared
// through the suite link (suite/SuiteLink.h: SongStyle), so the keys ("genre.style") must never change.
//
// Mix styles: pick a genre, then a style. Each style is a tonal target (bass weight, low-mid scoop, top sheen,
// all in dB against a flat, tilted reference) for the Tone page and Analyze Mix; the genre also sets the
// level targets Balance and Auto Mix aim for (MixPlanner).

const g = (id, name, icon, colour, styles) => ({ id, name, icon, colour, styles: styles.map(([sid, sname, bass, scoop, air, about]) => ({ id: sid, name: sname, bass, scoop, air, about })) });

export const GENRES = [
  g("hiphop", "Hip Hop", "mic", "#ff9a1f", [
    ["modern", "Modern Clean", 4, 1.5, 2, "Full low end, clean mids, crisp top. Vocal on top of the beat."],
    ["boombap", "Boom Bap", 3, 0, -1, "Punchy drums, warm mids, rolled-off top like vinyl."],
    ["westcoast", "West Coast", 4.5, 1, 1.5, "Bouncy kick and bass, clear claps, smooth top."],
    ["gfunk", "G-Funk", 4.5, 0.5, 1, "Warm, round bass and synth leads, softer top."],
    ["oldschool", "Old School", 3, 0.5, 0, "Breakbeat drums up front, mid-heavy, less sub."],
    ["lofi", "Lo-Fi", 2.5, 0, -2.5, "Soft, warm and dusty. Nothing sharp, nothing loud."],
    ["poprap", "Pop Rap", 3, 1.5, 3, "Forward vocal, bright and radio-ready balance."],
  ]),
  g("trap", "Trap & Drill", "bass", "#ff2d2d", [
    ["hard808", "Hard 808", 6, 2, 2.5, "Big 808, scooped low mids, bright hats."],
    ["melodic", "Melodic", 5, 1.5, 2.5, "Deep 808 under airy melodies and a smooth vocal."],
    ["drill", "Drill", 5.5, 2.5, 0.5, "Sliding 808s, dark keys, tight hats."],
    ["rage", "Rage", 6, 2.5, 3, "Distorted 808s, loud synths, aggressive top."],
    ["plugg", "Plugg", 4.5, 1.5, 2, "Soft bells and pads over a round 808."],
  ]),
  g("rnb", "R&B & Soul", "vox", "#ff36e1", [
    ["smooth", "Smooth R&B", 3.5, 1, 1.5, "Silky vocals, soft low end, lush space."],
    ["90s", "90s R&B", 3.5, 0.5, 0.5, "Warm, round and full. Vocals and harmonies in front."],
    ["alt", "Alternative R&B", 4, 1.5, 2, "Dark, spacious, deep sub, airy vocals."],
    ["neosoul", "Neo-Soul", 3, 0, 0.5, "Live bass and keys, warm mids, relaxed top."],
    ["soul", "Classic Soul", 1.5, -0.5, 0, "Band in a room: warm mids, gentle top, little sub."],
    ["funk", "Funk", 3, 0.5, 1.5, "Tight kick and slap bass, crisp guitars and horns."],
  ]),
  g("pop", "Pop", "star", "#19d3ff", [
    ["modern", "Modern", 3, 1.5, 3, "Polished and bright. Vocal clear on top of everything."],
    ["dance", "Dance", 4, 1.5, 3, "Four-on-the-floor kick and bass, shiny top."],
    ["synth", "Synth / 80s", 2.5, 1, 2.5, "Gated drums, big synths, bright and wide."],
    ["indie", "Indie", 2, 0.5, 1.5, "Natural and warm, a little lo-fi charm."],
    ["acoustic", "Acoustic", 1.5, 0.5, 2, "Guitar and piano led, open and clear."],
    ["ballad", "Ballad", 2, 0.5, 2, "Piano and strings, vocal right up front."],
    ["kpop", "K-Pop", 3.5, 1.5, 3.5, "Punchy, dense and very bright. Every part clear."],
    ["hyperpop", "Hyperpop", 4, 2, 4, "Extreme: huge low end, sizzling top."],
  ]),
  g("rock", "Rock", "guitar", "#ff4a2a", [
    ["classic", "Classic", 2, 0, 0.5, "Warm guitars, natural drums, vocal in the band."],
    ["hard", "Hard", 2.5, 1, 2, "Big drums and crunchy guitars, punchy low end."],
    ["alternative", "Alternative", 2.5, 1, 1.5, "Layered guitars, full drums, clear vocal."],
    ["indie", "Indie", 1.5, 0.5, 1, "Raw and roomy, guitars a bit jangly."],
    ["punk", "Punk / Pop Punk", 2, 1.5, 2.5, "Fast and bright, guitars wide, vocal on top."],
    ["grunge", "Grunge", 2, 0, 0.5, "Thick, muddy-on-purpose guitars, heavy drums."],
    ["metal", "Metal", 3, 2.5, 2.5, "Tight kick, scooped guitars, aggressive top."],
    ["modernmetal", "Modern Metal", 3.5, 3, 3, "Very tight low end, scooped mids, sharp attack."],
    ["soft", "Soft / Light", 1.5, 0, 1, "Gentle, warm and smooth. Nothing harsh."],
    ["blues", "Blues Rock", 2, -0.5, 0, "Warm overdriven guitars, live room feel."],
  ]),
  g("country", "Country", "hat", "#e3b260", [
    ["modern", "Modern", 3, 1, 2.5, "Polished, punchy drums, bright acoustic and vocal."],
    ["pop", "Country Pop", 3, 1, 3, "Radio-ready: pop low end with country instruments."],
    ["classic", "Classic", 1.5, 0, 0.5, "Natural band sound, warm vocal, steel and fiddle."],
    ["southern", "Outlaw / Southern", 2.5, 0.5, 1, "Gritty guitars, big drums, rough edges."],
    ["bluegrass", "Bluegrass", 0.5, 0, 1.5, "All acoustic: clear, open, almost no sub."],
    ["americana", "Americana", 1.5, 0, 1, "Earthy and warm, roots instruments up close."],
  ]),
  g("gospel", "Gospel", "choir", "#ffd21f", [
    ["contemporary", "Contemporary", 3.5, 1, 2, "Big low end, polished choir and lead vocal."],
    ["traditional", "Traditional", 2, 0, 1, "Organ, piano and choir in a warm room."],
    ["choir", "Mass Choir", 1.5, 0.5, 2, "Choir is the star: wide, clear and full."],
    ["urban", "Urban Gospel", 4.5, 1.5, 2, "Hip hop drums and bass under the message."],
  ]),
  g("worship", "Worship", "sun", "#ffe9a8", [
    ["modern", "Modern", 3, 1, 2.5, "Pads, big drums, clear lead vocal, lots of space."],
    ["live", "Live / Big Room", 3.5, 1.5, 2, "Arena sound: huge drums and crowd, wide pads."],
    ["acoustic", "Acoustic", 1.5, 0.5, 2, "Guitar and vocal, intimate and clear."],
    ["hymns", "Hymns", 1, 0, 1, "Piano or organ and voices, natural and warm."],
  ]),
  g("electronic", "Electronic", "wave", "#2f86ff", [
    ["house", "House", 4, 1.5, 2.5, "Steady kick and bass, clean hats, groove first."],
    ["edm", "EDM / Big Room", 4.5, 2, 3.5, "Huge kick, wide supersaws, bright drops."],
    ["techno", "Techno", 4, 1.5, 2, "Driving kick, dark and hypnotic, controlled top."],
    ["dnb", "Drum & Bass", 4.5, 1.5, 3, "Fast breaks, deep sub, crisp snare."],
    ["dubstep", "Dubstep", 5.5, 2, 3, "Massive sub, aggressive mid bass, sharp top."],
    ["futurebass", "Future Bass", 4.5, 2, 3.5, "Big chords, punchy drums, shimmering top."],
    ["ambient", "Ambient", 2, 0.5, 1.5, "Soft, wide and calm. No sharp edges."],
  ]),
  g("latin", "Latin", "perc", "#ff7a12", [
    ["reggaeton", "Reggaeton", 5, 1.5, 2.5, "Dembow groove, deep bass, forward vocal."],
    ["pop", "Latin Pop", 3.5, 1, 3, "Bright and danceable, vocal on top."],
    ["salsa", "Salsa / Tropical", 2, 0, 2, "Horns, piano and percussion, live and bright."],
    ["bachata", "Bachata", 2.5, 0.5, 2, "Clean guitars, warm bass, romantic vocal."],
    ["regional", "Regional Mexican", 2, 0, 1.5, "Brass and guitars up front, natural low end."],
  ]),
  g("afro", "Afro & Caribbean", "kick", "#3dff8a", [
    ["afrobeats", "Afrobeats", 4, 1, 2.5, "Bouncy percussion, warm bass, smooth vocal."],
    ["amapiano", "Amapiano", 5, 1, 2, "Log drums and deep bass, airy keys."],
    ["reggae", "Reggae", 4, 0.5, 0.5, "Heavy bass, warm skank, laid-back top."],
    ["dancehall", "Dancehall", 4.5, 1, 2, "Punchy riddim, deep bass, vocal in front."],
  ]),
  g("jazz", "Jazz & Blues", "keys", "#b04dff", [
    ["jazz", "Jazz", 1, 0, 0.5, "Natural and dynamic: a band in a room."],
    ["smooth", "Smooth Jazz", 2, 0.5, 1.5, "Polished, warm bass, silky sax and keys."],
    ["blues", "Blues", 1.5, -0.5, 0, "Warm guitar and voice, live and gritty."],
  ]),
  g("folk", "Folk & Acoustic", "note", "#9adf5a", [
    ["songwriter", "Singer-Songwriter", 1, 0.5, 1.5, "Voice and guitar or piano, intimate and clear."],
    ["folk", "Folk", 0.5, 0, 1, "Acoustic instruments, natural and open."],
    ["indiefolk", "Indie Folk", 1, 0.5, 1.5, "Warm, roomy, layered acoustic textures."],
  ]),
  g("cinematic", "Cinematic", "film", "#c0c8d6", [
    ["trailer", "Epic Trailer", 4, 1, 2, "Huge hits and low end, wide and powerful."],
    ["orchestral", "Orchestral", 1, 0, 1, "Natural orchestra: wide, deep, dynamic."],
    ["score", "Soft Score", 1.5, 0.5, 1, "Gentle, spacious and warm under dialogue."],
  ]),
];

// Flat list: every style with its genre and its full name ("Rock - Metal").
export const STYLES = GENRES.flatMap((genre) => genre.styles.map((s) => ({
  ...s, key: `${genre.id}.${s.id}`, genre, fullName: `${genre.name} - ${s.name}`,
})));

export const DEFAULT_STYLE = "hiphop.modern";

// Projects saved by v0.1 stored the style as a number (its place in the old list of eight).
const LEGACY = ["hiphop.modern", "trap.hard808", "hiphop.gfunk", "hiphop.boombap", "trap.drill", "rnb.smooth", "hiphop.poprap", "hiphop.westcoast"];

export function styleByKey(key) {
  if (typeof key === "number") key = LEGACY[key] || DEFAULT_STYLE;
  return STYLES.find((s) => s.key === key) || STYLES.find((s) => s.key === DEFAULT_STYLE);
}
