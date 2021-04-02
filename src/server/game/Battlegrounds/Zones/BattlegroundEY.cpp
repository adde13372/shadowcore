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

#include "BattlegroundEY.h"
#include "BattlegroundMgr.h"
#include "BattlegroundPackets.h"
#include "Creature.h"
#include "DB2Stores.h"
#include "GameObject.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "SpellInfo.h"
#include "Util.h"
#include "WorldStatePackets.h"

// these variables aren't used outside of this file, so declare them only here
uint32 BG_EY_HonorScoreTicks[BG_HONOR_MODE_NUM] =
{
    260, // normal honor
    160  // holiday
};

BattlegroundEY::BattlegroundEY()
{
    m_BuffChange = true;
    BgObjects.resize(BG_EY_OBJECT_MAX);
    BgCreatures.resize(BG_EY_CREATURES_MAX);
    m_Points_Trigger[FEL_REAVER] = TR_FEL_REAVER_BUFF;
    m_Points_Trigger[BLOOD_ELF] = TR_BLOOD_ELF_BUFF;
    m_Points_Trigger[DRAENEI_RUINS] = TR_DRAENEI_RUINS_BUFF;
    m_Points_Trigger[MAGE_TOWER] = TR_MAGE_TOWER_BUFF;
    m_HonorScoreTics[TEAM_ALLIANCE] = 0;
    m_HonorScoreTics[TEAM_HORDE] = 0;
    m_TeamPointsCount[TEAM_ALLIANCE] = 0;
    m_TeamPointsCount[TEAM_HORDE] = 0;
    m_FlagKeeper.Clear();
    m_DroppedFlagGUID.Clear();
    m_FlagCapturedBgObjectType = 0;
    m_FlagState = BG_EY_FLAG_STATE_ON_BASE;
    m_FlagsTimer = 0;
    m_TowerCapCheckTimer = 0;
    m_PointAddingTimer = 0;
    m_HonorTics = 0;

    for (uint8 i = 0; i < EY_POINTS_MAX; ++i)
    {
        m_PointOwnedByTeam[i] = EY_POINT_NO_OWNER;
        m_PointState[i] = EY_POINT_STATE_UNCONTROLLED;
        m_PointBarStatus[i] = BG_EY_PROGRESS_BAR_STATE_MIDDLE;
    }

    for (uint8 i = 0; i < 2 * EY_POINTS_MAX; ++i)
        m_CurrentPointPlayersCount[i] = 0;
}

BattlegroundEY::~BattlegroundEY() { }

void BattlegroundEY::PostUpdateImpl(uint32 diff)
{
    if (GetStatus() == STATUS_IN_PROGRESS)
    {
        m_PointAddingTimer -= diff;
        if (m_PointAddingTimer <= 0)
        {
            m_PointAddingTimer = BG_EY_FPOINTS_TICK_TIME;
            if (m_TeamPointsCount[TEAM_ALLIANCE] > 0)
                AddPoints(ALLIANCE, BG_EY_TickPoints[m_TeamPointsCount[TEAM_ALLIANCE] - 1]);
            if (m_TeamPointsCount[TEAM_HORDE] > 0)
                AddPoints(HORDE, BG_EY_TickPoints[m_TeamPointsCount[TEAM_HORDE] - 1]);
        }

        if (m_FlagState == BG_EY_FLAG_STATE_WAIT_RESPAWN || m_FlagState == BG_EY_FLAG_STATE_ON_GROUND)
        {
            m_FlagsTimer -= diff;

            if (m_FlagsTimer < 0)
            {
                m_FlagsTimer = 0;
                if (m_FlagState == BG_EY_FLAG_STATE_WAIT_RESPAWN)
                    RespawnFlag(true);
                else
                    RespawnFlagAfterDrop();
            }
        }

        m_TowerCapCheckTimer -= diff;
        if (m_TowerCapCheckTimer <= 0)
        {
            //check if player joined point
            /*I used this order of calls, because although we will check if one player is in gameobject's distance 2 times
              but we can count of players on current point in CheckSomeoneLeftPoint
            */
            this->CheckSomeoneJoinedPoint();
            //check if player left point
            this->CheckSomeoneLeftPoint();
            this->UpdatePointStatuses();
            m_TowerCapCheckTimer = BG_EY_FPOINTS_TICK_TIME;
        }
    }
}

void BattlegroundEY::GetPlayerPositionData(std::vector<WorldPackets::Battleground::BattlegroundPlayerPosition>* positions) const
{
    if (Player* player = ObjectAccessor::GetPlayer(GetBgMap(), m_FlagKeeper))
    {
        WorldPackets::Battleground::BattlegroundPlayerPosition position;
        position.Guid = player->GetGUID();
        position.Pos = player->GetPosition();
        position.IconID = player->GetTeam() == ALLIANCE ? PLAYER_POSITION_ICON_ALLIANCE_FLAG : PLAYER_POSITION_ICON_HORDE_FLAG;
        position.ArenaSlot = PLAYER_POSITION_ARENA_SLOT_NONE;
        positions->push_back(position);
    }
}

void BattlegroundEY::StartingEventCloseDoors()
{
    SpawnBGObject(BG_EY_OBJECT_DOOR_A, RESPAWN_IMMEDIATELY);
    SpawnBGObject(BG_EY_OBJECT_DOOR_H, RESPAWN_IMMEDIATELY);

    for (uint32 i = BG_EY_OBJECT_A_BANNER_FEL_REAVER_CENTER; i < BG_EY_OBJECT_MAX; ++i)
        SpawnBGObject(i, RESPAWN_ONE_DAY);
}

void BattlegroundEY::StartingEventOpenDoors()
{
    SpawnBGObject(BG_EY_OBJECT_DOOR_A, RESPAWN_ONE_DAY);
    SpawnBGObject(BG_EY_OBJECT_DOOR_H, RESPAWN_ONE_DAY);

    for (uint32 i = BG_EY_OBJECT_N_BANNER_FEL_REAVER_CENTER; i <= BG_EY_OBJECT_FLAG_NETHERSTORM; ++i)
        SpawnBGObject(i, RESPAWN_IMMEDIATELY);
    for (uint32 i = 0; i < EY_POINTS_MAX; ++i)
    {
        //randomly spawn buff
        uint8 buff = urand(0, 2);
        SpawnBGObject(BG_EY_OBJECT_SPEEDBUFF_FEL_REAVER + buff + i * 3, RESPAWN_IMMEDIATELY);
    }

    // Achievement: Flurry
    StartCriteriaTimer(CRITERIA_TIMED_TYPE_EVENT, BG_EY_EVENT_START_BATTLE);
}

void BattlegroundEY::AddPoints(uint32 Team, uint32 Points)
{
    TeamId team_index = GetTeamIndexByTeamId(Team);
    m_TeamScores[team_index] += Points;
    m_HonorScoreTics[team_index] += Points;
    if (m_HonorScoreTics[team_index] >= m_HonorTics)
    {
        RewardHonorToTeam(GetBonusHonorFromKill(1), Team);
        m_HonorScoreTics[team_index] -= m_HonorTics;
    }
    UpdateTeamScore(team_index);
}

void BattlegroundEY::CheckSomeoneJoinedPoint()
{
    GameObject* obj = nullptr;
    for (uint8 i = 0; i < EY_POINTS_MAX; ++i)
    {
        obj = GetBgMap()->GetGameObject(BgObjects[BG_EY_OBJECT_TOWER_CAP_FEL_REAVER + i]);
        if (obj)
        {
            uint8 j = 0;
            while (j < m_PlayersNearPoint[EY_POINTS_MAX].size())
            {
                Player* player = ObjectAccessor::FindPlayer(m_PlayersNearPoint[EY_POINTS_MAX][j]);
                if (!player)
                {
                    TC_LOG_ERROR("bg.battleground", "BattlegroundEY:CheckSomeoneJoinedPoint: Player (%s) could not be found!", m_PlayersNearPoint[EY_POINTS_MAX][j].ToString().c_str());
                    ++j;
                    continue;
                }
                if (player->CanCaptureTowerPoint() && player->IsWithinDistInMap(obj, BG_EY_POINT_RADIUS))
                {
                    //player joined point!
                    //show progress bar
                    player->SendUpdateWorldState(PROGRESS_BAR_PERCENT_GREY, BG_EY_PROGRESS_BAR_PERCENT_GREY);
                    player->SendUpdateWorldState(PROGRESS_BAR_STATUS, m_PointBarStatus[i]);
                    player->SendUpdateWorldState(PROGRESS_BAR_SHOW, BG_EY_PROGRESS_BAR_SHOW);
                    //add player to point
                    m_PlayersNearPoint[i].push_back(m_PlayersNearPoint[EY_POINTS_MAX][j]);
                    //remove player from "free space"
                    m_PlayersNearPoint[EY_POINTS_MAX].erase(m_PlayersNearPoint[EY_POINTS_MAX].begin() + j);
                }
                else
                    ++j;
            }
        }
    }
}

void BattlegroundEY::CheckSomeoneLeftPoint()
{
    //reset current point counts
    for (uint8 i = 0; i < 2*EY_POINTS_MAX; ++i)
        m_CurrentPointPlayersCount[i] = 0;
    GameObject* obj = nullptr;
    for (uint8 i = 0; i < EY_POINTS_MAX; ++i)
    {
        obj = GetBgMap()->GetGameObject(BgObjects[BG_EY_OBJECT_TOWER_CAP_FEL_REAVER + i]);
        if (obj)
        {
            uint8 j = 0;
            while (j < m_PlayersNearPoint[i].size())
            {
                Player* player = ObjectAccessor::FindPlayer(m_PlayersNearPoint[i][j]);
                if (!player)
                {
                    TC_LOG_ERROR("bg.battleground", "BattlegroundEY:CheckSomeoneLeftPoint Player (%s) could not be found!", m_PlayersNearPoint[i][j].ToString().c_str());
                    //move non-existing players to "free space" - this will cause many errors showing in log, but it is a very important bug
                    m_PlayersNearPoint[EY_POINTS_MAX].push_back(m_PlayersNearPoint[i][j]);
                    m_PlayersNearPoint[i].erase(m_PlayersNearPoint[i].begin() + j);
                    continue;
                }
                if (!player->CanCaptureTowerPoint() || !player->IsWithinDistInMap(obj, BG_EY_POINT_RADIUS))
                    //move player out of point (add him to players that are out of points
                {
                    m_PlayersNearPoint[EY_POINTS_MAX].push_back(m_PlayersNearPoint[i][j]);
                    m_PlayersNearPoint[i].erase(m_PlayersNearPoint[i].begin() + j);
                    player->SendUpdateWorldState(PROGRESS_BAR_SHOW, BG_EY_PROGRESS_BAR_DONT_SHOW);
                }
                else
                {
                    //player is neat flag, so update count:
                    m_CurrentPointPlayersCount[2 * i + GetTeamIndexByTeamId(player->GetTeam())]++;
                    ++j;
                }
            }
        }
    }
}

void BattlegroundEY::UpdatePointStatuses()
{
    for (uint8 point = 0; point < EY_POINTS_MAX; ++point)
    {
        if (m_PlayersNearPoint[point].empty())
            continue;
        //count new point bar status:
        m_PointBarStatus[point] += (m_CurrentPointPlayersCount[2 * point] - m_CurrentPointPlayersCount[2 * point + 1] < BG_EY_POINT_MAX_CAPTURERS_COUNT) ? m_CurrentPointPlayersCount[2 * point] - m_CurrentPointPlayersCount[2 * point + 1] : BG_EY_POINT_MAX_CAPTURERS_COUNT;

        if (m_PointBarStatus[point] > BG_EY_PROGRESS_BAR_ALI_CONTROLLED)
            //point is fully alliance's
            m_PointBarStatus[point] = BG_EY_PROGRESS_BAR_ALI_CONTROLLED;
        if (m_PointBarStatus[point] < BG_EY_PROGRESS_BAR_HORDE_CONTROLLED)
            //point is fully horde's
            m_PointBarStatus[point] = BG_EY_PROGRESS_BAR_HORDE_CONTROLLED;

        uint32 pointOwnerTeamId = 0;
        //find which team should own this point
        if (m_PointBarStatus[point] <= BG_EY_PROGRESS_BAR_NEUTRAL_LOW)
            pointOwnerTeamId = HORDE;
        else if (m_PointBarStatus[point] >= BG_EY_PROGRESS_BAR_NEUTRAL_HIGH)
            pointOwnerTeamId = ALLIANCE;
        else
            pointOwnerTeamId = EY_POINT_NO_OWNER;

        for (uint8 i = 0; i < m_PlayersNearPoint[point].size(); ++i)
        {
            Player* player = ObjectAccessor::FindPlayer(m_PlayersNearPoint[point][i]);
            if (player)
            {
                player->SendUpdateWorldState(PROGRESS_BAR_STATUS, m_PointBarStatus[point]);
                //if point owner changed we must evoke event!
                if (pointOwnerTeamId != m_PointOwnedByTeam[point])
                {
                    //point was uncontrolled and player is from team which captured point
                    if (m_PointState[point] == EY_POINT_STATE_UNCONTROLLED && player->GetTeam() == pointOwnerTeamId)
                        this->EventTeamCapturedPoint(player, point);

                    //point was under control and player isn't from team which controlled it
                    if (m_PointState[point] == EY_POINT_UNDER_CONTROL && player->GetTeam() != m_PointOwnedByTeam[point])
                        this->EventTeamLostPoint(player, point);
                }

                /// @workaround The original AreaTrigger is covered by a bigger one and not triggered on client side.
                if (point == FEL_REAVER && m_PointOwnedByTeam[point] == player->GetTeam())
                {
                    if (m_FlagState && GetFlagPickerGUID() == player->GetGUID())
                    {
                        if (player->GetDistance2d(2044, 1730) < 2)
                            EventPlayerCapturedFlag(player, BG_EY_OBJECT_FLAG_FEL_REAVER);
                    }
                }

                if (point == BLOOD_ELF && m_PointOwnedByTeam[point] == player->GetBGTeam())
                {
                    if (m_FlagState && GetFlagPickerGUID() == player->GetGUID())
                    {
                        if (player->GetDistance2d(2048, 1393) < 2)
                            EventPlayerCapturedFlag(player, BG_EY_OBJECT_FLAG_BLOOD_ELF);
                    }
                }

                if (point == MAGE_TOWER && m_PointOwnedByTeam[point] == player->GetBGTeam())
                {
                    if (m_FlagState && GetFlagPickerGUID() == player->GetGUID())
                    {
                        if (player->GetDistance2d(2284, 1731) < 2)
                            EventPlayerCapturedFlag(player, BG_EY_OBJECT_FLAG_BLOOD_ELF);
                    }
                }

                if (point == DRAENEI_RUINS && m_PointOwnedByTeam[point] == player->GetBGTeam())
                {
                    if (m_FlagState && GetFlagPickerGUID() == player->GetGUID())
                    {
                        if (player->GetDistance2d(2286, 1402) < 2)
                            EventPlayerCapturedFlag(player, BG_EY_OBJECT_FLAG_BLOOD_ELF);
                    }
                }
            }
        }
    }
}

void BattlegroundEY::UpdateTeamScore(uint32 Team)
{
    uint32 score = GetTeamScore(Team);

    if (score >= BG_EY_MAX_TEAM_SCORE)
    {
        score = BG_EY_MAX_TEAM_SCORE;
        if (Team == TEAM_ALLIANCE)
            EndBattleground(ALLIANCE);
        else
            EndBattleground(HORDE);
    }

    if (Team == TEAM_ALLIANCE)
        UpdateWorldState(EY_ALLIANCE_RESOURCES, score);
    else
        UpdateWorldState(EY_HORDE_RESOURCES, score);
}

void BattlegroundEY::EndBattleground(uint32 winner)
{
    // Win reward
    if (winner == ALLIANCE)
        RewardHonorToTeam(GetBonusHonorFromKill(1), ALLIANCE);
    if (winner == HORDE)
        RewardHonorToTeam(GetBonusHonorFromKill(1), HORDE);
    // Complete map reward
    RewardHonorToTeam(GetBonusHonorFromKill(1), ALLIANCE);
    RewardHonorToTeam(GetBonusHonorFromKill(1), HORDE);

    Battleground::EndBattleground(winner);
}

void BattlegroundEY::UpdatePointsCount(uint32 Team)
{
    if (Team == ALLIANCE)
        UpdateWorldState(EY_ALLIANCE_BASE, m_TeamPointsCount[TEAM_ALLIANCE]);
    else
        UpdateWorldState(EY_HORDE_BASE, m_TeamPointsCount[TEAM_HORDE]);
}

void BattlegroundEY::UpdatePointsIcons(uint32 Team, uint32 Point)
{
    //we MUST firstly send 0, after that we can send 1!!!
    if (m_PointState[Point] == EY_POINT_UNDER_CONTROL)
    {
        UpdateWorldState(m_PointsIconStruct[Point].WorldStateControlIndex, 0);
        if (Team == ALLIANCE)
            UpdateWorldState(m_PointsIconStruct[Point].WorldStateAllianceControlledIndex, 1);
        else
            UpdateWorldState(m_PointsIconStruct[Point].WorldStateHordeControlledIndex, 1);
    }
    else
    {
        if (Team == ALLIANCE)
            UpdateWorldState(m_PointsIconStruct[Point].WorldStateAllianceControlledIndex, 0);
        else
            UpdateWorldState(m_PointsIconStruct[Point].WorldStateHordeControlledIndex, 0);
        UpdateWorldState(m_PointsIconStruct[Point].WorldStateControlIndex, 1);
    }
}

void BattlegroundEY::AddPlayer(Player* player)
{
    Battleground::AddPlayer(player);
    PlayerScores[player->GetGUID()] = new BattlegroundEYScore(player->GetGUID(), player->GetBGTeam());

    m_PlayersNearPoint[EY_POINTS_MAX].push_back(player->GetGUID());
}

void BattlegroundEY::RemovePlayer(Player* player, ObjectGuid guid, uint32 /*team*/)
{
    // sometimes flag aura not removed :(
    for (int j = EY_POINTS_MAX; j >= 0; --j)
    {
        for (size_t i = 0; i < m_PlayersNearPoint[j].size(); ++i)
            if (m_PlayersNearPoint[j][i] == guid)
                m_PlayersNearPoint[j].erase(m_PlayersNearPoint[j].begin() + i);
    }
    if (IsFlagPickedup())
    {
        if (m_FlagKeeper == guid)
        {
            if (player)
                EventPlayerDroppedFlag(player);
            else
            {
                SetFlagPicker(ObjectGuid::Empty);
                RespawnFlag(true);
            }
        }
    }
}

void BattlegroundEY::HandleAreaTrigger(Player* player, uint32 trigger, bool entered)
{
    if (!player->IsAlive())                                  //hack code, must be removed later
        return;

    switch (trigger)
    {
        case 4530: // Horde Start
        case 4531: // Alliance Start
            if (GetStatus() == STATUS_WAIT_JOIN && !entered)
                TeleportPlayerToExploitLocation(player);
            break;
        case TR_BLOOD_ELF_POINT:
            if (m_PointState[BLOOD_ELF] == EY_POINT_UNDER_CONTROL && m_PointOwnedByTeam[BLOOD_ELF] == player->GetTeam())
                if (m_FlagState && GetFlagPickerGUID() == player->GetGUID())
                    EventPlayerCapturedFlag(player, BG_EY_OBJECT_FLAG_BLOOD_ELF);
            break;
        case TR_FEL_REAVER_POINT:
            if (m_PointState[FEL_REAVER] == EY_POINT_UNDER_CONTROL && m_PointOwnedByTeam[FEL_REAVER] == player->GetTeam())
                if (m_FlagState && GetFlagPickerGUID() == player->GetGUID())
                    EventPlayerCapturedFlag(player, BG_EY_OBJECT_FLAG_FEL_REAVER);
            break;
        case TR_MAGE_TOWER_POINT:
            if (m_PointState[MAGE_TOWER] == EY_POINT_UNDER_CONTROL && m_PointOwnedByTeam[MAGE_TOWER] == player->GetTeam())
                if (m_FlagState && GetFlagPickerGUID() == player->GetGUID())
                    EventPlayerCapturedFlag(player, BG_EY_OBJECT_FLAG_MAGE_TOWER);
            break;
        case TR_DRAENEI_RUINS_POINT:
            if (m_PointState[DRAENEI_RUINS] == EY_POINT_UNDER_CONTROL && m_PointOwnedByTeam[DRAENEI_RUINS] == player->GetTeam())
                if (m_FlagState && GetFlagPickerGUID() == player->GetGUID())
                    EventPlayerCapturedFlag(player, BG_EY_OBJECT_FLAG_DRAENEI_RUINS);
            break;
        case 4512:
        case 4515:
        case 4517:
        case 4519:
        case 4568:
        case 4569:
        case 4570:
        case 4571:
        case 5866:
            break;
        default:
            Battleground::HandleAreaTrigger(player, trigger, entered);
            break;
    }
}

bool BattlegroundEY::SetupBattleground()
{
        // doors
    if (!AddObject(BG_EY_OBJECT_DOOR_A, BG_OBJECT_A_DOOR_EY_ENTRY, 2527.6f, 1596.91f, 1246.0f, -3.12414f, -0.173642f, -0.001515f, 0.98477f, -0.008594f, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_EY_OBJECT_DOOR_H, BG_OBJECT_H_DOOR_EY_ENTRY, 1803.21f, 1539.49f, 1246.0f, 3.14159f, 0.173648f, 0, 0.984808f, 0, RESPAWN_IMMEDIATELY)
        // banners (alliance)
        || !AddObject(BG_EY_OBJECT_A_BANNER_FEL_REAVER_CENTER, BG_OBJECT_A_BANNER_EY_ENTRY, 2057.46f, 1735.07f, 1187.91f, -0.925024f, 0, 0, 0.446198f, -0.894934f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_A_BANNER_FEL_REAVER_LEFT, BG_OBJECT_A_BANNER_EY_ENTRY, 2032.25f, 1729.53f, 1190.33f, 1.8675f, 0, 0, 0.803857f, 0.594823f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_A_BANNER_FEL_REAVER_RIGHT, BG_OBJECT_A_BANNER_EY_ENTRY, 2092.35f, 1775.46f, 1187.08f, -0.401426f, 0, 0, 0.199368f, -0.979925f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_A_BANNER_BLOOD_ELF_CENTER, BG_OBJECT_A_BANNER_EY_ENTRY, 2047.19f, 1349.19f, 1189.0f, -1.62316f, 0, 0, 0.725374f, -0.688354f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_A_BANNER_BLOOD_ELF_LEFT, BG_OBJECT_A_BANNER_EY_ENTRY, 2074.32f, 1385.78f, 1194.72f, 0.488692f, 0, 0, 0.241922f, 0.970296f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_A_BANNER_BLOOD_ELF_RIGHT, BG_OBJECT_A_BANNER_EY_ENTRY, 2025.13f, 1386.12f, 1192.74f, 2.3911f, 0, 0, 0.930418f, 0.366501f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_A_BANNER_DRAENEI_RUINS_CENTER, BG_OBJECT_A_BANNER_EY_ENTRY, 2276.8f, 1400.41f, 1196.33f, 2.44346f, 0, 0, 0.939693f, 0.34202f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_A_BANNER_DRAENEI_RUINS_LEFT, BG_OBJECT_A_BANNER_EY_ENTRY, 2305.78f, 1404.56f, 1199.38f, 1.74533f, 0, 0, 0.766044f, 0.642788f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_A_BANNER_DRAENEI_RUINS_RIGHT, BG_OBJECT_A_BANNER_EY_ENTRY, 2245.4f, 1366.41f, 1195.28f, 2.21657f, 0, 0, 0.894934f, 0.446198f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_A_BANNER_MAGE_TOWER_CENTER, BG_OBJECT_A_BANNER_EY_ENTRY, 2270.84f, 1784.08f, 1186.76f, 2.42601f, 0, 0, 0.936672f, 0.350207f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_A_BANNER_MAGE_TOWER_LEFT, BG_OBJECT_A_BANNER_EY_ENTRY, 2269.13f, 1737.7f, 1186.66f, 0.994838f, 0, 0, 0.477159f, 0.878817f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_A_BANNER_MAGE_TOWER_RIGHT, BG_OBJECT_A_BANNER_EY_ENTRY, 2300.86f, 1741.25f, 1187.7f, -0.785398f, 0, 0, 0.382683f, -0.92388f, RESPAWN_ONE_DAY)
        // banners (horde)
        || !AddObject(BG_EY_OBJECT_H_BANNER_FEL_REAVER_CENTER, BG_OBJECT_H_BANNER_EY_ENTRY, 2057.46f, 1735.07f, 1187.91f, -0.925024f, 0, 0, 0.446198f, -0.894934f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_H_BANNER_FEL_REAVER_LEFT, BG_OBJECT_H_BANNER_EY_ENTRY, 2032.25f, 1729.53f, 1190.33f, 1.8675f, 0, 0, 0.803857f, 0.594823f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_H_BANNER_FEL_REAVER_RIGHT, BG_OBJECT_H_BANNER_EY_ENTRY, 2092.35f, 1775.46f, 1187.08f, -0.401426f, 0, 0, 0.199368f, -0.979925f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_H_BANNER_BLOOD_ELF_CENTER, BG_OBJECT_H_BANNER_EY_ENTRY, 2047.19f, 1349.19f, 1189.0f, -1.62316f, 0, 0, 0.725374f, -0.688354f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_H_BANNER_BLOOD_ELF_LEFT, BG_OBJECT_H_BANNER_EY_ENTRY, 2074.32f, 1385.78f, 1194.72f, 0.488692f, 0, 0, 0.241922f, 0.970296f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_H_BANNER_BLOOD_ELF_RIGHT, BG_OBJECT_H_BANNER_EY_ENTRY, 2025.13f, 1386.12f, 1192.74f, 2.3911f, 0, 0, 0.930418f, 0.366501f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_H_BANNER_DRAENEI_RUINS_CENTER, BG_OBJECT_H_BANNER_EY_ENTRY, 2276.8f, 1400.41f, 1196.33f, 2.44346f, 0, 0, 0.939693f, 0.34202f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_H_BANNER_DRAENEI_RUINS_LEFT, BG_OBJECT_H_BANNER_EY_ENTRY, 2305.78f, 1404.56f, 1199.38f, 1.74533f, 0, 0, 0.766044f, 0.642788f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_H_BANNER_DRAENEI_RUINS_RIGHT, BG_OBJECT_H_BANNER_EY_ENTRY, 2245.4f, 1366.41f, 1195.28f, 2.21657f, 0, 0, 0.894934f, 0.446198f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_H_BANNER_MAGE_TOWER_CENTER, BG_OBJECT_H_BANNER_EY_ENTRY, 2270.84f, 1784.08f, 1186.76f, 2.42601f, 0, 0, 0.936672f, 0.350207f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_H_BANNER_MAGE_TOWER_LEFT, BG_OBJECT_H_BANNER_EY_ENTRY, 2269.13f, 1737.7f, 1186.66f, 0.994838f, 0, 0, 0.477159f, 0.878817f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_H_BANNER_MAGE_TOWER_RIGHT, BG_OBJECT_H_BANNER_EY_ENTRY, 2300.86f, 1741.25f, 1187.7f, -0.785398f, 0, 0, 0.382683f, -0.92388f, RESPAWN_ONE_DAY)
        // banners (natural)
        || !AddObject(BG_EY_OBJECT_N_BANNER_FEL_REAVER_CENTER, BG_OBJECT_N_BANNER_EY_ENTRY, 2057.46f, 1735.07f, 1187.91f, -0.925024f, 0, 0, 0.446198f, -0.894934f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_N_BANNER_FEL_REAVER_LEFT, BG_OBJECT_N_BANNER_EY_ENTRY, 2032.25f, 1729.53f, 1190.33f, 1.8675f, 0, 0, 0.803857f, 0.594823f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_N_BANNER_FEL_REAVER_RIGHT, BG_OBJECT_N_BANNER_EY_ENTRY, 2092.35f, 1775.46f, 1187.08f, -0.401426f, 0, 0, 0.199368f, -0.979925f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_N_BANNER_BLOOD_ELF_CENTER, BG_OBJECT_N_BANNER_EY_ENTRY, 2047.19f, 1349.19f, 1189.0f, -1.62316f, 0, 0, 0.725374f, -0.688354f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_N_BANNER_BLOOD_ELF_LEFT, BG_OBJECT_N_BANNER_EY_ENTRY, 2074.32f, 1385.78f, 1194.72f, 0.488692f, 0, 0, 0.241922f, 0.970296f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_N_BANNER_BLOOD_ELF_RIGHT, BG_OBJECT_N_BANNER_EY_ENTRY, 2025.13f, 1386.12f, 1192.74f, 2.3911f, 0, 0, 0.930418f, 0.366501f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_N_BANNER_DRAENEI_RUINS_CENTER, BG_OBJECT_N_BANNER_EY_ENTRY, 2276.8f, 1400.41f, 1196.33f, 2.44346f, 0, 0, 0.939693f, 0.34202f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_N_BANNER_DRAENEI_RUINS_LEFT, BG_OBJECT_N_BANNER_EY_ENTRY, 2305.78f, 1404.56f, 1199.38f, 1.74533f, 0, 0, 0.766044f, 0.642788f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_N_BANNER_DRAENEI_RUINS_RIGHT, BG_OBJECT_N_BANNER_EY_ENTRY, 2245.4f, 1366.41f, 1195.28f, 2.21657f, 0, 0, 0.894934f, 0.446198f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_N_BANNER_MAGE_TOWER_CENTER, BG_OBJECT_N_BANNER_EY_ENTRY, 2270.84f, 1784.08f, 1186.76f, 2.42601f, 0, 0, 0.936672f, 0.350207f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_N_BANNER_MAGE_TOWER_LEFT, BG_OBJECT_N_BANNER_EY_ENTRY, 2269.13f, 1737.7f, 1186.66f, 0.994838f, 0, 0, 0.477159f, 0.878817f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_N_BANNER_MAGE_TOWER_RIGHT, BG_OBJECT_N_BANNER_EY_ENTRY, 2300.86f, 1741.25f, 1187.7f, -0.785398f, 0, 0, 0.382683f, -0.92388f, RESPAWN_ONE_DAY)
        // flags
        || !AddObject(BG_EY_OBJECT_FLAG_NETHERSTORM, BG_OBJECT_FLAG2_EY_ENTRY, 2174.782227f, 1569.054688f, 1160.361938f, -1.448624f, 0, 0, 0.662620f, -0.748956f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_FLAG_FEL_REAVER, BG_OBJECT_FLAG1_EY_ENTRY, 2044.28f, 1729.68f, 1189.96f, -0.017453f, 0, 0, 0.008727f, -0.999962f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_FLAG_BLOOD_ELF, BG_OBJECT_FLAG1_EY_ENTRY, 2048.83f, 1393.65f, 1194.49f, 0.20944f, 0, 0, 0.104528f, 0.994522f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_FLAG_DRAENEI_RUINS, BG_OBJECT_FLAG1_EY_ENTRY, 2286.56f, 1402.36f, 1197.11f, 3.72381f, 0, 0, 0.957926f, -0.287016f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_FLAG_MAGE_TOWER, BG_OBJECT_FLAG1_EY_ENTRY, 2284.48f, 1731.23f, 1189.99f, 2.89725f, 0, 0, 0.992546f, 0.121869f, RESPAWN_ONE_DAY)
        // tower cap
        || !AddObject(BG_EY_OBJECT_TOWER_CAP_FEL_REAVER, BG_OBJECT_FR_TOWER_CAP_EY_ENTRY, 2024.600708f, 1742.819580f, 1195.157715f, 2.443461f, 0, 0, 0.939693f, 0.342020f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_TOWER_CAP_BLOOD_ELF, BG_OBJECT_BE_TOWER_CAP_EY_ENTRY, 2050.493164f, 1372.235962f, 1194.563477f, 1.710423f, 0, 0, 0.754710f, 0.656059f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_TOWER_CAP_DRAENEI_RUINS, BG_OBJECT_DR_TOWER_CAP_EY_ENTRY, 2301.010498f, 1386.931641f, 1197.183472f, 1.570796f, 0, 0, 0.707107f, 0.707107f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_TOWER_CAP_MAGE_TOWER, BG_OBJECT_HU_TOWER_CAP_EY_ENTRY, 2282.121582f, 1760.006958f, 1189.707153f, 1.919862f, 0, 0, 0.819152f, 0.573576f, RESPAWN_ONE_DAY)
)
    {
        TC_LOG_ERROR("sql.sql", "BatteGroundEY: Failed to spawn some objects. The battleground was not created.");
        return false;
    }

    //buffs
    for (int i = 0; i < EY_POINTS_MAX; ++i)
    {
        AreaTriggerEntry const* at = sAreaTriggerStore.LookupEntry(m_Points_Trigger[i]);
        if (!at)
        {
            TC_LOG_ERROR("bg.battleground", "BattlegroundEY: Unknown trigger: %u", m_Points_Trigger[i]);
            continue;
        }
        if (!AddObject(BG_EY_OBJECT_SPEEDBUFF_FEL_REAVER + i * 3, Buff_Entries[0], at->Pos.X, at->Pos.Y, at->Pos.Z, 0.907571f, 0, 0, 0.438371f, 0.898794f, RESPAWN_ONE_DAY)
            || !AddObject(BG_EY_OBJECT_SPEEDBUFF_FEL_REAVER + i * 3 + 1, Buff_Entries[1], at->Pos.X, at->Pos.Y, at->Pos.Z, 0.907571f, 0, 0, 0.438371f, 0.898794f, RESPAWN_ONE_DAY)
            || !AddObject(BG_EY_OBJECT_SPEEDBUFF_FEL_REAVER + i * 3 + 2, Buff_Entries[2], at->Pos.X, at->Pos.Y, at->Pos.Z, 0.907571f, 0, 0, 0.438371f, 0.898794f, RESPAWN_ONE_DAY)
)
            TC_LOG_ERROR("bg.battleground", "BattlegroundEY: Could not spawn Speedbuff Fel Reaver.");
    }

    WorldSafeLocsEntry const* sg = sObjectMgr->GetWorldSafeLoc(EY_GRAVEYARD_MAIN_ALLIANCE);
    if (!sg || !AddSpiritGuide(EY_SPIRIT_MAIN_ALLIANCE, sg->Loc.GetPositionX(), sg->Loc.GetPositionY(), sg->Loc.GetPositionZ(), 3.124139f, TEAM_ALLIANCE))
    {
        TC_LOG_ERROR("sql.sql", "BatteGroundEY: Failed to spawn spirit guide. The battleground was not created.");
        return false;
    }

    sg = sObjectMgr->GetWorldSafeLoc(EY_GRAVEYARD_MAIN_HORDE);
    if (!sg || !AddSpiritGuide(EY_SPIRIT_MAIN_HORDE, sg->Loc.GetPositionX(), sg->Loc.GetPositionY(), sg->Loc.GetPositionZ(), 3.193953f, TEAM_HORDE))
    {
        TC_LOG_ERROR("sql.sql", "BatteGroundEY: Failed to spawn spirit guide. The battleground was not created.");
        return false;
    }

    return true;
}

void BattlegroundEY::Reset()
{
    //call parent's class reset
    Battleground::Reset();

    m_TeamScores[TEAM_ALLIANCE] = 0;
    m_TeamScores[TEAM_HORDE] = 0;
    m_TeamPointsCount[TEAM_ALLIANCE] = 0;
    m_TeamPointsCount[TEAM_HORDE] = 0;
    m_HonorScoreTics[TEAM_ALLIANCE] = 0;
    m_HonorScoreTics[TEAM_HORDE] = 0;
    m_FlagState = BG_EY_FLAG_STATE_ON_BASE;
    m_FlagCapturedBgObjectType = 0;
    m_FlagKeeper.Clear();
    m_DroppedFlagGUID.Clear();
    m_PointAddingTimer = 0;
    m_TowerCapCheckTimer = 0;
    bool isBGWeekend = sBattlegroundMgr->IsBGWeekend(GetTypeID());
    m_HonorTics = (isBGWeekend) ? BG_EY_EYWeekendHonorTicks : BG_EY_NotEYWeekendHonorTicks;

    for (uint8 i = 0; i < EY_POINTS_MAX; ++i)
    {
        m_PointOwnedByTeam[i] = EY_POINT_NO_OWNER;
        m_PointState[i] = EY_POINT_STATE_UNCONTROLLED;
        m_PointBarStatus[i] = BG_EY_PROGRESS_BAR_STATE_MIDDLE;
        m_PlayersNearPoint[i].clear();
        m_PlayersNearPoint[i].reserve(15);                  //tip size
    }
    m_PlayersNearPoint[EY_PLAYERS_OUT_OF_POINTS].clear();
    m_PlayersNearPoint[EY_PLAYERS_OUT_OF_POINTS].reserve(30);
}

void BattlegroundEY::RespawnFlag(bool send_message)
{
    if (m_FlagCapturedBgObjectType > 0)
        SpawnBGObject(m_FlagCapturedBgObjectType, RESPAWN_ONE_DAY);

    m_FlagCapturedBgObjectType = 0;
    m_FlagState = BG_EY_FLAG_STATE_ON_BASE;
    SpawnBGObject(BG_EY_OBJECT_FLAG_NETHERSTORM, RESPAWN_IMMEDIATELY);

    if (send_message)
    {
        SendBroadcastText(BG_EY_TEXT_FLAG_RESET, CHAT_MSG_BG_SYSTEM_NEUTRAL);
        PlaySoundToAll(BG_EY_SOUND_FLAG_RESET);             // flags respawned sound...
    }

    UpdateWorldState(NETHERSTORM_FLAG, 1);
}

void BattlegroundEY::RespawnFlagAfterDrop()
{
    RespawnFlag(true);

    GameObject* obj = GetBgMap()->GetGameObject(GetDroppedFlagGUID());
    if (obj)
        obj->Delete();
    else
        TC_LOG_ERROR("bg.battleground", "BattlegroundEY: Unknown dropped flag (%s).", GetDroppedFlagGUID().ToString().c_str());

    SetDroppedFlagGUID(ObjectGuid::Empty);
}

void BattlegroundEY::HandleKillPlayer(Player* player, Player* killer)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    Battleground::HandleKillPlayer(player, killer);
    EventPlayerDroppedFlag(player);
}

void BattlegroundEY::EventPlayerDroppedFlag(Player* player)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
    {
        // if not running, do not cast things at the dropper player, neither send unnecessary messages
        // just take off the aura
        if (IsFlagPickedup() && GetFlagPickerGUID() == player->GetGUID())
        {
            SetFlagPicker(ObjectGuid::Empty);
            player->RemoveAurasDueToSpell(BG_EY_NETHERSTORM_FLAG_SPELL);
        }
        return;
    }

    if (!IsFlagPickedup())
        return;

    if (GetFlagPickerGUID() != player->GetGUID())
        return;

    SetFlagPicker(ObjectGuid::Empty);
    player->RemoveAurasDueToSpell(BG_EY_NETHERSTORM_FLAG_SPELL);
    m_FlagState = BG_EY_FLAG_STATE_ON_GROUND;
    m_FlagsTimer = BG_EY_FLAG_RESPAWN_TIME;
    player->CastSpell(player, SPELL_RECENTLY_DROPPED_FLAG, true);
    player->CastSpell(player, BG_EY_PLAYER_DROPPED_FLAG_SPELL, true);
    //this does not work correctly :((it should remove flag carrier name)
    UpdateWorldState(NETHERSTORM_FLAG_STATE_HORDE, BG_EY_FLAG_STATE_WAIT_RESPAWN);
    UpdateWorldState(NETHERSTORM_FLAG_STATE_ALLIANCE, BG_EY_FLAG_STATE_WAIT_RESPAWN);

    if (player->GetTeam() == ALLIANCE)
        SendBroadcastText(BG_EY_TEXT_FLAG_DROPPED, CHAT_MSG_BG_SYSTEM_ALLIANCE);
    else
        SendBroadcastText(BG_EY_TEXT_FLAG_DROPPED, CHAT_MSG_BG_SYSTEM_HORDE);
}

void BattlegroundEY::EventPlayerClickedOnFlag(Player* player, GameObject* target_obj)
{
    if (GetStatus() != STATUS_IN_PROGRESS || IsFlagPickedup() || !player->IsWithinDistInMap(target_obj, 10))
        return;

    if (player->GetTeam() == ALLIANCE)
    {
        UpdateWorldState(NETHERSTORM_FLAG_STATE_ALLIANCE, BG_EY_FLAG_STATE_ON_PLAYER);
        PlaySoundToAll(BG_EY_SOUND_FLAG_PICKED_UP_ALLIANCE);
    }
    else
    {
        UpdateWorldState(NETHERSTORM_FLAG_STATE_HORDE, BG_EY_FLAG_STATE_ON_PLAYER);
        PlaySoundToAll(BG_EY_SOUND_FLAG_PICKED_UP_HORDE);
    }

    if (m_FlagState == BG_EY_FLAG_STATE_ON_BASE)
        UpdateWorldState(NETHERSTORM_FLAG, 0);
    m_FlagState = BG_EY_FLAG_STATE_ON_PLAYER;

    SpawnBGObject(BG_EY_OBJECT_FLAG_NETHERSTORM, RESPAWN_ONE_DAY);
    SetFlagPicker(player->GetGUID());
    //get flag aura on player
    player->CastSpell(player, BG_EY_NETHERSTORM_FLAG_SPELL, true);
    player->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_ENTER_PVP_COMBAT);

    if (player->GetTeam() == ALLIANCE)
        SendBroadcastText(BG_EY_TEXT_TAKEN_FLAG, CHAT_MSG_BG_SYSTEM_ALLIANCE, player);
    else
        SendBroadcastText(BG_EY_TEXT_TAKEN_FLAG, CHAT_MSG_BG_SYSTEM_HORDE, player);
}

void BattlegroundEY::EventTeamLostPoint(Player* player, uint32 Point)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    //Natural point
    uint32 Team = m_PointOwnedByTeam[Point];

    if (!Team)
        return;

    if (Team == ALLIANCE)
    {
        m_TeamPointsCount[TEAM_ALLIANCE]--;
        SpawnBGObject(m_LosingPointTypes[Point].DespawnObjectTypeAlliance, RESPAWN_ONE_DAY);
        SpawnBGObject(m_LosingPointTypes[Point].DespawnObjectTypeAlliance + 1, RESPAWN_ONE_DAY);
        SpawnBGObject(m_LosingPointTypes[Point].DespawnObjectTypeAlliance + 2, RESPAWN_ONE_DAY);
    }
    else
    {
        m_TeamPointsCount[TEAM_HORDE]--;
        SpawnBGObject(m_LosingPointTypes[Point].DespawnObjectTypeHorde, RESPAWN_ONE_DAY);
        SpawnBGObject(m_LosingPointTypes[Point].DespawnObjectTypeHorde + 1, RESPAWN_ONE_DAY);
        SpawnBGObject(m_LosingPointTypes[Point].DespawnObjectTypeHorde + 2, RESPAWN_ONE_DAY);
    }

    SpawnBGObject(m_LosingPointTypes[Point].SpawnNeutralObjectType, RESPAWN_IMMEDIATELY);
    SpawnBGObject(m_LosingPointTypes[Point].SpawnNeutralObjectType + 1, RESPAWN_IMMEDIATELY);
    SpawnBGObject(m_LosingPointTypes[Point].SpawnNeutralObjectType + 2, RESPAWN_IMMEDIATELY);

    //buff isn't despawned

    m_PointOwnedByTeam[Point] = EY_POINT_NO_OWNER;
    m_PointState[Point] = EY_POINT_NO_OWNER;

    if (Team == ALLIANCE)
        SendBroadcastText(m_LosingPointTypes[Point].MessageIdAlliance, CHAT_MSG_BG_SYSTEM_ALLIANCE, player);
    else
        SendBroadcastText(m_LosingPointTypes[Point].MessageIdHorde, CHAT_MSG_BG_SYSTEM_HORDE, player);

    UpdatePointsIcons(Team, Point);
    UpdatePointsCount(Team);

    //remove bonus honor aura trigger creature when node is lost
     if (Point < EY_POINTS_MAX)
         DelCreature(Point + 6);//NULL checks are in DelCreature! 0-5 spirit guides
}

void BattlegroundEY::EventTeamCapturedPoint(Player* player, uint32 Point)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    uint32 Team = player->GetTeam();

    SpawnBGObject(m_CapturingPointTypes[Point].DespawnNeutralObjectType, RESPAWN_ONE_DAY);
    SpawnBGObject(m_CapturingPointTypes[Point].DespawnNeutralObjectType + 1, RESPAWN_ONE_DAY);
    SpawnBGObject(m_CapturingPointTypes[Point].DespawnNeutralObjectType + 2, RESPAWN_ONE_DAY);

    if (Team == ALLIANCE)
    {
        m_TeamPointsCount[TEAM_ALLIANCE]++;
        SpawnBGObject(m_CapturingPointTypes[Point].SpawnObjectTypeAlliance, RESPAWN_IMMEDIATELY);
        SpawnBGObject(m_CapturingPointTypes[Point].SpawnObjectTypeAlliance + 1, RESPAWN_IMMEDIATELY);
        SpawnBGObject(m_CapturingPointTypes[Point].SpawnObjectTypeAlliance + 2, RESPAWN_IMMEDIATELY);
    }
    else
    {
        m_TeamPointsCount[TEAM_HORDE]++;
        SpawnBGObject(m_CapturingPointTypes[Point].SpawnObjectTypeHorde, RESPAWN_IMMEDIATELY);
        SpawnBGObject(m_CapturingPointTypes[Point].SpawnObjectTypeHorde + 1, RESPAWN_IMMEDIATELY);
        SpawnBGObject(m_CapturingPointTypes[Point].SpawnObjectTypeHorde + 2, RESPAWN_IMMEDIATELY);
    }

    //buff isn't respawned

    m_PointOwnedByTeam[Point] = Team;
    m_PointState[Point] = EY_POINT_UNDER_CONTROL;

    if (Team == ALLIANCE)
        SendBroadcastText(m_CapturingPointTypes[Point].MessageIdAlliance, CHAT_MSG_BG_SYSTEM_ALLIANCE, player);
    else
        SendBroadcastText(m_CapturingPointTypes[Point].MessageIdHorde, CHAT_MSG_BG_SYSTEM_HORDE, player);

    if (!BgCreatures[Point].IsEmpty())
        DelCreature(Point);

    WorldSafeLocsEntry const* sg = sObjectMgr->GetWorldSafeLoc(m_CapturingPointTypes[Point].GraveYardId);
    if (!sg || !AddSpiritGuide(Point, sg->Loc.GetPositionX(), sg->Loc.GetPositionY(), sg->Loc.GetPositionZ(), 3.124139f, GetTeamIndexByTeamId(Team)))
        TC_LOG_ERROR("bg.battleground", "BatteGroundEY: Failed to spawn spirit guide. point: %u, team: %u, graveyard_id: %u",
            Point, Team, m_CapturingPointTypes[Point].GraveYardId);

//    SpawnBGCreature(Point, RESPAWN_IMMEDIATELY);

    UpdatePointsIcons(Team, Point);
    UpdatePointsCount(Team);

    if (Point >= EY_POINTS_MAX)
        return;

    Creature* trigger = GetBGCreature(Point + 6, false);//0-5 spirit guides
    if (!trigger)
        trigger = AddCreature(WORLD_TRIGGER, Point+6, BG_EY_TriggerPositions[Point], GetTeamIndexByTeamId(Team));

    //add bonus honor aura trigger creature when node is accupied
    //cast bonus aura (+50% honor in 25yards)
    //aura should only apply to players who have accupied the node, set correct faction for trigger
    if (trigger)
    {
        trigger->SetFaction(Team == ALLIANCE ? 84 : 83);
        trigger->CastSpell(trigger, SPELL_HONORABLE_DEFENDER_25Y, false);
    }
}

void BattlegroundEY::EventPlayerCapturedFlag(Player* player, uint32 BgObjectType)
{
    if (GetStatus() != STATUS_IN_PROGRESS || GetFlagPickerGUID() != player->GetGUID())
        return;

    SetFlagPicker(ObjectGuid::Empty);
    m_FlagState = BG_EY_FLAG_STATE_WAIT_RESPAWN;
    player->RemoveAurasDueToSpell(BG_EY_NETHERSTORM_FLAG_SPELL);

    player->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_ENTER_PVP_COMBAT);

    if (player->GetTeam() == ALLIANCE)
    {
        SendBroadcastText(BG_EY_TEXT_ALLIANCE_CAPTURED_FLAG, CHAT_MSG_BG_SYSTEM_ALLIANCE, player);
        PlaySoundToAll(BG_EY_SOUND_FLAG_CAPTURED_ALLIANCE);
    }
    else
    {
        SendBroadcastText(BG_EY_TEXT_HORDE_CAPTURED_FLAG, CHAT_MSG_BG_SYSTEM_HORDE, player);
        PlaySoundToAll(BG_EY_SOUND_FLAG_CAPTURED_HORDE);
    }

    SpawnBGObject(BgObjectType, RESPAWN_IMMEDIATELY);

    m_FlagsTimer = BG_EY_FLAG_RESPAWN_TIME;
    m_FlagCapturedBgObjectType = BgObjectType;

    uint8 team_id = player->GetTeam() == ALLIANCE ? TEAM_ALLIANCE : TEAM_HORDE;
    if (m_TeamPointsCount[team_id] > 0)
        AddPoints(player->GetTeam(), BG_EY_FlagPoints[m_TeamPointsCount[team_id] - 1]);

    UpdatePlayerScore(player, SCORE_FLAG_CAPTURES, 1);
}

bool BattlegroundEY::UpdatePlayerScore(Player* player, uint32 type, uint32 value, bool doAddHonor)
{
    if (!Battleground::UpdatePlayerScore(player, type, value, doAddHonor))
        return false;

    switch (type)
    {
        case SCORE_FLAG_CAPTURES:
            player->UpdateCriteria(CRITERIA_TYPE_BG_OBJECTIVE_CAPTURE, EY_OBJECTIVE_CAPTURE_FLAG);
            break;
        default:
            break;
    }
    return true;
}

void BattlegroundEY::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    packet.Worldstates.emplace_back(uint32(EY_HORDE_BASE), int32(m_TeamPointsCount[TEAM_HORDE]));
    packet.Worldstates.emplace_back(uint32(EY_ALLIANCE_BASE), int32(m_TeamPointsCount[TEAM_ALLIANCE]));
    packet.Worldstates.emplace_back(0xAB6, 0x0);
    packet.Worldstates.emplace_back(0xAB5, 0x0);
    packet.Worldstates.emplace_back(0xAB4, 0x0);
    packet.Worldstates.emplace_back(0xAB3, 0x0);
    packet.Worldstates.emplace_back(0xAB2, 0x0);
    packet.Worldstates.emplace_back(0xAB1, 0x0);
    packet.Worldstates.emplace_back(0xAB0, 0x0);
    packet.Worldstates.emplace_back(0xAAF, 0x0);

    packet.Worldstates.emplace_back(uint32(DRAENEI_RUINS_HORDE_CONTROL), int32(m_PointOwnedByTeam[DRAENEI_RUINS] == HORDE && m_PointState[DRAENEI_RUINS] == EY_POINT_UNDER_CONTROL));
    packet.Worldstates.emplace_back(uint32(DRAENEI_RUINS_ALLIANCE_CONTROL), int32(m_PointOwnedByTeam[DRAENEI_RUINS] == ALLIANCE && m_PointState[DRAENEI_RUINS] == EY_POINT_UNDER_CONTROL));
    packet.Worldstates.emplace_back(uint32(DRAENEI_RUINS_UNCONTROL), int32(m_PointState[DRAENEI_RUINS] != EY_POINT_UNDER_CONTROL));
    packet.Worldstates.emplace_back(uint32(MAGE_TOWER_ALLIANCE_CONTROL), int32(m_PointOwnedByTeam[MAGE_TOWER] == ALLIANCE && m_PointState[MAGE_TOWER] == EY_POINT_UNDER_CONTROL));
    packet.Worldstates.emplace_back(uint32(MAGE_TOWER_HORDE_CONTROL), int32(m_PointOwnedByTeam[MAGE_TOWER] == HORDE && m_PointState[MAGE_TOWER] == EY_POINT_UNDER_CONTROL));
    packet.Worldstates.emplace_back(uint32(MAGE_TOWER_UNCONTROL), int32(m_PointState[MAGE_TOWER] != EY_POINT_UNDER_CONTROL));
    packet.Worldstates.emplace_back(uint32(FEL_REAVER_HORDE_CONTROL), int32(m_PointOwnedByTeam[FEL_REAVER] == HORDE && m_PointState[FEL_REAVER] == EY_POINT_UNDER_CONTROL));
    packet.Worldstates.emplace_back(uint32(FEL_REAVER_ALLIANCE_CONTROL), int32(m_PointOwnedByTeam[FEL_REAVER] == ALLIANCE && m_PointState[FEL_REAVER] == EY_POINT_UNDER_CONTROL));
    packet.Worldstates.emplace_back(uint32(FEL_REAVER_UNCONTROL), int32(m_PointState[FEL_REAVER] != EY_POINT_UNDER_CONTROL));
    packet.Worldstates.emplace_back(uint32(BLOOD_ELF_HORDE_CONTROL), int32(m_PointOwnedByTeam[BLOOD_ELF] == HORDE && m_PointState[BLOOD_ELF] == EY_POINT_UNDER_CONTROL));
    packet.Worldstates.emplace_back(uint32(BLOOD_ELF_ALLIANCE_CONTROL), int32(m_PointOwnedByTeam[BLOOD_ELF] == ALLIANCE && m_PointState[BLOOD_ELF] == EY_POINT_UNDER_CONTROL));
    packet.Worldstates.emplace_back(uint32(BLOOD_ELF_UNCONTROL), int32(m_PointState[BLOOD_ELF] != EY_POINT_UNDER_CONTROL));
    packet.Worldstates.emplace_back(uint32(NETHERSTORM_FLAG), int32(m_FlagState == BG_EY_FLAG_STATE_ON_BASE));

    packet.Worldstates.emplace_back(uint32(EY_ACTIVATE_HORDE_RESOURCES), int32(true));
    packet.Worldstates.emplace_back(uint32(EY_ACTIVATE_ALLIANCE_RESOURCES), int32(true));
    packet.Worldstates.emplace_back(uint32(EY_MAX_RESOURCES), int32(BG_EY_MAX_TEAM_SCORE));

    packet.Worldstates.emplace_back(0xABE, int32(GetTeamScore(TEAM_HORDE)));
    packet.Worldstates.emplace_back(0xABD, int32(GetTeamScore(TEAM_ALLIANCE)));

    packet.Worldstates.emplace_back(0xA05, 0x8E);
    packet.Worldstates.emplace_back(0xAA0, 0x0);
    packet.Worldstates.emplace_back(0xA9F, 0x0);
    packet.Worldstates.emplace_back(0xA9E, 0x0);
    packet.Worldstates.emplace_back(0xC0D, 0x17B);
}

WorldSafeLocsEntry const* BattlegroundEY::GetClosestGraveYard(Player* player)
{
    uint32 g_id = 0;

    switch (player->GetTeam())
    {
        case ALLIANCE: g_id = EY_GRAVEYARD_MAIN_ALLIANCE; break;
        case HORDE:    g_id = EY_GRAVEYARD_MAIN_HORDE;    break;
        default:       return nullptr;
    }

    float distance, nearestDistance;

    WorldSafeLocsEntry const* entry = nullptr;
    WorldSafeLocsEntry const* nearestEntry = nullptr;
    entry = sObjectMgr->GetWorldSafeLoc(g_id);
    nearestEntry = entry;

    if (!entry)
    {
        TC_LOG_ERROR("bg.battleground", "BattlegroundEY: The main team graveyard could not be found. The graveyard system will not be operational!");
        return nullptr;
    }

    float plr_x = player->GetPositionX();
    float plr_y = player->GetPositionY();
    float plr_z = player->GetPositionZ();

    distance = (entry->Loc.GetPositionX() - plr_x) * (entry->Loc.GetPositionX() - plr_x)
        + (entry->Loc.GetPositionY() - plr_y) * (entry->Loc.GetPositionY() - plr_y)
        + (entry->Loc.GetPositionZ() - plr_z) * (entry->Loc.GetPositionZ() - plr_z);
    nearestDistance = distance;

    for (uint8 i = 0; i < EY_POINTS_MAX; ++i)
    {
        if (m_PointOwnedByTeam[i] == player->GetTeam() && m_PointState[i] == EY_POINT_UNDER_CONTROL)
        {
            entry = sObjectMgr->GetWorldSafeLoc(m_CapturingPointTypes[i].GraveYardId);
            if (!entry)
                TC_LOG_ERROR("bg.battleground", "BattlegroundEY: Graveyard %u could not be found.", m_CapturingPointTypes[i].GraveYardId);
            else
            {
                distance = (entry->Loc.GetPositionX() - plr_x) * (entry->Loc.GetPositionX() - plr_x)
                    + (entry->Loc.GetPositionY() - plr_y) * (entry->Loc.GetPositionY() - plr_y)
                    + (entry->Loc.GetPositionZ() - plr_z) * (entry->Loc.GetPositionZ() - plr_z);
                if (distance < nearestDistance)
                {
                    nearestDistance = distance;
                    nearestEntry = entry;
                }
            }
        }
    }

    return nearestEntry;
}

WorldSafeLocsEntry const* BattlegroundEY::GetExploitTeleportLocation(Team team)
{
    return sObjectMgr->GetWorldSafeLoc(team == ALLIANCE ? EY_EXPLOIT_TELEPORT_LOCATION_ALLIANCE : EY_EXPLOIT_TELEPORT_LOCATION_HORDE);
}

bool BattlegroundEY::IsAllNodesControlledByTeam(uint32 team) const
{
    uint32 count = 0;
    for (int i = 0; i < EY_POINTS_MAX; ++i)
        if (m_PointOwnedByTeam[i] == team && m_PointState[i] == EY_POINT_UNDER_CONTROL)
            ++count;

    return count == EY_POINTS_MAX;
}

uint32 BattlegroundEY::GetPrematureWinner()
{
    if (GetTeamScore(TEAM_ALLIANCE) > GetTeamScore(TEAM_HORDE))
        return ALLIANCE;
    else if (GetTeamScore(TEAM_HORDE) > GetTeamScore(TEAM_ALLIANCE))
        return HORDE;

    return Battleground::GetPrematureWinner();
}
