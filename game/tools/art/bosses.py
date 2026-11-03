"""bosses.py - registry of the mythic boss stages (one module per god)."""

import boss_zeus
import boss_poseidon
import boss_hades
import boss_ares
import boss_kronos

BOSSES = {
    "zeus": boss_zeus,
    "poseidon": boss_poseidon,
    "hades": boss_hades,
    "ares": boss_ares,
    "kronos": boss_kronos,
}

if __name__ == "__main__":
    import sys
    from pathlib import Path
    out = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    only = sys.argv[2:] or list(BOSSES)
    for name in only:
        c = BOSSES[name].paint()
        c.save(out / f"{name}.png", 3)
        print(name, len(c.colors()), "colours")
