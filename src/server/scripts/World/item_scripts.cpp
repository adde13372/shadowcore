/*
 * Copyright 2021 ShadowCore
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/* ScriptData
SDName: Item_Scripts
SD%Complete: 100
SDComment: Items for a range of different items. See content below (in script)
SDCategory: Items
EndScriptData */

/* ContentData
item_nether_wraith_beacon(i31742)   Summons creatures for quest Becoming a Spellfire Tailor (q10832)
item_flying_machine(i34060, i34061)  Engineering crafted flying machines
item_gor_dreks_ointment(i30175)     Protecting Our Own(q10488)
item_only_for_flight                Items which should only useable while flying
EndContentData */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "Item.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "TemporarySummon.h"

/*#####
# item_only_for_flight
#####*/

enum OnlyForFlight
{
    SPELL_ARCANE_CHARGES    = 45072
};

class item_only_for_flight : public ItemScript
{
public:
    item_only_for_flight() : ItemScript("item_only_for_flight") { }

    bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/, ObjectGuid castId) override
    {
        uint32 itemId = item->GetEntry();
        bool disabled = false;

        //for special scripts
        switch (itemId)
        {
            case 24538:
                if (player->GetAreaId() != AREA_HALAA)
                    disabled = true;
                break;
            case 34489:
                if (player->GetZoneId() != ZONE_ISLE_OF_QUEL_DANAS)
                    disabled = true;
                break;
            case 34475:
                if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(SPELL_ARCANE_CHARGES, DIFFICULTY_NONE))
                    Spell::SendCastResult(player, spellInfo, {}, castId, SPELL_FAILED_NOT_ON_GROUND);
                break;
        }

        // allow use in flight only
        if (player->IsInFlight() && !disabled)
            return false;

        // error
        player->SendEquipError(EQUIP_ERR_CLIENT_LOCKED_OUT, item, nullptr);
        return true;
    }
};

/*#####
# item_nether_wraith_beacon
#####*/

class item_nether_wraith_beacon : public ItemScript
{
public:
    item_nether_wraith_beacon() : ItemScript("item_nether_wraith_beacon") { }

    bool OnUse(Player* player, Item* /*item*/, SpellCastTargets const& /*targets*/, ObjectGuid /*castId*/) override
    {
        if (player->GetQuestStatus(10832) == QUEST_STATUS_INCOMPLETE)
        {
            if (Creature* nether = player->SummonCreature(22408, player->GetPositionX(), player->GetPositionY()+20, player->GetPositionZ(), 0, TEMPSUMMON_TIMED_DESPAWN, 180000))
                nether->AI()->AttackStart(player);

            if (Creature* nether = player->SummonCreature(22408, player->GetPositionX(), player->GetPositionY()-20, player->GetPositionZ(), 0, TEMPSUMMON_TIMED_DESPAWN, 180000))
                nether->AI()->AttackStart(player);
        }
        return false;
    }
};

/*#####
# item_gor_dreks_ointment
#####*/

class item_gor_dreks_ointment : public ItemScript
{
public:
    item_gor_dreks_ointment() : ItemScript("item_gor_dreks_ointment") { }

    bool OnUse(Player* player, Item* item, SpellCastTargets const& targets, ObjectGuid /*castId*/) override
    {
        if (targets.GetUnitTarget() && targets.GetUnitTarget()->GetTypeId() == TYPEID_UNIT &&
            targets.GetUnitTarget()->GetEntry() == 20748 && !targets.GetUnitTarget()->HasAura(32578))
            return false;

        player->SendEquipError(EQUIP_ERR_CLIENT_LOCKED_OUT, item, nullptr);
        return true;
    }
};

/*#####
# item_incendiary_explosives
#####*/

class item_incendiary_explosives : public ItemScript
{
public:
    item_incendiary_explosives() : ItemScript("item_incendiary_explosives") { }

    bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/, ObjectGuid /*castId*/) override
    {
        if (player->FindNearestCreature(26248, 15) || player->FindNearestCreature(26249, 15))
            return false;
        else
        {
            player->SendEquipError(EQUIP_ERR_OUT_OF_RANGE, item, nullptr);
            return true;
        }
    }
};

/*#####
# item_mysterious_egg
#####*/

class item_mysterious_egg : public ItemScript
{
public:
    item_mysterious_egg() : ItemScript("item_mysterious_egg") { }

    bool OnExpire(Player* player, ItemTemplate const* /*pItemProto*/) override
    {
        ItemPosCountVec dest;
        uint8 msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, 39883, 1); // Cracked Egg
        if (msg == EQUIP_ERR_OK)
            player->StoreNewItem(dest, 39883, true, GenerateItemRandomBonusListId(39883));

        return true;
    }
};

/*#####
# item_disgusting_jar
#####*/

class item_disgusting_jar : public ItemScript
{
public:
    item_disgusting_jar() : ItemScript("item_disgusting_jar") { }

    bool OnExpire(Player* player, ItemTemplate const* /*pItemProto*/) override
    {
        ItemPosCountVec dest;
        uint8 msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, 44718, 1); // Ripe Disgusting Jar
        if (msg == EQUIP_ERR_OK)
            player->StoreNewItem(dest, 44718, true, GenerateItemRandomBonusListId(44718));

        return true;
    }
};

/*#####
# item_pile_fake_furs
#####*/

enum PileFakeFur
{
    GO_CARIBOU_TRAP_1                                      = 187982,
    GO_CARIBOU_TRAP_2                                      = 187995,
    GO_CARIBOU_TRAP_3                                      = 187996,
    GO_CARIBOU_TRAP_4                                      = 187997,
    GO_CARIBOU_TRAP_5                                      = 187998,
    GO_CARIBOU_TRAP_6                                      = 187999,
    GO_CARIBOU_TRAP_7                                      = 188000,
    GO_CARIBOU_TRAP_8                                      = 188001,
    GO_CARIBOU_TRAP_9                                      = 188002,
    GO_CARIBOU_TRAP_10                                     = 188003,
    GO_CARIBOU_TRAP_11                                     = 188004,
    GO_CARIBOU_TRAP_12                                     = 188005,
    GO_CARIBOU_TRAP_13                                     = 188006,
    GO_CARIBOU_TRAP_14                                     = 188007,
    GO_CARIBOU_TRAP_15                                     = 188008,
    GO_HIGH_QUALITY_FUR                                    = 187983,
    NPC_NESINGWARY_TRAPPER                                 = 25835
};

#define CaribouTrapsNum 15
const uint32 CaribouTraps[CaribouTrapsNum] =
{
    GO_CARIBOU_TRAP_1, GO_CARIBOU_TRAP_2, GO_CARIBOU_TRAP_3, GO_CARIBOU_TRAP_4, GO_CARIBOU_TRAP_5,
    GO_CARIBOU_TRAP_6, GO_CARIBOU_TRAP_7, GO_CARIBOU_TRAP_8, GO_CARIBOU_TRAP_9, GO_CARIBOU_TRAP_10,
    GO_CARIBOU_TRAP_11, GO_CARIBOU_TRAP_12, GO_CARIBOU_TRAP_13, GO_CARIBOU_TRAP_14, GO_CARIBOU_TRAP_15,
};

class item_pile_fake_furs : public ItemScript
{
public:
    item_pile_fake_furs() : ItemScript("item_pile_fake_furs") { }

    bool OnUse(Player* player, Item* /*item*/, SpellCastTargets const& /*targets*/, ObjectGuid /*castId*/) override
    {
        GameObject* go = nullptr;
        for (uint8 i = 0; i < CaribouTrapsNum; ++i)
        {
            go = player->FindNearestGameObject(CaribouTraps[i], 5.0f);
            if (go)
                break;
        }

        if (!go)
            return false;

        if (go->FindNearestCreature(NPC_NESINGWARY_TRAPPER, 10.0f, true) || go->FindNearestCreature(NPC_NESINGWARY_TRAPPER, 10.0f, false) || go->FindNearestGameObject(GO_HIGH_QUALITY_FUR, 2.0f))
            return true;

        float x, y, z;
        go->GetClosePoint(x, y, z, go->GetObjectSize() / 3, 7.0f);
        go->SummonGameObject(GO_HIGH_QUALITY_FUR, *go, QuaternionData(), 1);
        if (TempSummon* summon = player->SummonCreature(NPC_NESINGWARY_TRAPPER, x, y, z, go->GetOrientation(), TEMPSUMMON_DEAD_DESPAWN, 1000))
        {
            summon->SetVisible(false);
            summon->SetReactState(REACT_PASSIVE);
            summon->AddUnitFlag(UNIT_FLAG_IMMUNE_TO_PC);
        }
        return false;
    }
};

/*#####
# item_petrov_cluster_bombs
#####*/

enum PetrovClusterBombs
{
    SPELL_PETROV_BOMB = 42406,
};

class item_petrov_cluster_bombs : public ItemScript
{
public:
    item_petrov_cluster_bombs() : ItemScript("item_petrov_cluster_bombs") { }

    bool OnUse(Player* player, Item* /*item*/, SpellCastTargets const& /*targets*/, ObjectGuid castId) override
    {
        if (player->GetZoneId() != ZONE_HOWLING_FJORD)
            return false;

        if (!player->GetTransport() || player->GetAreaId() != AREA_HOWLING_FJORD_SHATTERED_STRAITS)
        {
            if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(SPELL_PETROV_BOMB, DIFFICULTY_NONE))
                Spell::SendCastResult(player, spellInfo, {}, castId, SPELL_FAILED_NOT_HERE);

            return true;
        }

        return false;
    }
};

/*######
# item_dehta_trap_smasher
# For quest 11876, Help Those That Cannot Help Themselves
######*/
enum HelpThemselves
{
    QUEST_CANNOT_HELP_THEMSELVES                  =  11876,
    NPC_TRAPPED_MAMMOTH_CALF                      =  25850,
    GO_MAMMOTH_TRAP_1                             = 188022,
    GO_MAMMOTH_TRAP_2                             = 188024,
    GO_MAMMOTH_TRAP_3                             = 188025,
    GO_MAMMOTH_TRAP_4                             = 188026,
    GO_MAMMOTH_TRAP_5                             = 188027,
    GO_MAMMOTH_TRAP_6                             = 188028,
    GO_MAMMOTH_TRAP_7                             = 188029,
    GO_MAMMOTH_TRAP_8                             = 188030,
    GO_MAMMOTH_TRAP_9                             = 188031,
    GO_MAMMOTH_TRAP_10                            = 188032,
    GO_MAMMOTH_TRAP_11                            = 188033,
    GO_MAMMOTH_TRAP_12                            = 188034,
    GO_MAMMOTH_TRAP_13                            = 188035,
    GO_MAMMOTH_TRAP_14                            = 188036,
    GO_MAMMOTH_TRAP_15                            = 188037,
    GO_MAMMOTH_TRAP_16                            = 188038,
    GO_MAMMOTH_TRAP_17                            = 188039,
    GO_MAMMOTH_TRAP_18                            = 188040,
    GO_MAMMOTH_TRAP_19                            = 188041,
    GO_MAMMOTH_TRAP_20                            = 188042,
    GO_MAMMOTH_TRAP_21                            = 188043,
    GO_MAMMOTH_TRAP_22                            = 188044,
};

#define MammothTrapsNum 22
const uint32 MammothTraps[MammothTrapsNum] =
{
    GO_MAMMOTH_TRAP_1, GO_MAMMOTH_TRAP_2, GO_MAMMOTH_TRAP_3, GO_MAMMOTH_TRAP_4, GO_MAMMOTH_TRAP_5,
    GO_MAMMOTH_TRAP_6, GO_MAMMOTH_TRAP_7, GO_MAMMOTH_TRAP_8, GO_MAMMOTH_TRAP_9, GO_MAMMOTH_TRAP_10,
    GO_MAMMOTH_TRAP_11, GO_MAMMOTH_TRAP_12, GO_MAMMOTH_TRAP_13, GO_MAMMOTH_TRAP_14, GO_MAMMOTH_TRAP_15,
    GO_MAMMOTH_TRAP_16, GO_MAMMOTH_TRAP_17, GO_MAMMOTH_TRAP_18, GO_MAMMOTH_TRAP_19, GO_MAMMOTH_TRAP_20,
    GO_MAMMOTH_TRAP_21, GO_MAMMOTH_TRAP_22
};

class item_dehta_trap_smasher : public ItemScript
{
public:
    item_dehta_trap_smasher() : ItemScript("item_dehta_trap_smasher") { }

    bool OnUse(Player* player, Item* /*item*/, SpellCastTargets const& /*targets*/, ObjectGuid /*castId*/) override
    {
        if (player->GetQuestStatus(QUEST_CANNOT_HELP_THEMSELVES) != QUEST_STATUS_INCOMPLETE)
            return false;

        Creature* pMammoth = player->FindNearestCreature(NPC_TRAPPED_MAMMOTH_CALF, 5.0f);
        if (!pMammoth)
            return false;

        GameObject* pTrap = nullptr;
        for (uint8 i = 0; i < MammothTrapsNum; ++i)
        {
            pTrap = player->FindNearestGameObject(MammothTraps[i], 11.0f);
            if (pTrap)
            {
                pMammoth->AI()->DoAction(1);
                pTrap->SetGoState(GO_STATE_READY);
                player->KilledMonsterCredit(NPC_TRAPPED_MAMMOTH_CALF);
                return true;
            }
        }
        return false;
    }
};

enum TheEmissary
{
    QUEST_THE_EMISSARY      =   11626,
    NPC_LEVIROTH            =   26452
};

class item_trident_of_nazjan : public ItemScript
{
public:
    item_trident_of_nazjan() : ItemScript("item_Trident_of_Nazjan") { }

    bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/, ObjectGuid /*castId*/) override
    {
        if (player->GetQuestStatus(QUEST_THE_EMISSARY) == QUEST_STATUS_INCOMPLETE)
        {
            if (Creature* pLeviroth = player->FindNearestCreature(NPC_LEVIROTH, 10.0f)) // spell range
            {
                pLeviroth->AI()->AttackStart(player);
                return false;
            } else
                player->SendEquipError(EQUIP_ERR_OUT_OF_RANGE, item, nullptr);
        } else
            player->SendEquipError(EQUIP_ERR_CLIENT_LOCKED_OUT, item, nullptr);
        return true;
    }
};

enum CapturedFrog
{
    QUEST_THE_PERFECT_SPIES      = 25444,
    NPC_VANIRAS_SENTRY_TOTEM     = 40187
};

class item_captured_frog : public ItemScript
{
public:
    item_captured_frog() : ItemScript("item_captured_frog") { }

    bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/, ObjectGuid /*castId*/) override
    {
        if (player->GetQuestStatus(QUEST_THE_PERFECT_SPIES) == QUEST_STATUS_INCOMPLETE)
        {
            if (player->FindNearestCreature(NPC_VANIRAS_SENTRY_TOTEM, 10.0f))
                return false;
            else
                player->SendEquipError(EQUIP_ERR_OUT_OF_RANGE, item, nullptr);
        }
        else
            player->SendEquipError(EQUIP_ERR_CLIENT_LOCKED_OUT, item, nullptr);
        return true;
    }
};

/*#####
# item_primal_egg
#####*/

class item_primal_egg : public ItemScript
{
public:
    item_primal_egg() : ItemScript("item_primal_egg") { }

    bool OnExpire(Player* player, ItemTemplate const* /*pItemProto*/) override
    {
        ItemPosCountVec dest;
        uint8 msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, 94296, 1); // Cracked Primal Egg
        if (msg == EQUIP_ERR_OK)
            player->StoreNewItem(dest, 94296, true, GenerateItemRandomBonusListId(94296));

        return true;
    }
};

/*#####
# item_pulsating_sac
#####*/

class item_pulsating_sac : public ItemScript
{
public:
    item_pulsating_sac() : ItemScript("item_pulsating_sac") { }

    bool OnExpire(Player* player, ItemTemplate const* /*pItemProto*/) override
    {
        ItemPosCountVec dest;
        uint8 msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, 137608, 1); // Growling Sac
        if (msg == EQUIP_ERR_OK)
            player->StoreNewItem(dest, 137608, true, GenerateItemRandomBonusListId(137608));

        return true;
    }
};

class item_titanium_seal_of_dalaran : public ItemScript
{
public:
    item_titanium_seal_of_dalaran() : ItemScript("item_titanium_seal_of_dalaran") { }

    bool OnUse(Player* pPlayer, Item* pItem, SpellCastTargets const& /*targets*/, ObjectGuid /*castId*/) override
    {
        uint8 luky = urand(0, 1);

        if (pPlayer)
        {
            pPlayer->CastSpell(pPlayer, 60458, true);
            pPlayer->TextEmote("casually flips his Titanium Seal of Dalaran");

            if (luky)
                pPlayer->TextEmote("caches the coin heads up!");
            else
                pPlayer->TextEmote("finds the coin face down for tails!");
        }
        return false;
    }
};

enum eBrewfestSampler
{
    QUEST_CHUG_AND_CHUCK_A = 12022,
    QUEST_CHUG_AND_CHUCK_H = 12191,
    NPC_BREWFEST_STOUT = 24108
};
class item_brewfest_sampler : public ItemScript
{
public:
    item_brewfest_sampler() : ItemScript("item_brewfest_sampler") { }

    bool OnUse(Player* pPlayer, Item* pItem, SpellCastTargets const& /*targets*/, ObjectGuid /*castId*/) override
    {
        if (pPlayer->GetQuestStatus(QUEST_CHUG_AND_CHUCK_A) == QUEST_STATUS_INCOMPLETE
            || pPlayer->GetQuestStatus(QUEST_CHUG_AND_CHUCK_H) == QUEST_STATUS_INCOMPLETE)
        {
            if (Creature* pStout = pPlayer->FindNearestCreature(NPC_BREWFEST_STOUT, 10.0f)) // spell range
            {
                return false;
            }
            else
                pPlayer->SendEquipError(EQUIP_ERR_OUT_OF_RANGE, pItem, NULL);
        }
        else
            pPlayer->SendEquipError(EQUIP_ERR_CLIENT_LOCKED_OUT, pItem, NULL);
        return true;
    }
};

enum eBrewfestRamReins
{
    SPELL_BREWFEST_RAM = 43880,
    SPELL_RAM_FATIGUE = 43052,
    SPELL_SPEED_RAM_GALLOP = 42994,
    SPELL_SPEED_RAM_CANTER = 42993,
    SPELL_SPEED_RAM_TROT = 42992,
    SPELL_SPEED_RAM_NORMAL = 43310,
    SPELL_SPEED_RAM_EXHAUSED = 43332
};

class item_brewfest_ram_reins : public ItemScript
{
public:
    item_brewfest_ram_reins() : ItemScript("item_brewfest_ram_reins") { }

    bool OnUse(Player* pPlayer, Item* pItem, SpellCastTargets const& /*targets*/, ObjectGuid /*castId*/) override
    {
        if (pPlayer->HasAura(SPELL_BREWFEST_RAM) && !pPlayer->HasAura(SPELL_SPEED_RAM_EXHAUSED))
        {
            if (pPlayer->HasAura(SPELL_SPEED_RAM_NORMAL))
                pPlayer->CastSpell(pPlayer, SPELL_SPEED_RAM_TROT, false);
            else if (pPlayer->HasAura(SPELL_SPEED_RAM_TROT))
            {
               // if (pPlayer->GetAura(SPELL_SPEED_RAM_TROT)->GetDuration() < 3000)
               //     pPlayer->GetAura(SPELL_SPEED_RAM_TROT)->SetDuration(4000);
              //  else
                    pPlayer->CastSpell(pPlayer, SPELL_SPEED_RAM_CANTER, false);
            }
            else if (pPlayer->HasAura(SPELL_SPEED_RAM_CANTER))
            {
               // if (pPlayer->GetAura(SPELL_SPEED_RAM_CANTER)->GetDuration() < 3000)
                //    pPlayer->GetAura(SPELL_SPEED_RAM_CANTER)->SetDuration(4000);
               // else
                    pPlayer->CastSpell(pPlayer, SPELL_SPEED_RAM_GALLOP, false);
            }
           // else if (pPlayer->HasAura(SPELL_SPEED_RAM_GALLOP))
              //  pPlayer->GetAura(SPELL_SPEED_RAM_GALLOP)->SetDuration(4000);
        }
        else
            pPlayer->SendEquipError(EQUIP_ERR_CLIENT_LOCKED_OUT, pItem, NULL);
        return true;
    }
};

// Leyara's Locket item=71259
class item_leyara_locket : public ItemScript
{
public:
    item_leyara_locket() : ItemScript("item_leyara_locket") { }

    bool OnUse(Player* pPlayer, Item* pItem, SpellCastTargets const& /*targets*/, ObjectGuid /*castId*/) override
    {
        if (pPlayer->getGender() == GENDER_MALE)
            pPlayer->CastSpell(pPlayer, 101185); // model male
        else
            pPlayer->CastSpell(pPlayer, 101186); // female

        return false;
    }
};

//153190
class item_fel_spotted_egg : public ItemScript
{
public:
    item_fel_spotted_egg() : ItemScript("item_fel_spotted_egg") { }

    bool OnExpire(Player* player, ItemTemplate const* /*pItemProto*/) override
    {
        ItemPosCountVec dest;
        uint8 msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, 153191, 1);
        if (msg == EQUIP_ERR_OK)
            player->StoreNewItem(dest, 153191, true, GenerateItemRandomBonusListId(153191));

        return true;
    }
};

//500500
class item_level_boost_50 : public ItemScript
{
public:
    item_level_boost_50() : ItemScript("item_level_boost_50") { }

    bool OnRemove(Player* player, Item* /*item*/) override
    {
        if (player->getLevel() < 50)
        {
            player->SetLevel(50);
            player->SetFullHealth();
            player->SetFullPower(player->GetPowerType());
            player->AddItem(182716, 4);
            player->ModifyMoney(25000000, false);
            switch (player->getClass())
            {
            case CLASS_WARRIOR:
            case CLASS_PALADIN:
            case CLASS_DEATH_KNIGHT:
                player->AddItem(164438, 1);
                player->AddItem(164440, 1);
                player->AddItem(164435, 1);
                player->AddItem(164437, 1);
                player->AddItem(164439, 1);
                player->AddItem(164441, 1);
                player->AddItem(164442, 1);
                player->AddItem(164436, 1);
                break;
            case CLASS_HUNTER:
            case CLASS_SHAMAN:
                player->AddItem(164399, 1);
                player->AddItem(164401, 1);
                player->AddItem(164402, 1);
                player->AddItem(164395, 1);
                player->AddItem(164400, 1);
                player->AddItem(164398, 1);
                player->AddItem(164396, 1);
                player->AddItem(164397, 1);
                break;
            case CLASS_MAGE:
            case CLASS_WARLOCK:
                player->AddItem(146786, 1);
                player->AddItem(146791, 1);
                player->AddItem(146789, 1);
                player->AddItem(146785, 1);
                player->AddItem(146788, 1);
                player->AddItem(146790, 1);
                player->AddItem(146792, 1);
                player->AddItem(146787, 1);
                break;
            case CLASS_PRIEST:
                break;
            case CLASS_ROGUE:
            case CLASS_DEMON_HUNTER:
            case CLASS_MONK:
                player->AddItem(160166, 1);
                player->AddItem(160168, 1);
                player->AddItem(160167, 1);
                player->AddItem(160169, 1);
                player->AddItem(160164, 1);
                player->AddItem(160163, 1);
                player->AddItem(160162, 1);
                player->AddItem(160165, 1);
                break;
            case CLASS_DRUID:
                player->AddItem(164559, 1);
                player->AddItem(164561, 1);
                player->AddItem(164560, 1);
                player->AddItem(164562, 1);
                player->AddItem(164557, 1);
                player->AddItem(164556, 1);
                player->AddItem(164555, 1);
                player->AddItem(164558, 1);
                break;
            }
            player->SaveToDB();
        }

        return true;
    }
};

void AddSC_item_scripts()
{
    new item_only_for_flight();
    new item_nether_wraith_beacon();
    new item_gor_dreks_ointment();
    new item_incendiary_explosives();
    new item_mysterious_egg();
    new item_disgusting_jar();
    new item_pile_fake_furs();
    new item_petrov_cluster_bombs();
    new item_dehta_trap_smasher();
    new item_trident_of_nazjan();
    new item_captured_frog();
    new item_primal_egg();
    new item_pulsating_sac();
    new item_titanium_seal_of_dalaran();
    new item_brewfest_sampler;
    new item_brewfest_ram_reins;
    new item_leyara_locket();
    new item_fel_spotted_egg();
    RegisterItemScript(item_level_boost_50);
}
