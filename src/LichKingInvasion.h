/*
 * mod-lich-king-invasion
 * The Lich King periodically tears open a portal at the start of the bridge
 * outside Stormwind and marches into the city with an army of Scourge,
 * attacking every player and NPC in his way until he is stopped.
 *
 */

#ifndef MOD_LICH_KING_INVASION_H
#define MOD_LICH_KING_INVASION_H

#include "Define.h"
#include "ObjectGuid.h"
#include "Position.h"
#include <mutex>
#include <string>
#include <vector>

class Creature;
class Map;

// Custom creature entries created by data/sql/db-world/lich_king_invasion_world.sql
enum LKIEntries : uint32
{
    NPC_LKI_LICH_KING       = 9500000,
    NPC_LKI_PORTAL          = 9500001,
    NPC_LKI_GHOUL           = 9500010,
    NPC_LKI_SKELETON        = 9500011,
    NPC_LKI_ABOMINATION     = 9500012,
    NPC_LKI_NECROMANCER     = 9500013,
    NPC_LKI_HORROR          = 9500014,
};

enum LKIActions : int32
{
    ACTION_LKI_RETREAT      = 1,    // invasion timed out / GM stop: Lich King leaves with his army
};

enum LKIData : int32
{
    DATA_LKI_RAISE_CORPSE   = 1,    // SetGUID(victim, DATA_LKI_RAISE_CORPSE): a minion killed something, raise it
};

enum LKIDestination : uint8
{
    LKI_DEST_TRADE_DISTRICT = 1,
    LKI_DEST_KEEP           = 2,
};

struct LKIConfig
{
    bool   Enabled             = true;
    bool   Announce            = true;
    uint32 MinIntervalMin      = 120;
    uint32 MaxIntervalMin      = 240;
    uint32 WarningMin          = 2;
    uint32 MinPlayersOnline    = 1;
    uint32 MaxDurationMin      = 60;
    uint8  Destination         = LKI_DEST_KEEP;
    bool   MarchWalk           = true;

    uint32 LichKingHealth      = 2000000;
    uint32 LichKingDamagePct   = 100;
    uint32 MinionHealthPct     = 100;
    uint32 MinionDamagePct     = 100;

    uint32 StartingMinions     = 8;
    uint32 Bodyguards          = 2;
    uint32 WaveIntervalSec     = 45;
    uint32 WaveSize            = 4;
    uint32 MaxMinions          = 20;
    uint32 RaiseDeadChance     = 40;
    float  LichKingScanRange   = 25.0f;
    float  MinionScanRange     = 15.0f;
    uint32 PortalDisplayId     = 0;
};

class LichKingInvasionMgr
{
public:
    static LichKingInvasionMgr* instance();

    void LoadConfig();
    LKIConfig const& Config() const { return _cfg; }

    // ---- called from the world thread (WorldScript / GM commands) ----
    void OnStartup();
    void Update(uint32 diff);
    bool StartInvasion(std::string& error);
    void StopInvasion(bool announce);
    bool KillLichKing();
    bool IsActive() const { return _active; }
    uint32 GetSecondsUntilNext() const { return _timerMs / 1000; }
    uint32 GetSecondsActive() const { return _elapsedMs / 1000; }
    std::string GetStatusText() const;

    // ---- called by the creature AIs (map thread) ----
    void NotifyLichKingDied(std::string const& killerName);
    void NotifyReachedDestination();

    // March route, from the bridge outside the gates into the city
    std::vector<Position> const& GetPath() const { return _path; }
    Position const& GetPortalPosition() const;
    Position const& GetLichKingSpawnPosition() const;

private:
    LichKingInvasionMgr();

    void ScheduleNext();
    void EndInvasion();
    void Announce(std::string const& text) const;
    void BuildPath();
    Map* GetStormwindMap() const;

    LKIConfig _cfg;
    std::vector<Position> _path;

    bool   _active = false;
    bool   _warned = false;
    uint32 _timerMs = 0;        // time until next invasion
    uint32 _elapsedMs = 0;      // time since invasion started
    uint32 _checkMs = 0;

    ObjectGuid _lichKingGuid;
    ObjectGuid _portalGuid;

    // Filled from the map thread, consumed in the world thread
    std::mutex _eventLock;
    bool _pendingVictory = false;
    bool _pendingArrived = false;
    bool _defeated = false;
    std::string _killerName;
};

#define sLichKingInvasion LichKingInvasionMgr::instance()

#endif
