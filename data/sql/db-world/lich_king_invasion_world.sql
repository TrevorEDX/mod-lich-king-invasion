-- ============================================================================
--  mod-lich-king-invasion : world database content
--  Creates the invasion Lich King, his Death Gate portal and his Scourge army.
--  Entries 9500000 - 9500014 are used. Safe to run more than once.
--
--  Templates are cloned from existing Blizzard creatures so the SQL keeps
--  working if AzerothCore adds columns to creature_template later.
-- ============================================================================

SET @LK          := 9500000;
SET @PORTAL      := 9500001;
SET @GHOUL       := 9500010;
SET @SKELETON    := 9500011;
SET @ABOMINATION := 9500012;
SET @NECROMANCER := 9500013;
SET @HORROR      := 9500014;

-- Faction 1630 = "Scourge Invaders": hostile to everyone (players of both
-- factions AND city NPCs). Stormwind guards attack it on sight.
SET @FACTION     := 1630;

-- ---------------------------------------------------------------------------
--  Clean up any previous version
-- ---------------------------------------------------------------------------
DELETE FROM `creature_template`       WHERE `entry`      IN (@LK, @PORTAL, @GHOUL, @SKELETON, @ABOMINATION, @NECROMANCER, @HORROR);
DELETE FROM `creature_template_model` WHERE `CreatureID` IN (@LK, @PORTAL, @GHOUL, @SKELETON, @ABOMINATION, @NECROMANCER, @HORROR);
DELETE FROM `creature_template_addon` WHERE `entry`      IN (@LK, @PORTAL, @GHOUL, @SKELETON, @ABOMINATION, @NECROMANCER, @HORROR);
DELETE FROM `creature_equip_template` WHERE `CreatureID` IN (@LK, @PORTAL, @GHOUL, @SKELETON, @ABOMINATION, @NECROMANCER, @HORROR);
DELETE FROM `creature_text`           WHERE `CreatureID` IN (@LK);

-- ---------------------------------------------------------------------------
--  creature_template (cloned, then customised)
-- ---------------------------------------------------------------------------
DROP TEMPORARY TABLE IF EXISTS `tmp_lki_ct`;
CREATE TEMPORARY TABLE `tmp_lki_ct` LIKE `creature_template`;

-- source creatures:
--   24446 The Lich King          37695 Drudge Ghoul          16299 Skeletal Shocktrooper
--   30844 Death Gate (portal)    31098 Terrifying Abomination 31096 Scourge Necromancer
--   37698 Shambling Horror
INSERT INTO `tmp_lki_ct` SELECT * FROM `creature_template` WHERE `entry` = 24446;
UPDATE `tmp_lki_ct` SET `entry` = @LK WHERE `entry` = 24446;
INSERT INTO `tmp_lki_ct` SELECT * FROM `creature_template` WHERE `entry` = 30844;
UPDATE `tmp_lki_ct` SET `entry` = @PORTAL WHERE `entry` = 30844;
INSERT INTO `tmp_lki_ct` SELECT * FROM `creature_template` WHERE `entry` = 37695;
UPDATE `tmp_lki_ct` SET `entry` = @GHOUL WHERE `entry` = 37695;
INSERT INTO `tmp_lki_ct` SELECT * FROM `creature_template` WHERE `entry` = 16299;
UPDATE `tmp_lki_ct` SET `entry` = @SKELETON WHERE `entry` = 16299;
INSERT INTO `tmp_lki_ct` SELECT * FROM `creature_template` WHERE `entry` = 31098;
UPDATE `tmp_lki_ct` SET `entry` = @ABOMINATION WHERE `entry` = 31098;
INSERT INTO `tmp_lki_ct` SELECT * FROM `creature_template` WHERE `entry` = 31096;
UPDATE `tmp_lki_ct` SET `entry` = @NECROMANCER WHERE `entry` = 31096;
INSERT INTO `tmp_lki_ct` SELECT * FROM `creature_template` WHERE `entry` = 37698;
UPDATE `tmp_lki_ct` SET `entry` = @HORROR WHERE `entry` = 37698;

-- settings shared by everything
UPDATE `tmp_lki_ct` SET
    `difficulty_entry_1` = 0, `difficulty_entry_2` = 0, `difficulty_entry_3` = 0,
    `KillCredit1` = 0, `KillCredit2` = 0, `gossip_menu_id` = 0, `npcflag` = 0,
    `faction` = @FACTION, `exp` = 2, `dynamicflags` = 0, `unit_flags2` = 2048,
    `lootid` = 0, `pickpocketloot` = 0, `skinloot` = 0, `mingold` = 0, `maxgold` = 0,
    `AIName` = '', `MovementType` = 0, `VehicleId` = 0, `flags_extra` = 0,
    `ScriptName` = 'npc_lki_minion', `VerifiedBuild` = 0;

-- The Lich King
UPDATE `tmp_lki_ct` SET
    `name` = 'The Lich King', `subname` = 'Scourge Invasion',
    `minlevel` = 83, `maxlevel` = 83, `rank` = 3, `type` = 6, `type_flags` = 4,
    `unit_class` = 1, `unit_flags` = 0,
    `speed_walk` = 1.2, `speed_run` = 1.42857, `detection_range` = 30,
    `DamageModifier` = 20, `BaseAttackTime` = 2000, `HealthModifier` = 150, `ArmorModifier` = 1.5,
    `RegenHealth` = 0,                       -- damage done to him sticks between fights
    `mingold` = 1000000, `maxgold` = 2000000,  -- 100 - 200 gold
    `ScriptName` = 'npc_lki_lich_king'
WHERE `entry` = @LK;

-- Death Gate portal (visual only, cannot be attacked)
UPDATE `tmp_lki_ct` SET
    `name` = 'Death Gate', `subname` = 'Portal to Icecrown',
    `minlevel` = 80, `maxlevel` = 80, `rank` = 0, `unit_flags` = 33555200,
    `RegenHealth` = 1, `AIName` = 'NullCreatureAI', `ScriptName` = ''
WHERE `entry` = @PORTAL;

-- Minions
UPDATE `tmp_lki_ct` SET `name` = 'Risen Ghoul', `subname` = NULL,
    `minlevel` = 80, `maxlevel` = 80, `rank` = 0, `unit_class` = 1, `unit_flags` = 0, `type` = 6, `type_flags` = 0,
    `speed_walk` = 1, `speed_run` = 1.14286, `DamageModifier` = 2.5, `HealthModifier` = 1, `RegenHealth` = 1
WHERE `entry` = @GHOUL;

UPDATE `tmp_lki_ct` SET `name` = 'Scourge Shocktrooper', `subname` = NULL,
    `minlevel` = 80, `maxlevel` = 80, `rank` = 0, `unit_class` = 1, `unit_flags` = 0, `type` = 6, `type_flags` = 0,
    `speed_walk` = 1, `speed_run` = 1.14286, `DamageModifier` = 3, `HealthModifier` = 1.5, `RegenHealth` = 1
WHERE `entry` = @SKELETON;

UPDATE `tmp_lki_ct` SET `name` = 'Terrifying Abomination', `subname` = NULL,
    `minlevel` = 80, `maxlevel` = 80, `rank` = 1, `unit_class` = 1, `unit_flags` = 0, `type` = 6, `type_flags` = 0,
    `speed_walk` = 1, `speed_run` = 1.14286, `DamageModifier` = 6, `HealthModifier` = 4, `RegenHealth` = 1
WHERE `entry` = @ABOMINATION;

UPDATE `tmp_lki_ct` SET `name` = 'Scourge Necromancer', `subname` = NULL,
    `minlevel` = 80, `maxlevel` = 80, `rank` = 1, `unit_class` = 2, `unit_flags` = 0, `type` = 6, `type_flags` = 0,
    `speed_walk` = 1, `speed_run` = 1.14286, `DamageModifier` = 4, `HealthModifier` = 2.5, `ManaModifier` = 3, `RegenHealth` = 1
WHERE `entry` = @NECROMANCER;

UPDATE `tmp_lki_ct` SET `name` = 'Shambling Horror', `subname` = 'Bodyguard of the Lich King',
    `minlevel` = 82, `maxlevel` = 82, `rank` = 1, `unit_class` = 1, `unit_flags` = 0, `type` = 6, `type_flags` = 0,
    `speed_walk` = 1.2, `speed_run` = 1.42857, `DamageModifier` = 10, `HealthModifier` = 8, `RegenHealth` = 1
WHERE `entry` = @HORROR;

INSERT INTO `creature_template` SELECT * FROM `tmp_lki_ct`;
DROP TEMPORARY TABLE IF EXISTS `tmp_lki_ct`;

-- ---------------------------------------------------------------------------
--  Models
-- ---------------------------------------------------------------------------
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(@LK,          0, 30721, 1,   1, 0),   -- Icecrown Citadel Lich King
(@PORTAL,      0, 27346, 2,   1, 0),   -- Death Gate
(@GHOUL,       0, 24999, 1,   1, 0),
(@SKELETON,    0, 12074, 1,   1, 0),
(@ABOMINATION, 0, 24812, 1,   1, 0),
(@ABOMINATION, 1, 25201, 1,   1, 0),
(@ABOMINATION, 2, 24833, 1,   1, 0),
(@ABOMINATION, 3, 25281, 1,   1, 0),
(@NECROMANCER, 0, 25351, 1,   1, 0),
(@NECROMANCER, 1, 25352, 1,   1, 0),
(@HORROR,      0, 25297, 1,   1, 0);

-- Frostmourne and the abomination's hook
INSERT INTO `creature_equip_template` (`CreatureID`, `ID`, `ItemID1`, `ItemID2`, `ItemID3`, `VerifiedBuild`) VALUES
(@LK,          1, 36942, 0, 0, 0),
(@ABOMINATION, 1, 43625, 0, 0, 0);

-- visibilityDistanceType 3 = Large: the Lich King and portal can be seen from far away
INSERT INTO `creature_template_addon` (`entry`, `path_id`, `mount`, `bytes1`, `bytes2`, `emote`, `visibilityDistanceType`, `auras`) VALUES
(@LK,          0, 0, 0, 1, 0, 3, NULL),
(@PORTAL,      0, 0, 0, 0, 0, 3, NULL),
(@GHOUL,       0, 0, 0, 1, 0, 0, NULL),
(@SKELETON,    0, 0, 0, 1, 0, 0, NULL),
(@ABOMINATION, 0, 0, 0, 1, 0, 0, NULL),
(@NECROMANCER, 0, 0, 0, 1, 0, 0, NULL),
(@HORROR,      0, 0, 0, 1, 0, 0, NULL);

-- ---------------------------------------------------------------------------
--  Voiced yells (original Lich King sound files)
--  TextRange: 2 = whole zone, 3 = whole continent
-- ---------------------------------------------------------------------------
INSERT INTO `creature_text` (`CreatureID`, `GroupID`, `ID`, `Text`, `Type`, `Language`, `Probability`, `Emote`, `Duration`, `Sound`, `BroadcastTextId`, `TextRange`, `comment`) VALUES
(@LK, 0, 0, 'Bow before your king!',                                       14, 0, 100, 0, 0, 14796, 29511, 3, 'Lich King Invasion - Spawn'),
(@LK, 0, 1, 'Lay down your arms and surrender your souls!',                14, 0, 100, 0, 0, 14802, 29517, 3, 'Lich King Invasion - Spawn'),
(@LK, 0, 2, 'Soon you will belong to the Scourge.',                        14, 0, 100, 0, 0, 14794, 29509, 3, 'Lich King Invasion - Spawn'),
(@LK, 1, 0, 'Leave no survivors!',                                         14, 0, 100, 0, 0, 14793, 29508, 2, 'Lich King Invasion - March'),
(@LK, 1, 1, 'Ravage the living, minions!',                                 14, 0, 100, 0, 0, 14788, 29502, 2, 'Lich King Invasion - March'),
(@LK, 1, 2, 'There is nowhere to run...',                                  14, 0, 100, 0, 0, 14791, 29505, 2, 'Lich King Invasion - March'),
(@LK, 1, 3, 'The Light has abandoned you!',                                14, 0, 100, 0, 0, 14797, 29512, 2, 'Lich King Invasion - March'),
(@LK, 1, 4, 'There is no Light. Only darkness!',                           14, 0, 100, 0, 0, 14798, 29513, 2, 'Lich King Invasion - March'),
(@LK, 1, 5, 'Feast, my children! Feast upon the flesh of the living!',     14, 0, 100, 0, 0, 14801, 29516, 2, 'Lich King Invasion - March'),
(@LK, 2, 0, 'Succumb to the chill of the grave.',                          14, 0, 100, 0, 0, 17218, 37175, 2, 'Lich King Invasion - Aggro'),
(@LK, 2, 1, 'Death''s cold embrace awaits.',                               14, 0, 100, 0, 0, 17221, 37174, 2, 'Lich King Invasion - Aggro'),
(@LK, 2, 2, 'Suffer, insects!',                                            14, 0, 100, 0, 0, 14787, 29467, 2, 'Lich King Invasion - Aggro'),
(@LK, 3, 0, 'Hope wanes...',                                               14, 0, 100, 0, 0, 17363, 38038, 2, 'Lich King Invasion - Killed player'),
(@LK, 3, 1, 'The end has come!',                                           14, 0, 100, 0, 0, 17364, 38040, 2, 'Lich King Invasion - Killed player'),
(@LK, 3, 2, 'Die well, fool.',                                             14, 0, 100, 0, 0, 17256, 37510, 2, 'Lich King Invasion - Killed player'),
(@LK, 4, 0, 'Arise minions.  Do not let them pass.',                       14, 0, 100, 0, 0, 17216, 38669, 2, 'Lich King Invasion - Summon wave'),
(@LK, 4, 1, 'Cower before my terrible creations!',                         14, 0, 100, 0, 0, 14800, 29515, 2, 'Lich King Invasion - Summon wave'),
(@LK, 5, 0, 'Arise, and accept my gift.',                                  14, 0, 100, 0, 0, 14752, 28134, 2, 'Lich King Invasion - Raise dead'),
(@LK, 5, 1, 'Frostmourne feeds on the soul of your fallen ally!',          14, 0, 100, 0, 0, 17368, 38044, 2, 'Lich King Invasion - Raise dead'),
(@LK, 5, 2, 'Frostmourne hungers...',                                      14, 0, 100, 0, 0, 17366, 38042, 2, 'Lich King Invasion - Raise dead'),
(@LK, 6, 0, 'Face now your tragic end.',                                   14, 0, 100, 0, 0, 17365, 38041, 2, 'Lich King Invasion - 25% health'),
(@LK, 7, 0, 'Let the destruction of this place serve as a lesson to all those who would dare oppose the Scourge!', 14, 0, 100, 0, 0, 14792, 29506, 2, 'Lich King Invasion - Reached destination'),
(@LK, 8, 0, 'The frozen heart of Icecrown awaits....',                     14, 0, 100, 0, 0, 15606, 31695, 2, 'Lich King Invasion - Retreat'),
(@LK, 9, 0, 'Impossible...',                                               14, 0, 100, 0, 0, 17358, 38080, 2, 'Lich King Invasion - Death');
