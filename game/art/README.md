# War Table — B07/B08 art handoff

Original pixel-art references for the title/save handoff (B07) and campaign
ledger (B08). Rebuild deterministically from the repository root:

```sh
python3 game/tools/gen_war_table_art.py
```

- `title-board.png` — 256×384 dual-screen composition: read-only verified-slot
  ledger above, two selectable cards and the single Continue action below.
- `campaign-board.png` — 256×192, four visible ledger rows. It intentionally
  has no map or province illustrations: each row is a decision, not a miniature
  dashboard. Need amounts remain readable text with a dashed disabled edge.
- `game/data/war_table_art.png` — 128×64, 8×8-aligned 4bpp-ready atlas: 32×32
  shield mark, four 16 px threat pins, supply pin and oilcloth stitch. The PNG
  coordinates are source of truth; grit compiles it through the existing
  wildcard build rule.

The boards use the TOKENS palette and crisp pixel construction: canvas grain
stays outside copy, seams are the only elevation, and enamel color is reserved
for action/progress (green), supplies (amber) and threat (red). Runtime text,
hit targets, animation and state remain owned by the UI modules; this art does
not change save or simulation contracts.
