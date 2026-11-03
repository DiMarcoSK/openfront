# War Table: art direction and asset pipeline

Every visual in `game/art/` is generated from Python sources in
`game/tools/art/`. Nothing is traced or downloaded, and every colour is snapped
to the DS's 15-bit palette when it is painted. Rebuild everything with:

```sh
python3 game/tools/art/export.py          # from the repo root, about 25 s
cd game/artview && make && make run       # gallery ROM, both screens, melonDS
make SCENE=5                              # artview_s5.nds boots straight into scene 5
```

## 1. Concept: the Table and the Myth

The game has two visual worlds, and the art keeps them apart on purpose.

- **The Table** is every menu: the general's campaign table. It uses the
  BRANDBOOK tokens unchanged: parchment `paper`/`surface` order sheets, `ink`
  text, enamel pins in green, amber and red. Dark **bronze** and **iron**
  fittings hold the sheets down. These are the brandbook's two support
  colours, `#C89A3C` and `#2B2F33`.
- **The Myth** is the battlefield and the gods: pixel art in dark fantasy.

Bronze connects the two. It is the metal of the panel rivets, the hoplite
shields, the boss name plates and Ares himself.

Brandbook deviations (deliberate, limited to the Myth layer): boss intro
stages use dithered light and rim lighting, as the brandbook's one
"earned drama" moment. Menus stay flat.

## 2. The battle camera: high, wide, crowded

The player ends up commanding thousands of men, so the battle is seen from
a **high camera over a field larger than the screen**.

- **Maps are 512x512** (2x2 screens), 8bpp text BGs scrolled by the camera.
  There are four: `plains` (a river valley with a ford, a road, a village
  and fields), `desert` (mesas, an oasis and a caravan track), `highlands`
  (a pass between rock walls and a tarn), and `arena` (a marble plaza ringed
  by a chasm, the gods' battlefield).
- **They are built to fit VRAM.** Wang-corner autotiling on an 8 px corner
  grid plus textures that repeat every 16 px give organic coasts and cliffs,
  while identical neighbourhoods produce identical tiles. The worst map,
  plains, uses 880 tiles of the 1024 a text BG can index (63 KB).
- **Water, lava, souls and the void animate by palette cycle.** The water
  ramp is always palette indices 0-4.
- **The arena has one tileset and five palettes** (`art_palettes.h`). Zeus
  is storm, Poseidon the sea, Hades the Styx, Ares lava and Kronos the void.

## 3. The army: squads, not loose men

At this height a soldier is about 9 px tall, so **one OAM sprite carries a
block of men**:

| Sprite | Size | Content |
|---|---|---|
| Infantry squad | 32x16 | 12 men in two ranks: spear (an upright forest of spears, levelled in the attack), blade, archer (volley) |
| Cavalry squad | 32x16 | 4 riders |
| General | 16x16 | mounted, with the standard (the shape the player looks for) |

- Every squad sheet has **four strengths: 12, 9, 6 and 3 men**. Casualties
  thin the block back rank first, then from the flank, so losses are visible
  without one sprite per man.
- Animations are idle 2, walk 4 (men step out of phase, not as one stamp)
  and attack 3.
- 128 OAM slots hold about 1,500 men on screen. The gallery's field scenes
  run 50 squads, 8 cavalry blocks and 2 generals (632 men), animated,
  at 60 fps in melonDS.
- The four civilisations (Hellenes, Rome, Carthage, Assyria) keep their
  silhouettes at this scale: crest and round shield, scutum wall, plume and
  ivory oval, cone and wicker.
- **Portraits:** the 16x16 and 32x32 units (`unit_*`) stay as close-up
  figures for menus, recruit cards and save slots.

## 4. The gods: entities on the field

Each god is a **128x128 sprite**: four 64x64 OAM blocks sharing one
16-colour palette, with one frame streamed per animation tick. That costs
8 KB of VRAM per god, whatever the length of the sheet.

| God | Walk | Attack (+ effect) | Special | Death |
|---|---|---|---|---|
| Zeus | rides his rolling storm cloud | hurls the keraunos: the bolt strikes the line | the air crackles, eyes flare | dissolves into cloud |
| Poseidon | swims, the neck swaying | rears and lunges: a wave rolls over the ranks | rises and roars | sinks |
| Hades | glides on a hem of souls | bident thrust: hands of the dead rise | raises the dead, soul aura | turns to soul dust |
| Ares | a real stride | spear thrust: the ground erupts in fire | war cry, crest and ground burn | falls to his knees, embers |
| Kronos | drifts, the time rings turning | harpe sweep with a smear: a time ring freezes men | the rings expand | implodes to a point |

- Animations: idle 4, walk 6, attack 6, special 6, hurt 2, death 6. That is
  30 frames, `ART_GOD[]` in `art_index.h`.
- **Effects** (`fx_*`, 64x64, 6 frames) use the god's own palette, so they
  cost no extra palette RAM.
- All frames face right. Facing left means swapping the two quadrant columns
  and h-flipping each block (see `tick_god` in `artview/main.c`).
- The **full-screen stages** (`boss_*`, 8bpp, with palette cycles) and the
  **intro cards** (`card_*`) are the cinematic introduction shown before each
  god fight.

## 5. Interface kit (the Table)

- **Type.** A 5x7 ledger face in 8x8 cells (ASCII 32-126, lowercase,
  tabular digits) plus a bronze-engraved display cut.
- **Panels** are 9-slice (`sheet`, `iron`, `boss`) with bronze corner
  brackets.
- **Buttons** come in four states. Text on accent is always `on-accent`.
- **Meters** for hp, morale and boss; the four threat pins (each with a
  glyph); 16 px icons; the five divine relics; the CONFIRMED stamp.
- **Battle HUD:** the bottom screen holds a **128x128 minimap of the whole
  512x512 field** with the camera frame, plus divisions (hp + morale), the
  general's orders with cooldown and the alert line.

## 6. Hardware budget (measured by `export.py`, see `art/ds/manifest.json`)

| Family | Format | VRAM |
|---|---|---|
| Battle map | 8bpp text BG 512x512, flip-reduced | 37-63 KB (tiles + 8 KB map) |
| Boss stage / screens | 8bpp text BG 256x192 | 15-44 KB |
| God | 4 x 64x64 4bpp OAM, streamed | 8 KB live (240 KB of frames in ROM) |
| Squad / general | 32x16 / 16x16 4bpp OAM | 256 B / 128 B per live sprite |
| Effect | 64x64 4bpp OAM | 2 KB live |

- Gallery layout: BG0 stills (map 0-2 KB, tiles 16-64 KB) and BG2 battle map
  (map 8-16 KB, tiles 64-128 KB) in bank A; OBJ in bank B; bottom screen in
  bank C.
- BGs sit at priority 3 so every sprite draws over the field.
- The gallery ROM is 2.4 MB, and its ARM9 image fits main RAM.

### Pipeline gotchas, measured

- grit's `-gT!` silently switches the output to a **linear bitmap**. OAM
  needs tiles, so the sheets use `-gt -Mw/-Mh` without it.
- 512x512 maps need **`-mLs`** (screenblock layout). Without it the map is
  written as flat 64-wide rows and comes out scrambled on the DS.

## 7. Files

```
tools/art/px.py          canvas, DS colour snap, masks, distance-field shading, Bayer dither
tools/art/stage.py       boss-stage helpers (sky, clouds, locks, tubes, scales, lightning)
tools/art/boss_*.py      the five intro stages (paint() + PAL + CYCLES)
tools/art/gods.py        the gods as rigged 128x128 entities: pose tables per animation
tools/art/effects.py     attack payloads (bolt, wave, soul hands, fire, time ring)
tools/art/maps.py        512x512 autotiled battle maps + arena palettes per god
tools/art/squads.py      battle-scale squads (12 men / sprite), cavalry, generals
tools/art/units.py       civ palettes, gear, 16/32 px portraits
tools/art/ui.py          tokens, panels, buttons, bars, pins, icons, relics, stamp
tools/art/font.py        5x7 ledger face + bronze display cut
tools/art/screens.py     title, battle view + HUD with minimap, boss cards, result, recruit
tools/art/export.py      indexed PNG + .grit, art_index.h, art_assets.h, art_palettes.h, manifest
art/ds/                  DS-ready output (grit inputs + generated headers)
art/preview/             previews and contact sheets
artview/                 the gallery ROM (main.c + Makefile)
```

To move an asset into the game ROM, copy its `.png` and `.grit` from
`art/ds/` into `game/data/`. The existing wildcard rule in `game/Makefile`
compiles them, and `art_index.h` carries the frame, palette and cycle tables.
