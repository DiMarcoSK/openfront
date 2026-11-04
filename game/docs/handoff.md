# Handoff fields (B26: one owner per field, 0 defined twice)

Every value that crosses a module boundary, where it is defined, and who
writes it. A field appears here once; everything else reads it.

| Field | Defined in | Writer | Readers |
|---|---|---|---|
| treasury, day, selected, flags | `sim_snapshot_t` (sim_api.h, 8 B) | sim | ui, queue |
| command envelope (seq, cmd, arg) | `sim_cmd_t` (sim_api.h, 8 B) | ui (enqueue) | queue → sim_apply |
| battle outcome (22 B) | `battle_outcome_t` (sim_api.h) | sim (seal) | ui Result, save via aggregate |
| pre-battle snapshot (129 B) | `pre_battle_snapshot_t` (sim_api.h) | sim | sim Retry |
| save aggregate (63 B) | `save_aggregate_t` (save.h) | sim_export_aggregate | save_commit |
| slot header (16 B) / slot (170 B) | `save_header_t` / `save_slot_t` (save.h) | save | save, title cards via `save_slot_info_t` |
| SaveStatus | `save_status_t` + `save_status_next` (save.h, status.c) | save | ui rows (`save_row`) |
| last commit (seq, slot, day, treasury, attempts) | `save_commit_info_t` (save.h) | save | ui rows |
| trace record (12 B) | `trace_rec_t` (trace.h) | every hop | loop pump |
| queue diag | `queue_diag_t` (queue.h) | queue | overlay |
| screen | `ui_screen_t` (ui.h) | ui | loop, queue |
| sim hold | `ui_sim_hold()` (ui.h) | ui (battle panel, intro) | loop gate |
| backdrop / sprite vocabulary | `render_backdrop_t`, `RENDER_*` (render.h) | ui (calls) | render (only VRAM writer) |
| front dressing (field, enemy, god) | `front_art_t` (ui/screens.h) | ui/scene.c | ui screens |
| art asset tables | `art_assets_game.h` (generated) | tools/art/export.py | render.c only |
