"""Kilmer's worn-armour payload for the droplet's whisper/buff bot.

The #667 gear sync renders every player's armour on other clients, but only if
their client (or bot) SENDS it: a compact per-slot descriptor on the "input"
message, which the server sanitizes, caches, and stamps into every snapshot.
The Kilmer bot on the droplet predates that sync, so Kilmer shows a bare body
under his nameplate. This module gives the bot his "of the Tenfold" set.

Wire format (see sanitize_gear in dereth_server.py):
    gear = { "<slot>": [name, class, material, dye], ... }
    the "back" slot appends two more fields: [cape? 1/0, tint]
    slots: head chest upperarm lowerarm hands legs feet shirt pants back

Bot integration — ONE line where the bot builds its position/"input" message
(after entering the world, and again after any reconnect; resending it on
every input tick is also fine — the server treats it as a cheap overwrite):

    from kilmer_gear import KILMER_GEAR
    msg = {"t": "input", "x": x, "z": z, ..., "gear": KILMER_GEAR}

KILMER_GEAR below is derived from admin_kilmer.json's armorSlots at import
time so it can never drift from the character sheet; the literal fallback is
used only if that file is missing.
"""
import json
import os

_SLOTS = ["head", "chest", "upperarm", "lowerarm", "hands",
          "legs", "feet", "shirt", "pants", "back"]

_FALLBACK = {
    "head":     ["Diadem of the Tenfold",        "heavy", "Diamond", 0],
    "chest":    ["Aegis Cuirass of the Tenfold", "heavy", "Diamond", 0],
    "upperarm": ["Pauldrons of the Tenfold",     "heavy", "Diamond", 0],
    "lowerarm": ["Vambraces of the Tenfold",     "heavy", "Diamond", 0],
    "hands":    ["Gauntlets of the Tenfold",     "heavy", "Diamond", 0],
    "legs":     ["Greaves of the Tenfold",       "heavy", "Diamond", 0],
    "feet":     ["Sabatons of the Tenfold",      "heavy", "Diamond", 0],
    "shirt":    ["Shirt of the Tenfold",         "light", "Silk",    0],
    "pants":    ["Breeches of the Tenfold",      "light", "Silk",    0],
    "back":     ["Kilmer's Cape",                "light", "Diamond", 0, 1, 1846380],
}


def gear_from_char(char):
    """Build the wire-format gear dict from a character save's armorSlots."""
    out = {}
    for s in _SLOTS:
        it = (char.get("armorSlots") or {}).get(s)
        if not it or not it.get("name"):
            continue
        e = [it["name"], it.get("at", "medium"), it.get("mat", "Steel"), int(it.get("dye") or 0)]
        if s == "back":
            e.append(1 if it.get("cape") else 0)
            e.append(int(it.get("tint") if it.get("tint") is not None else (it.get("dye") or 0)))
        out[s] = e
    return out


def _load():
    path = os.path.join(os.path.dirname(__file__), "admin_kilmer.json")
    try:
        with open(path, encoding="utf-8") as f:
            gear = gear_from_char(json.load(f))
        return gear or dict(_FALLBACK)
    except Exception:
        return dict(_FALLBACK)


KILMER_GEAR = _load()

if __name__ == "__main__":
    print(json.dumps(KILMER_GEAR, indent=2))
