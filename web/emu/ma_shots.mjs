// SPDX-License-Identifier: GPL-3.0-only
// MANTA's screens as the firmware draws them (build/ma_emu/manta.wasm, web/emu/ma_web.c): the README's animation
// and the docs' screenshots, with the real sound behind the scopes.
//   node web/emu/ma_shots.mjs build/ma_emu/manta.wasm OUTDIR
// Writes OUTDIR/anim_NNNN.ppm (the tour: SCOPES, PLAY, the instrument pages, an EDIT) and OUTDIR/NAME.ppm stills;
// tools/ma_shots.sh turns them into docs/manta-play.gif and docs/screens/*.png.
import fs from "fs";

const bytes = fs.readFileSync(process.argv[2] || "build/ma_emu/manta.wasm");
const out = process.argv[3] || "build/ma_shots";
fs.mkdirSync(out, { recursive: true });
const stub = new Proxy({}, { get: () => () => 0 });
const { instance } = await WebAssembly.instantiate(bytes, { wasi_snapshot_preview1: stub });
const w = instance.exports;
const B = { FX: 0, SCL: 1, ENV: 2, LFO: 3, EDIT: 4, GLO: 5, HOME: 6, SAVE: 7, ARP: 8, SEQ: 9, PLAY: 10, REC: 11 };
const EN = { SELECT: 0, ALGO: 1, PRESETS: 2, K1: 3, K2: 4, K3: 5, K4: 6 };

const run = (sec) => { for (let i = 0; i < (sec * 44100) / 512; i++) w.web_render(512); };
const ppm = (name) => {
  const fb = new Uint16Array(w.memory.buffer, w.web_screen(), 240 * 240);
  const px = Buffer.alloc(240 * 240 * 3);
  for (let i = 0; i < fb.length; i++) {
    const c = ((fb[i] >> 8) | (fb[i] << 8)) & 0xffff;
    px[i * 3] = ((c >> 11) & 31) * 255 / 31;
    px[i * 3 + 1] = ((c >> 5) & 63) * 255 / 63;
    px[i * 3 + 2] = (c & 31) * 255 / 31;
  }
  fs.writeFileSync(`${out}/${name}.ppm`, Buffer.concat([Buffer.from("P6\n240 240\n255\n"), px]));
};
let frame = 0;
const film = (sec, fps = 12) => { for (let k = 0; k < sec * fps; k++) { run(1 / fps); ppm(`anim_${String(frame++).padStart(4, "0")}`); } };
const press = (b) => { w.web_buttons(1 << b); run(0.04); w.web_buttons(0); run(0.04); };
const keys = (mask, sec) => { w.web_keys(mask); run(sec); w.web_keys(0); };

w._initialize();
w.web_nor_erase();
w.web_boot();
run(1.5);
press(B.PLAY);
run(6);                                       // (the intro's groove in, the scopes alive)
film(3.5);                                    // SCOPES: the band
ppm("scopes");
press(B.HOME);                                // PLAY
film(2.5);
ppm("play");
press(B.HOME);                                // PIANO
film(2);
ppm("piano");
press(B.HOME);                                // BASS
film(1.6);
ppm("bass");
press(B.HOME);                                // HORNS
film(1.6);
ppm("horns");
press(B.HOME);                                // PERC
film(1.6);
ppm("perc");
press(B.HOME);                                // SCOPES again, then the piano's notes written over on the keys
press(B.HOME);
press(B.HOME);
press(B.EDIT);
film(0.5);
for (let k = 0; k < 4; k++) {                 // a chord a beat, held
  w.web_keys((1 << 7) | (1 << 11) | (1 << 14));
  film(0.25);
  w.web_keys(0);
  film(0.25);
}
ppm("piano_edit");
press(B.EDIT);                                // saved: it plays them
film(1.5);
press(B.HOME); press(B.HOME); press(B.HOME); press(B.HOME); press(B.HOME);   // round to SCOPES
film(1.5);
console.log(`ma_shots: ${frame} frames and the stills in ${out}`);
