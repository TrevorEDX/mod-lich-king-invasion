# mod-lich-king-invasion (AzerothCore)

Every few hours the Lich King tears open a **Death Gate portal at the start of the bridge outside Stormwind** and marches into the city with his Scourge army. He and his minions attack **every player (Alliance and Horde) and every NPC** in their way: guards, vendors, quest givers, even King Varian. The invasion only ends when someone kills him.

## What happens

1. **Warning** (2 min before): a server-wide message says the Scourge is gathering outside Stormwind.
2. **The portal opens** on the Elwynn side of the bridge. The Lich King steps out yelling "Bow before your king!" (original voice lines). Everyone on the continent hears it.
3. **His army arrives**: 8 minions (Risen Ghouls, Scourge Shocktroopers, Terrifying Abominations, Scourge Necromancers) plus 2 Shambling Horror bodyguards.
4. **The march**: he walks across the bridge, through the Valley of Heroes and the Trade District, along the canals, and up to King Varian's throne room in Stormwind Keep.
   - He attacks anything alive within 25 yards. The guards fight back.
   - His minions follow in formation. When anything attacks the Lich King, they rush in to protect him.
   - Every 45 seconds he raises more minions (20 at most).
   - Players and NPCs killed by the Scourge can rise again as ghouls.
   - At 50% health he summons more Shambling Horrors. At 25% he gets angrier.
5. **He fights with** Icy Touch, Death Coil, Chains of Ice, Death Grip (he pulls ranged players to him), Infest, and Frostmourne itself.
6. **Victory**: when he dies the whole army collapses, and the server announces who struck him down. His corpse drops 100–200 gold.
7. If nobody kills him within 60 minutes, he goes back to Icecrown. You can change this time in the config.

## Installation

You install it the same way as any other AzerothCore module.

1. **Copy the folder** `mod-lich-king-invasion` into your source tree's `modules` folder:
   ```
   azerothcore-wotlk/
     modules/
       mod-lich-king-invasion/   <-- here
   ```
2. **Re-build the server.** The module is C++, so the server has to be compiled again.
   - **Linux**:
     ```
     cd azerothcore-wotlk/build
     cmake ..   (add the same options you used the first time)
     make -j$(nproc)
     make install
     ```
     If you use the `acore.sh` dashboard, just run `./acore.sh compiler build`.
   - **Windows**: open CMake GUI and press **Configure**, then **Generate**. Open the `.sln` in Visual Studio and build **ALL_BUILD**, then **INSTALL**.
   - **Docker**: `docker compose up -d --build`
3. **Config file.** In your server's `etc/modules/` folder (on Windows it's `configs/modules/`), copy `mod_lich_king_invasion.conf.dist` to `mod_lich_king_invasion.conf`. Change settings there if you want to.
4. **Database.** Start the worldserver and the SQL file is imported for you. You'll see `lich_king_invasion_world.sql` in the startup log. If you turned automatic updates off, import `data/sql/db-world/lich_king_invasion_world.sql` into your `acore_world` database yourself (HeidiSQL works fine).

## Testing it right away

Log in with a GM account and type:

| Command | What it does |
|---|---|
| `.lkinvasion start` | Starts an invasion now |
| `.lkinvasion stop` | Sends him and his army back to Icecrown |
| `.lkinvasion status` | Shows whether an invasion is running, or how long until the next one |
| `.lkinvasion kill` | Instantly slays the Lich King (to test the victory ending) |

Then go to the bridge at the Stormwind gates (`.tele stormwind`, then walk out the main gate).

## Tuning

All the settings are in `mod_lich_king_invasion.conf`. Each one has a description. The ones you'll most likely change:

| Setting | Default | Meaning |
|---|---|---|
| `MinIntervalMinutes` / `MaxIntervalMinutes` | 120 / 240 | How often invasions happen |
| `LichKing.Health` | 2,000,000 | Lower it for a small server, raise it for a big one |
| `LichKing.DamagePercent` | 100 | 50 = he hits half as hard |
| `Minions.Max`, `Minions.WaveSize` | 20, 4 | Size of his army |
| `Destination` | 2 | 1 = he stops in the Trade District, 2 = he marches to the throne room |
| `MaxDurationMinutes` | 60 | 0 = he never leaves until he is killed |

The fight is balanced for **level 80** players. Lower-level characters will die very quickly. That's the Lich King.

## Notes

- **Movement maps (mmaps) must be enabled.** `MoveMaps.Enable = 1` is the AzerothCore default. Without them the army walks in straight lines through walls.
- The NPCs he kills in Stormwind (vendors, trainers, flight master, and so on) respawn on their normal timers.
- Custom creature IDs 9500000–9500014 are used. If another module of yours uses the same IDs, change them in both `src/LichKingInvasion.h` and the SQL file.
- For detailed logging, set `Logger.module=5,Console Server` in `worldserver.conf`. The log will then show every waypoint he reaches.
- Built and tested on current AzerothCore master (November 2026), compiled with both GCC and Clang. The test server ran the full march from the bridge to the throne room. It also tested the victory ending, `.lkinvasion stop`, the 60-minute time-out, and automatic scheduling. No errors or crashes.
