/*
 * mod-lich-king-invasion - creature AI for the Lich King and his Scourge army
 */

#include "LichKingInvasion.h"
#include "CellImpl.h"
#include "CreatureAI.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"
#include <cmath>
#include <vector>

enum LKISpells : uint32
{
    // The Lich King (death knight abilities tuned for level 80 players + ICC Infest)
    SPELL_LKI_INFEST            = 70541,
    SPELL_LKI_DEATH_COIL        = 66019,
    SPELL_LKI_DEATH_GRIP        = 66017,
    SPELL_LKI_DEATH_GRIP_PULL   = 49575,
    SPELL_LKI_CHAINS_OF_ICE     = 66020,
    SPELL_LKI_ICY_TOUCH         = 66021,

    // Visuals
    SPELL_LKI_TELEPORT_VISUAL   = 51347,
    SPELL_LKI_RISE_VISUAL       = 69639,

    // Minions
    SPELL_LKI_INFECTED_BITE     = 7367,
    SPELL_LKI_GHOUL_ENRAGE      = 8599,
    SPELL_LKI_BONE_SHARDS       = 17014,
    SPELL_LKI_CLEAVE            = 15496,
    SPELL_LKI_SCOURGE_HOOK      = 50335,
    SPELL_LKI_SHADOW_BOLT       = 20298,
    SPELL_LKI_SHADOWFLAME       = 51337,
    SPELL_LKI_SHOCKWAVE         = 72149,
    SPELL_LKI_HORROR_ENRAGE     = 72143,
};

enum LKITexts : uint8
{
    SAY_LK_SPAWN        = 0,
    SAY_LK_MARCH        = 1,
    SAY_LK_AGGRO        = 2,
    SAY_LK_KILL         = 3,
    SAY_LK_SUMMON       = 4,
    SAY_LK_RAISE        = 5,
    SAY_LK_LOW_HEALTH   = 6,
    SAY_LK_ARRIVED      = 7,
    SAY_LK_RETREAT      = 8,
    SAY_LK_DEATH        = 9,
};

enum LKIEvents : uint16
{
    // Lich King - always running (march / army)
    EVENT_LK_SUMMON_ESCORT = 1,
    EVENT_LK_START_MARCH,
    EVENT_LK_WAVE,
    EVENT_LK_TAUNT,

    // Lich King - combat
    EVENT_LK_ICY_TOUCH,
    EVENT_LK_DEATH_COIL,
    EVENT_LK_CHAINS_OF_ICE,
    EVENT_LK_DEATH_GRIP,
    EVENT_LK_INFEST,

    // Minions - combat
    EVENT_M_BITE,
    EVENT_M_BONE_SHARDS,
    EVENT_M_CLEAVE,
    EVENT_M_HOOK,
    EVENT_M_SHADOW_BOLT,
    EVENT_M_SHADOWFLAME,
    EVENT_M_SHOCKWAVE,
    EVENT_M_ENRAGE,
};

constexpr uint32 POINT_LKI_MARCH = 95000;       // + waypoint index
constexpr uint32 MINION_CORPSE_DESPAWN_MS = 20000;

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------
namespace
{
    bool IsInvasionEntry(uint32 entry)
    {
        return entry == NPC_LKI_LICH_KING || entry == NPC_LKI_PORTAL ||
               (entry >= NPC_LKI_GHOUL && entry <= NPC_LKI_HORROR);
    }

    // Units that are not worth chasing (critters, totems, invisible triggers, ...)
    bool IsIgnoredTarget(Unit const* unit)
    {
        if (Creature const* creature = unit->ToCreature())
        {
            if (creature->IsCritter() || creature->IsTotem() || creature->IsTrigger() ||
                creature->GetCreatureType() == CREATURE_TYPE_NON_COMBAT_PET ||
                IsInvasionEntry(creature->GetEntry()))
                return true;
        }
        return false;
    }

    // Finds the nearest living player or NPC the Scourge can attack
    class LKITargetCheck
    {
    public:
        LKITargetCheck(Creature const* me, float range) : _me(me), _range(range) { }

        bool operator()(Unit* unit)
        {
            if (!unit->IsAlive() || !_me->IsWithinDistInMap(unit, _range))
                return false;

            if (IsIgnoredTarget(unit))
                return false;

            if (!_me->IsValidAttackTarget(unit) || _me->IsFriendlyTo(unit))
                return false;

            if (Creature const* creature = unit->ToCreature())
                if (creature->IsInEvadeMode())
                    return false;

            if (!_me->CanCreatureAttack(unit) || !_me->IsWithinLOSInMap(unit))
                return false;

            _range = _me->GetDistance(unit);   // keep searching for something closer
            return true;
        }

    private:
        Creature const* _me;
        float _range;
    };

    Unit* FindInvasionTarget(Creature* me, float range)
    {
        Unit* target = nullptr;
        LKITargetCheck check(me, range);
        Acore::UnitLastSearcher<LKITargetCheck> searcher(me, target, check);
        Cell::VisitObjects(me, searcher, range);
        return target;
    }

    void SetCreatureHealth(Creature* creature, uint32 health)
    {
        if (!health)
            return;

        creature->SetCreateHealth(health);
        creature->SetStatFlatModifier(UNIT_MOD_HEALTH, BASE_VALUE, float(health));
        creature->UpdateMaxHealth();
        creature->SetFullHealth();
    }

    void ScaleMeleeDamage(Creature* creature, uint32 pct)
    {
        if (pct == 100)
            return;

        creature->ApplyStatPctModifier(UNIT_MOD_DAMAGE_MAINHAND, TOTAL_PCT, float(int32(pct) - 100));
        creature->UpdateDamagePhysical(BASE_ATTACK);
    }

    uint32 RandomMinionEntry()
    {
        uint32 roll = urand(0, 99);
        if (roll < 45)
            return NPC_LKI_GHOUL;
        if (roll < 70)
            return NPC_LKI_SKELETON;
        if (roll < 85)
            return NPC_LKI_ABOMINATION;
        return NPC_LKI_NECROMANCER;
    }
}

// ---------------------------------------------------------------------------
//  The Lich King
// ---------------------------------------------------------------------------
struct npc_lki_lich_king : public ScriptedAI
{
    npc_lki_lich_king(Creature* creature) : ScriptedAI(creature), _summons(creature) { }

    void Reset() override { }

    void Initialize()
    {
        _initialized = true;
        LKIConfig const& cfg = sLichKingInvasion->Config();

        me->setActive(true);
        me->SetReactState(REACT_AGGRESSIVE);
        me->SetWalk(cfg.MarchWalk);
        SetCreatureHealth(me, cfg.LichKingHealth);
        ScaleMeleeDamage(me, cfg.LichKingDamagePct);
        me->SetHomePosition(me->GetPosition());
        LOG_DEBUG("module", "LichKingInvasion: Lich King spawned with {} health", me->GetMaxHealth());

        DoCastSelf(SPELL_LKI_TELEPORT_VISUAL, true);
        Talk(SAY_LK_SPAWN);

        _marchEvents.ScheduleEvent(EVENT_LK_SUMMON_ESCORT, 2s);
        _marchEvents.ScheduleEvent(EVENT_LK_START_MARCH, 8s);
        _marchEvents.ScheduleEvent(EVENT_LK_WAVE, Seconds(cfg.WaveIntervalSec));
        _marchEvents.ScheduleEvent(EVENT_LK_TAUNT, 60s, 90s);
    }

    // ---- army management ----------------------------------------------------
    uint32 CountAliveMinions() const
    {
        uint32 count = 0;
        for (ObjectGuid const& guid : _summons)
            if (Creature* minion = ObjectAccessor::GetCreature(*me, guid))
                if (minion->IsAlive())
                    ++count;
        return count;
    }

    Creature* SummonMinion(uint32 entry, Position const& pos)
    {
        TempSummon* minion = me->SummonCreature(entry, pos, TEMPSUMMON_CORPSE_TIMED_DESPAWN, MINION_CORPSE_DESPAWN_MS);
        if (minion)
            minion->CastSpell(minion, SPELL_LKI_RISE_VISUAL, true);
        return minion;
    }

    void SummonMinionNearMe(uint32 entry)
    {
        // Stay close: on the bridge a wide spread would drop minions into the moat below
        Position pos = me->GetRandomNearPosition(5.0f);
        if (std::fabs(pos.GetPositionZ() - me->GetPositionZ()) > 3.0f)
            pos = me->GetPosition();
        SummonMinion(entry, pos);
    }

    void SummonEscort()
    {
        LKIConfig const& cfg = sLichKingInvasion->Config();
        for (uint32 i = 0; i < cfg.StartingMinions; ++i)
            SummonMinionNearMe(RandomMinionEntry());
        for (uint32 i = 0; i < cfg.Bodyguards; ++i)
            SummonMinionNearMe(NPC_LKI_HORROR);
    }

    void SummonWave()
    {
        LKIConfig const& cfg = sLichKingInvasion->Config();
        uint32 alive = CountAliveMinions();
        if (alive >= cfg.MaxMinions || !cfg.WaveSize)
            return;

        uint32 count = std::min(cfg.WaveSize, cfg.MaxMinions - alive);
        if (!_summonTalkCooldown)
        {
            Talk(SAY_LK_SUMMON);
            _summonTalkCooldown = 120000;
        }
        for (uint32 i = 0; i < count; ++i)
            SummonMinionNearMe(RandomMinionEntry());

        LOG_DEBUG("module", "LichKingInvasion: summoned {} reinforcements ({} were alive)", count, alive);
    }

    void TryRaiseDead(Unit* victim)
    {
        if (!victim || victim == me || IsIgnoredTarget(victim) || _retreating || !me->IsAlive())
            return;

        LKIConfig const& cfg = sLichKingInvasion->Config();
        if (!roll_chance_i(int32(cfg.RaiseDeadChance)) || CountAliveMinions() >= cfg.MaxMinions)
            return;

        if (SummonMinion(NPC_LKI_GHOUL, victim->GetPosition()) && !_raiseTalkCooldown)
        {
            Talk(SAY_LK_RAISE);
            _raiseTalkCooldown = 30000;
        }
    }

    void CallMinionsToDefend(Unit* enemy)
    {
        if (!enemy || !enemy->IsAlive() || IsIgnoredTarget(enemy))
            return;

        std::vector<ObjectGuid> guids(_summons.begin(), _summons.end());
        for (ObjectGuid const& guid : guids)
            if (Creature* minion = ObjectAccessor::GetCreature(*me, guid))
                if (minion->IsAlive() && minion->IsAIEnabled && !minion->GetVictim() && minion->IsWithinDistInMap(enemy, 50.0f))
                    minion->AI()->AttackStart(enemy);
    }

    void JustSummoned(Creature* summon) override
    {
        _summons.Summon(summon);
        summon->setActive(true);

        if (Unit* victim = me->GetVictim())
            if (summon->IsAIEnabled)
                summon->AI()->AttackStart(victim);
    }

    void SummonedCreatureDespawn(Creature* summon) override
    {
        _summons.Despawn(summon);
    }

    void SummonedCreatureDies(Creature* summon, Unit* /*killer*/) override
    {
        _summons.Despawn(summon);
    }

    void SetGUID(ObjectGuid const& guid, int32 id) override
    {
        if (id == DATA_LKI_RAISE_CORPSE)
            TryRaiseDead(ObjectAccessor::GetUnit(*me, guid));
    }

    // ---- marching -----------------------------------------------------------
    void MoveToCurrentWaypoint()
    {
        std::vector<Position> const& path = sLichKingInvasion->GetPath();
        if (path.empty())
            return;

        if (_waypoint >= path.size())
            _waypoint = uint32(path.size() - 1);

        LKIConfig const& cfg = sLichKingInvasion->Config();
        me->GetMotionMaster()->MovePoint(POINT_LKI_MARCH + _waypoint, path[_waypoint],
            cfg.MarchWalk ? FORCED_MOVEMENT_WALK : FORCED_MOVEMENT_RUN);
    }

    void MovementInform(uint32 type, uint32 id) override
    {
        if (type != POINT_MOTION_TYPE || id < POINT_LKI_MARCH)
            return;

        uint32 index = id - POINT_LKI_MARCH;
        if (index != _waypoint)
            return;

        me->SetHomePosition(me->GetPosition());
        _stuckTimer = 0;
        _idleTimer = 0;

        std::vector<Position> const& path = sLichKingInvasion->GetPath();
        LOG_DEBUG("module", "LichKingInvasion: Lich King reached waypoint {}/{} at ({:.1f}, {:.1f}, {:.1f})",
            _waypoint + 1, path.size(), me->GetPositionX(), me->GetPositionY(), me->GetPositionZ());

        if (_waypoint + 1 < path.size())
        {
            ++_waypoint;
            _moveNext = true;   // issued from UpdateAI, not from inside the movement generator
        }
        else if (!_arrived)
        {
            _arrived = true;
            Talk(SAY_LK_ARRIVED);
            sLichKingInvasion->NotifyReachedDestination();
        }
    }

    // ---- combat -------------------------------------------------------------
    void MoveInLineOfSight(Unit* who) override
    {
        if (!who || IsIgnoredTarget(who) || _retreating)
            return;

        ScriptedAI::MoveInLineOfSight(who);
    }

    void JustEngagedWith(Unit* who) override
    {
        _events.Reset();
        _events.ScheduleEvent(EVENT_LK_ICY_TOUCH, 4s, 6s);
        _events.ScheduleEvent(EVENT_LK_DEATH_COIL, 7s, 11s);
        _events.ScheduleEvent(EVENT_LK_CHAINS_OF_ICE, 10s, 14s);
        _events.ScheduleEvent(EVENT_LK_DEATH_GRIP, 12s, 18s);
        _events.ScheduleEvent(EVENT_LK_INFEST, 18s, 24s);

        LOG_DEBUG("module", "LichKingInvasion: Lich King engaged {} (entry {})", who ? who->GetName() : "nobody",
            who && who->IsCreature() ? who->GetEntry() : 0);

        if (who && who->IsPlayer() && !_aggroTalkCooldown)
        {
            Talk(SAY_LK_AGGRO);
            _aggroTalkCooldown = 60000;
        }

        CallMinionsToDefend(who);
    }

    void DamageTaken(Unit* attacker, uint32& damage, DamageEffectType /*type*/, SpellSchoolMask /*mask*/) override
    {
        if (!_halfHealth && me->HealthBelowPctDamaged(50, damage))
        {
            _halfHealth = true;
            _pendingBodyguards = true;
        }

        if (!_lowHealth && me->HealthBelowPctDamaged(25, damage))
        {
            _lowHealth = true;
            _pendingLowHealth = true;
        }

        if (attacker && attacker != me && !_defendCooldown)
        {
            _defendCooldown = 2000;
            CallMinionsToDefend(attacker);
        }
    }

    void KilledUnit(Unit* victim) override
    {
        if (victim->IsPlayer() && !_killTalkCooldown)
        {
            Talk(SAY_LK_KILL);
            _killTalkCooldown = 10000;
        }

        TryRaiseDead(victim);
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        if (_retreating || !me->IsAlive())
            return;

        if (!_EnterEvadeMode(why))
            return;

        // No running home: the Lich King keeps marching from wherever the fight ended.
        LOG_DEBUG("module", "LichKingInvasion: Lich King left combat, resuming march to waypoint {}", _waypoint + 1);
        me->ClearUnitState(UNIT_STATE_EVADE);
        _events.Reset();
        me->SetHomePosition(me->GetPosition());
        me->GetMotionMaster()->Clear();
        _scanTimer = 1500;
        _stuckTimer = 0;
        _idleTimer = 0;
        _lastPos = me->GetPosition();

        if (_marching)
            MoveToCurrentWaypoint();
        else
            me->GetMotionMaster()->MoveIdle();
    }

    void JustDied(Unit* killer) override
    {
        LOG_DEBUG("module", "LichKingInvasion: Lich King was killed by {}", killer ? killer->GetName() : "unknown");
        Talk(SAY_LK_DEATH);
        _marchEvents.Reset();
        _events.Reset();

        // Without its master the Scourge army collapses
        std::vector<ObjectGuid> guids(_summons.begin(), _summons.end());
        for (ObjectGuid const& guid : guids)
            if (Creature* minion = ObjectAccessor::GetCreature(*me, guid))
                if (minion->IsAlive())
                    minion->KillSelf();

        std::string killerName;
        if (killer)
            if (Player* player = killer->GetCharmerOrOwnerPlayerOrPlayerItself())
                killerName = player->GetName();

        sLichKingInvasion->NotifyLichKingDied(killerName);
    }

    void DoAction(int32 action) override
    {
        if (action != ACTION_LKI_RETREAT || _retreating)
            return;

        _retreating = true;
        Talk(SAY_LK_RETREAT);

        _marchEvents.Reset();
        _events.Reset();
        me->SetReactState(REACT_PASSIVE);
        me->CombatStop(true);
        me->GetMotionMaster()->Clear();
        me->GetMotionMaster()->MoveIdle();
        DoCastSelf(SPELL_LKI_TELEPORT_VISUAL, true);

        std::vector<ObjectGuid> guids(_summons.begin(), _summons.end());
        for (ObjectGuid const& guid : guids)
        {
            if (Creature* minion = ObjectAccessor::GetCreature(*me, guid))
            {
                if (minion->IsAlive())
                {
                    minion->SetReactState(REACT_PASSIVE);
                    minion->CombatStop(true);
                    minion->CastSpell(minion, SPELL_LKI_TELEPORT_VISUAL, true);
                }
                minion->DespawnOrUnsummon(2s);
            }
        }

        me->DespawnOrUnsummon(3s);
    }

    void SkipWaypoint()
    {
        std::vector<Position> const& path = sLichKingInvasion->GetPath();
        LOG_WARN("module", "LichKingInvasion: Lich King could not reach waypoint {} from ({:.1f}, {:.1f}, {:.1f}) - skipping it",
            _waypoint + 1, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ());

        _stuckTimer = 0;
        _idleTimer = 0;
        if (_waypoint + 1 < path.size())
            ++_waypoint;
        else
            me->NearTeleportTo(path.back().GetPositionX(), path.back().GetPositionY(), path.back().GetPositionZ(), me->GetOrientation());

        MoveToCurrentWaypoint();
    }

    void UpdateTimer(uint32& timer, uint32 diff)
    {
        timer = timer > diff ? timer - diff : 0;
    }

    void UpdateAI(uint32 diff) override
    {
        if (!_initialized)
            Initialize();

        if (_retreating)
            return;

        UpdateTimer(_killTalkCooldown, diff);
        UpdateTimer(_aggroTalkCooldown, diff);
        UpdateTimer(_raiseTalkCooldown, diff);
        UpdateTimer(_defendCooldown, diff);
        UpdateTimer(_summonTalkCooldown, diff);

        if (_pendingBodyguards)
        {
            _pendingBodyguards = false;
            Talk(SAY_LK_SUMMON);
            uint32 count = std::max<uint32>(1, sLichKingInvasion->Config().Bodyguards);
            for (uint32 i = 0; i < count; ++i)
                SummonMinionNearMe(NPC_LKI_HORROR);
        }

        if (_pendingLowHealth)
        {
            _pendingLowHealth = false;
            Talk(SAY_LK_LOW_HEALTH);
            _events.RescheduleEvent(EVENT_LK_INFEST, 1s);
        }

        _marchEvents.Update(diff);
        while (uint32 eventId = _marchEvents.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_LK_SUMMON_ESCORT:
                    SummonEscort();
                    break;
                case EVENT_LK_START_MARCH:
                    _marching = true;
                    if (!me->IsEngaged())
                        MoveToCurrentWaypoint();
                    break;
                case EVENT_LK_WAVE:
                    SummonWave();
                    _marchEvents.ScheduleEvent(EVENT_LK_WAVE, Seconds(sLichKingInvasion->Config().WaveIntervalSec));
                    break;
                case EVENT_LK_TAUNT:
                    if (!me->IsEngaged())
                        Talk(SAY_LK_MARCH);
                    _marchEvents.ScheduleEvent(EVENT_LK_TAUNT, 75s, 120s);
                    break;
                default:
                    break;
            }
        }

        if (!UpdateVictim())
        {
            if (_moveNext)
            {
                _moveNext = false;
                MoveToCurrentWaypoint();
            }

            // If a waypoint cannot be reached (blocked path), skip ahead instead of standing still
            if (_marching && !_arrived)
            {
                _stuckTimer += diff;
                if (_stuckTimer >= 90000 || _idleTimer >= 6000)
                    SkipWaypoint();
            }

            if (_scanTimer <= diff)
            {
                _scanTimer = 1000;

                // standing still while he should be marching?
                if (_marching && !_arrived && me->GetExactDist2d(&_lastPos) < 0.5f)
                    _idleTimer += 1000;
                else
                    _idleTimer = 0;
                _lastPos = me->GetPosition();

                LKIConfig const& cfg = sLichKingInvasion->Config();
                float range = _arrived ? cfg.LichKingScanRange + 15.0f : cfg.LichKingScanRange;
                if (Unit* target = FindInvasionTarget(me, range))
                {
                    AttackStart(target);
                    return;
                }

                // Make sure the march never stalls (e.g. after a knockback or a broken path)
                if (_marching && me->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
                {
                    if (!_arrived || me->GetExactDist2d(&sLichKingInvasion->GetPath().back()) > 5.0f)
                        MoveToCurrentWaypoint();
                }
            }
            else
                _scanTimer -= diff;
            return;
        }

        // Keep the leash anchored to wherever he is fighting
        if (_homeTimer <= diff)
        {
            _homeTimer = 1000;
            me->SetHomePosition(me->GetPosition());
        }
        else
            _homeTimer -= diff;

        _events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_LK_ICY_TOUCH:
                    DoCastVictim(SPELL_LKI_ICY_TOUCH);
                    _events.ScheduleEvent(EVENT_LK_ICY_TOUCH, 6s, 9s);
                    break;
                case EVENT_LK_DEATH_COIL:
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 30.0f, false))
                        DoCast(target, SPELL_LKI_DEATH_COIL);
                    _events.ScheduleEvent(EVENT_LK_DEATH_COIL, 8s, 12s);
                    break;
                case EVENT_LK_CHAINS_OF_ICE:
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 30.0f, false))
                        DoCast(target, SPELL_LKI_CHAINS_OF_ICE);
                    _events.ScheduleEvent(EVENT_LK_CHAINS_OF_ICE, 12s, 18s);
                    break;
                case EVENT_LK_DEATH_GRIP:
                {
                    Unit* target = SelectTarget(SelectTargetMethod::Random, 0, [this](Unit* unit)
                    {
                        return unit->IsPlayer() && me->GetDistance(unit) > 10.0f && me->GetDistance(unit) < 30.0f;
                    });
                    if (target)
                    {
                        DoCast(target, SPELL_LKI_DEATH_GRIP, true);
                        Position pos = me->GetNearPosition(3.0f, 0.0f);
                        target->CastSpell(pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), SPELL_LKI_DEATH_GRIP_PULL, true);
                    }
                    _events.ScheduleEvent(EVENT_LK_DEATH_GRIP, 20s, 25s);
                    break;
                }
                case EVENT_LK_INFEST:
                    DoCastSelf(SPELL_LKI_INFEST);
                    _events.ScheduleEvent(EVENT_LK_INFEST, 25s, 30s);
                    break;
                default:
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();
    }

private:
    SummonList _summons;
    EventMap _events;
    EventMap _marchEvents;

    bool _initialized = false;
    bool _marching = false;
    bool _moveNext = false;
    bool _arrived = false;
    bool _retreating = false;
    bool _halfHealth = false;
    bool _lowHealth = false;
    bool _pendingBodyguards = false;
    bool _pendingLowHealth = false;

    uint32 _waypoint = 0;
    uint32 _scanTimer = 1000;
    uint32 _homeTimer = 1000;
    uint32 _killTalkCooldown = 0;
    uint32 _aggroTalkCooldown = 0;
    uint32 _raiseTalkCooldown = 0;
    uint32 _defendCooldown = 0;
    uint32 _summonTalkCooldown = 0;
    uint32 _stuckTimer = 0;
    uint32 _idleTimer = 0;
    Position _lastPos;
};

// ---------------------------------------------------------------------------
//  Scourge minions (ghouls, skeletons, abominations, necromancers, horrors)
// ---------------------------------------------------------------------------
struct npc_lki_minion : public ScriptedAI
{
    npc_lki_minion(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override { }

    void Initialize()
    {
        _initialized = true;
        LKIConfig const& cfg = sLichKingInvasion->Config();

        if (TempSummon* summon = me->ToTempSummon())
            _leaderGuid = summon->GetSummonerGUID();

        if (cfg.MinionHealthPct != 100)
            SetCreatureHealth(me, std::max<uint32>(1, uint32(uint64(me->GetMaxHealth()) * cfg.MinionHealthPct / 100)));
        ScaleMeleeDamage(me, cfg.MinionDamagePct);

        me->setActive(true);
        me->SetReactState(REACT_AGGRESSIVE);

        if (me->GetEntry() == NPC_LKI_HORROR)
        {
            // bodyguards flank the Lich King
            _followDist = 3.0f;
            _followAngle = urand(0, 1) ? float(M_PI) * 0.5f : float(M_PI) * 1.5f;
        }
        else
        {
            _followDist = frand(2.5f, 6.0f);
            _followAngle = frand(float(M_PI) * 0.7f, float(M_PI) * 1.3f);   // marching behind him
        }

        if (!me->GetVictim())
            FollowLeader();
    }

    Creature* GetLeader() const
    {
        return _leaderGuid ? ObjectAccessor::GetCreature(*me, _leaderGuid) : nullptr;
    }

    void FollowLeader()
    {
        Creature* leader = GetLeader();
        if (!leader || !leader->IsAlive())
            return;

        me->GetMotionMaster()->Clear();
        me->GetMotionMaster()->MoveFollow(leader, _followDist, _followAngle);
    }

    void MoveInLineOfSight(Unit* who) override
    {
        if (!who || IsIgnoredTarget(who))
            return;

        ScriptedAI::MoveInLineOfSight(who);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        _events.Reset();
        switch (me->GetEntry())
        {
            case NPC_LKI_GHOUL:
                _events.ScheduleEvent(EVENT_M_BITE, 3s, 8s);
                break;
            case NPC_LKI_SKELETON:
                _events.ScheduleEvent(EVENT_M_BONE_SHARDS, 5s, 10s);
                break;
            case NPC_LKI_ABOMINATION:
                _events.ScheduleEvent(EVENT_M_CLEAVE, 4s, 7s);
                _events.ScheduleEvent(EVENT_M_HOOK, 3s, 6s);
                break;
            case NPC_LKI_NECROMANCER:
                _events.ScheduleEvent(EVENT_M_SHADOW_BOLT, 1s, 3s);
                _events.ScheduleEvent(EVENT_M_SHADOWFLAME, 10s, 14s);
                break;
            case NPC_LKI_HORROR:
                _events.ScheduleEvent(EVENT_M_SHOCKWAVE, 12s, 16s);
                _events.ScheduleEvent(EVENT_M_ENRAGE, 18s, 24s);
                break;
            default:
                break;
        }
    }

    void KilledUnit(Unit* victim) override
    {
        if (Creature* leader = GetLeader())
            if (leader->IsAlive() && leader->IsAIEnabled)
                leader->AI()->SetGUID(victim->GetGUID(), DATA_LKI_RAISE_CORPSE);
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        if (!me->IsAlive())
            return;

        if (!_EnterEvadeMode(why))
            return;

        // Regroup with the Lich King instead of running back to the portal
        me->ClearUnitState(UNIT_STATE_EVADE);
        _events.Reset();
        me->SetHomePosition(me->GetPosition());
        _checkTimer = 1500;
        FollowLeader();
    }

    void ExecuteEvent(uint32 eventId)
    {
        switch (eventId)
        {
            case EVENT_M_BITE:
                DoCastVictim(SPELL_LKI_INFECTED_BITE);
                _events.ScheduleEvent(EVENT_M_BITE, 8s, 14s);
                break;
            case EVENT_M_BONE_SHARDS:
                DoCastVictim(SPELL_LKI_BONE_SHARDS);
                _events.ScheduleEvent(EVENT_M_BONE_SHARDS, 12s, 16s);
                break;
            case EVENT_M_CLEAVE:
                DoCastVictim(SPELL_LKI_CLEAVE);
                _events.ScheduleEvent(EVENT_M_CLEAVE, 7s, 9s);
                break;
            case EVENT_M_HOOK:
            {
                Unit* target = SelectTarget(SelectTargetMethod::Random, 0, [this](Unit* unit)
                {
                    float dist = me->GetDistance(unit);
                    return dist > 8.0f && dist < 20.0f;
                });
                if (target)
                    DoCast(target, SPELL_LKI_SCOURGE_HOOK);
                _events.ScheduleEvent(EVENT_M_HOOK, 12s, 16s);
                break;
            }
            case EVENT_M_SHADOW_BOLT:
                DoCastVictim(SPELL_LKI_SHADOW_BOLT);
                _events.ScheduleEvent(EVENT_M_SHADOW_BOLT, 3400ms, 4800ms);
                break;
            case EVENT_M_SHADOWFLAME:
                DoCastAOE(SPELL_LKI_SHADOWFLAME);
                _events.ScheduleEvent(EVENT_M_SHADOWFLAME, 16s, 19s);
                break;
            case EVENT_M_SHOCKWAVE:
                DoCastVictim(SPELL_LKI_SHOCKWAVE);
                _events.ScheduleEvent(EVENT_M_SHOCKWAVE, 20s, 25s);
                break;
            case EVENT_M_ENRAGE:
                DoCastSelf(SPELL_LKI_HORROR_ENRAGE);
                _events.ScheduleEvent(EVENT_M_ENRAGE, 25s, 35s);
                break;
            default:
                break;
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!_initialized)
            Initialize();

        if (!UpdateVictim())
        {
            if (_checkTimer <= diff)
            {
                _checkTimer = 1000;
                Creature* leader = GetLeader();

                // 1) protect the Lich King
                if (leader && leader->IsAlive() && leader->IsInCombat())
                {
                    Unit* threat = leader->getAttackerForHelper();
                    if (!threat)
                        threat = leader->GetVictim();
                    if (threat && !IsIgnoredTarget(threat) && me->IsValidAttackTarget(threat) && me->IsWithinDistInMap(threat, 50.0f))
                    {
                        AttackStart(threat);
                        return;
                    }
                }

                // 2) attack anything living nearby
                if (Unit* target = FindInvasionTarget(me, sLichKingInvasion->Config().MinionScanRange))
                {
                    AttackStart(target);
                    return;
                }

                // 3) stay in formation
                if (leader && leader->IsAlive() && me->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
                    FollowLeader();
            }
            else
                _checkTimer -= diff;
            return;
        }

        if (_homeTimer <= diff)
        {
            _homeTimer = 1000;
            me->SetHomePosition(me->GetPosition());

            // never get dragged too far away from the Lich King
            if (Creature* leader = GetLeader())
            {
                if (leader->IsAlive() && !me->IsWithinDistInMap(leader, 70.0f))
                {
                    EnterEvadeMode(EVADE_REASON_OTHER);
                    return;
                }
            }
        }
        else
            _homeTimer -= diff;

        if (!_enraged && me->GetEntry() == NPC_LKI_GHOUL && HealthBelowPct(20))
        {
            _enraged = true;
            DoCastSelf(SPELL_LKI_GHOUL_ENRAGE, true);
        }

        _events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = _events.ExecuteEvent())
        {
            ExecuteEvent(eventId);
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();
    }

private:
    EventMap _events;
    ObjectGuid _leaderGuid;
    bool _initialized = false;
    bool _enraged = false;
    float _followDist = 5.0f;
    float _followAngle = float(M_PI);
    uint32 _checkTimer = 1000;
    uint32 _homeTimer = 1000;
};

void AddSC_LichKingInvasionCreatures()
{
    RegisterCreatureAI(npc_lki_lich_king);
    RegisterCreatureAI(npc_lki_minion);
}
