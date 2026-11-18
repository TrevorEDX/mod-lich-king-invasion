/*
 * mod-lich-king-invasion - invasion controller, world hooks and GM commands
 */

#include "LichKingInvasion.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "Config.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "TemporarySummon.h"
#include "WorldSessionMgr.h"
#include <algorithm>

using namespace Acore::ChatCommands;

namespace
{
    constexpr uint32 LKI_MAP_EASTERN_KINGDOMS = 0;
    constexpr uint32 MINUTE_MS            = 60 * 1000;
    constexpr uint32 RETRY_NO_PLAYERS_MS  = 5 * MINUTE_MS;
    constexpr uint32 LICH_KING_CORPSE_MS  = 5 * MINUTE_MS;

    // The Death Gate opens on the Elwynn side of the bridge, just in front of
    // the guards posted at the start of the bridge into Stormwind.
    Position const PortalPos   = { -9104.0f, 405.3f, 92.2f, 0.64f };
    Position const LichKingPos = { -9099.5f, 408.6f, 92.2f, 0.64f };
}

LichKingInvasionMgr::LichKingInvasionMgr()
{
    BuildPath();
}

LichKingInvasionMgr* LichKingInvasionMgr::instance()
{
    static LichKingInvasionMgr instance;
    return &instance;
}

void LichKingInvasionMgr::BuildPath()
{
    // Walkable points taken from existing Stormwind patrol / Scourge-invasion paths.
    // Movement uses pathfinding between points, so mmaps should be enabled.
    _path =
    {
        // --- across the bridge to the city gates ---
        { -9070.4f, 430.3f,  93.4f, 0.0f },
        { -9020.0f, 470.0f,  96.0f, 0.0f },
        { -8975.5f, 505.0f,  96.7f, 0.0f },
        { -8947.4f, 510.7f,  96.4f, 0.0f },
        // --- Valley of Heroes into the Trade District ---
        { -8923.8f, 536.2f,  94.6f, 0.0f },
        { -8893.2f, 563.9f,  92.7f, 0.0f },
        { -8869.5f, 596.1f,  92.4f, 0.0f },
        { -8839.0f, 623.1f,  93.4f, 0.0f },
        { -8809.7f, 605.0f,  96.1f, 0.0f },
        { -8792.0f, 589.8f,  97.6f, 0.0f },
        { -8769.0f, 606.3f,  97.0f, 0.0f },   // index 10 = end of Trade District route
        // --- through the canals towards Old Town ---
        { -8742.3f, 579.3f,  97.5f, 0.0f },
        { -8712.2f, 593.8f,  98.6f, 0.0f },
        { -8703.6f, 624.0f, 100.3f, 0.0f },
        { -8720.5f, 670.6f,  98.65f, 0.0f },
        { -8741.3f, 714.1f,  98.8f, 0.0f },
        { -8730.5f, 722.1f, 101.65f, 0.0f },
        { -8711.9f, 732.7f,  97.8f, 0.0f },
        { -8688.3f, 701.8f,  97.4f, 0.0f },
        { -8664.1f, 672.1f, 100.2f, 0.0f },
        { -8634.85f, 656.2f, 100.8f, 0.0f },
        { -8593.3f, 658.3f,  98.2f, 0.0f },
        { -8562.5f, 674.1f,  97.0f, 0.0f },
        { -8537.2f, 689.6f,  97.7f, 0.0f },
        { -8522.15f, 670.0f, 102.8f, 0.0f },
        { -8510.1f, 652.2f, 100.3f, 0.0f },
        { -8534.0f, 634.75f, 100.25f, 0.0f },
        { -8551.8f, 617.8f, 102.0f, 0.0f },
        { -8575.4f, 602.5f, 103.2f, 0.0f },
        { -8584.7f, 581.0f, 103.3f, 0.0f },
        // --- up to Stormwind Keep and the throne room ---
        { -8578.3f, 547.4f, 101.8f, 0.0f },
        { -8559.4f, 524.2f, 100.5f, 0.0f },
        { -8544.85f, 502.6f, 98.5f, 0.0f },
        { -8535.2f, 487.3f, 101.3f, 0.0f },
        { -8539.4f, 477.9f, 102.9f, 0.0f },
        { -8548.45f, 468.8f, 104.5f, 0.0f },
        { -8533.6f, 449.0f, 104.9f, 0.0f },
        { -8518.5f, 430.1f, 106.6f, 0.0f },
        { -8497.8f, 404.2f, 108.4f, 0.0f },
        { -8477.2f, 378.4f, 112.3f, 0.0f },
        { -8457.7f, 353.9f, 120.1f, 0.0f },
        { -8446.5f, 339.6f, 122.6f, 0.0f },
    };

    if (_cfg.Destination == LKI_DEST_TRADE_DISTRICT && _path.size() > 11)
        _path.resize(11);
}

Position const& LichKingInvasionMgr::GetPortalPosition() const { return PortalPos; }
Position const& LichKingInvasionMgr::GetLichKingSpawnPosition() const { return LichKingPos; }

void LichKingInvasionMgr::LoadConfig()
{
    _cfg.Enabled           = sConfigMgr->GetOption<bool>("LichKingInvasion.Enable", true);
    _cfg.Announce          = sConfigMgr->GetOption<bool>("LichKingInvasion.Announce", true);
    _cfg.MinIntervalMin    = sConfigMgr->GetOption<uint32>("LichKingInvasion.MinIntervalMinutes", 120);
    _cfg.MaxIntervalMin    = sConfigMgr->GetOption<uint32>("LichKingInvasion.MaxIntervalMinutes", 240);
    _cfg.WarningMin        = sConfigMgr->GetOption<uint32>("LichKingInvasion.WarningMinutes", 2);
    _cfg.MinPlayersOnline  = sConfigMgr->GetOption<uint32>("LichKingInvasion.MinPlayersOnline", 1);
    _cfg.MaxDurationMin    = sConfigMgr->GetOption<uint32>("LichKingInvasion.MaxDurationMinutes", 60);
    _cfg.Destination       = uint8(sConfigMgr->GetOption<uint32>("LichKingInvasion.Destination", LKI_DEST_KEEP));
    _cfg.MarchWalk         = sConfigMgr->GetOption<bool>("LichKingInvasion.MarchWalk", true);

    _cfg.LichKingHealth    = sConfigMgr->GetOption<uint32>("LichKingInvasion.LichKing.Health", 2000000);
    _cfg.LichKingDamagePct = sConfigMgr->GetOption<uint32>("LichKingInvasion.LichKing.DamagePercent", 100);
    _cfg.MinionHealthPct   = sConfigMgr->GetOption<uint32>("LichKingInvasion.Minion.HealthPercent", 100);
    _cfg.MinionDamagePct   = sConfigMgr->GetOption<uint32>("LichKingInvasion.Minion.DamagePercent", 100);

    _cfg.StartingMinions   = sConfigMgr->GetOption<uint32>("LichKingInvasion.Minions.Starting", 8);
    _cfg.Bodyguards        = sConfigMgr->GetOption<uint32>("LichKingInvasion.Minions.Bodyguards", 2);
    _cfg.WaveIntervalSec   = sConfigMgr->GetOption<uint32>("LichKingInvasion.Minions.WaveIntervalSeconds", 45);
    _cfg.WaveSize          = sConfigMgr->GetOption<uint32>("LichKingInvasion.Minions.WaveSize", 4);
    _cfg.MaxMinions        = sConfigMgr->GetOption<uint32>("LichKingInvasion.Minions.Max", 20);
    _cfg.RaiseDeadChance   = sConfigMgr->GetOption<uint32>("LichKingInvasion.Minions.RaiseDeadChance", 40);
    _cfg.LichKingScanRange = sConfigMgr->GetOption<float>("LichKingInvasion.LichKing.AttackRange", 25.0f);
    _cfg.MinionScanRange   = sConfigMgr->GetOption<float>("LichKingInvasion.Minion.AttackRange", 15.0f);
    _cfg.PortalDisplayId   = sConfigMgr->GetOption<uint32>("LichKingInvasion.PortalDisplayId", 0);

    // sanity
    _cfg.MinIntervalMin = std::max<uint32>(1, _cfg.MinIntervalMin);
    _cfg.MaxIntervalMin = std::max(_cfg.MinIntervalMin, _cfg.MaxIntervalMin);
    _cfg.LichKingDamagePct = std::max<uint32>(1, _cfg.LichKingDamagePct);
    _cfg.MinionDamagePct = std::max<uint32>(1, _cfg.MinionDamagePct);
    _cfg.MinionHealthPct = std::max<uint32>(1, _cfg.MinionHealthPct);
    _cfg.WaveIntervalSec = std::max<uint32>(5, _cfg.WaveIntervalSec);
    _cfg.RaiseDeadChance = std::min<uint32>(100, _cfg.RaiseDeadChance);
    if (_cfg.Destination != LKI_DEST_TRADE_DISTRICT)
        _cfg.Destination = LKI_DEST_KEEP;

    BuildPath();
}

void LichKingInvasionMgr::OnStartup()
{
    ScheduleNext();
    if (_cfg.Enabled)
        LOG_INFO("module", "mod-lich-king-invasion: first invasion in {} minutes.", _timerMs / MINUTE_MS);
}

void LichKingInvasionMgr::ScheduleNext()
{
    _timerMs = urand(_cfg.MinIntervalMin, _cfg.MaxIntervalMin) * MINUTE_MS;
    _warned = false;
}

Map* LichKingInvasionMgr::GetStormwindMap() const
{
    return sMapMgr->CreateBaseMap(LKI_MAP_EASTERN_KINGDOMS);
}

void LichKingInvasionMgr::Announce(std::string const& text) const
{
    if (!_cfg.Announce)
        return;

    ChatHandler(nullptr).SendWorldText("|cff00ccff[Scourge Invasion]|r {}", text);
}

void LichKingInvasionMgr::Update(uint32 diff)
{
    if (!_active)
    {
        if (!_cfg.Enabled)
            return;

        if (_cfg.WarningMin && !_warned && _timerMs <= _cfg.WarningMin * MINUTE_MS)
        {
            _warned = true;
            if (sWorldSessionMgr->GetPlayerCount() >= _cfg.MinPlayersOnline)
                Announce("A bitter chill sweeps across Elwynn Forest... the Scourge is gathering outside the gates of Stormwind!");
        }

        if (_timerMs > diff)
        {
            _timerMs -= diff;
            return;
        }

        if (sWorldSessionMgr->GetPlayerCount() < _cfg.MinPlayersOnline)
        {
            _timerMs = RETRY_NO_PLAYERS_MS;
            return;
        }

        std::string error;
        if (!StartInvasion(error))
        {
            LOG_ERROR("module", "mod-lich-king-invasion: could not start invasion: {}", error);
            _timerMs = RETRY_NO_PLAYERS_MS;
        }
        return;
    }

    _elapsedMs += diff;

    // events reported by the creature AIs
    {
        std::lock_guard<std::mutex> guard(_eventLock);
        if (_pendingArrived)
        {
            _pendingArrived = false;
            if (_cfg.Destination == LKI_DEST_KEEP)
                Announce("The Lich King has reached the throne room of Stormwind Keep! Defend King Varian!");
            else
                Announce("The Lich King has taken the Trade District! Drive him out of Stormwind!");
        }
        if (_pendingVictory)
        {
            _pendingVictory = false;
            if (_killerName.empty())
                Announce("The defenders of Stormwind have struck down the Lich King! The Scourge army crumbles to dust!");
            else
                Announce("The Lich King has been struck down by " + _killerName + " and the defenders of Stormwind! The Scourge army crumbles to dust!");
        }
    }

    if (_checkMs > diff)
    {
        _checkMs -= diff;
        return;
    }
    _checkMs = 1000;

    Map* map = GetStormwindMap();
    Creature* lichKing = map ? map->GetCreature(_lichKingGuid) : nullptr;

    if (!lichKing || !lichKing->IsAlive())
    {
        EndInvasion();
        return;
    }

    if (_cfg.MaxDurationMin && _elapsedMs >= _cfg.MaxDurationMin * MINUTE_MS)
    {
        Announce("The Lich King grows bored of Stormwind and withdraws to Icecrown... for now.");
        StopInvasion(false);
    }
}

bool LichKingInvasionMgr::StartInvasion(std::string& error)
{
    if (_active)
    {
        error = "an invasion is already in progress";
        return false;
    }

    Map* map = GetStormwindMap();
    if (!map)
    {
        error = "Eastern Kingdoms map is not available";
        return false;
    }

    map->LoadGrid(PortalPos.GetPositionX(), PortalPos.GetPositionY());

    TempSummon* portal = map->SummonCreature(NPC_LKI_PORTAL, PortalPos);
    if (!portal)
    {
        error = "failed to summon the portal (is the module SQL imported? creature entry 9500001)";
        return false;
    }
    portal->setActive(true);
    if (_cfg.PortalDisplayId)
        portal->SetDisplayId(_cfg.PortalDisplayId);

    TempSummon* lichKing = portal->SummonCreature(NPC_LKI_LICH_KING, LichKingPos, TEMPSUMMON_CORPSE_TIMED_DESPAWN, LICH_KING_CORPSE_MS);
    if (!lichKing)
    {
        portal->DespawnOrUnsummon();
        error = "failed to summon the Lich King (is the module SQL imported? creature entry 9500000)";
        return false;
    }
    lichKing->setActive(true);

    _portalGuid = portal->GetGUID();
    _lichKingGuid = lichKing->GetGUID();
    _active = true;
    _elapsedMs = 0;
    _checkMs = 5000;
    _defeated = false;
    {
        std::lock_guard<std::mutex> guard(_eventLock);
        _pendingVictory = false;
        _pendingArrived = false;
        _killerName.clear();
    }

    Announce("A portal to Icecrown has torn open at the bridge outside Stormwind! The Lich King marches on the city with his Scourge army - champions of Azeroth, to arms!");
    LOG_INFO("module", "mod-lich-king-invasion: invasion started.");
    return true;
}

void LichKingInvasionMgr::StopInvasion(bool announce)
{
    if (!_active)
        return;

    if (Map* map = GetStormwindMap())
    {
        if (Creature* lichKing = map->GetCreature(_lichKingGuid))
            if (lichKing->IsAlive() && lichKing->IsAIEnabled)
                lichKing->AI()->DoAction(ACTION_LKI_RETREAT);
    }

    if (announce)
        Announce("The Lich King's portal collapses and the Scourge withdraws from Stormwind.");

    EndInvasion();
}

bool LichKingInvasionMgr::KillLichKing()
{
    if (!_active)
        return false;

    Map* map = GetStormwindMap();
    Creature* lichKing = map ? map->GetCreature(_lichKingGuid) : nullptr;
    if (!lichKing || !lichKing->IsAlive())
        return false;

    Unit::Kill(lichKing, lichKing);
    return true;
}

void LichKingInvasionMgr::EndInvasion()
{
    if (Map* map = GetStormwindMap())
        if (Creature* portal = map->GetCreature(_portalGuid))
            portal->DespawnOrUnsummon(3s);

    _active = false;
    _portalGuid.Clear();
    _lichKingGuid.Clear();
    ScheduleNext();
    LOG_INFO("module", "mod-lich-king-invasion: invasion ended, next one in {} minutes.", _timerMs / MINUTE_MS);
}

void LichKingInvasionMgr::NotifyLichKingDied(std::string const& killerName)
{
    std::lock_guard<std::mutex> guard(_eventLock);
    _defeated = true;
    _pendingVictory = true;
    _killerName = killerName;
}

void LichKingInvasionMgr::NotifyReachedDestination()
{
    std::lock_guard<std::mutex> guard(_eventLock);
    _pendingArrived = true;
}

std::string LichKingInvasionMgr::GetStatusText() const
{
    if (!_cfg.Enabled && !_active)
        return "Lich King invasion is disabled in the config (LichKingInvasion.Enable = 0).";

    if (_active)
        return "The Lich King invasion is IN PROGRESS (running for " + std::to_string(_elapsedMs / 1000 / 60) + " min).";

    uint32 secs = _timerMs / 1000;
    return "No invasion active. Next invasion in " + std::to_string(secs / 60) + " min " + std::to_string(secs % 60) + " sec.";
}

// ---------------------------------------------------------------------------
//  World hooks
// ---------------------------------------------------------------------------
class LichKingInvasion_WorldScript : public WorldScript
{
public:
    LichKingInvasion_WorldScript() : WorldScript("LichKingInvasion_WorldScript",
        { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE }) { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        sLichKingInvasion->LoadConfig();
    }

    void OnStartup() override
    {
        sLichKingInvasion->OnStartup();
    }

    void OnUpdate(uint32 diff) override
    {
        sLichKingInvasion->Update(diff);
    }
};

// ---------------------------------------------------------------------------
//  GM commands:  .lkinvasion start | stop | status
// ---------------------------------------------------------------------------
class LichKingInvasion_CommandScript : public CommandScript
{
public:
    LichKingInvasion_CommandScript() : CommandScript("LichKingInvasion_CommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable lkiTable =
        {
            { "start",  HandleStart,  SEC_GAMEMASTER, Console::Yes },
            { "stop",   HandleStop,   SEC_GAMEMASTER, Console::Yes },
            { "status", HandleStatus, SEC_GAMEMASTER, Console::Yes },
            { "kill",   HandleKill,   SEC_GAMEMASTER, Console::Yes },
        };

        static ChatCommandTable commandTable =
        {
            { "lkinvasion", lkiTable },
        };

        return commandTable;
    }

    static bool HandleStart(ChatHandler* handler)
    {
        std::string error;
        if (!sLichKingInvasion->StartInvasion(error))
        {
            handler->SendErrorMessage("Could not start the Lich King invasion: {}", error);
            return false;
        }
        handler->SendSysMessage("The Lich King invasion has begun at the Stormwind bridge.");
        return true;
    }

    static bool HandleStop(ChatHandler* handler)
    {
        if (!sLichKingInvasion->IsActive())
        {
            handler->SendErrorMessage("There is no Lich King invasion in progress.");
            return false;
        }
        sLichKingInvasion->StopInvasion(true);
        handler->SendSysMessage("The Lich King invasion has been stopped.");
        return true;
    }

    static bool HandleKill(ChatHandler* handler)
    {
        if (!sLichKingInvasion->KillLichKing())
        {
            handler->SendErrorMessage("There is no living Lich King to kill.");
            return false;
        }
        handler->SendSysMessage("The Lich King has been slain (victory sequence triggered).");
        return true;
    }

    static bool HandleStatus(ChatHandler* handler)
    {
        handler->SendSysMessage(sLichKingInvasion->GetStatusText());
        return true;
    }
};

void AddSC_LichKingInvasionCreatures(); // LichKingInvasionAI.cpp

void AddSC_LichKingInvasion()
{
    new LichKingInvasion_WorldScript();
    new LichKingInvasion_CommandScript();
    AddSC_LichKingInvasionCreatures();
}
