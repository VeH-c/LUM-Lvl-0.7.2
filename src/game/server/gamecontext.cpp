/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/math.h>

#include <engine/shared/config.h>
#include <engine/shared/memheap.h>
#include <engine/map.h>

#include <generated/server_data.h>
#include <game/collision.h>
#include <game/gamecore.h>
#include <game/version.h>

#include "entities/character.h"
#include "gamemodes/ctf.h"
#include "gamemodes/dm.h"
#include "gamemodes/lms.h"
#include "gamemodes/lts.h"
#include "gamemodes/mod.h"
#include "gamemodes/tdm.h"
#include "gamecontext.h"
#include "player.h"

#include <string.h>
#include <stdio.h>
#include <Windows.h>
#include <time.h>

enum
{
	RESET,
	NO_RESET
};

enum
{
	ACC_USERNAME,
	ACC_PASSWORD,
	ACC_STATUS,
	ACC_UTIL_1,
	ACC_UTIL_2,
	ACC_UTIL_3,
	ACC_UTIL_4,
	ACC_UTIL_5,
	ACC_LEVEL,
	ACC_EXPERIENCE,
	ACC_MONEY,
	ACC_LVL_HAMMER,
	ACC_LVL_GUN,
	ACC_LVL_SHOTGUN,
	ACC_LVL_GRENADE,
	ACC_LVL_RIFLE,
	ACC_LVL_LIFE,
	ACC_LVL_HANDLE,
	ACC_NAME_PLAYER
};

void CGameContext::Construct(int Resetting)
{
	m_Resetting = 0;
	m_pServer = 0;

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		m_apPlayers[i] = 0;
		m_ClUpdateTime[i] = m_ClUpdateTimeStart;
	}

	m_pController = 0;
	m_VoteCloseTime = 0;
	m_VoteCancelTime = 0;
	m_pVoteOptionFirst = 0;
	m_pVoteOptionLast = 0;
	m_NumVoteOptions = 0;
	m_LockTeams = 0;

	// base damages
	m_aBaseDmg[0] = 3;// hammer
	m_aBaseDmg[1] = 1;// gun
	m_aBaseDmg[2] = 1;// shotgun
	m_aBaseDmg[3] = 6;// grenade
	m_aBaseDmg[4] = 5;// rifle

	// tune mod settings
	if (TuneModSettings(FILEPATH_MODSETTINGS) == false)
	{
		printf("[%s]: error initializing %s\n", __func__, FILEPATH_MODSETTINGS);
	}
	else
	{
		printf("[%s]: successfully initialized %s\n", __func__, FILEPATH_MODSETTINGS);
	}

	if(Resetting==NO_RESET)
		m_pVoteOptionHeap = new CHeap();
}

CGameContext::CGameContext(int Resetting)
{
	Construct(Resetting);
}

CGameContext::CGameContext()
{
	Construct(NO_RESET);
}

CGameContext::~CGameContext()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
		delete m_apPlayers[i];
	if(!m_Resetting)
		delete m_pVoteOptionHeap;
}

void CGameContext::Clear()
{
	CHeap *pVoteOptionHeap = m_pVoteOptionHeap;
	CVoteOptionServer *pVoteOptionFirst = m_pVoteOptionFirst;
	CVoteOptionServer *pVoteOptionLast = m_pVoteOptionLast;
	int NumVoteOptions = m_NumVoteOptions;
	CTuningParams Tuning = m_Tuning;

	m_Resetting = true;
	this->~CGameContext();
	mem_zero(this, sizeof(*this));
	new (this) CGameContext(RESET);

	m_pVoteOptionHeap = pVoteOptionHeap;
	m_pVoteOptionFirst = pVoteOptionFirst;
	m_pVoteOptionLast = pVoteOptionLast;
	m_NumVoteOptions = NumVoteOptions;
	m_Tuning = Tuning;
}


class CCharacter *CGameContext::GetPlayerChar(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !m_apPlayers[ClientID])
		return 0;
	return m_apPlayers[ClientID]->GetCharacter();
}

void CGameContext::CreateDamage(vec2 Pos, int Id, vec2 Source, int HealthAmount, int ArmorAmount, bool Self)
{
	float f = angle(Source);
	CNetEvent_Damage *pEvent = (CNetEvent_Damage *)m_Events.Create(NETEVENTTYPE_DAMAGE, sizeof(CNetEvent_Damage));
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_ClientID = Id;
		pEvent->m_Angle = (int)(f*256.0f);
		pEvent->m_HealthAmount = HealthAmount;
		pEvent->m_ArmorAmount = ArmorAmount;
		pEvent->m_Self = Self;
	}
}

void CGameContext::CreateHammerHit(vec2 Pos)
{
	// create the event
	CNetEvent_HammerHit *pEvent = (CNetEvent_HammerHit *)m_Events.Create(NETEVENTTYPE_HAMMERHIT, sizeof(CNetEvent_HammerHit));
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}
}

/* levels
	0 - All knockback / damage
	1 - No self knockback / damage
	2 - No knockback / damage
*/
void CGameContext::CreateExplosionExt(vec2 Pos, int Owner, int Weapon, float Damage, int KnockLvl, int DamageLvl, bool Sound)
{
	// create the event
	CNetEvent_Explosion *pEvent = (CNetEvent_Explosion *)m_Events.Create(NETEVENTTYPE_EXPLOSION, sizeof(CNetEvent_Explosion));
	if (pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}

	// sound
	if (Sound)
		CreateSound(Pos, SOUND_GRENADE_EXPLODE);

	// deal damage
	CCharacter *apEnts[MAX_CLIENTS];
	float Radius = 135.0f;
	float InnerRadius = 48.0f;
	int Num = m_World.FindEntities(Pos, Radius, (CEntity**)apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
	for (int i = 0; i < Num; i++)
	{
		if (!apEnts[i]) // crash safety
			continue;

		vec2 Diff = apEnts[i]->GetPos() - Pos;
		vec2 ForceDir(0, 0);
		vec2 ModKnockback(0, 0);
		int ModDamage = 0;

		float l = length(Diff);
		if (l)
			ForceDir = normalize(Diff);
		l = 1 - clamp((l - InnerRadius) / (Radius - InnerRadius), 0.0f, 1.0f);
		float Dmg = Damage * l;
		if ((int)Dmg)
		{
			ModDamage = (int)Dmg;
			ModKnockback = ForceDir * (6 * l) * 2;

			switch (KnockLvl)
			{
			case 0://all knockback
				apEnts[i]->ImpulseAdd(ModKnockback);
				break;

			case 1://no self knockback
				if (apEnts[i]->GetPlayer()->GetCID() != Owner)
				{
					apEnts[i]->ImpulseAdd(ModKnockback);
				}
				break;
			}

			switch (DamageLvl)
			{
			case 0://all damage
				apEnts[i]->TakeDamage(ModDamage, Owner, Weapon);
				break;

			case 1://no self damage
				if (apEnts[i]->GetPlayer()->GetCID() != Owner)
				{
					apEnts[i]->TakeDamage(ModDamage, Owner, Weapon);
				}
				break;
			}
		}
	}
}

void CGameContext::CreatePlayerSpawn(vec2 Pos)
{
	// create the event
	CNetEvent_Spawn *ev = (CNetEvent_Spawn *)m_Events.Create(NETEVENTTYPE_SPAWN, sizeof(CNetEvent_Spawn));
	if(ev)
	{
		ev->m_X = (int)Pos.x;
		ev->m_Y = (int)Pos.y;
	}
}

void CGameContext::CreateDeath(vec2 Pos, int ClientID)
{
	// create the event
	CNetEvent_Death *pEvent = (CNetEvent_Death *)m_Events.Create(NETEVENTTYPE_DEATH, sizeof(CNetEvent_Death));
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_ClientID = ClientID;
	}
}

void CGameContext::CreateSound(vec2 Pos, int Sound, int64 Mask)
{
	if (Sound < 0)
		return;

	// create a sound
	CNetEvent_SoundWorld *pEvent = (CNetEvent_SoundWorld *)m_Events.Create(NETEVENTTYPE_SOUNDWORLD, sizeof(CNetEvent_SoundWorld), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_SoundID = Sound;
	}
}

void CGameContext::SendChat(int ChatterClientID, int Mode, int To, const char *pText)
{
	char aBuf[256];
	char aCName[256] = { 0 };

	if(ChatterClientID >= 0 && ChatterClientID < MAX_CLIENTS)
		str_format(aBuf, sizeof(aBuf), "%d:%d:%s: %s", ChatterClientID, Mode, Server()->ClientName(ChatterClientID), pText);
	else
		str_format(aBuf, sizeof(aBuf), "*** %s", pText);

	char aBufMode[32];
	if (Mode == CHAT_WHISPER)
		str_copy(aBufMode, "whisper", sizeof(aBufMode));
	else if (Mode == CHAT_TEAM)
		str_copy(aBufMode, "teamchat", sizeof(aBufMode));
	else if (Mode == CHAT_ALL)
		str_copy(aBufMode, "chat", sizeof(aBufMode));
	else if (Mode == CHAT_NONE)
		str_copy(aBufMode, "chat", sizeof(aBufMode));

	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, aBufMode, aBuf);

	CNetMsg_Sv_Chat Msg;
	Msg.m_Mode = Mode;
	Msg.m_ClientID = ChatterClientID;
	Msg.m_pMessage = pText;
	Msg.m_TargetID = -1;

	if(Mode == CHAT_ALL)
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
	else if(Mode == CHAT_TEAM)
	{
		// pack one for the recording only
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NOSEND, -1);

		To = m_apPlayers[ChatterClientID]->GetTeam();

		// send to the clients
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_apPlayers[i] && m_apPlayers[i]->GetTeam() == To)
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
		}
	}
	else if(Mode == CHAT_WHISPER)
	{
		// send to the clients
		Msg.m_TargetID = To;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ChatterClientID);
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, To);
	}
	else if(Mode == CHAT_NONE)
	{
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, To);
	}
}

void CGameContext::SendBroadcast(const char* pText, int ClientID)
{
	CNetMsg_Sv_Broadcast Msg;
	Msg.m_pMessage = pText;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendEmoticon(int ClientID, int Emoticon)
{
	CNetMsg_Sv_Emoticon Msg;
	Msg.m_ClientID = ClientID;
	Msg.m_Emoticon = Emoticon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
}

void CGameContext::SendWeaponPickup(int ClientID, int Weapon)
{
	CNetMsg_Sv_WeaponPickup Msg;
	Msg.m_Weapon = Weapon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendMotd(int ClientID)
{
	CNetMsg_Sv_Motd Msg;
	Msg.m_pMessage = g_Config.m_SvMotd;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendSettings(int ClientID)
{
	CNetMsg_Sv_ServerSettings Msg;
	Msg.m_KickVote = g_Config.m_SvVoteKick;
	Msg.m_KickMin = g_Config.m_SvVoteKickMin;
	Msg.m_SpecVote = g_Config.m_SvVoteSpectate;
	Msg.m_TeamLock = m_LockTeams != 0;
	Msg.m_TeamBalance = g_Config.m_SvTeambalanceTime != 0;
	Msg.m_PlayerSlots = g_Config.m_SvPlayerSlots;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendGameMsg(int GameMsgID, int ClientID)
{
	CMsgPacker Msg(NETMSGTYPE_SV_GAMEMSG);
	Msg.AddInt(GameMsgID);
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendGameMsg(int GameMsgID, int ParaI1, int ClientID)
{
	CMsgPacker Msg(NETMSGTYPE_SV_GAMEMSG);
	Msg.AddInt(GameMsgID);
	Msg.AddInt(ParaI1);
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendGameMsg(int GameMsgID, int ParaI1, int ParaI2, int ParaI3, int ClientID)
{
	CMsgPacker Msg(NETMSGTYPE_SV_GAMEMSG);
	Msg.AddInt(GameMsgID);
	Msg.AddInt(ParaI1);
	Msg.AddInt(ParaI2);
	Msg.AddInt(ParaI3);
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

//
void CGameContext::StartVote(const char *pDesc, const char *pCommand, const char *pReason)
{
	// check if a vote is already running
	if(m_VoteCloseTime)
		return;

	// reset votes
	m_VoteEnforce = VOTE_ENFORCE_UNKNOWN;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			m_apPlayers[i]->m_Vote = 0;
			m_apPlayers[i]->m_VotePos = 0;
		}
	}

	// start vote
	m_VoteCloseTime = time_get() + time_freq()*VOTE_TIME;
	m_VoteCancelTime = time_get() + time_freq()*VOTE_CANCEL_TIME;
	str_copy(m_aVoteDescription, pDesc, sizeof(m_aVoteDescription));
	str_copy(m_aVoteCommand, pCommand, sizeof(m_aVoteCommand));
	str_copy(m_aVoteReason, pReason, sizeof(m_aVoteReason));
	SendVoteSet(m_VoteType, -1);
	m_VoteUpdate = true;
}


void CGameContext::EndVote(int Type, bool Force)
{
	m_VoteCloseTime = 0;
	m_VoteCancelTime = 0;
	if(Force)
		m_VoteCreator = -1;
	SendVoteSet(Type, -1);
}

void CGameContext::ForceVote(int Type, const char *pDescription, const char *pReason)
{
	CNetMsg_Sv_VoteSet Msg;
	Msg.m_Type = Type;
	Msg.m_Timeout = 0;
	Msg.m_ClientID = -1;
	Msg.m_pDescription = pDescription;
	Msg.m_pReason = pReason;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
}

void CGameContext::SendVoteSet(int Type, int ToClientID)
{
	CNetMsg_Sv_VoteSet Msg;
	if(m_VoteCloseTime)
	{
		Msg.m_ClientID = m_VoteCreator;
		Msg.m_Type = Type;
		Msg.m_Timeout = (m_VoteCloseTime-time_get())/time_freq();
		Msg.m_pDescription = m_aVoteDescription;
		Msg.m_pReason = m_aVoteReason;
	}
	else
	{
		Msg.m_Type = Type;
		Msg.m_Timeout = 0;
		Msg.m_ClientID = m_VoteCreator;
		Msg.m_pDescription = "";
		Msg.m_pReason = "";
	}
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ToClientID);
}

void CGameContext::SendVoteStatus(int ClientID, int Total, int Yes, int No)
{
	CNetMsg_Sv_VoteStatus Msg = {0};
	Msg.m_Total = Total;
	Msg.m_Yes = Yes;
	Msg.m_No = No;
	Msg.m_Pass = Total - (Yes+No);

	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);

}

void CGameContext::AbortVoteOnDisconnect(int ClientID)
{
	if(m_VoteCloseTime && ClientID == m_VoteClientID && (!str_comp_num(m_aVoteCommand, "kick ", 5) ||
		!str_comp_num(m_aVoteCommand, "set_team ", 9) || (!str_comp_num(m_aVoteCommand, "ban ", 4) && Server()->IsBanned(ClientID))))
		m_VoteCloseTime = -1;
}

void CGameContext::AbortVoteOnTeamChange(int ClientID)
{
	if(m_VoteCloseTime && ClientID == m_VoteClientID && !str_comp_num(m_aVoteCommand, "set_team ", 9))
		m_VoteCloseTime = -1;
}


void CGameContext::CheckPureTuning()
{
	// might not be created yet during start up
	if(!m_pController)
		return;

	if(	str_comp(m_pController->GetGameType(), "DM")==0 ||
		str_comp(m_pController->GetGameType(), "TDM")==0 ||
		str_comp(m_pController->GetGameType(), "CTF")==0 ||
		str_comp(m_pController->GetGameType(), "LMS")==0 ||
		str_comp(m_pController->GetGameType(), "LTS")==0)
	{
		CTuningParams p;
		if(mem_comp(&p, &m_Tuning, sizeof(p)) != 0)
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "resetting tuning due to pure server");
			m_Tuning = p;
		}
	}
}

void CGameContext::SendTuningParams(int ClientID)
{
	CheckPureTuning();

	CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
	int *pParams = (int *)&m_Tuning;
	for(unsigned i = 0; i < sizeof(m_Tuning)/sizeof(int); i++)
		Msg.AddInt(pParams[i]);
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SwapTeams()
{
	if(!m_pController->IsTeamplay())
		return;

	SendGameMsg(GAMEMSG_TEAM_SWAP, -1);

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
			m_pController->DoTeamChange(m_apPlayers[i], m_apPlayers[i]->GetTeam()^1, false);
	}
}

void CGameContext::OnTick()
{
	// check tuning
	CheckPureTuning();

	// copy tuning
	m_World.m_Core.m_Tuning = m_Tuning;
	m_World.Tick();

	//if(world.paused) // make sure that the game object always updates
	m_pController->Tick();

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			m_apPlayers[i]->Tick();
			m_apPlayers[i]->PostTick();
			
			if (m_apPlayers[i]->m_Player_logged == true)
			{
				if (m_ClUpdateTime[i] > 0)
					m_ClUpdateTime[i]--;
			}
		}
	}

	// send login help broadcast as long as the player remains unlogged
	if (!m_HelpBroadcastDelay)
	{
		m_HelpBroadcastDelay = m_HelpBroadcastDelayDefault * Server()->TickSpeed();

		for (int i = 0; i < MAX_CLIENTS; ++i)
		{
			if (!m_apPlayers[i] || m_apPlayers[i]->IsDummy())
				continue;

			if (!m_apPlayers[i]->m_Player_logged)
				SendBroadcast("To join, write into chat:\n/register username password - registers your account\n/login username password - logs you in", m_apPlayers[i]->GetCID());
		}
	}

	// update an account when its time again
	if (m_AccUpdateTime <= 0)
	{
		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			// continue if not existing
			if (!m_apPlayers[i])
				continue;

			// continue if not logged
			if (m_apPlayers[i]->m_Player_logged != true)
				continue;

			// update first queued client and take a break
			if (m_ClUpdateTime[i] <= 0)
			{
				m_AccUpdateTime = m_AccUpdateDelay;

				AccountUpdate(m_apPlayers[i]->GetCID());
				m_ClUpdateTime[i] = m_ClUpdateSet + MAX_CLIENTS * m_AccUpdateDelay;
				break;
			}
		}
	}
	else
	{
		m_AccUpdateTime--;
	}

	// tell stats delay
	for (int i = 0; i < MAX_CLIENTS; ++i)
	{
		if (m_aTellDelay[i])
			m_aTellDelay[i]--;
	}

	// submit ticket delay
	for (int i = 0; i < MAX_CLIENTS; ++i)
	{
		if (m_aTicketDelay[i])
			m_aTicketDelay[i]--;
	}

	// redeem code delay
	for (int i = 0; i < MAX_CLIENTS; ++i)
	{
		if (m_aRedeemDelay[i])
			m_aRedeemDelay[i]--;
	}

	// event times save delay
	if (m_EvtFileUpdateTime)
		m_EvtFileUpdateTime--;

	// help broadcast delay
	if (m_HelpBroadcastDelay)
		m_HelpBroadcastDelay--;

	// botchat delay
	if (m_botchat_delay)
		m_botchat_delay--;

	// handle event system
	HandleEventSystem();

	// update voting
	if(m_VoteCloseTime)
	{
		// abort the kick-vote on player-leave
		if(m_VoteCloseTime == -1)
			EndVote(VOTE_END_ABORT, false);
		else
		{
			int Total = 0, Yes = 0, No = 0;
			if(m_VoteUpdate)
			{
				// count votes
				char aaBuf[MAX_CLIENTS][NETADDR_MAXSTRSIZE] = {{0}};
				for(int i = 0; i < MAX_CLIENTS; i++)
					if(m_apPlayers[i])
						Server()->GetClientAddr(i, aaBuf[i], NETADDR_MAXSTRSIZE);
				bool aVoteChecked[MAX_CLIENTS] = {0};
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(!m_apPlayers[i] || m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS || aVoteChecked[i] || m_apPlayers[i]->IsDummy())	// don't count in votes by spectators
						continue;

					int ActVote = m_apPlayers[i]->m_Vote;
					int ActVotePos = m_apPlayers[i]->m_VotePos;

					// check for more players with the same ip (only use the vote of the one who voted first)
					for(int j = i+1; j < MAX_CLIENTS; ++j)
					{
						if(!m_apPlayers[j] || aVoteChecked[j] || str_comp(aaBuf[j], aaBuf[i]))
							continue;

						aVoteChecked[j] = true;
						if(m_apPlayers[j]->m_Vote && (!ActVote || ActVotePos > m_apPlayers[j]->m_VotePos))
						{
							ActVote = m_apPlayers[j]->m_Vote;
							ActVotePos = m_apPlayers[j]->m_VotePos;
						}
					}

					Total++;
					if(ActVote > 0)
						Yes++;
					else if(ActVote < 0)
						No++;
				}
			}

			if(m_VoteEnforce == VOTE_ENFORCE_YES || (m_VoteUpdate && Yes >= Total/2+1))
			{
				Server()->SetRconCID(IServer::RCON_CID_VOTE);
				Console()->ExecuteLine(m_aVoteCommand);
				Server()->SetRconCID(IServer::RCON_CID_SERV);
				if(m_VoteCreator != -1 && m_apPlayers[m_VoteCreator])
					m_apPlayers[m_VoteCreator]->m_LastVoteCall = 0;

				EndVote(VOTE_END_PASS, m_VoteEnforce==VOTE_ENFORCE_YES);

				HandleCustomVote(m_aVoteCommand);
			}
			else if(m_VoteEnforce == VOTE_ENFORCE_NO || (m_VoteUpdate && No >= (Total+1)/2) || time_get() > m_VoteCloseTime)
				EndVote(VOTE_END_FAIL, m_VoteEnforce==VOTE_ENFORCE_NO);
			else if(m_VoteUpdate)
			{
				m_VoteUpdate = false;
				SendVoteStatus(-1, Total, Yes, No);
			}
		}
	}

	//*dummy handle system
	HandleDummySystem();

	// dont allow sv_register 1 if the version is private
	if (IS_PRIVATE_VERSION)
	{
		if (g_Config.m_SvRegister == 1)
			exit(0);
	}

#ifdef CONF_DEBUG
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->IsDummy())
		{
			CNetObj_PlayerInput Input = {0};
			Input.m_Direction = (i&1)?-1:1;
			m_apPlayers[i]->OnPredictedInput(&Input);
		}
	}
#endif
}

void CGameContext::HandleDummySystem()
{
	// *dummy system
	int amtBotsFinal = 0;

	m_has_human_players = false;
	m_has_human_active_players = false;

	// check for human players
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!m_apPlayers[i])
			continue;

		if (!m_apPlayers[i]->IsDummy())
		{
			m_has_human_players = true;
			
			if (m_apPlayers[i]->m_Player_logged && m_apPlayers[i]->m_Player_status != 1 && m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
			{
				m_has_human_active_players = true;
				break;
			}
		}
	}

	// always keep a certain amount of dummies around
	amtBotsFinal = clamp(amtBotsFinal + m_Vote_AmountBots, 0, (int)MAX_PLAYERS);

	int maxdummies = min(amtBotsFinal, max(0, MAX_PLAYERS - AmtHumanPlayers()));

	while (AmtDummies() < maxdummies)
		DummyAdd(1);
	while (AmtDummies() > maxdummies)
		DummyRemove(1);

	// swap weapons every once in a while
	if (m_dummy_wepswapdur >= Server()->TickSpeed() * 300)
	{
		for (int i = 0; i < MAX_PLAYERS; ++i)
		{
			if (!m_apPlayers[i] || !m_apPlayers[i]->IsDummy())
				continue;

			m_apPlayers[i]->m_dum_wprimpref = floor(min((int)WEAPON_LASER, (int)floor(frandom() * 5)));
			m_apPlayers[i]->m_dum_wsecpref = frandom() > 0.5 ? WEAPON_GUN : WEAPON_HAMMER;

			// if preferred secondary weapon is equal to primary weapon change that
			if (m_apPlayers[i]->m_dum_wsecpref == m_apPlayers[i]->m_dum_wprimpref)
				m_apPlayers[i]->m_dum_wsecpref = m_apPlayers[i]->m_dum_wprimpref == WEAPON_GUN ? WEAPON_HAMMER : WEAPON_GUN;
		
			// use switchmode 50% of the time
			if (frandom() > 0.5f)
				m_apPlayers[i]->m_aPlayer_option[0] = 1;
			else
				m_apPlayers[i]->m_aPlayer_option[0] = 0;
		}

		m_dummy_wepswapdur = 0;
	}

	m_dummy_wepswapdur++;

	// update dummy stats
	if (m_has_human_players)
	{
		for (int i = 0; i < MAX_PLAYERS; ++i)
			DummyAdaptLevel(i);
	}
	else
	{
		m_Vote_AmountBots = m_AmountBotsDefault;

		// reset bot chat index when there are no players
		m_botchat_index = 0;// reset botchat index
	}

	// if the dummy with the highest bot ID exists send a message to newcomers if they are chatting to bots
	if (m_botchat_delay == 1)
	{
		if (m_apPlayers[15])
		{
			SendChat(15, TEAM_RED, -1, m_aBotChatLine[m_botchat_index]);
			WriteChatLog("%s: %s", m_apPlayers[15]->m_dum_name, m_aBotChatLine[m_botchat_index]);

			if (m_botchat_index < 11)
				m_botchat_index++;
			else
				m_botchat_index = 0;
		}
	}
}

void CGameContext::HandleEventSystem()
{
	// note: max amount of event timers: 10
	FILE *fpointer;

	int numberEvents = 3;// number of events choosable
	int eventChosen = 0;// event that is chosen randomly
	int eventsActive = 0;// if any event is active
	char broadcastMessage[256] = { 0 };// broadcasting message

	char fileType[128] = { 0 };// buffer for type in file;
	char fileTime[128] = { 0 };// buffer for time in file;

	time_t my_time;
	tm *timeinfo;

	time(&my_time);
	timeinfo = localtime(&my_time);

	// random event trigger
	if (m_EvtTriggerTime <= 0)
	{
		// choose random event if not -1 (starting timer)
		if (m_EvtTriggerTime == 0)
		{
			eventChosen = floor(frandom() * numberEvents);

			m_EvtTime[eventChosen] += Server()->TickSpeed() * m_EvtDurationBase;

			// chatlog info that event has started
//			WriteChatLog("[%02d:%02d]<><><><><><><><><><><><><><><> Event '%s' has started!", timeinfo->tm_hour, timeinfo->tm_min, m_aEventName[eventChosen]);
		}

		m_EvtTriggerTime = Server()->TickSpeed() * (300 + frandom() * 2700 + m_EvtDurationBase);// min 5min, max 45min between events
	}
	else
		m_EvtTriggerTime--;

	// info that event has ended
	for (int i = 0; i < numberEvents; ++i)
	{
//		if (m_EvtTime[i] == 1)
//			WriteChatLog("[%02d:%02d]<><><><><><><><><><><><><><><> Event '%s' has ended!", timeinfo->tm_hour, timeinfo->tm_min, m_aEventName[i]);
	}

	// count down event timers
	for (int i = 0; i < numberEvents; ++i)
		if (m_EvtTime[i])
			m_EvtTime[i]--;

	if (m_EvtBroadcastTime)
		m_EvtBroadcastTime--;
	else
		m_EvtBroadcastTime = Server()->TickSpeed() * 1;

	// double experience
	if (m_EvtTime[0] > 0)
		m_ExpRatio = 2;
	else
		m_ExpRatio = 1;

	// low gravity
	if (m_EvtTime[1] > 0)
	{
		// world
		Tuning()->m_Gravity = m_GravityDefault * m_EvtGravityScale;
		Tuning()->m_AirControlSpeed = Tuning()->m_GroundControlSpeed;
		Tuning()->m_AirControlAccel = Tuning()->m_GroundControlAccel;
		Tuning()->m_AirFriction = 1;
		// hook
		Tuning()->m_HookDragSpeed = 30;
		Tuning()->m_HookLength = 9999;
		// weapons
		Tuning()->m_GunCurvature = m_GunCurvatureDefault * m_EvtGravityScale;
		Tuning()->m_ShotgunCurvature = m_ShotgunCurvatureDefault * m_EvtGravityScale;
		Tuning()->m_GrenadeCurvature = m_GrenadeCurvatureDefault * m_EvtGravityScale;

		Tuning()->m_GunSpeed = m_GunSpeedDefault * m_EvtGravityScale;
		Tuning()->m_ShotgunSpeed = m_ShotgunSpeedDefault * m_EvtGravityScale;
		Tuning()->m_GrenadeSpeed = m_GrenadeSpeedDefault * m_EvtGravityScale;

		Tuning()->m_GunLifetime = m_GunLifetimeDefault * (2 - m_EvtGravityScale);
		Tuning()->m_ShotgunLifetime = m_ShotgunLifetimeDefault * (2 - m_EvtGravityScale);
		Tuning()->m_GrenadeLifetime = m_GrenadeLifetimeDefault * (2 - m_EvtGravityScale);

		SendTuningParams(-1);
	}
	else
	{
		// world
		Tuning()->m_Gravity = m_GravityDefault;
		Tuning()->m_AirControlSpeed = m_AirControlSpeedDefault;
		Tuning()->m_AirControlAccel = m_AirControlAccelDefault;
		Tuning()->m_AirFriction = m_AirFrictionDefault;
		// hook
		Tuning()->m_HookDragSpeed = m_HookDragSpeedDefault;
		Tuning()->m_HookLength = m_HookLengthDefault;
		// weapons
		Tuning()->m_GunCurvature = m_GunCurvatureDefault;
		Tuning()->m_ShotgunCurvature = m_ShotgunCurvatureDefault;
		Tuning()->m_GrenadeCurvature = m_GrenadeCurvatureDefault;

		Tuning()->m_GunSpeed = m_GunSpeedDefault;
		Tuning()->m_ShotgunSpeed = m_ShotgunSpeedDefault;
		Tuning()->m_GrenadeSpeed = m_GrenadeSpeedDefault;

		Tuning()->m_GunLifetime = m_GunLifetimeDefault;
		Tuning()->m_ShotgunLifetime = m_ShotgunLifetimeDefault;
		Tuning()->m_GrenadeLifetime = m_GrenadeLifetimeDefault;

		SendTuningParams(-1);
	}

	// rapid fire
	if (m_EvtTime[2] > 0)
	{
		m_EvtBonusHandle = 100;
		m_EvtNoclip = true;
	}
	else
	{
		m_EvtBonusHandle = 0;
		m_EvtNoclip = false;
	}

	// broadcast active events to players
	for (int i = 0; i < 10; ++i)
	{
		if (m_EvtTime[i] > 0)
		{
			eventsActive = 1;
			break;
		}
	}

	if (eventsActive)
	{
		str_format(broadcastMessage, sizeof(broadcastMessage), "Event: ");

		// add event names
		for (int i = 0; i < 10; ++i)
		{
			if (m_EvtTime[i] >= Server()->TickSpeed() * 9)
			{
				if (eventsActive > 1)
					strcat(broadcastMessage, " / ");

				strcat(broadcastMessage, m_aEventName[i]);
				eventsActive++;
			}
		}

		if (m_EvtBroadcastTime == 0 && eventsActive > 1)
		{
			for (int i = 0; i < MAX_CLIENTS; ++i)
			{
				if (!m_apPlayers[i] || !m_apPlayers[i]->m_Player_logged)
					continue;

				SendBroadcast(broadcastMessage, m_apPlayers[i]->GetCID());
			}
		}
	}

	// save event times for persistent events (throughout map change, etc...)
	if (!m_EvtFileUpdateTime)
	{
		m_EvtFileUpdateTime = m_EvtFileUpdateTimeBase * Server()->TickSpeed();

		// read times
		for (int i = 0; i < 10; ++i)
		{
			GetFromFile(i * 2, fileType, sizeof(fileType), FILEPATH_EVENTTIMES);
			GetFromFile(i * 2 + 1, fileTime, sizeof(fileTime), FILEPATH_EVENTTIMES);

			if (!atoi(fileTime))// no more entries
				break;

			// if event is not active, write event
			if (!m_EvtTime[atoi(fileType)])
				m_EvtTime[atoi(fileType)] = atoi(fileTime);
		}

		// write times
		fpointer = fopen(FILEPATH_EVENTTIMES, "w");

		for (int i = 0; i < 10; ++i)
		{
			if (m_EvtTime[i])
			{
				fprintf(fpointer, "%d\n", i);
				fprintf(fpointer, "%d\n", max(0, m_EvtTime[i] - m_EvtFileUpdateTimeBase * Server()->TickSpeed()));
			}

			// don't write negative numbers to file
		}

		fclose(fpointer);
	}
}

// Server hooks
void CGameContext::OnClientDirectInput(int ClientID, void *pInput)
{
	int NumFailures = m_NetObjHandler.NumObjFailures();
	if(m_NetObjHandler.ValidateObj(NETOBJTYPE_PLAYERINPUT, pInput, sizeof(CNetObj_PlayerInput)) == -1)
	{
		if(g_Config.m_Debug && NumFailures != m_NetObjHandler.NumObjFailures())
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "NETOBJTYPE_PLAYERINPUT failed on '%s'", m_NetObjHandler.FailedObjOn());
			Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
		}
	}
	else
		m_apPlayers[ClientID]->OnDirectInput((CNetObj_PlayerInput *)pInput);
}

void CGameContext::OnClientPredictedInput(int ClientID, void *pInput)
{
	if(!m_World.m_Paused)
	{
		int NumFailures = m_NetObjHandler.NumObjFailures();
		if(m_NetObjHandler.ValidateObj(NETOBJTYPE_PLAYERINPUT, pInput, sizeof(CNetObj_PlayerInput)) == -1)
		{
			if(g_Config.m_Debug && NumFailures != m_NetObjHandler.NumObjFailures())
			{
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "NETOBJTYPE_PLAYERINPUT corrected on '%s'", m_NetObjHandler.FailedObjOn());
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
			}
		}
		else
			m_apPlayers[ClientID]->OnPredictedInput((CNetObj_PlayerInput *)pInput);
	}
}

void CGameContext::OnClientEnter(int ClientID)
{
	m_pController->OnPlayerConnect(m_apPlayers[ClientID]);

	// send starter help
	if (m_apPlayers[ClientID] && !m_apPlayers[ClientID]->IsDummy())
	{
		SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, m_aSepLine);
		SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "Welcome to the LUM level server!");
		ShowAccountHelp(ClientID);
		SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/help - show command list");
		SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, m_aSepLine);

//		broadcast help is sent permanently until logged in in OnTick()
	}

	m_VoteUpdate = true;

	// update client infos (others before local)
	CNetMsg_Sv_ClientInfo NewClientInfoMsg;
	NewClientInfoMsg.m_ClientID = ClientID;
	NewClientInfoMsg.m_Local = 0;
	NewClientInfoMsg.m_Team = m_apPlayers[ClientID]->GetTeam();
	NewClientInfoMsg.m_pName = Server()->ClientName(ClientID);
	NewClientInfoMsg.m_pClan = Server()->ClientClan(ClientID);
	NewClientInfoMsg.m_Country = Server()->ClientCountry(ClientID);
	NewClientInfoMsg.m_Silent = false;

	// *dummy overwrite info
	if (m_apPlayers[ClientID]->IsDummy())
	{
		NewClientInfoMsg.m_pName = m_apPlayers[ClientID]->m_dum_name;
		NewClientInfoMsg.m_pClan = m_apPlayers[ClientID]->m_dum_clan;
		NewClientInfoMsg.m_Country = m_apPlayers[ClientID]->m_dum_country;
	}
	/**********************/

	if(g_Config.m_SvSilentSpectatorMode && m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS)
		NewClientInfoMsg.m_Silent = true;

	for(int p = 0; p < 6; p++)
	{
		NewClientInfoMsg.m_apSkinPartNames[p] = m_apPlayers[ClientID]->m_TeeInfos.m_aaSkinPartNames[p];
		NewClientInfoMsg.m_aUseCustomColors[p] = m_apPlayers[ClientID]->m_TeeInfos.m_aUseCustomColors[p];
		NewClientInfoMsg.m_aSkinPartColors[p] = m_apPlayers[ClientID]->m_TeeInfos.m_aSkinPartColors[p];
	}

	for (int i = 0; i < MAX_CLIENTS; ++i)
	{
		if (i == ClientID || !m_apPlayers[i] || (!Server()->ClientIngame(i) && !m_apPlayers[i]->IsDummy()))
			continue;

		// new info for others
		if (Server()->ClientIngame(i))
			Server()->SendPackMsg(&NewClientInfoMsg, MSGFLAG_VITAL | MSGFLAG_NORECORD, i);

		// existing infos for new player
		CNetMsg_Sv_ClientInfo ClientInfoMsg;
		ClientInfoMsg.m_ClientID = i;
		ClientInfoMsg.m_Local = 0;
		ClientInfoMsg.m_Team = m_apPlayers[i]->GetTeam();
		ClientInfoMsg.m_pName = Server()->ClientName(i);
		ClientInfoMsg.m_pClan = Server()->ClientClan(i);
		ClientInfoMsg.m_Country = Server()->ClientCountry(i);
		ClientInfoMsg.m_Silent = false;

		// *dummy overwrite info
		if (m_apPlayers[i]->IsDummy())
		{
			ClientInfoMsg.m_pName = m_apPlayers[i]->m_dum_name;
			ClientInfoMsg.m_pClan = m_apPlayers[i]->m_dum_clan;
			ClientInfoMsg.m_Country = m_apPlayers[i]->m_dum_country;
		}
		/**********************/

		for (int p = 0; p < 6; p++)
		{
			ClientInfoMsg.m_apSkinPartNames[p] = m_apPlayers[i]->m_TeeInfos.m_aaSkinPartNames[p];
			ClientInfoMsg.m_aUseCustomColors[p] = m_apPlayers[i]->m_TeeInfos.m_aUseCustomColors[p];
			ClientInfoMsg.m_aSkinPartColors[p] = m_apPlayers[i]->m_TeeInfos.m_aSkinPartColors[p];
		}

		Server()->SendPackMsg(&ClientInfoMsg, MSGFLAG_VITAL|MSGFLAG_NORECORD, ClientID);
	}

	// local info
	NewClientInfoMsg.m_Local = 1;
	Server()->SendPackMsg(&NewClientInfoMsg, MSGFLAG_VITAL|MSGFLAG_NORECORD, ClientID);

	if(Server()->DemoRecorder_IsRecording())
	{
		CNetMsg_De_ClientEnter Msg;
		Msg.m_pName = NewClientInfoMsg.m_pName;
		Msg.m_Team = NewClientInfoMsg.m_Team;
		Server()->SendPackMsg(&Msg, MSGFLAG_NOSEND, -1);
	}
}

void CGameContext::OnClientConnected(int ClientID, bool Dummy)
{
	// remove dummy if player wants to connect on that slot
	if (m_apPlayers[ClientID] && m_apPlayers[ClientID]->IsDummy())
	{
		delete m_apPlayers[ClientID];
		m_apPlayers[ClientID] = 0;
	}

	m_apPlayers[ClientID] = new(ClientID) CPlayer(this, ClientID, Dummy);

	if(Dummy)
		return;

	// send active vote
	if(m_VoteCloseTime)
		SendVoteSet(m_VoteType, ClientID);

	// send motd
	SendMotd(ClientID);

	// send settings
	SendSettings(ClientID);
}

void CGameContext::OnClientTeamChange(int ClientID)
{
	if(m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS)
		AbortVoteOnTeamChange(ClientID);
}

void CGameContext::OnClientDrop(int ClientID, const char *pReason)
{
	if (!m_apPlayers[ClientID])
		return;

	// update account
	if (!m_apPlayers[ClientID]->IsDummy())
	{
		if (m_apPlayers[ClientID]->m_Player_logged == true)
			AccountUpdate(ClientID);
	}

	AbortVoteOnDisconnect(ClientID);
	m_pController->OnPlayerDisconnect(m_apPlayers[ClientID]);

	// update clients on drop
	if(Server()->ClientIngame(ClientID))
	{
		if(Server()->DemoRecorder_IsRecording())
		{
			CNetMsg_De_ClientLeave Msg;
			Msg.m_pName = Server()->ClientName(ClientID);
			Msg.m_pReason = pReason;
			Server()->SendPackMsg(&Msg, MSGFLAG_NOSEND, -1);
		}

		CNetMsg_Sv_ClientDrop Msg;
		Msg.m_ClientID = ClientID;
		Msg.m_pReason = pReason;
		Msg.m_Silent = false;
		if(g_Config.m_SvSilentSpectatorMode && m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS)
			Msg.m_Silent = true;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, -1);
	}

	delete m_apPlayers[ClientID];
	m_apPlayers[ClientID] = 0;

	m_VoteUpdate = true;
}

void CGameContext::OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID)
{
	void *pRawMsg = m_NetObjHandler.SecureUnpackMsg(MsgID, pUnpacker);
	CPlayer *pPlayer = m_apPlayers[ClientID];

	if(!pRawMsg)
	{
		if(g_Config.m_Debug)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "dropped weird message '%s' (%d), failed on '%s'", m_NetObjHandler.GetMsgName(MsgID), MsgID, m_NetObjHandler.FailedMsgOn());
			Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
		}
		return;
	}

	if(Server()->ClientIngame(ClientID))
	{
		if(MsgID == NETMSGTYPE_CL_SAY)
		{
			if(g_Config.m_SvSpamprotection && pPlayer->m_LastChat && pPlayer->m_LastChat+Server()->TickSpeed() > Server()->Tick())
				return;

			CNetMsg_Cl_Say *pMsg = (CNetMsg_Cl_Say *)pRawMsg;

			// trim right and set maximum length to 128 utf8-characters
			int Length = 0;
			const char *p = pMsg->m_pMessage;
			const char *pEnd = 0;
			while(*p)
			{
				const char *pStrOld = p;
				int Code = str_utf8_decode(&p);

				// check if unicode is not empty
				if(Code > 0x20 && Code != 0xA0 && Code != 0x034F && (Code < 0x2000 || Code > 0x200F) && (Code < 0x2028 || Code > 0x202F) &&
					(Code < 0x205F || Code > 0x2064) && (Code < 0x206A || Code > 0x206F) && (Code < 0xFE00 || Code > 0xFE0F) &&
					Code != 0xFEFF && (Code < 0xFFF9 || Code > 0xFFFC))
				{
					pEnd = 0;
				}
				else if(pEnd == 0)
					pEnd = pStrOld;

				if(++Length >= 127)
				{
					*(const_cast<char *>(p)) = 0;
					break;
				}
			}
			if(pEnd != 0)
				*(const_cast<char *>(pEnd)) = 0;

			// drop empty and autocreated spam messages (more than 20 characters per second)
			if(Length == 0 || (g_Config.m_SvSpamprotection && pPlayer->m_LastChat && pPlayer->m_LastChat + Server()->TickSpeed()*(Length/20) > Server()->Tick()))
				return;

			// process chat input
			if (pMsg->m_pMessage[0] == '/')
			{
				int cnt = 0;

				char aComString[MAX_INPUT_SIZE] = { 0 };//contains command string
				char aComPart[4][MAX_INPUT_SIZE] = { 0 };//contains up to three commands
				char aMsgPart[MAX_INPUT_SIZE] = { 0 };// message part
				char *pChar;
				char aTextBuf[MAX_INPUT_SIZE] = { 0 };
				char aWeaponName[7][MAX_INPUT_SIZE] = { "hammer", "gun", "shotgun", "grenade", "rifle", "life", "handle" };

				str_copy(aComString, pMsg->m_pMessage, sizeof(aComString));//copy message to buffer
				memmove(aComString, aComString + 1, sizeof(aComString));//remove first character from buffer

				//tokenize command string
				pChar = strtok(aComString, " ");

				while (pChar != NULL)
				{
					str_copy(aComPart[cnt], pChar, sizeof(aComPart[0]));
					pChar = strtok(NULL, " ");
					cnt++;

					if (cnt == 4)
						break;
				}

				// for message commands we need the whole rest of the string
				cnt = 0;
				cnt = strlen(aComPart[0]) + 2;
				while (1) {
					str_format(aMsgPart, sizeof(aMsgPart), "%s%c", aMsgPart, pMsg->m_pMessage[cnt]);
					if (pMsg->m_pMessage[cnt] == '\0')
						break;
					cnt++;
				}
				//evaluate commands
				if (str_comp_nocase(aComPart[0], "info") == 0)//server info
				{
					SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, m_aSepLine);
					SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "*** Levels, Upgrades & Mayhem ~ By Nolay ***");
					SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, m_aSepLine);// TODO - add donations
					SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "Contact: Nolay.LUM@gmail.com");
					SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "Discord: discord.gg/ju4z5Kj");
					SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "Mod details: Teeworlds forum --> Search --> \"LUM\"");
					//					SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "Donations are welcome!");
					SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, m_aSepLine);
				}
				else if (str_comp_nocase(aComPart[0], "rules") == 0)// server rules
				{
					SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, m_aSepLine);
					SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "No bad language, show us your good manners");
					SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "No cheating, be a sportsmanlike player");
					SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "No afk / farming, show us some action");
					SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, m_aSepLine);
				}
				else if (str_comp_nocase(aComPart[0], "topten") == 0)// show top ten players
				{
					ShowTopTen(ClientID);
				}
				else if (str_comp_nocase(aComPart[0], "help") == 0)//help commands
				{
					if (str_comp_nocase(aComPart[1], "game") == 0)//help game
					{
						ShowGameHelp(ClientID);
					}
					else if (str_comp_nocase(aComPart[1], "account") == 0)//help account
					{
						ShowAccountHelp(ClientID);
					}
					else if (str_comp_nocase(aComPart[1], "moderator") == 0)//help moderator
					{
						if (m_apPlayers[ClientID]->m_Player_status == 2 || m_apPlayers[ClientID]->m_Player_status == 3) // if moderator or admin
						{
							ShowModeratorHelp(ClientID);
						}
						else
						{
							ShowDefaultHelp(ClientID);
						}
					}
					else if (str_comp_nocase(aComPart[1], "emote") == 0)//help emote
					{
						ShowEmoteHelp(ClientID);
					}
					else//help server
					{
						ShowServerHelp(ClientID);
					}
				}
				else if (str_comp_nocase(aComPart[0], "upgr") == 0)//upgrade command
				{
					UpgradeStats(ClientID, aComPart[1], aComPart[2]);
				}
				else if (str_comp_nocase(aComPart[0], "stats") == 0)//list stats
				{
					ShowStats(ClientID, aComPart[1]);
				}
				else if (str_comp_nocase(aComPart[0], "switchmode") == 0)//switch weapon mode
				{
					ToggleSwitchMode(ClientID);
				}
				else if (str_comp_nocase(aComPart[0], "showexp") == 0)//showexp
				{
					ToggleShowExp(ClientID);
				}
				else if (str_comp_nocase(aComPart[0], "register") == 0)//register account
				{
					AccountRegister(ClientID, aComPart[1], aComPart[2]);
				}
				else if (str_comp_nocase(aComPart[0], "login") == 0)//login account
				{
					AccountLogIn(ClientID, aComPart[1], aComPart[2]);
				}
				else if (str_comp_nocase(aComPart[0], "logout") == 0)//login account
				{
					AccountLogOut(ClientID);
				}
				else if (str_comp_nocase(aComPart[0], "newpassword") == 0)//change password
				{
					AccountChangePassword(ClientID, aComPart[1], aComPart[2], aComPart[3]);
				}
				else if (str_comp_nocase(aComPart[0], "ticket") == 0)//send a ticket to the creator
				{
					SubmitTicket(ClientID, aMsgPart);
				}
				else if (str_comp_nocase(aComPart[0], "redeem") == 0)//redeem a code
				{
					RedeemCode(ClientID, aComPart[1]);
				}
				else if (str_comp_nocase(aComPart[0], "angry") == 0)//emote angry
				{
					SetTeeEmote(ClientID, EMOTE_ANGRY);
				}
				else if (str_comp_nocase(aComPart[0], "happy") == 0)//emote happy
				{
					SetTeeEmote(ClientID, EMOTE_HAPPY);
				}
				else if (str_comp_nocase(aComPart[0], "default") == 0)//emote default
				{
					SetTeeEmote(ClientID, EMOTE_NORMAL);
				}
				else if (str_comp_nocase(aComPart[0], "pain") == 0)//emote pain
				{
					SetTeeEmote(ClientID, EMOTE_PAIN);
				}
				else if (str_comp_nocase(aComPart[0], "blink") == 0)//emote blink
				{
					SetTeeEmote(ClientID, EMOTE_BLINK);
				}
				else if (str_comp_nocase(aComPart[0], "surprise") == 0)//emote surprise
				{
					SetTeeEmote(ClientID, EMOTE_SURPRISE);
				}
				else if (str_comp_nocase(aComPart[0], "notify") == 0)//moderator send notification
				{
					if (m_apPlayers[ClientID]->m_Player_status == 2 || m_apPlayers[ClientID]->m_Player_status == 3) // if moderator or admin
					{//******************************************************
						SendChat(TEAM_SPECTATORS, CHAT_ALL, ClientID, "######################################");
						SendChat(TEAM_SPECTATORS, CHAT_ALL, ClientID, "*");
						SendChat(TEAM_SPECTATORS, CHAT_ALL, ClientID, aMsgPart);
						SendChat(TEAM_SPECTATORS, CHAT_ALL, ClientID, "*");
						SendChat(TEAM_SPECTATORS, CHAT_ALL, ClientID, "######################################");
					}
					else
						ShowDefaultHelp(ClientID);
				}
				else if (str_comp_nocase(aComPart[0], "undercover") == 0)//moderator undercover
				{
					if (m_apPlayers[ClientID]->m_Player_status == 2 || m_apPlayers[ClientID]->m_Player_status == 3) // if moderator or admin
						ToggleUndercover(ClientID);
					else
						ShowDefaultHelp(ClientID);
				}
				else if (str_comp_nocase(aComPart[0], "freeze") == 0)//moderator freeze player
				{
					if (m_apPlayers[ClientID]->m_Player_status == 2 || m_apPlayers[ClientID]->m_Player_status == 3) // if moderator or admin
						FreezePlayer(aComPart[1], ClientID, 0);
					else
						ShowDefaultHelp(ClientID);
				}
				else if (str_comp_nocase(aComPart[0], "unfreeze") == 0)//moderator unfreeze player
				{
					if (m_apPlayers[ClientID]->m_Player_status == 2 || m_apPlayers[ClientID]->m_Player_status == 3) // if moderator or admin
						FreezePlayer(aComPart[1], ClientID, 1);
					else
						ShowDefaultHelp(ClientID);
				}
				else if (str_comp_nocase(aComPart[0], "idlist") == 0)//moderator idlist
				{
					if (m_apPlayers[ClientID]->m_Player_status == 2 || m_apPlayers[ClientID]->m_Player_status == 3) // if moderator or admin
						ShowIdList(ClientID);
					else
						ShowDefaultHelp(ClientID);
				}
				else if (str_comp_nocase(aComPart[0], "showstats") == 0)//moderator showstats
				{
					if (m_apPlayers[ClientID]->m_Player_status == 2 || m_apPlayers[ClientID]->m_Player_status == 3) // if moderator or admin
						ShowPlayerStats(atoi(aComPart[1]), ClientID);
					else
						ShowDefaultHelp(ClientID);
				}
				else if (str_comp_nocase(aComPart[0], "kick") == 0)// moderator kick player
				{
					if (m_apPlayers[ClientID]->m_Player_status == 2 || m_apPlayers[ClientID]->m_Player_status == 3) // if moderator or admin
					{
						TreatPlayer(aComPart[1], ClientID, 0, atoi(aComPart[2]));
					}
					else
						ShowDefaultHelp(ClientID);
				}
				else if (str_comp_nocase(aComPart[0], "ban") == 0)// moderator kick player
				{
					if (m_apPlayers[ClientID]->m_Player_status == 2 || m_apPlayers[ClientID]->m_Player_status == 3) // if moderator or admin
					{
						TreatPlayer(aComPart[1], ClientID, 1, atoi(aComPart[2]));
					}
					else
						ShowDefaultHelp(ClientID);
				}
				else if (str_comp_nocase(aComPart[0], "hesoyam") == 0)// GTA san andreas easter egg ^^
				{
					SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "This is not GTA");

					// log in modlog
					WriteModLog("%s has found the GTA easter egg", Server()->ClientName(ClientID));
				}
				/*else if (str_comp(aComPart[0], "CowBelliesAreFluffy") == 0)// Crash the server with a secret command that only the developer knows
				{
					// log in modlog
					WriteModLog("%s has crashed the server... DELIBERATELY. Not so smart now are ya? (I could do this all day)", Server()->ClientName(ClientID));
					exit(0);
				}*/
				else//type command that does not exist show help general
				{
					ShowDefaultHelp(ClientID);
				}
			}
			else
			{
				pPlayer->m_LastChat = Server()->Tick();

				// don't allow spectators to disturb players during a running game in tournament mode
				int Mode = pMsg->m_Mode;
				if ((g_Config.m_SvTournamentMode == 2) &&
					pPlayer->GetTeam() == TEAM_SPECTATORS &&
					m_pController->IsGameRunning() &&
					!Server()->IsAuthed(ClientID))
				{
					if (Mode != CHAT_WHISPER)
						Mode = CHAT_TEAM;
					else if (m_apPlayers[pMsg->m_Target] && m_apPlayers[pMsg->m_Target]->GetTeam() != TEAM_SPECTATORS)
						Mode = CHAT_NONE;
				}

				if (Mode != CHAT_NONE)
				{
					SendChat(ClientID, Mode, pMsg->m_Target, pMsg->m_pMessage);

					// if there are no active players, bots are helping
					if (!m_has_human_active_players)
						m_botchat_delay = Server()->TickSpeed() * 0.5;
				}

				WriteChatLog("[%s]%s: %s", m_apPlayers[ClientID]->m_Player_username, Server()->ClientName(ClientID), pMsg->m_pMessage);
			}
		}
		else if(MsgID == NETMSGTYPE_CL_CALLVOTE)
		{
			// update accounts to not lose too much progress when switching maps
			for (int i = 0; i < MAX_CLIENTS; ++i)
			{
				if (!m_apPlayers[i])
					continue;

				if (m_apPlayers[i]->m_Player_logged != true)
					continue;

				AccountUpdate(m_apPlayers[i]->GetCID());
			}

			CNetMsg_Cl_CallVote *pMsg = (CNetMsg_Cl_CallVote *)pRawMsg;
			int64 Now = Server()->Tick();

			if(pMsg->m_Force)
			{
				if(!Server()->IsAuthed(ClientID))
					return;
			}
			else
			{
				if((g_Config.m_SvSpamprotection && ((pPlayer->m_LastVoteTry && pPlayer->m_LastVoteTry+Server()->TickSpeed()*3 > Now) ||
					(pPlayer->m_LastVoteCall && pPlayer->m_LastVoteCall+Server()->TickSpeed()*VOTE_COOLDOWN > Now))) ||
					pPlayer->GetTeam() == TEAM_SPECTATORS || m_VoteCloseTime)
					return;

				pPlayer->m_LastVoteTry = Now;
			}

			m_VoteType = VOTE_UNKNOWN;
			char aDesc[VOTE_DESC_LENGTH] = {0};
			char aCmd[VOTE_CMD_LENGTH] = {0};
			const char *pReason = pMsg->m_Reason[0] ? pMsg->m_Reason : "No reason given";

			if(str_comp_nocase(pMsg->m_Type, "option") == 0)
			{
				CVoteOptionServer *pOption = m_pVoteOptionFirst;
				while(pOption)
				{
					if(str_comp_nocase(pMsg->m_Value, pOption->m_aDescription) == 0)
					{
						str_format(aDesc, sizeof(aDesc), "%s", pOption->m_aDescription);
						str_format(aCmd, sizeof(aCmd), "%s", pOption->m_aCommand);

						if (pMsg->m_Force)
						{
							Server()->SetRconCID(ClientID);
							Console()->ExecuteLine(aCmd);
							Server()->SetRconCID(IServer::RCON_CID_SERV);
							ForceVote(VOTE_START_OP, aDesc, pReason);
							return;
						}

						m_VoteType = VOTE_START_OP;

						break;
					}

					pOption = pOption->m_pNext;
				}

				if(!pOption)
					return;
			}
			else if(str_comp_nocase(pMsg->m_Type, "kick") == 0)
			{
				if(!g_Config.m_SvVoteKick || m_pController->GetRealPlayerNum() < g_Config.m_SvVoteKickMin)
					return;

				int KickID = str_toint(pMsg->m_Value);
				if(KickID < 0 || KickID >= MAX_CLIENTS || !m_apPlayers[KickID] || KickID == ClientID || Server()->IsAuthed(KickID))
					return;

				str_format(aDesc, sizeof(aDesc), "%2d: %s", KickID, Server()->ClientName(KickID));
				if (!g_Config.m_SvVoteKickBantime)
					str_format(aCmd, sizeof(aCmd), "kick %d Kicked by vote", KickID);
				else
				{
					char aAddrStr[NETADDR_MAXSTRSIZE] = {0};
					Server()->GetClientAddr(KickID, aAddrStr, sizeof(aAddrStr));
					str_format(aCmd, sizeof(aCmd), "ban %s %d Banned by vote", aAddrStr, g_Config.m_SvVoteKickBantime);
				}
				if(pMsg->m_Force)
				{
					Server()->SetRconCID(ClientID);
					Console()->ExecuteLine(aCmd);
					Server()->SetRconCID(IServer::RCON_CID_SERV);
					return;
				}
				m_VoteType = VOTE_START_KICK;
				m_VoteClientID = KickID;
			}
			else if(str_comp_nocase(pMsg->m_Type, "spectate") == 0)
			{
				if(!g_Config.m_SvVoteSpectate)
					return;

				int SpectateID = str_toint(pMsg->m_Value);
				if(SpectateID < 0 || SpectateID >= MAX_CLIENTS || !m_apPlayers[SpectateID] || m_apPlayers[SpectateID]->GetTeam() == TEAM_SPECTATORS || SpectateID == ClientID)
					return;

				str_format(aDesc, sizeof(aDesc), "%2d: %s", SpectateID, Server()->ClientName(SpectateID));
				str_format(aCmd, sizeof(aCmd), "set_team %d -1 %d", SpectateID, g_Config.m_SvVoteSpectateRejoindelay);
				if(pMsg->m_Force)
				{
					Server()->SetRconCID(ClientID);
					Console()->ExecuteLine(aCmd);
					Server()->SetRconCID(IServer::RCON_CID_SERV);
					ForceVote(VOTE_START_SPEC, aDesc, pReason);
					return;
				}
				m_VoteType = VOTE_START_SPEC;
				m_VoteClientID = SpectateID;
			}

			if(m_VoteType != VOTE_UNKNOWN)
			{
				m_VoteCreator = ClientID;
				StartVote(aDesc, aCmd, pReason);
				pPlayer->m_Vote = 1;
				pPlayer->m_VotePos = m_VotePos = 1;
				pPlayer->m_LastVoteCall = Now;
			}
		}
		else if(MsgID == NETMSGTYPE_CL_VOTE)
		{
			if(!m_VoteCloseTime)
				return;

			if(pPlayer->m_Vote == 0)
			{
				CNetMsg_Cl_Vote *pMsg = (CNetMsg_Cl_Vote *)pRawMsg;
				if(!pMsg->m_Vote)
					return;

				pPlayer->m_Vote = pMsg->m_Vote;
				pPlayer->m_VotePos = ++m_VotePos;
				m_VoteUpdate = true;
			}
			else if(m_VoteCreator == pPlayer->GetCID())
			{
				CNetMsg_Cl_Vote *pMsg = (CNetMsg_Cl_Vote *)pRawMsg;
				if(pMsg->m_Vote != -1 || m_VoteCancelTime<time_get())
					return;

				m_VoteCloseTime = -1;
			}
		}
		else if(MsgID == NETMSGTYPE_CL_SETTEAM && m_pController->IsTeamChangeAllowed())
		{
			if (pPlayer->m_Player_logged == true)
			{
				CNetMsg_Cl_SetTeam *pMsg = (CNetMsg_Cl_SetTeam *)pRawMsg;

				if (pPlayer->GetTeam() == pMsg->m_Team ||
					(g_Config.m_SvSpamprotection && pPlayer->m_LastSetTeam && pPlayer->m_LastSetTeam + Server()->TickSpeed() * 3 > Server()->Tick()) ||
					(pMsg->m_Team != TEAM_SPECTATORS && m_LockTeams) || pPlayer->m_TeamChangeTick > Server()->Tick())
					return;

				pPlayer->m_LastSetTeam = Server()->Tick();

				// Switch team on given client and kill/respawn him
				if (m_pController->CanJoinTeam(pMsg->m_Team, ClientID) && m_pController->CanChangeTeam(pPlayer, pMsg->m_Team))
				{
					if (pPlayer->GetTeam() == TEAM_SPECTATORS || pMsg->m_Team == TEAM_SPECTATORS)
						m_VoteUpdate = true;
					pPlayer->m_TeamChangeTick = Server()->Tick() + Server()->TickSpeed() * 3;
					m_pController->DoTeamChange(pPlayer, pMsg->m_Team);
				}
			}
			else
			{
				SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "You need to be logged in to join the game");
				ShowAccountHelp(ClientID);
			}
		}
		else if (MsgID == NETMSGTYPE_CL_SETSPECTATORMODE && !m_World.m_Paused)
		{
			CNetMsg_Cl_SetSpectatorMode *pMsg = (CNetMsg_Cl_SetSpectatorMode *)pRawMsg;

			if(g_Config.m_SvSpamprotection && pPlayer->m_LastSetSpectatorMode && pPlayer->m_LastSetSpectatorMode+Server()->TickSpeed() > Server()->Tick())
				return;

			pPlayer->m_LastSetSpectatorMode = Server()->Tick();
			if(!pPlayer->SetSpectatorID(pMsg->m_SpecMode, pMsg->m_SpectatorID))
				SendGameMsg(GAMEMSG_SPEC_INVALIDID, ClientID);
		}
		else if (MsgID == NETMSGTYPE_CL_EMOTICON && !m_World.m_Paused)
		{
			CNetMsg_Cl_Emoticon *pMsg = (CNetMsg_Cl_Emoticon *)pRawMsg;

			if(g_Config.m_SvSpamprotection && pPlayer->m_LastEmote && pPlayer->m_LastEmote+Server()->TickSpeed()*3 > Server()->Tick())
				return;

			pPlayer->m_LastEmote = Server()->Tick();

			SendEmoticon(ClientID, pMsg->m_Emoticon);
		}
		else if (MsgID == NETMSGTYPE_CL_KILL && !m_World.m_Paused)
		{
			if(pPlayer->m_LastKill && pPlayer->m_LastKill+Server()->TickSpeed()*3 > Server()->Tick())
				return;

			pPlayer->m_LastKill = Server()->Tick();
			pPlayer->KillCharacter(WEAPON_SELF);
		}
		else if (MsgID == NETMSGTYPE_CL_READYCHANGE)
		{
			if(pPlayer->m_LastReadyChange && pPlayer->m_LastReadyChange+Server()->TickSpeed()*1 > Server()->Tick())
				return;

			pPlayer->m_LastReadyChange = Server()->Tick();
			m_pController->OnPlayerReadyChange(pPlayer);
		}
	}
	else
	{
		if (MsgID == NETMSGTYPE_CL_STARTINFO)
		{
			if(pPlayer->m_IsReadyToEnter)
				return;

			CNetMsg_Cl_StartInfo *pMsg = (CNetMsg_Cl_StartInfo *)pRawMsg;
			pPlayer->m_LastChangeInfo = Server()->Tick();

			// set start infos
			Server()->SetClientName(ClientID, pMsg->m_pName);
			Server()->SetClientClan(ClientID, pMsg->m_pClan);
			Server()->SetClientCountry(ClientID, pMsg->m_Country);

			for(int p = 0; p < 6; p++)
			{
				str_copy(pPlayer->m_TeeInfos.m_aaSkinPartNames[p], pMsg->m_apSkinPartNames[p], 24);
				pPlayer->m_TeeInfos.m_aUseCustomColors[p] = pMsg->m_aUseCustomColors[p];
				pPlayer->m_TeeInfos.m_aSkinPartColors[p] = pMsg->m_aSkinPartColors[p];
			}

			m_pController->OnPlayerInfoChange(pPlayer);

			// send vote options
			CNetMsg_Sv_VoteClearOptions ClearMsg;
			Server()->SendPackMsg(&ClearMsg, MSGFLAG_VITAL, ClientID);

			CVoteOptionServer *pCurrent = m_pVoteOptionFirst;
			while(pCurrent)
			{
				// count options for actual packet
				int NumOptions = 0;
				for(CVoteOptionServer *p = pCurrent; p && NumOptions < MAX_VOTE_OPTION_ADD; p = p->m_pNext, ++NumOptions);

				// pack and send vote list packet
				CMsgPacker Msg(NETMSGTYPE_SV_VOTEOPTIONLISTADD);
				Msg.AddInt(NumOptions);
				while(pCurrent && NumOptions--)
				{
					Msg.AddString(pCurrent->m_aDescription, VOTE_DESC_LENGTH);
					pCurrent = pCurrent->m_pNext;
				}
				Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
			}

			// send tuning parameters to client
			SendTuningParams(ClientID);

			// client is ready to enter
			pPlayer->m_IsReadyToEnter = true;
			CNetMsg_Sv_ReadyToEnter m;
			Server()->SendPackMsg(&m, MSGFLAG_VITAL|MSGFLAG_FLUSH, ClientID);
		}
	}
}

int CGameContext::TuneModSettings(char *Filepath)
{
	FILE *fpointer;

	int cnt = 0;
	char ch = 0;
	char aLineBuf[256] = { 0 };
	char aSaveString[MAX_LINES_MODSETTINGS][256] = { 0 };// save all file lines
	char* pChar;// token pointer
	char aStrPart[2][128] = { 0 };// tokenized file lines

	if (GetFileExists(Filepath) == false)
		return false;

	fpointer = fopen(Filepath, "r");

	while (1)
	{
		ch = fgetc(fpointer);

		if (ch == EOF)
			break;

		if (ch == '\n')
		{
			cnt++;
		}
		else
		{
			snprintf(aSaveString[cnt], sizeof(aSaveString[0]), "%s%c", aSaveString[cnt], ch);
		}
	}

	fclose(fpointer);

	// eval commands
	for (int i = 0; i < MAX_LINES_MODSETTINGS; i++)
	{
		// skip lines who begin as comments and empty ones
		if (aSaveString[i][0] == '/' || aSaveString[i][0] == 0)
			continue;

		// tokenize string
		pChar = strtok(aSaveString[i], " ");

		cnt = 0;
		while (pChar != NULL)
		{
			str_copy(aStrPart[cnt], pChar, sizeof(aStrPart[0]));
			pChar = strtok(NULL, " ");
			cnt++;

			// so you can put comments after the lines
			if (cnt == 2)
				break;
		}

		printf("[%s]: Line %02d: %s %s\n", __func__, i, aStrPart[0], aStrPart[1]);

		// write new values
		if (str_comp_nocase(aStrPart[0], "sv_req_hammer_auto") == 0)
			m_Req_hammer_auto = atoi(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_req_hammer_fly") == 0)
			m_Req_hammer_fly = atoi(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_req_gun_auto") == 0)
			m_Req_gun_auto = atoi(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_req_gun_spread") == 0)
			m_Req_gun_spread = atoi(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_req_grenade_bounce") == 0)
			m_Req_grenade_bounce = atoi(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_req_grenade_bounce2") == 0)
			m_Req_grenade_bounce2 = atoi(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_req_rifle_exp") == 0)
			m_Req_rifle_dual = atoi(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_req_rifle_range") == 0)
			m_Req_rifle_range = atoi(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_req_rifle_spread") == 0)
			m_Req_rifle_triple = atoi(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_shotgun_spreadbase") == 0)
			twep_shotgun_spreadbase = atoi(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_shotgun_speeddiff") == 0)
			twep_shotgun_speeddiff = atoi(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_shotgun_rangegain") == 0)
			twep_shotgun_rangegain = atof(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_health_per_point") == 0)
			m_Per_health_max = atof(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_armor_per_point") == 0)
			m_Per_armor_max = atof(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_damage_ratio") == 0)
			m_DamageScaling = atoi(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_level_min") == 0)
			m_MinAllowedLevel = atoi(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_level_max") == 0)
			m_MaxAllowedLevel = atoi(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_spawnprotection") == 0)
			m_SpawnProtectionBase = atof(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_reward_ammo") == 0)
			m_AmmoReward = atoi(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_reward_streak") == 0)
			m_BonusPerStreak = atoi(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_reward_leveldiff") == 0)
			m_BonusPer100 = atoi(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_kills_for_streak") == 0)
			m_StepKillstreak = atoi(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_mine_lifetime") == 0)
			m_MineLifeTime = atoi(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_mine_radius") == 0)
			m_MineRadius = atoi(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_droplife_ratio") == 0)
			m_DropLifeRewardRatio = atoi(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_droplife_lifetime") == 0)
			m_DropLifeLifeTime = atoi(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_droplife_gravity") == 0)
			m_DropLifeGravity = atof(aStrPart[1]);
		else if (str_comp_nocase(aStrPart[0], "sv_droplife_bounce") == 0)
			m_DropLifeBounceForce = atoi(aStrPart[1]);
	}

	return true;
}

void CGameContext::ShowDefaultHelp(int ClientID)
{
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, m_aSepLine);
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/help - show command list");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, m_aSepLine);
}

void CGameContext::ShowServerHelp(int ClientID)
{
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, m_aSepLine);
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/info - show server info");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/rules - show server rules");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/topten - show top ten players");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/help game - show upgrade / game help");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/help account - show account help");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/help emote - show emote help");
	// moderator help
	if (m_apPlayers[ClientID]->m_Player_status == 2 || m_apPlayers[ClientID]->m_Player_status == 3)// if moderator or admin
		SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/help moderator - show moderator help");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/ticket <message> - send a message to the creator");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/redeem <code> - redeem a code");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, m_aSepLine);
}

void CGameContext::ShowGameHelp(int ClientID)
{
	char aTextBuf[256] = { 5 };
	char aWeaponName[7][MAX_INPUT_SIZE] = { "hammer", "gun", "shotgun", "grenade", "rifle", "life", "handle" };

	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, m_aSepLine);

	/*
		for (int i = 0; i < 7; i++)
		{
			str_format(aTextBuf, sizeof(aTextBuf), "/upgr %s ...", aWeaponName[i], aWeaponName[i]);
			SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, aTextBuf);
		}
	*/

	ServerMessage(ClientID, "/upgr hammer ... - upgrades hammer");
	ServerMessage(ClientID, "/upgr gun ... - upgrades gun");
	ServerMessage(ClientID, "/upgr shotgun ... - upgrades shotgun");
	ServerMessage(ClientID, "/upgr grenade ... - upgrades grenade");
	ServerMessage(ClientID, "/upgr rifle ... - upgrades rifle");
	ServerMessage(ClientID, "/upgr life ... - upgrades health/armor");
	ServerMessage(ClientID, "/upgr handle ... - upgrades firerate");

	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/switchmode - switch weapon modes");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/showexp - toggle experience notifications");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/stats (tell) - view / tell your stats");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, m_aSepLine);
}

void CGameContext::ShowAccountHelp(int ClientID)
{
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, m_aSepLine);
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/register username password - create account");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/login username password - log in to account and join");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/logout - log out");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/newpassword <old> <new> <confirm new> - change pw");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "*Username and password may only contain [aA-zZ] and [0-9]");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, m_aSepLine);
}

void CGameContext::ShowEmoteHelp(int ClientID)
{
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, m_aSepLine);
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/angry");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/happy");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/pain");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/blink");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/surprise");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/default");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, m_aSepLine);
}

void CGameContext::ShowModeratorHelp(int ClientID)
{
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, m_aSepLine);
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/undercover - toggle to hide your status");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/idlist - show the IDs with names of all active players");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/showstats <ID> - view a player's stats");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/notify <message> - server notification");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/kick <ID> - kicks a player");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/ban <ID> <minutes> - bans a player for a duration");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/freeze <ID> - freezes a player");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "/unfreeze <ID> - unfreezes a player");
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, m_aSepLine);
}

void CGameContext::ShowStats(int ClientID, char* pParam)
{
	if (m_apPlayers[ClientID]->m_Player_logged == true)
	{
		char aTextBuf[256] = { 0 };
		char aWeaponName[7][MAX_INPUT_SIZE] = { "hammer", "gun", "shotgun", "grenade", "rifle", "life", "handle" };
		int cClientID = 0;
		int shownum = 0;

		// if an additional parameter is given
		if (pParam[0] != NULL)
		{
			// and valid
			if (!str_comp_nocase(pParam, "tell"))
			{
				if (!m_aTellDelay[ClientID])
				{
					m_aTellDelay[ClientID] = Server()->TickSpeed() * m_TellDelayDefault;
					shownum = MAX_CLIENTS;
				}
				else
				{
					str_format(aTextBuf, sizeof(aTextBuf), "You must wait %d seconds before you can tell your stats again", m_aTellDelay[ClientID] / Server()->TickSpeed());
					SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, aTextBuf);
					return;
				}
			}
			else
			{
				SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "No such command, use \"/stats\" or \"/stats tell\"");
				return;
			}
		}
		else
			shownum = 1;

		for (int p = 0; p < shownum; ++p)
		{
			// if telling others
			if (pParam[0] != NULL)
			{
				cClientID = p;

				// don't tell stats to self
				/*if (cClientID == ClientID)
				{
					SendChat(TEAM_SPECTATORS, CHAT_NONE, cClientID, "You told your stats");
					continue;
				}*/
			}
			else
				cClientID = ClientID;

			// skip non existing players
			if (!m_apPlayers[cClientID])
				continue;

			str_format(aTextBuf, sizeof(aTextBuf), "**** %s ****", Server()->ClientName(ClientID));
			SendChat(TEAM_SPECTATORS, CHAT_NONE, cClientID, aTextBuf);
			
			str_format(aTextBuf, sizeof(aTextBuf), "level - %d", m_apPlayers[ClientID]->m_Player_level);
			SendChat(TEAM_SPECTATORS, CHAT_NONE, cClientID, aTextBuf);
			
			// don't show others certain stats
			if (shownum == 1)
			{
				str_format(aTextBuf, sizeof(aTextBuf), "exp - %d / %d", m_apPlayers[ClientID]->m_Player_experience, m_apPlayers[ClientID]->m_Player_level);
				SendChat(TEAM_SPECTATORS, CHAT_NONE, cClientID, aTextBuf);
				str_format(aTextBuf, sizeof(aTextBuf), "money - %d", m_apPlayers[ClientID]->m_Player_money);
				SendChat(TEAM_SPECTATORS, CHAT_NONE, cClientID, aTextBuf);
			}

			SendChat(TEAM_SPECTATORS, CHAT_NONE, cClientID, "*******************");

			for (int i = 0; i < 7; i++)
			{
				str_format(aTextBuf, sizeof(aTextBuf), "%s - %d", aWeaponName[i], m_apPlayers[ClientID]->m_aPlayer_stat[i]);
				SendChat(TEAM_SPECTATORS, CHAT_NONE, cClientID, aTextBuf);
			}
		}
	}
	else
	{
		SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "You are not logged in");
	}
}

void CGameContext::ShowTopTen(int ClientID)
{
	char aBuf[256] = { 0 };
	char aName[256] = { 0 };
	char aLevel[256] = { 0 };
	char aFileName[256] = { 0 };

	str_format(aFileName, sizeof(aFileName), "%s/topten.ini", FOLDERPATH_TOPTEN);

	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "~~~~~ TOP TEN ~~~~~");
	for (int i = 0; i < 10; ++i)
	{
		// get name
		GetFromFile(i * 2 + 0, aName, sizeof(aName), aFileName);
		// get level
		GetFromFile(i * 2 + 1, aLevel, sizeof(aLevel), aFileName);

		str_format(aBuf, sizeof(aBuf), "#%d [%d]%s", i + 1, atoi(aLevel), aName);
		SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, aBuf);
		
		// reset buffers
		memset(aLevel, 0, sizeof(aLevel));
		memset(aName, 0, sizeof(aName));
		memset(aBuf, 0, sizeof(aBuf));
	}
}

bool CGameContext::GetFileExists(char *Filepath)
{
	FILE *fpointer;

	fpointer = fopen(Filepath, "r");

	//check if file exists
	if (fpointer == NULL)
	{
		return false;
	}

	fclose(fpointer);
	return true;
}

bool CGameContext::GetDirExists(const char* dirName) {
	DWORD attribs = ::GetFileAttributesA(dirName);
	if (attribs == INVALID_FILE_ATTRIBUTES) {
		return false;
	}
	return (attribs & FILE_ATTRIBUTE_DIRECTORY);
}

bool CGameContext::GetFromFile(int Position, char *Buffer, unsigned int Size, char *Filepath)
{
	FILE *fpointer;

	int counter = 0;
	char ch = 0;

	char aLineBuf[256] = { 0 };

	if (GetFileExists(Filepath) == false)
		return false;

	fpointer = fopen(Filepath, "r");

	while (1)
	{
		ch = fgetc(fpointer);

		if (ch == EOF)
			break;

		if (ch == '\n')
		{
			counter++;
		}
		else
		{
			if (counter == Position)
			{
				str_format(aLineBuf, sizeof(aLineBuf), "%s%c", aLineBuf, ch);
			}
		}

		if (counter > Position)
			break;
	}

	str_copy(Buffer, aLineBuf, Size);

	fclose(fpointer);

	return true;
}

void CGameContext::AccountRegister(int ClientID, char *Username, char *Password)
{
	if (m_apPlayers[ClientID]->m_Player_logged == false)
	{
		FILE* fpointer;
		
		char aInfoText[256] = { 0 };//info return text
		char aFilePathComplete[256] = { 0 };

		// check format
		if (!CheckRegisterFormat(Username, strlen(Username), ClientID) || !CheckRegisterFormat(Password, strlen(Password), ClientID))
		{
			SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "Username and password may only contain [aA - zZ] and [0 - 9]");
			return;
		}

		str_format(aFilePathComplete, sizeof(aFilePathComplete), "%s/%s.ini", FOLDERPATH_ACCOUNTS, Username);

		//check empty
		if (Username[0] != 0 && Password[0] != 0)
		{
			//check length
			if (strlen(Username) <= MAX_LEN_REGSTR && strlen(Password) <= MAX_LEN_REGSTR)
			{
				// check if file username is reserved by the windows PIP system and prevent crashing
				if (!str_comp_nocase(Username, "con") ||
					!str_comp_nocase(Username, "aux") ||
					!str_comp_nocase(Username, "prn") ||
					!str_comp_nocase(Username, "nul"))
				{
					ServerMessage(ClientID, "Username already exists");
					return;
				}

				//check if username is already taken
				if (GetFileExists(aFilePathComplete) == false)
				{
					//create account with default values
					fpointer = fopen(aFilePathComplete, "w");
					fprintf(fpointer, Username);//username
					fprintf(fpointer, "\n");
					fprintf(fpointer, Password);//password
					fprintf(fpointer, "\n");
					fprintf(fpointer, "0");		//status (frozen, moderator, admin)
					fprintf(fpointer, "\n");
					fprintf(fpointer, "0");		//option undercover
					fprintf(fpointer, "\n");
					fprintf(fpointer, "0");		//reserved 2
					fprintf(fpointer, "\n");
					fprintf(fpointer, "0");		//reserved 3
					fprintf(fpointer, "\n");
					fprintf(fpointer, "0");		//reserved 4
					fprintf(fpointer, "\n");
					fprintf(fpointer, "0");		//reserved 5
					fprintf(fpointer, "\n");
					fprintf(fpointer, "1");		//level
					fprintf(fpointer, "\n");
					fprintf(fpointer, "0");		//experience
					fprintf(fpointer, "\n");
					fprintf(fpointer, "20");	//money (20 money ~ 4 free upgrades at the start)
					for (int i = 0; i < 7; i++)
					{
						fprintf(fpointer, "\n");//stats
						if (i < 2)// hammer / gun have 1 free level
							fprintf(fpointer, "1");
						else
							fprintf(fpointer, "0");
					}
					fprintf(fpointer, "\n");
					fprintf(fpointer, Server()->ClientName(ClientID));
					fclose(fpointer);

					// log in automatically
					AccountLogIn(ClientID, Username, Password);

					str_copy(aInfoText, "Account registered successfully", sizeof(aInfoText));
					WriteModLog("%s registered account: %s", Server()->ClientName(ClientID), Username);
				}
				else
				{
					str_copy(aInfoText, "Username already exists", sizeof(aInfoText));
				}
			}
			else
			{
				str_format(aInfoText, sizeof(aInfoText), "Maximum length of username / password: %d", MAX_LEN_REGSTR);
			}
		}
		else
		{
			str_copy(aInfoText, "Username / password must not be empty", sizeof(aInfoText));
		}

		SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, aInfoText);
	}
	else
	{
		SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "You must log out to register an account");
	}
}

bool CGameContext::CheckRegisterFormat(char *Text, int Length, int ClientID)
{
	char aBuf[256] = { 0 };

	// check empty
	if (Length < 1)
		return false;

	// check characters
	for (int i = 0; i < Length; i++)
	{
		if ((Text[i] >= '0' && Text[i] <= '9') ||
			(Text[i] >= 'A' && Text[i] <= 'Z') ||
			(Text[i] >= 'a' && Text[i] <= 'z'))
		{
			// nothing
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "Invalid character: '%c'", Text[i]);
			SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, aBuf);
			return false;
		}
	}

	return true;
}

bool CGameContext::CheckRedeemFormat(char *Text, int Length, int ClientID)
{
	char aBuf[256] = { 0 };

	// check length
	if (Length != 6)
		return false;

	// check characters
	for (int i = 0; i < Length; i++)
	{
		if ((Text[i] >= '0' && Text[i] <= '9') ||
			(Text[i] >= 'A' && Text[i] <= 'Z') ||
			(Text[i] >= 'a' && Text[i] <= 'z'))
		{
			// nothing
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "Invalid character: '%c'", Text[i]);
			SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, aBuf);
			return false;
		}
	}

	return true;
}

void CGameContext::AccountLogIn(int ClientID, char *Username, char *Password)
{
	FILE* fpointer;

	bool AccIsLogged = false;//account logged in
	char aInfoText[256] = { 0 };//info return text
	char aWelMsg[256] = { 0 };//welcome message
	char aRetBuf[256] = { 0 };//return buffer
	char aRetBuf2[256] = { 0 };//return buffer 2
	char aRetBufAccStatus[256] = { 0 };// for status checking
	char aFilePathComplete[256] = { 0 };//full filepath
	char aNameTrans[256] = { 0 };//name with level indicator

	str_format(aFilePathComplete, sizeof(aFilePathComplete), "%s/%s.ini", FOLDERPATH_ACCOUNTS, Username);

	if (m_apPlayers[ClientID]->m_Player_logged == false)
	{
		// check if file username is reserved by the windows PIP system
		if (!str_comp_nocase(Username, "con") ||
			!str_comp_nocase(Username, "aux") ||
			!str_comp_nocase(Username, "prn") ||
			!str_comp_nocase(Username, "nul"))
		{
			ServerMessage(ClientID, "Wrong username / password");
			return;
		}

		//check if account exists
		if (GetFileExists(aFilePathComplete) == true)
		{
			//check password
			GetFromFile(ACC_USERNAME, aRetBuf, sizeof(aRetBuf), aFilePathComplete);
			GetFromFile(ACC_PASSWORD, aRetBuf2, sizeof(aRetBuf2), aFilePathComplete);

			// check account status
			GetFromFile(ACC_STATUS, aRetBufAccStatus, sizeof(aRetBufAccStatus), aFilePathComplete);

			//compare username and password
			if (str_comp(Username, aRetBuf) == 0 && str_comp(Password, aRetBuf2) == 0)
			{
				//check if account already logged in
				for (int i = 0; i < MAX_CLIENTS; i++)
				{
					if (!m_apPlayers[i])// skip unfindable players
						continue;

					if (str_comp(m_apPlayers[i]->m_Player_username, aRetBuf) == 0)
					{
						AccIsLogged = true;
						break;
					}
				}

				if (AccIsLogged == false)
				{
					// get level before logging in
					GetFromFile(ACC_LEVEL, aRetBuf, sizeof(aRetBuf), aFilePathComplete);

					if (atoi(aRetBuf) >= m_MinAllowedLevel || m_MinAllowedLevel == -1
						|| atoi(aRetBufAccStatus) == 2 || atoi(aRetBufAccStatus) == 3)// moderator / admin can always log in
					{
						if (atoi(aRetBuf) <= m_MaxAllowedLevel || m_MaxAllowedLevel == -1
							|| atoi(aRetBufAccStatus) == 2 || atoi(aRetBufAccStatus) == 3)// moderator / admin can always log in
						{
							//do login stuff
							//logged in
							m_apPlayers[ClientID]->m_Player_logged = true;
							//name raw
							str_copy(m_apPlayers[ClientID]->m_Player_nameraw, Server()->ClientName(ClientID), sizeof(m_apPlayers[ClientID]->m_Player_nameraw));
							m_apPlayers[ClientID]->m_Player_nameraw[strlen(m_apPlayers[ClientID]->m_Player_nameraw)] = 0;

							// move player to active players
							m_pController->DoTeamChange(m_apPlayers[ClientID], TEAM_RED);

							//username
							GetFromFile(ACC_USERNAME, aRetBuf, sizeof(aRetBuf), aFilePathComplete);
							str_copy(m_apPlayers[ClientID]->m_Player_username, aRetBuf, sizeof(m_apPlayers[ClientID]->m_Player_username));
							//password
							GetFromFile(ACC_PASSWORD, aRetBuf2, sizeof(aRetBuf2), aFilePathComplete);
							str_copy(m_apPlayers[ClientID]->m_Player_password, aRetBuf2, sizeof(m_apPlayers[ClientID]->m_Player_username));
							//status
							GetFromFile(ACC_STATUS, aRetBuf, sizeof(aRetBuf), aFilePathComplete);
							m_apPlayers[ClientID]->m_Player_status = atoi(aRetBuf);
							//util undercover
							GetFromFile(ACC_UTIL_1, aRetBuf, sizeof(aRetBuf), aFilePathComplete);
							m_apPlayers[ClientID]->m_aPlayer_util[0] = atoi(aRetBuf);
							//util showexp
							GetFromFile(ACC_UTIL_2, aRetBuf, sizeof(aRetBuf), aFilePathComplete);
							m_apPlayers[ClientID]->m_aPlayer_util[1] = atoi(aRetBuf);
							//util emote
							GetFromFile(ACC_UTIL_3, aRetBuf, sizeof(aRetBuf), aFilePathComplete);
							m_apPlayers[ClientID]->m_aPlayer_util[2] = atoi(aRetBuf);
							//reserved 4
							GetFromFile(ACC_UTIL_4, aRetBuf, sizeof(aRetBuf), aFilePathComplete);
							m_apPlayers[ClientID]->m_aPlayer_util[3] = atoi(aRetBuf);
							//reserved 5
							GetFromFile(ACC_UTIL_5, aRetBuf, sizeof(aRetBuf), aFilePathComplete);
							m_apPlayers[ClientID]->m_aPlayer_util[4] = atoi(aRetBuf);
							//level
							GetFromFile(ACC_LEVEL, aRetBuf, sizeof(aRetBuf), aFilePathComplete);
							m_apPlayers[ClientID]->m_Player_level = atoi(aRetBuf);
							//experience
							GetFromFile(ACC_EXPERIENCE, aRetBuf, sizeof(aRetBuf), aFilePathComplete);
							m_apPlayers[ClientID]->m_Player_experience = atoi(aRetBuf);
							//money
							GetFromFile(ACC_MONEY, aRetBuf, sizeof(aRetBuf), aFilePathComplete);
							m_apPlayers[ClientID]->m_Player_money = atoi(aRetBuf);
							//stats
							for (int i = 0; i < 7; i++)
							{
								GetFromFile(ACC_LVL_HAMMER + i, aRetBuf, sizeof(aRetBuf), aFilePathComplete);
								m_apPlayers[ClientID]->m_aPlayer_stat[i] = atoi(aRetBuf);
							}
							//character name update
							str_copy(m_apPlayers[ClientID]->m_Player_charname, m_apPlayers[ClientID]->m_Player_nameraw, sizeof(m_apPlayers[ClientID]->m_Player_charname));

							//welcome message
							if (m_apPlayers[ClientID]->m_Player_level == 1)
							{
								str_format(aWelMsg, sizeof(aWelMsg), "Welcome %s!", Server()->ClientName(ClientID));
								SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, aWelMsg);
							}
							else
							{
								str_format(aWelMsg, sizeof(aWelMsg), "Welcome back %s!", Server()->ClientName(ClientID));
								SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, aWelMsg);
							}

							// overwrite starter broadcast info
							SendBroadcast("", ClientID);

							SetTeeScore(ClientID);
						}
						else
						{
							str_format(aInfoText, sizeof(aInfoText), "Your need to be under level %d to join this server, your level: %s", m_MaxAllowedLevel + 1, aRetBuf);
						}
					}
					else
					{
						str_format(aInfoText, sizeof(aInfoText), "You need to be at least level %d to join this server, your level: %s", m_MinAllowedLevel, aRetBuf);
					}
				}
				else
				{
					SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "This account is already logged in");
				}
			}
			else
			{
				str_copy(aInfoText, "Wrong username / password", sizeof(aInfoText));
			}
		}
		else//account does not exist
		{
			str_copy(aInfoText, "Wrong username / password", sizeof(aInfoText));
		}
		SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, aInfoText);
	}
	else
	{
		SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "You are already logged in");
	}
}

void CGameContext::AccountLogOut(int ClientID)
{
	if (m_apPlayers[ClientID]->m_Player_logged == true)
	{
		// update account, very important to save progress
		AccountUpdate(ClientID);

		//reset all attributes
		//logged out
		m_apPlayers[ClientID]->m_Player_logged = false;
		//reset options
		memset(m_apPlayers[ClientID]->m_aPlayer_option, 0, sizeof(m_apPlayers[ClientID]->m_aPlayer_option));
		//reset nameraw
		memset(m_apPlayers[ClientID]->m_Player_nameraw, 0, sizeof(m_apPlayers[ClientID]->m_Player_nameraw));
		// reset charname
		memset(m_apPlayers[ClientID]->m_Player_charname, 0, sizeof(m_apPlayers[ClientID]->m_Player_charname));
		//reset last weapon
		m_apPlayers[ClientID]->m_Player_lastweapon = WEAPON_GUN;
		//reset killcount
		m_apPlayers[ClientID]->m_Player_killcount = 0;

		// move player to spectators
		m_pController->DoTeamChange(m_apPlayers[ClientID], TEAM_SPECTATORS);

		//username
		str_copy(m_apPlayers[ClientID]->m_Player_username, "", sizeof(m_apPlayers[ClientID]->m_Player_username));
		//password
		str_copy(m_apPlayers[ClientID]->m_Player_password, "", sizeof(m_apPlayers[ClientID]->m_Player_password));
		//status
		m_apPlayers[ClientID]->m_Player_status = 0;
		//level
		m_apPlayers[ClientID]->m_Player_level = 0;
		//experience
		m_apPlayers[ClientID]->m_Player_experience = 0;
		//money
		m_apPlayers[ClientID]->m_Player_money = 0;
		//stats
		for (int i = 0; i < 7; i++)
		{
			m_apPlayers[ClientID]->m_aPlayer_stat[i] = 0;
		}

		SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "Logged out");
	}
	else
	{
		SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "You are already logged out");
	}
}

void CGameContext::AccountChangePassword(int ClientID, char *Password, char *Newpassword, char *Newpasswordconfirm)
{
	char aInfoText[256] = { 0 };//info return text
	char aRetBuf[256] = { 0 };//return buffer
	char aRetBufAccStatus[256] = { 0 };// for status checking
	char aFilePathComplete[256] = { 0 };//filepath buffer

	if (m_apPlayers[ClientID]->m_Player_logged == true)
	{
		str_format(aFilePathComplete, sizeof(aFilePathComplete), "%s/%s.ini", FOLDERPATH_ACCOUNTS, m_apPlayers[ClientID]->m_Player_username);

		GetFromFile(ACC_PASSWORD, aRetBuf, sizeof(aRetBuf), aFilePathComplete);

		//check if password matches
		if (str_comp(Password, aRetBuf) == 0)
		{
			// check confirm match
			if (!str_comp(Newpassword, Newpasswordconfirm) == 0)
			{
				SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "Confirm password doesn't match");
				return;
			}

			// check format
			if (!CheckRegisterFormat(Newpassword, strlen(Newpassword), ClientID))
			{
				SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "New password may only contain [aA - zZ] and [0 - 9]");
				return;
			}

			// check length
			if (strlen(Newpassword) > MAX_LEN_REGSTR)
			{
				ServerMessage(ClientID, "Maximum length of password: %d", MAX_LEN_REGSTR);
				return;
			}

			//change password
			str_copy(m_apPlayers[ClientID]->m_Player_password, Newpassword, sizeof(m_apPlayers[ClientID]->m_Player_password));
			AccountUpdate(ClientID);

			SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "Your password has been changed successfully");

			WriteModLog("%s changed password: %s --> %s", Server()->ClientName(ClientID), Password, Newpassword);
		}
		else
		{
			SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "Wrong password");
		}
	}
	else
	{
		SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "You are not logged in");
		return;
	}
}

void CGameContext::AccountUpdate(int ClientID)
{
	if (m_apPlayers[ClientID]->IsDummy())
		return;

	FILE *fpointer;
	char aBuf[256] = { 0 };
/*
	str_format(aBuf, sizeof(aBuf), "%d - account updated", ClientID);
	SendChat(TEAM_SPECTATORS, CHAT_NONE, 0, aBuf);
*/
	// return if not existing
	if (!m_apPlayers[ClientID])
	{
		WriteModLog("AccountUpdate() was called with player %d not existing", ClientID);
		return;
	}

	// return if not logged
	if (m_apPlayers[ClientID]->m_Player_logged != true)
	{
		WriteModLog("AccountUpdate() was called with player %d not being logged in", ClientID);
		return;
	}

	char aNameTrans[256] = { 0 };//name with level indicator

	char aFilePathComplete[256] = { 0 };
	char aStr[256] = { 0 };
	char encStr[256] = { 0 };// encrypted string

	// encrypt password (unused)
	for (int i = 0; i < strlen(m_apPlayers[ClientID]->m_Player_password); ++i)
		encStr[i] = m_apPlayers[ClientID]->m_Player_password[i] + m_EncryptLetterShift;

	str_format(aFilePathComplete, sizeof(aFilePathComplete), "%s/%s.ini", FOLDERPATH_ACCOUNTS, m_apPlayers[ClientID]->m_Player_username);

	fpointer = fopen(aFilePathComplete, "w");
	//username
	fprintf(fpointer, m_apPlayers[ClientID]->m_Player_username);
	//password
	fprintf(fpointer, "\n");
	fprintf(fpointer, m_apPlayers[ClientID]->m_Player_password);
	//status
	fprintf(fpointer, "\n");
	str_format(aStr, sizeof(aStr), "%d", m_apPlayers[ClientID]->m_Player_status);
	fprintf(fpointer, aStr);
	//option undercover
	fprintf(fpointer, "\n");
	str_format(aStr, sizeof(aStr), "%d", m_apPlayers[ClientID]->m_aPlayer_util[0]);
	fprintf(fpointer, aStr);
	//reserved 2
	fprintf(fpointer, "\n");
	str_format(aStr, sizeof(aStr), "%d", m_apPlayers[ClientID]->m_aPlayer_util[1]);
	fprintf(fpointer, aStr);
	//reserved 3
	fprintf(fpointer, "\n");
	str_format(aStr, sizeof(aStr), "%d", m_apPlayers[ClientID]->m_aPlayer_util[2]);
	fprintf(fpointer, aStr);
	//reserved 4
	fprintf(fpointer, "\n");
	str_format(aStr, sizeof(aStr), "%d", m_apPlayers[ClientID]->m_aPlayer_util[3]);
	fprintf(fpointer, aStr);
	//reserved 5
	fprintf(fpointer, "\n");
	str_format(aStr, sizeof(aStr), "%d", m_apPlayers[ClientID]->m_aPlayer_util[4]);
	fprintf(fpointer, aStr);
	//level
	fprintf(fpointer, "\n");
	str_format(aStr, sizeof(aStr), "%d", m_apPlayers[ClientID]->m_Player_level);
	fprintf(fpointer, aStr);
	//experience
	fprintf(fpointer, "\n");
	str_format(aStr, sizeof(aStr), "%d", m_apPlayers[ClientID]->m_Player_experience);
	fprintf(fpointer, aStr);
	//money
	fprintf(fpointer, "\n");
	str_format(aStr, sizeof(aStr), "%d", m_apPlayers[ClientID]->m_Player_money);
	fprintf(fpointer, aStr);

	for (int i = 0; i < 7; i++)
	{
		//stats
		fprintf(fpointer, "\n");
		str_format(aStr, sizeof(aStr), "%d", m_apPlayers[ClientID]->m_aPlayer_stat[i]);
		fprintf(fpointer, aStr);
	}
	//character name
	fprintf(fpointer, "\n");
	fprintf(fpointer, m_apPlayers[ClientID]->m_Player_charname);

	fclose(fpointer);
/*
	str_format(aBuf, sizeof(aBuf), "Client %d update!", ClientID);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "debug", aBuf);
*/
}

void CGameContext::UpgradeStats(int ClientID, char* pStat, char* pAmount)
{
	if (m_apPlayers[ClientID]->m_Player_logged == true)
	{
		int ArrPos = -1;
		int i;
		char aInfoText[256] = { 0 };
		char aInfoLevel[256] = { 0 };

		for (i = 0; i < max(1, atoi(pAmount)); ++i)
		{
			// admins shop for free
			if (m_apPlayers[ClientID]->m_Player_money >= 5 || m_apPlayers[ClientID]->m_Player_status == 3)
			{
				if (str_comp_nocase(pStat, "hammer") == 0)//hammer
					ArrPos = 0;
				else if (str_comp_nocase(pStat, "gun") == 0)//gun
					ArrPos = 1;
				else if (str_comp_nocase(pStat, "shotgun") == 0)//shotgun
					ArrPos = 2;
				else if (str_comp_nocase(pStat, "grenade") == 0)//grenade
					ArrPos = 3;
				else if (str_comp_nocase(pStat, "rifle") == 0)//rifle
					ArrPos = 4;
				else if (str_comp_nocase(pStat, "life") == 0)//life
					ArrPos = 5;
				else if (str_comp_nocase(pStat, "handle") == 0)//handle
					ArrPos = 6;
				else
				{
					SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "No such upgrade command");
					break;
				}

				if (ArrPos != -1)
				{
					// admins don't need to pay
					if (m_apPlayers[ClientID]->m_Player_status != 3)
						m_apPlayers[ClientID]->m_Player_money -= 5;

					m_apPlayers[ClientID]->m_aPlayer_stat[ArrPos]++;

					// special weapons
					if (ArrPos >= WEAPON_SHOTGUN && ArrPos <= WEAPON_LASER)
					{
						if (GetPlayerChar(ClientID)) // crash safety
						{
							// give ammo
							m_apPlayers[ClientID]->GetCharacter()->GiveWeapon(ArrPos, 1);
							// switch to weapon if first time upgrade
							if (m_apPlayers[ClientID]->m_aPlayer_stat[ArrPos] == 1)
								m_apPlayers[ClientID]->GetCharacter()->SetWeapon(ArrPos);
						}
					}

					// give health instantly
					if (ArrPos == 5)
					{
						if (GetPlayerChar(ClientID)) // crash safety
						{
							m_apPlayers[ClientID]->GetCharacter()->IncreaseHealth(min(m_Per_health_max, (float)m_apPlayers[ClientID]->m_aPlayer_stat[5]));
							m_apPlayers[ClientID]->GetCharacter()->IncreaseArmor(min(m_Per_health_max, (float)m_apPlayers[ClientID]->m_aPlayer_stat[5]));
						
//							ServerMessage(ClientID, "%.2f", min(m_Per_health_max, (float)m_apPlayers[ClientID]->m_aPlayer_stat[5]));
						}
					}

					switch (ArrPos)
					{
					case 0://hammer
						if (m_apPlayers[ClientID]->m_aPlayer_stat[ArrPos] == m_Req_hammer_auto)
							SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "You now have autohammer!");
						if (m_apPlayers[ClientID]->m_aPlayer_stat[ArrPos] == m_Req_hammer_fly) {
							SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "You can now fly!");
							SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "Toggle mines with \"/switchmode\"");
						}
						break;

					case 1://gun
						if (m_apPlayers[ClientID]->m_aPlayer_stat[ArrPos] == m_Req_gun_auto)
							SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "You now have autogun!");
						if (m_apPlayers[ClientID]->m_aPlayer_stat[ArrPos] == m_Req_gun_spread)
							SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "Your gun now has spreadshot!");
						break;

					case 2://shotgun
						break;

					case 3://grenade
						if (m_apPlayers[ClientID]->m_aPlayer_stat[ArrPos] == m_Req_grenade_bounce)
							SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "Your grenades now bounce!");
						if (m_apPlayers[ClientID]->m_aPlayer_stat[ArrPos] == m_Req_grenade_bounce2)
							SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "Your grenades now bounce twice!");
						break;

					case 4://rifle
						if (m_apPlayers[ClientID]->m_aPlayer_stat[ArrPos] == m_Req_rifle_triple)
							SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "Your rifle now has triple spreadshot!");
						if (m_apPlayers[ClientID]->m_aPlayer_stat[ArrPos] == m_Req_rifle_range)
							SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "Your rifle now has increased range!");
						if (m_apPlayers[ClientID]->m_aPlayer_stat[ArrPos] == m_Req_rifle_dual)
							SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "Your rifle now has double spreadshot and you can powerjump!");
						break;
					}
				}
			}
			else
			{
				SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "Not enough money");
				break;
			}
		}

		// send new level status once if upgraded at least once
		if (i > 0)
		{
			str_format(aInfoText, sizeof(aInfoText), "Upgrade: %s is now on level %d", pStat, m_apPlayers[ClientID]->m_aPlayer_stat[ArrPos]);
			SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, aInfoText);
		}
	}
	else
	{
		SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "You are not logged in");
	}
}

void CGameContext::ResetAccount(int ClientID)
{
	int moneyStored = 0;

	// not logged in
	if (m_apPlayers[ClientID]->m_Player_logged == false)
	{
		ServerMessage(ClientID, "You are not logged in");
		return;
	}

	// save unspent money
	moneyStored = m_apPlayers[ClientID]->m_Player_money;
	m_apPlayers[ClientID]->m_Player_money = 0;

	// calculate upgrade money and reset stats
	for (int i = 0; i < 7; ++i)
	{
		m_apPlayers[ClientID]->m_Player_money += m_apPlayers[ClientID]->m_aPlayer_stat[i] * 5;

		if (i < 2)// hammer / gun have 1 free level
			m_apPlayers[ClientID]->m_aPlayer_stat[i] = 1;
		else
			m_apPlayers[ClientID]->m_aPlayer_stat[i] = 0;
	}

	// give money
	m_apPlayers[ClientID]->m_Player_money += moneyStored - 10;// subtract free gun and hammer level

	AccountUpdate(ClientID);

	ServerMessage(ClientID, "Your account has been reset!");
}

void CGameContext::SubmitTicket(int ClientID, char *Message)
{
	FILE* fpointer;
	va_list argList;
	char buffer[256] = { 0 };

	// player has to be logged in
	if (m_apPlayers[ClientID]->m_Player_logged == true)
	{
		// if empty ticket
		if (Message[0] == NULL)
		{
			ServerMessage(ClientID, "Ticket message must not be empty");
			return;
		}

		if (!m_aTicketDelay[ClientID])
		{
			m_aTicketDelay[ClientID] = Server()->TickSpeed() * m_TicketDelayDefault;

			fpointer = fopen(FILEPATH_TICKET, "a");

			str_format(buffer, sizeof(buffer), "\n\n[%s]%s\n%s", m_apPlayers[ClientID]->m_Player_username, Server()->ClientName(ClientID), Message);

			fprintf(fpointer, buffer);

			fclose(fpointer);

			WriteModLog("[%s]%s has submitted a ticket", m_apPlayers[ClientID]->m_Player_username, Server()->ClientName(ClientID));

			ServerMessage(ClientID, "Your ticket has been submitted");
		}
		else
		{
			ServerMessage(ClientID, "You must wait %d seconds before you can submit another ticket", m_aTicketDelay[ClientID] / Server()->TickSpeed());
			return;
		}
	}
	else
	{
		ServerMessage(ClientID, "You need to log in to submit a ticket");
		return;
	}
}

void CGameContext::RedeemCode(int ClientID, char *Code)
{
	FILE* fpointer;
	va_list argList;
	char buffer[256] = { 0 };
	char aFilePathComplete[256] = { 0 };
	char aBufCode[256] = { 0 };
	char aBufType[256] = { 0 };
	char aBufVal1[256] = { 0 };
	char aTypeName[110][128] = { "Reset", "Level", "Money" };

	// add event names to the redeem names
	for (int i = 0; i < 10; ++i)
	{
		str_format(aTypeName[100 + i], sizeof(aTypeName[0]), "Event: %s", m_aEventName[i]);
	}

	// player has to be logged in
	if (m_apPlayers[ClientID]->m_Player_logged == true)
	{
		str_format(aFilePathComplete, sizeof(aFilePathComplete), "%s/%s.ini", FOLDERPATH_REDEEMCODES, Code);

		if (!m_aRedeemDelay[ClientID])
		{
			// delay to prevent botting / spamming
			m_aRedeemDelay[ClientID] = Server()->TickSpeed() * m_RedeemDelayDefault;

			// check code format
			if (!CheckRedeemFormat(Code, strlen(Code), ClientID))
			{
				ServerMessage(ClientID, "Invalid redeem code");
				// don't write code to modlog since writing invalid format could cause crashes
				return;
			}

			// check if code exists case insensitively
			if (!GetFileExists(aFilePathComplete))
			{
				ServerMessage(ClientID, "Invalid redeem code");
				WriteModLog("[%s]%s has tried redeem code: %s", m_apPlayers[ClientID]->m_Player_username, Server()->ClientName(ClientID), Code);
				return;
			}

			fpointer = fopen(aFilePathComplete, "r");

			// load code properties into buffers
			GetFromFile(0, aBufCode, sizeof(aBufCode), aFilePathComplete);
			GetFromFile(1, aBufType, sizeof(aBufType), aFilePathComplete);
			GetFromFile(2, aBufVal1, sizeof(aBufVal1), aFilePathComplete);
			
			fclose(fpointer);// close the file

			//check code case sensitively
			if (!str_comp(Code, aBufCode))
			{
				ServerMessage(ClientID, "Code redeemed successfully!");
				
				// get type of code
				switch (atoi(aBufType))
				{
				case 0:// reset
					ResetAccount(ClientID);
					break;

				case 1:// level
					m_apPlayers[ClientID]->m_Player_level += atoi(aBufVal1);
					m_apPlayers[ClientID]->m_Player_money += atoi(aBufVal1) * 5;
					SetTeeScore(ClientID);
					ServerMessage(ClientID, "You received %s level (%d)", aBufVal1, m_apPlayers[ClientID]->m_Player_level);
					break;

				case 2:// money
					m_apPlayers[ClientID]->m_Player_money += atoi(aBufVal1);
					ServerMessage(ClientID, "You received %s money (%d)", aBufVal1, m_apPlayers[ClientID]->m_Player_money);
					break;

				case 100: case 101: case 102: case 103: case 104:// start events
					m_EvtTime[atoi(aBufType) - 100] += Server()->TickSpeed() * atoi(aBufVal1) * 60;
					ServerMessage(ClientID, "You have started event '%s' for %s minutes", m_aEventName[atoi(aBufType) - 100], aBufVal1);
					break;
				}

				// save instantly
				AccountUpdate(ClientID);

				if (remove(aFilePathComplete) != 0)
				{
					WriteModLog("Error removing redeem code: %s", aFilePathComplete);
				}

				WriteModLog("[%s]%s has used redeem code: %s (%s, %s)", m_apPlayers[ClientID]->m_Player_username, Server()->ClientName(ClientID), aBufCode, aTypeName[atoi(aBufType)], aBufVal1);
			}
			else
			{
				ServerMessage(ClientID, "Invalid redeem code");
				return;
			}
		}
		else
		{
			ServerMessage(ClientID, "Please wait %d seconds before entering a new code", m_aRedeemDelay[ClientID] / Server()->TickSpeed());
			return;
		}
	}
	else
	{
		ServerMessage(ClientID, "You need to log in to redeem a code");
		return;
	}
}

void CGameContext::HandleKill(int ClientID, int Victim, int Weapon)
{
	int Ksteps = 0;// own streak steps
	int VKsteps = 0;// victim streak steps
	int lvlDiff = 0;// level difference
	char aInfBuf[256] = { 0 };

	// crash safety
	if (!m_apPlayers[ClientID] || !m_apPlayers[Victim])
		return;

	// exit if killer is game world or self
	if (Weapon == WEAPON_GAME || Weapon == WEAPON_WORLD || Victim == ClientID)
		return;

	// calculate own killstreak steps
	Ksteps = min(m_apPlayers[ClientID]->m_Player_killcount / m_StepKillstreak, 5);

	// calculate victim killstreak steps
	VKsteps = m_apPlayers[Victim]->m_Player_killcount / m_StepKillstreak;

	m_apPlayers[ClientID]->m_Player_killcount++;

	// show killstreak message
	if (m_apPlayers[ClientID]->m_Player_killcount % m_StepKillstreak == 0)
	{
		str_format(aInfBuf, sizeof(aInfBuf), "%s is on a killstreak with %d kills", m_apPlayers[ClientID]->m_Player_nameraw, m_apPlayers[ClientID]->m_Player_killcount);
		SendChat(TEAM_SPECTATORS, CHAT_ALL, CHAT_ALL, aInfBuf);
	}

	// give kill ammo reward
	if (GetPlayerChar(ClientID)) // crash safety
		m_apPlayers[ClientID]->GetCharacter()->GainAmmoBack(m_AmmoReward);

	// gain normal exp
	m_apPlayers[ClientID]->m_Player_experience += m_ExpRatio * 1;

	// gain level difference exp bonus
	lvlDiff = (m_apPlayers[Victim]->m_Player_level - m_apPlayers[ClientID]->m_Player_level) / 50;

	if (lvlDiff > 0)
	{
		m_apPlayers[ClientID]->m_Player_experience += lvlDiff * m_BonusPerDiff50;
//		ServerMessage(ClientID, "bonus: %d", lvlDiff * m_BonusPerDiff50);
	}

/* old level difference system
	// calculate % level difference
	LvlDiff = (float)m_apPlayers[Victim]->m_Player_level / (float)m_apPlayers[ClientID]->m_Player_level - 1;

	// if above level 10 give level bonus
	if (m_apPlayers[ClientID]->m_Player_level >= 10)
	{
		m_apPlayers[ClientID]->m_Player_experience += m_ExpRatio * ((int)LvlDiff * m_BonusPer100);
	}
	else // if below lvl 10 give a max of 1 bonus exp per high level kill
	{
		m_apPlayers[ClientID]->m_Player_experience += m_ExpRatio * (min(1, floor((int)LvlDiff * m_BonusPer100)));
	}
*/

	// if on killstreak
	if (VKsteps > 0)
	{
		// add killstreak bonus
		m_apPlayers[ClientID]->m_Player_experience += m_ExpRatio * (VKsteps * m_BonusPerStreak);

		// show shutdown message
		str_format(aInfBuf, sizeof(aInfBuf), "%s has ended %s's killstreak and gained %d bonus exp", m_apPlayers[ClientID]->m_Player_nameraw, m_apPlayers[Victim]->m_Player_nameraw, m_ExpRatio * (VKsteps * m_BonusPerStreak));
		SendChat(TEAM_SPECTATORS, CHAT_ALL, CHAT_ALL, aInfBuf);
	}

	// if showexp is enabled
	if (m_apPlayers[ClientID]->m_aPlayer_util[1] == 0)
	{
		str_format(aInfBuf, sizeof(aInfBuf), "Exp: (%d / %d)", m_apPlayers[ClientID]->m_Player_experience, m_apPlayers[ClientID]->m_Player_level);
		SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, aInfBuf);
	}

	//	check if enough experience to level up
	if (!m_apPlayers[ClientID]->IsDummy())
	if (m_apPlayers[ClientID]->m_Player_experience >= m_apPlayers[ClientID]->m_Player_level)
	{
		//	level up effect
		if (GetPlayerChar(ClientID)) // crash safety
		{
//			CreatePlayerSpawn(m_apPlayers[ClientID]->GetCharacter()->GetPos());
			for (int i = 0; i < 3; ++i)
				CreateDeath(m_apPlayers[ClientID]->GetCharacter()->GetPos(), ClientID);
		}

		while (m_apPlayers[ClientID]->m_Player_experience >= m_apPlayers[ClientID]->m_Player_level)
		{
			m_apPlayers[ClientID]->m_Player_experience -= m_apPlayers[ClientID]->m_Player_level;
			m_apPlayers[ClientID]->m_Player_level++;
			m_apPlayers[ClientID]->m_Player_money += 5;

			str_format(aInfBuf, sizeof(aInfBuf), "Level up (%d) Money (+5) (%d)", m_apPlayers[ClientID]->m_Player_level, m_apPlayers[ClientID]->m_Player_money);
			SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, aInfBuf);
			
			// as long as no weapon has been upgraded, send this help
			if (m_apPlayers[ClientID]->m_aPlayer_stat[0] + 
				m_apPlayers[ClientID]->m_aPlayer_stat[1] + 
				m_apPlayers[ClientID]->m_aPlayer_stat[2] + 
				m_apPlayers[ClientID]->m_aPlayer_stat[3] + 
				m_apPlayers[ClientID]->m_aPlayer_stat[4] + 
				m_apPlayers[ClientID]->m_aPlayer_stat[5] + 
				m_apPlayers[ClientID]->m_aPlayer_stat[6] < 3)
			{
				ServerMessage(ClientID, "Upgrade one of your weapons, type /help game");
				SendBroadcast("Upgrade one of your weapons, type /help game", ClientID);
			}
		}

		// set score (level)
		SetTeeScore(ClientID);
	}

	// update every kill to not lose progress when vote passes
	if (m_apPlayers[ClientID]->m_Player_logged == true)
		AccountUpdate(ClientID);

	// compare level with max level of server for human players
	if (!m_apPlayers[ClientID]->IsDummy())
	{
		if (m_apPlayers[ClientID]->m_Player_level > m_MaxAllowedLevel && m_MaxAllowedLevel != -1 &&
			m_apPlayers[ClientID]->m_Player_status != 2 && m_apPlayers[ClientID]->m_Player_status != 3)
		{
			// for some reason the game crashes if you kill someone with hammer and logout instantly so we put a delay in
			m_apPlayers[ClientID]->m_LogOutDelay = 2;
			SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "You have surpassed the level limit of this server, now you can play on the higher servers!");
		}
	}
}

void CGameContext::HandleDeath(int ClientID, int Killer, int Weapon)
{
	char aInfBuf[256] = { 0 };

	// crash safety
	if (!m_apPlayers[ClientID])
		return;

	// exit if killer is game world or self
	if (Weapon == WEAPON_GAME || Weapon == WEAPON_WORLD || Killer == ClientID)
	{
		// reset killstreak
		m_apPlayers[ClientID]->m_Player_killcount = 0;
		return;
	}

	// handle killstreak
	if (m_apPlayers[ClientID]->m_Player_killcount >= m_StepKillstreak)
	{
		// explosion effect
		if (GetPlayerChar(ClientID)) // crash safety
			CreateExplosionExt(m_apPlayers[ClientID]->GetCharacter()->GetPos(), ClientID, Weapon, 0.0f, 2, 2, true);
	}

	// reset killstreak
	m_apPlayers[ClientID]->m_Player_killcount = 0;
	return;
}

void CGameContext::TreatPlayer(char *ID, int ClientID, int Effect, int Duration)
{
	char aBuf[256] = { 0 };
	int IDInt = atoi(ID);

	// check if ID is a number, else return
	for (int i = 0; i < strlen(ID); ++i)
	{
		if (!isdigit(ID[i]))
		{
			SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "ID is not a number");
			return;
		}
	}

	if (GetIdStatus(IDInt, ClientID) == false)
		return;

	// found
	if (m_apPlayers[IDInt]->m_Player_status != 3 && m_apPlayers[IDInt]->m_Player_status != 2)
	{
		if (Effect == 0)// kick
		{
			str_format(aBuf, sizeof(aBuf), "kick %d", IDInt);
			ServerMessage(ClientID, "%s has been kicked", Server()->ClientName(IDInt));
			WriteModLog("%s has kicked %s", Server()->ClientName(ClientID), Server()->ClientName(IDInt));
			Console()->ExecuteLine(aBuf);
		}
		else if (Effect == 1)// ban
		{
			str_format(aBuf, sizeof(aBuf), "ban %d %d", IDInt, Duration);
			ServerMessage(ClientID, "%s has been banned for %d minutes", Server()->ClientName(IDInt), Duration);
			WriteModLog("%s has banned %s for %d minutes", Server()->ClientName(ClientID), Server()->ClientName(IDInt), Duration);
			Console()->ExecuteLine(aBuf);
		}
	}
	else// someone's trying to kick the admin or a moderator
	{
		ServerMessage(ClientID, "Moderators / Admins cannot be kicked");
		return;
	}

	return;
}

void CGameContext::FreezePlayer(char *ID, int ClientID, int Reverse)
{
	char aBuf[256] = { 0 };
	int IDInt = atoi(ID);
	
	// check if ID is a number, else return
	for (int i = 0; i < strlen(ID); ++i)
	{
		if (!isdigit(ID[i]))
		{
			SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "ID is not a number");
			return;
		}
	}

	if (GetIdStatus(IDInt, ClientID) == false)
	{
		return;
	}

	// found
	if (m_apPlayers[IDInt]->m_Player_status != 3 && m_apPlayers[IDInt]->m_Player_status != 2)
	{
		if (Reverse == 0)
		{
			if (m_apPlayers[IDInt]->m_Player_status != 1) // only if not frozen
			{
				if (GetPlayerChar(IDInt)) // crash safety
				{
					m_apPlayers[IDInt]->GetCharacter()->FreezeSelf();
					m_apPlayers[IDInt]->m_Player_status = 1;
					SendChat(TEAM_SPECTATORS, CHAT_NONE, IDInt, "You have been frozen");
					CreateSound(m_apPlayers[IDInt]->GetCharacter()->GetPos(), SOUND_LASER_BOUNCE);

					WriteModLog("%s has frozen %s", Server()->ClientName(ClientID), Server()->ClientName(IDInt));
				}
				else
				{
					SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "Bad timing, try again");
					return;
				}
			}
			else
			{
				str_format(aBuf, sizeof(aBuf), "%s is already frozen", m_apPlayers[IDInt]->m_Player_nameraw);
				SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, aBuf);
				return;
			}
		}
		else
		{
			if (m_apPlayers[IDInt]->m_Player_status == 1) // only if frozen
			{
				if (GetPlayerChar(IDInt)) // crash safety
				{
					m_apPlayers[IDInt]->m_Player_status = 0;
					m_apPlayers[IDInt]->GetCharacter()->Die(WEAPON_WORLD, WEAPON_GAME);
					SendChat(TEAM_SPECTATORS, CHAT_NONE, IDInt, "You have been unfrozen, please don't cheat anymore :)");

					WriteModLog("%s has unfrozen %s", Server()->ClientName(ClientID), Server()->ClientName(IDInt));
				}
				else
				{
					SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "Bad timing, try again");
					return;
				}
			}
			else
			{
				str_format(aBuf, sizeof(aBuf), "%s is not frozen", m_apPlayers[IDInt]->m_Player_nameraw);
				SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, aBuf);
				return;
			}
		}

		AccountUpdate(IDInt);
	}
	else// someone's trying to freeze the admin or a moderator
	{
		if (Reverse == 0)
		{
			if (GetPlayerChar(ClientID)) // crash safety
			{
				m_apPlayers[ClientID]->GetCharacter()->FreezeSelf();
				m_apPlayers[ClientID]->m_Player_status = 1;
				SendChat(TEAM_SPECTATORS, CHAT_NONE, m_apPlayers[ClientID]->GetCID(), "You became frozen for attempting to freeze a moderator / admin.");

				str_format(aBuf, sizeof(aBuf), "%s tried to freeze you and got punished", m_apPlayers[ClientID]->m_Player_nameraw);
				SendBroadcast(aBuf, IDInt);
				SendChat(TEAM_SPECTATORS, CHAT_NONE, IDInt, aBuf);

				WriteModLog("%s has attempted freezing %s and got punished", Server()->ClientName(ClientID), Server()->ClientName(IDInt));
			}
			else
			{
				SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "Bad timing, try again");
				return;
			}

			AccountUpdate(ClientID);

			return;
		}
		else
		{
			if (m_apPlayers[IDInt]->m_Player_status != 1) // only if not frozen
			{
				str_format(aBuf, sizeof(aBuf), "%s is not frozen", m_apPlayers[IDInt]->m_Player_nameraw);
				SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, aBuf);
				return;
			}
		}
	}

	if (Reverse == 0)
		str_format(aBuf, sizeof(aBuf), "%s has been frozen", m_apPlayers[IDInt]->m_Player_nameraw);
	else
		str_format(aBuf, sizeof(aBuf), "%s has been unfrozen", m_apPlayers[IDInt]->m_Player_nameraw);

	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, aBuf);
}

void CGameContext::ShowIdList(int ClientID)
{
	char aBuf[256] = { 0 };
	int cnt = 0;

	for (int i = 0; i < MAX_CLIENTS; ++i)
	{
		if (!m_apPlayers[i])
			continue;

		// ignore spectators and dummies
		if (m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS && !m_apPlayers[i]->IsDummy())
		{
			cnt++;
			str_format(aBuf, sizeof(aBuf), "%d - %s", m_apPlayers[i]->GetCID(), m_apPlayers[i]->m_Player_nameraw);
			SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, aBuf);
		}
	}

	// if no players were found
	if (cnt == 0)
		SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "No active players found");
}

void CGameContext::ShowPlayerStats(int ID, int ClientID)
{
	char aTextBuf[256] = { 0 };
	char aWeaponName[7][MAX_INPUT_SIZE] = { "hammer", "gun", "shotgun", "grenade", "rifle", "life", "handle" };
	char aBuf[256] = { 0 };

	// ID beyond bounds
	if (ID < 0 || ID >= MAX_CLIENTS)
	{
		ServerMessage(ClientID, "ID beyond bounds (0 - 15 only)");
		return;
	}

	// player non existent
	if (!m_apPlayers[ID])
	{
		ServerMessage(ClientID, "There is no player with that ID");
		return;
	}
	else if (m_apPlayers[ID]->IsDummy())
	{
		ServerMessage(ClientID, "Dummy says his stats are private");
		return;
	}

	// logged in
	if (m_apPlayers[ID]->m_Player_logged == false)
	{
		ServerMessage(ClientID, "%s is not logged in", Server()->ClientName(ID));
		return;
	}

	// found
	ServerMessage(ClientID, "**** %s ****", Server()->ClientName(ID));
	ServerMessage(ClientID, "level - %d", m_apPlayers[ID]->m_Player_level);
	ServerMessage(ClientID, "money - %d", m_apPlayers[ID]->m_Player_money);
	ServerMessage(ClientID, "experience - %d / %d", m_apPlayers[ID]->m_Player_experience, m_apPlayers[ID]->m_Player_level);
	ServerMessage(ClientID, "*******************");

	for (int i = 0; i < 7; i++)
	{
		ServerMessage(ClientID, "%s - %d", aWeaponName[i], m_apPlayers[ID]->m_aPlayer_stat[i]);
	}
}

bool CGameContext::GetIdStatus(int ID, int ClientID)
{
	char aBuf[256] = { 0 };

	// ID beyond bounds
	if (ID < 0 || ID >= MAX_CLIENTS)
	{
		SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "ID beyond bounds (0 - 15 only)");
		return false;
	}

	// player non existent
	if (!m_apPlayers[ID])
	{
		SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "There is no player with that ID");
		return false;
	}

	// logged in
	if (m_apPlayers[ID]->m_Player_logged == false)
	{
		str_format(aBuf, sizeof(aBuf), "%s is not logged in", Server()->ClientName(ID));
		SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, aBuf);
		return false;
	}

	return true;
}

void CGameContext::ToggleUndercover(int ClientID)
{
	char aBuf[256] = { 0 };

	if (m_apPlayers[ClientID]->m_Player_logged == true)
	{
		m_apPlayers[ClientID]->m_aPlayer_util[0] = !m_apPlayers[ClientID]->m_aPlayer_util[0];
		AccountUpdate(m_apPlayers[ClientID]->GetCID());

		if (m_apPlayers[ClientID]->m_aPlayer_util[0] == 0)
			str_copy(aBuf, "You are no longer undercover", sizeof(aBuf));
		else
			str_copy(aBuf, "You are now undercover", sizeof(aBuf));
	}
	else
	{
		str_copy(aBuf, "You are not logged in", sizeof(aBuf));
	}

	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, aBuf);
}

void CGameContext::ToggleSwitchMode(int ClientID)
{
	char aTextBuf[256] = { 0 };

	if (m_apPlayers[ClientID]->m_Player_logged == true)
	{
		m_apPlayers[ClientID]->m_aPlayer_option[0] = !m_apPlayers[ClientID]->m_aPlayer_option[0];

		str_format(aTextBuf, sizeof(aTextBuf), "mode: %s", m_apPlayers[ClientID]->m_aPlayer_option[0] == 0 ? "NORMAL" : "SPECIAL");

		SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, aTextBuf);
	}
	else
	{
		SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "You are not logged in");
	}
}

void CGameContext::ToggleShowExp(int ClientID)
{
	char aBuf[256] = { 0 };

	if (m_apPlayers[ClientID]->m_Player_logged == true)
	{
		m_apPlayers[ClientID]->m_aPlayer_util[1] = !m_apPlayers[ClientID]->m_aPlayer_util[1];

		if (m_apPlayers[ClientID]->m_aPlayer_util[1] == 0)
			str_copy(aBuf, "showexp: on", sizeof(aBuf));
		else
			str_copy(aBuf, "showexp: off", sizeof(aBuf));

		AccountUpdate(ClientID);
	}
	else
	{
		str_copy(aBuf, "You are not logged in", sizeof(aBuf));
	}

	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, aBuf);
}

/*
void CGameContext::GetTeeName(int ClientID, char* Buffer)
{
	if (!m_apPlayers[ClientID])
	{
		str_copy(Buffer, "[d] lod", MAX_NAME_LENGTH * 2);
		return;
	}

	char aPart[256] = { 0 };
	char aNameTrans[256] = { 0 };

	//add level tag to name
	str_format(aPart, sizeof(aPart), "[%d]%s", m_apPlayers[ClientID]->m_Player_level, m_apPlayers[ClientID]->m_Player_nameraw);

	//add mod tag to name if not undercover
	if (m_apPlayers[ClientID]->m_Player_status == 2 && m_apPlayers[ClientID]->m_aPlayer_util[0] == 0)
		str_format(aNameTrans, sizeof(aNameTrans), "[M]%s", aPart);
	else if (m_apPlayers[ClientID]->m_Player_status == 3 && m_apPlayers[ClientID]->m_aPlayer_util[0] == 0)
		str_format(aNameTrans, sizeof(aNameTrans), "[A]%s", aPart);
	else
		str_copy(aNameTrans, aPart, sizeof(aNameTrans));

	str_copy(Buffer, aNameTrans, MAX_NAME_LENGTH * 2);
}
*/

void CGameContext::SetTeeEmote(int ClientID, int Emote)
{
	if (m_apPlayers[ClientID]->m_Player_logged == true)
	{
		if (GetPlayerChar(ClientID)) // crash safety
		{
			m_apPlayers[ClientID]->GetCharacter()->SetEmote(Emote, Server()->Tick() + Server()->TickSpeed() * 1000 * 604800);// set emote for one week straight
			m_apPlayers[ClientID]->m_aPlayer_util[2] = Emote;
			AccountUpdate(ClientID);
		}
		else
		{
			SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "Bad timing, try again");
		}
	}
	else
	{
		SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, "You are not logged in");
	}
}

void CGameContext::SetTeeScore(int ClientID)
{
	m_apPlayers[ClientID]->m_Score = m_apPlayers[ClientID]->m_Player_level;
//	(((m_apPlayers[ClientID]->m_Player_level - 1) * (m_apPlayers[ClientID]->m_Player_level - 1)) + m_apPlayers[ClientID]->m_Player_level - 1) / 2 + m_apPlayers[ClientID]->m_Player_experience;
}

// *dummy functions
void CGameContext::DummyAdd(int Amount)
{
	int cnt = 0;

	Amount = min(16, Amount);

	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (cnt >= Amount)
			break;

		if (m_apPlayers[(MAX_PLAYERS - 1) - i])
			continue;

		DummyInit((MAX_PLAYERS - 1) - i);
		cnt++;
	}
}

void CGameContext::DummyRemove(int Amount)
{
	int cnt = 0;

	Amount = max(1, Amount);

	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!m_apPlayers[i])
			continue;

		if (m_apPlayers[i]->IsDummy())
		{
			DummyDelete(i);
			cnt++;
		}

		if (cnt >= Amount)
			break;
	}
}

void CGameContext::DummyInit(int ClientID)
{
	if (m_apPlayers[ClientID])
		return;

	// choose character
	int wanted_character = floor(min(15, (int)floor(frandom() * 16)));
	int cnt = 0;

	// set dummy information
	const char aRandomName[16][MAX_NAME_LENGTH] = { "Kitty", "Stripo", "Bearbo", "Cammo", "Wario", "Neddy", "Clyde", "Smitty", "Pinky", "Boppy", "Toddles", "Webtri", "Vincent", "Twinny", "Teetso", "Plopper" };

	OnClientConnected(ClientID, true);
	
	// wanted_character
	m_apPlayers[ClientID]->m_dum_chosenskin = wanted_character;

	while (DummyCharTaken(ClientID, m_apPlayers[ClientID]->m_dum_chosenskin))
	{
		m_apPlayers[ClientID]->m_dum_chosenskin = (wanted_character + cnt) % MAX_PLAYERS;
		cnt++;
	}

	strncpy(m_apPlayers[ClientID]->m_dum_name, aRandomName[m_apPlayers[ClientID]->m_dum_chosenskin], sizeof(m_apPlayers[ClientID]->m_dum_name) / sizeof(char));
	strncpy(m_apPlayers[ClientID]->m_dum_clan, "LUMMY", sizeof(m_apPlayers[ClientID]->m_dum_clan) / sizeof(char));
	m_apPlayers[ClientID]->m_dum_country = 0;

	m_apPlayers[ClientID]->m_Player_logged = true;
	strncpy(m_apPlayers[ClientID]->m_Player_nameraw, m_apPlayers[ClientID]->m_dum_name, sizeof(m_apPlayers[ClientID]->m_Player_nameraw));
	// add null terminating zero to name manually to prevent weird name bugs
	m_apPlayers[ClientID]->m_Player_nameraw[strlen(m_apPlayers[ClientID]->m_Player_nameraw)] = 0;

	// set some random emote (exclude surprise)
	int emote = min(EMOTE_ANGRY, floor(frandom() * EMOTE_BLINK));
	m_apPlayers[ClientID]->m_aPlayer_util[2] = emote == EMOTE_SURPRISE ? EMOTE_NORMAL:emote;

	// set dummy info
	for (int p = 0; p < 6; p++)
	{
		strncpy(m_apPlayers[ClientID]->m_TeeInfos.m_aaSkinPartNames[p], m_dum_skinpartnames[m_apPlayers[ClientID]->m_dum_chosenskin][p], sizeof(m_apPlayers[ClientID]->m_TeeInfos.m_aaSkinPartNames[p]));
		m_apPlayers[ClientID]->m_TeeInfos.m_aUseCustomColors[p] = m_dum_usecustomcolors[m_apPlayers[ClientID]->m_dum_chosenskin][p];
		m_apPlayers[ClientID]->m_TeeInfos.m_aSkinPartColors[p] = m_dum_skinpartcolors[m_apPlayers[ClientID]->m_dum_chosenskin][p];
	}

	// use switchmode 50% of the time
	if (frandom() > 0.5f)
		m_apPlayers[ClientID]->m_aPlayer_option[0] = 1;
	else
		m_apPlayers[ClientID]->m_aPlayer_option[0] = 0;

	// enter the game
	OnClientEnter(ClientID);
	return;
}

void CGameContext::DummyDelete(int ClientID)
{
	if (m_apPlayers[ClientID]->IsDummy())
		OnClientDrop(ClientID, "Napping");

	return;
}

bool CGameContext::DummyCharTaken(int ClientID, int CharID)
{
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!m_apPlayers[i] || ClientID == i)
			continue;

		if (m_apPlayers[i]->IsDummy())
		{
			if (m_apPlayers[i]->m_dum_chosenskin == CharID)
				return true;
		}
	}
	
	return false;
}

void CGameContext::DummyAdaptLevel(int ClientID)
{
	int cnt_players = 0;
	int sum_level = 0;
	int mylevel = 0;

	if (!m_apPlayers[ClientID])
		return;
	if (!m_apPlayers[ClientID]->IsDummy())// ignore dummy level
		return;
	if (m_apPlayers[ClientID]->m_Player_status > 0)
		return;

	// set level to the average human player level
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!m_apPlayers[i])
			continue;
		if (m_apPlayers[i]->IsDummy())
			continue;

		// ignore unlogged / frozen / spectating players
		if (m_apPlayers[i]->m_Player_logged && m_apPlayers[i]->m_Player_status != 1 && m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
		{
			cnt_players++;
			sum_level += m_apPlayers[i]->m_Player_level;
		}
	}

	m_apPlayers[ClientID]->m_Player_level = round(sum_level / max(1, cnt_players));

	SetTeeScore(ClientID);

	mylevel = m_apPlayers[ClientID]->m_Player_level;

	// "Spend money wisely"
	// spend on life later
	if (mylevel > 7)
		m_apPlayers[ClientID]->m_aPlayer_stat[5] = (int)ceil((float)mylevel * 0.1f);
	else
		m_apPlayers[ClientID]->m_aPlayer_stat[5] = 0;

	// preferred weapon
	m_apPlayers[ClientID]->m_aPlayer_stat[m_apPlayers[ClientID]->m_dum_wprimpref] = (int)ceil((float)mylevel * 0.6f);
	// secondary weapon needs some attention too
//	m_apPlayers[ClientID]->m_aPlayer_stat[m_apPlayers[ClientID]->m_dum_wsecpref] = (int)ceil((float)mylevel * 0.1f);

	// handlesome
	m_apPlayers[ClientID]->m_aPlayer_stat[6] = (int)ceil((float)mylevel * 0.2f);

	// default weapons default values
	m_apPlayers[ClientID]->m_aPlayer_stat[WEAPON_HAMMER] = max(1, m_apPlayers[ClientID]->m_aPlayer_stat[WEAPON_HAMMER]);
	m_apPlayers[ClientID]->m_aPlayer_stat[WEAPON_GUN] = max(1, m_apPlayers[ClientID]->m_aPlayer_stat[WEAPON_GUN]);
}

int CGameContext::AmtHumanPlayers(void)
{
	int cnt_players = 0;

	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!m_apPlayers[i] || m_apPlayers[i]->IsDummy())
			continue;

		cnt_players++;
	}

	return cnt_players;
}

int CGameContext::AmtDummies(void)
{
	int cnt_dummies = 0;

	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!m_apPlayers[i] || !m_apPlayers[i]->IsDummy())
			continue;

		cnt_dummies++;
	}

	return cnt_dummies;
}

// write to mod log (and chat log)
void CGameContext::WriteModLog(char *format, ...)
{
	FILE* fpointer;
	va_list argList;
	char buffer[256] = { 0 };

	fpointer = fopen(FILEPATH_MODLOG, "a");

	va_start(argList, format);

	fprintf(fpointer, "\n");

	vfprintf(fpointer, format, argList);

	fclose(fpointer);

	vsprintf(buffer, format, argList);// write arguments to buffer for chat log

	va_end(argList);

	// also write mod log line to chat log
	WriteChatLog(buffer);
}

// write to chat log
void CGameContext::WriteChatLog(char *format, ...)
{
	FILE* fpointer;
	va_list argList;
	char buffer[256] = { 0 };
	char filepath_complete[256] = { 0 };

	// store chats differently for each server
	if (strstr(g_Config.m_SvName, "[0 - 30]") != NULL)
		str_format(filepath_complete, sizeof(filepath_complete), "%s/%s", FOLDERPATH_CHATLOG_1, FILEPATH_CHATLOG);
	else if (strstr(g_Config.m_SvName, "[30 - 75]") != NULL)
		str_format(filepath_complete, sizeof(filepath_complete), "%s/%s", FOLDERPATH_CHATLOG_2, FILEPATH_CHATLOG);
	else if (strstr(g_Config.m_SvName, "[75 - 120]") != NULL)
		str_format(filepath_complete, sizeof(filepath_complete), "%s/%s", FOLDERPATH_CHATLOG_3, FILEPATH_CHATLOG);
	else if (strstr(g_Config.m_SvName, "[120 - 300]") != NULL)
		str_format(filepath_complete, sizeof(filepath_complete), "%s/%s", FOLDERPATH_CHATLOG_4, FILEPATH_CHATLOG);
	else if (strstr(g_Config.m_SvName, "[public]") != NULL)
		str_format(filepath_complete, sizeof(filepath_complete), "%s/%s", FOLDERPATH_CHATLOG_5, FILEPATH_CHATLOG);
	else
		return;

	fpointer = fopen(filepath_complete, "a");

	va_start(argList, format);

	fprintf(fpointer, "\n");

	vfprintf(fpointer, format, argList);

	fclose(fpointer);
	va_end(argList);
}

// finally some formatted server output
void CGameContext::ServerMessage(int ClientID, char *format, ...)
{
	va_list argList;
	char buffer[256] = { 0 };
	va_start(argList, format);
	vsprintf(buffer, format, argList);
	SendChat(TEAM_SPECTATORS, CHAT_NONE, ClientID, buffer);
	va_end(argList);
}

void CGameContext::PickUpKatana(int ClientID)
{
	int ownLevelBonus = 0;
	char aBuf[256] = { 0 };

	// crash safety
	if (!m_apPlayers[ClientID])
		return;

	// give +8 exp for picking up and 30% of own level
	ownLevelBonus = m_apPlayers[ClientID]->m_Player_level * swep_katana_bonus_percLevel;

	m_apPlayers[ClientID]->m_Player_experience += m_ExpRatio * (swep_katana_bonus_pickup + ownLevelBonus);
	ServerMessage(ClientID, "Katana pickup bonus: +%d exp (%d)", m_ExpRatio * (swep_katana_bonus_pickup + ownLevelBonus), m_apPlayers[ClientID]->m_Player_experience);
	
	str_format(aBuf, sizeof(aBuf), "%s has unleashed the katana and gained %d bonus exp", m_apPlayers[ClientID]->m_Player_nameraw, m_ExpRatio * (swep_katana_bonus_pickup + ownLevelBonus));

	// kill all players
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!m_apPlayers[i] || i == ClientID)
			continue;

		if (GetPlayerChar(m_apPlayers[i]->GetCID()))// crash safety
			m_apPlayers[i]->GetCharacter()->Die(ClientID, WEAPON_NINJA);
	}

	SendChat(TEAM_SPECTATORS, CHAT_ALL, ClientID, aBuf);
}

void CGameContext::HandleCustomVote(char *pVoteCommand)
{
	FILE *fpointer;
	int cnt = 0;
	char *pChar;
	char aTextBuf[MAX_INPUT_SIZE] = { 0 };
	char aCmdCopy[MAX_INPUT_SIZE] = { 0 };
	char aComPart[2][MAX_INPUT_SIZE] = { 0 };

	str_copy(aCmdCopy, pVoteCommand, sizeof(aCmdCopy));

	//tokenize vote command string
	pChar = strtok(aCmdCopy, " ");

	while (pChar != NULL)
	{
		str_copy(aComPart[cnt], pChar, sizeof(aComPart[0]));
		pChar = strtok(NULL, " ");
		cnt++;

		if (cnt == 2)
			break;
	}

	// handle votes pass
	if (str_comp_nocase(aComPart[0], "sv_bots") == 0)// handle bots amount
	{
		m_Vote_AmountBots = clamp(atoi(aComPart[1]), 0, (int)MAX_PLAYERS);
	}
	else if (str_comp_nocase(aComPart[0], "sv_skip_events") == 0)// skip all active events
	{
		for (int i = 0; i < 10; i++)
			m_EvtTime[i] = 0;

		// clear event times file
		fpointer = fopen(FILEPATH_EVENTTIMES, "w");
		fclose(fpointer);
		
		SendBroadcast("All active events have been skipped", -1);
	}
}

// own console commands
void CGameContext::ConMakeMod(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	char aBuf[256] = { 0 };
	int ID = pResult->GetInteger(0);

	// check if ID is a number, else return
	for (int i = 0; i < strlen(pResult->GetString(0)); ++i)
	{
		if (!isdigit(pResult->GetString(0)[i]))
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "makemod", "ID is not a number");
			return;
		}
	}

	// ID beyond bounds
	if (ID < 0 || ID >= MAX_CLIENTS)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "makemod", "ID beyond bounds (0 - 15 only)");
		return;
	}

	// player non existent
	if (!pSelf->m_apPlayers[ID])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "makemod", "There is no player with that ID");
		return;
	}

	// logged in
	if (pSelf->m_apPlayers[ID]->m_Player_logged == false)
	{
		str_format(aBuf, sizeof(aBuf), "%s is not logged in", pSelf->Server()->ClientName(ID));
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "makemod", aBuf);
		return;
	}

	// found
	if (pResult->GetInteger(1) == 0)// option moderator
	{
		if (pSelf->m_apPlayers[ID]->m_Player_status != 2)
		{
			pSelf->m_apPlayers[ID]->m_Player_status = 2;
			pSelf->SendChat(TEAM_SPECTATORS, CHAT_NONE, ID, "You have been promoted to moderator, type \"/help moderator\" to access moderator command help");
			pSelf->m_apPlayers[ID]->m_aPlayer_util[0] = 0;// reset undercover option
			pSelf->AccountUpdate(ID);

			str_format(aBuf, sizeof(aBuf), "%s has been promoted to moderator", pSelf->m_apPlayers[ID]->m_Player_nameraw);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "%s is already moderator", pSelf->m_apPlayers[ID]->m_Player_nameraw);
		}
	}
	else if (pResult->GetInteger(1) == 10000)// option admin
	{
		if (pSelf->m_apPlayers[ID]->m_Player_status != 3)
		{
			pSelf->m_apPlayers[ID]->m_Player_status = 3;
			pSelf->SendChat(TEAM_SPECTATORS, CHAT_NONE, ID, "You have been promoted to admin, type \"/help moderator\" to access moderator command help");
			pSelf->m_apPlayers[ID]->m_aPlayer_util[0] = 0;// reset undercover option
			pSelf->AccountUpdate(ID);

			str_format(aBuf, sizeof(aBuf), "%s has been promoted to admin", pSelf->m_apPlayers[ID]->m_Player_nameraw);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "%s is already admin", pSelf->m_apPlayers[ID]->m_Player_nameraw);
		}
	}
	else// option demote
	{
		if (pSelf->m_apPlayers[ID]->m_Player_status != 0)
		{
			pSelf->m_apPlayers[ID]->m_Player_status = 0;
			pSelf->SendChat(TEAM_SPECTATORS, CHAT_NONE, ID, "You are no longer a moderator");
			pSelf->AccountUpdate(ID);

			str_format(aBuf, sizeof(aBuf), "%s has been demoted", pSelf->m_apPlayers[ID]->m_Player_nameraw);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "%s is already rookie", pSelf->m_apPlayers[ID]->m_Player_nameraw);
		}
	}
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "makemod", aBuf);
}

void CGameContext::ConShowStats(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	char aTextBuf[256] = { 0 };
	char aWeaponName[7][MAX_INPUT_SIZE] = { "hammer", "gun", "shotgun", "grenade", "rifle", "life", "handle" };
	char aBuf[256] = { 0 };
	int ID = pResult->GetInteger(0);

	// check if ID is a number, else return
	for (int i = 0; i < strlen(pResult->GetString(0)); ++i)
	{
		if (!isdigit(pResult->GetString(0)[i]))
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "stats", "ID is not a number");
			return;
		}
	}

	// ID beyond bounds
	if (ID < 0 || ID >= MAX_CLIENTS)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "stats", "ID beyond bounds (0 - 15 only)");
		return;
	}

	// player non existent
	if (!pSelf->m_apPlayers[ID])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "stats", "There is no player with that ID");
		return;
	}
	else if (pSelf->m_apPlayers[ID]->IsDummy())
	{
		/*
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "stats", "Dummy says his stats are private");
		return;
		*/
	}

	// logged in
	if (pSelf->m_apPlayers[ID]->m_Player_logged == false)
	{
		str_format(aBuf, sizeof(aBuf), "%s is not logged in", pSelf->Server()->ClientName(ID));
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "stats", aBuf);
		return;
	}

	// found
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "stats", "*******************");
	str_format(aTextBuf, sizeof(aTextBuf), "level - %d", pSelf->m_apPlayers[ID]->m_Player_level);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "stats", aTextBuf);
	str_format(aTextBuf, sizeof(aTextBuf), "money - %d", pSelf->m_apPlayers[ID]->m_Player_money);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "stats", aTextBuf);
	str_format(aTextBuf, sizeof(aTextBuf), "experience - %d / %d", pSelf->m_apPlayers[ID]->m_Player_experience, pSelf->m_apPlayers[ID]->m_Player_level);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "stats", aTextBuf);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "stats", "*******************");

	for (int i = 0; i < 7; i++)
	{
		str_format(aTextBuf, sizeof(aTextBuf), "%s - %d", aWeaponName[i], pSelf->m_apPlayers[ID]->m_aPlayer_stat[i]);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "stats", aTextBuf);
	}

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "stats", "*******************");
}

void CGameContext::ConGiveMoney(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	char aBuf[256] = { 0 };
	int ID = pResult->GetInteger(0);

	// check if ID is a number, else return
	for (int i = 0; i < strlen(pResult->GetString(0)); ++i)
	{
		if (!isdigit(pResult->GetString(0)[i]))
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "makemod", "ID is not a number");
			return;
		}
	}

	// ID beyond bounds
	if (ID < 0 || ID >= MAX_CLIENTS)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "givemoney", "ID beyond bounds (0 - 15 only)");
		return;
	}

	// player non existent
	if (!pSelf->m_apPlayers[ID])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "givemoney", "There is no player with that ID");
		return;
	}

	// logged in
	if (pSelf->m_apPlayers[ID]->m_Player_logged == false)
	{
		str_format(aBuf, sizeof(aBuf), "%s is not logged in", pSelf->Server()->ClientName(ID));
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "givemoney", aBuf);
		return;
	}

	// found
	pSelf->m_apPlayers[ID]->m_Player_money += atoi(pResult->GetString(1));
	str_format(aBuf, sizeof(aBuf), "You just received %d money from the administrators", pResult->GetInteger(1));
	pSelf->SendChat(TEAM_SPECTATORS, CHAT_NONE, ID, aBuf);
	pSelf->AccountUpdate(ID);

	str_format(aBuf, sizeof(aBuf), "%s has received %d money", pSelf->m_apPlayers[ID]->m_Player_nameraw, pResult->GetInteger(1));

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "givemoney", aBuf);
}

void CGameContext::ConResetAcc(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int moneyStored = 0;
	char aBuf[256] = { 0 };
	int ID = pResult->GetInteger(0);

	// check if ID is a number, else return
	for (int i = 0; i < strlen(pResult->GetString(0)); ++i)
	{
		if (!isdigit(pResult->GetString(0)[i]))
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "makemod", "ID is not a number");
			return;
		}
	}

	// ID beyond bounds
	if (ID < 0 || ID >= MAX_CLIENTS)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "resetacc", "ID beyond bounds (0 - 15 only)");
		return;
	}

	// player non existent
	if (!pSelf->m_apPlayers[ID])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "resetacc", "There is no player with that ID");
		return;
	}

	// not logged in
	if (pSelf->m_apPlayers[ID]->m_Player_logged == false)
	{
		str_format(aBuf, sizeof(aBuf), "%s is not logged in", pSelf->Server()->ClientName(ID));
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "resetacc", aBuf);
		return;
	}

	// save unspent money
	moneyStored = pSelf->m_apPlayers[ID]->m_Player_money;
	pSelf->m_apPlayers[ID]->m_Player_money = 0;

	// calculate upgrade money and reset stats
	for (int i = 0; i < 7; ++i)
	{
		pSelf->m_apPlayers[ID]->m_Player_money += pSelf->m_apPlayers[ID]->m_aPlayer_stat[i] * 5;

		if (i < 2)// hammer / gun have 1 free level
			pSelf->m_apPlayers[ID]->m_aPlayer_stat[i] = 1;
		else
			pSelf->m_apPlayers[ID]->m_aPlayer_stat[i] = 0;
	}

	// give money
	pSelf->m_apPlayers[ID]->m_Player_money += moneyStored - 10;// subtract free gun and hammer level

	pSelf->AccountUpdate(ID);

	pSelf->SendChat(TEAM_SPECTATORS, CHAT_NONE, ID, "Your account has been reset, spend your money wisely!");
	str_format(aBuf, sizeof(aBuf), "%s's account has been reset", pSelf->m_apPlayers[ID]->m_Player_nameraw);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "resetacc", aBuf);
}

void CGameContext::ConIdList(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	char aBuf[256] = { 0 };
	int cnt = 0;

	for (int i = 0; i < MAX_CLIENTS; ++i)
	{
		if (!pSelf->m_apPlayers[i])
			continue;

		// ignore unlogged players and dummies
		if (pSelf->m_apPlayers[i]->m_Player_logged && !pSelf->m_apPlayers[i]->IsDummy())
		{
			cnt++;
			str_format(aBuf, sizeof(aBuf), "%d - %s (%s)", pSelf->m_apPlayers[i]->GetCID(), pSelf->m_apPlayers[i]->m_Player_nameraw, pSelf->m_apPlayers[i]->m_Player_username);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "idlist", aBuf);
		}
	}

	// if no players were found
	if (cnt == 0)
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "idlist", "No active players found");
}

void CGameContext::ConAccUpdate(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	// update all accounts manually (for whatever reason)
	for (int i = 0; i < MAX_CLIENTS; ++i)
	{
		if (!pSelf->m_apPlayers[i])
			continue;

		if (pSelf->m_apPlayers[i]->m_Player_logged != true)
			continue;

		pSelf->AccountUpdate(pSelf->m_apPlayers[i]->GetCID());
	}

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "accupdate", "Updated all accounts");
}

void CGameContext::ConDummyAdd(IConsole::IResult *pResult, void *pUserData)
{
	char aInfo[256] = { 0 };
	int amt = pResult->GetInteger(0);
	int amtModified = 0;

	CGameContext *pSelf = (CGameContext *)pUserData;

	if (amt > 0)
		amtModified = min(MAX_PLAYERS - pSelf->m_Vote_AmountBots, amt);
	else
		amtModified = min(pSelf->m_Vote_AmountBots, -amt) * -1;

	pSelf->m_Vote_AmountBots += amtModified;

	str_format(aInfo, sizeof(aInfo), "added %d bots", amtModified);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "dummyadd", aInfo);
}

void CGameContext::ConStartEvent(IConsole::IResult *pResult, void *pUserData)
{
	// note: event time is added to current event time

	CGameContext *pSelf = (CGameContext *)pUserData;
	char aBuf[256] = { 0 };
	int type = pResult->GetInteger(0);
	float duration = pResult->GetFloat(1);

	pSelf->m_EvtTime[type] += pSelf->Server()->TickSpeed() * duration * 60;

	str_format(aBuf, sizeof(aBuf), "Event %d has been started for %.1f minutes", type, duration);

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "startevent", aBuf);
}

void CGameContext::ConCmdList(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "cmdlist", "-----------------------------------------------------------------------------------------------");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "cmdlist", "makemod <ID> <1 - demote / 10000 - admin> - make a player moderator, admin or demote him");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "cmdlist", "showstats <ID> - list the stats of an other player");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "cmdlist", "givemoney <ID> <amount> - give a player money");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "cmdlist", "resetacc <ID> - reset a player's account");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "cmdlist", "idlist - show the IDs with names and username of all active players");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "cmdlist", "accupdate - update the accounts of all active players");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "cmdlist", "dummyadd <amount> - add dummies to the game");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "cmdlist", "startevent <type> <duration (min)> - start an event for a duration");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "cmdlist", "(0 - Exp x2, 1 - Low gravity, 2 - Rapid fire)");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "cmdlist", "cmdlist - list all mod console commands");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "cmdlist", "-----------------------------------------------------------------------------------------------");
}

void CGameContext::ConTuneParam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pParamName = pResult->GetString(0);
	float NewValue = pResult->GetFloat(1);

	if(pSelf->Tuning()->Set(pParamName, NewValue))
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "%s changed to %.2f", pParamName, NewValue);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
		pSelf->SendTuningParams(-1);
	}
	else
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "No such tuning parameter");
}

void CGameContext::ConTuneReset(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CTuningParams TuningParams;
	*pSelf->Tuning() = TuningParams;
	pSelf->SendTuningParams(-1);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "Tuning reset");
}

void CGameContext::ConTuneDump(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	char aBuf[256];
	for(int i = 0; i < pSelf->Tuning()->Num(); i++)
	{
		float v;
		pSelf->Tuning()->Get(i, &v);
		str_format(aBuf, sizeof(aBuf), "%s %.2f", pSelf->Tuning()->m_apNames[i], v);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
	}
}

void CGameContext::ConPause(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(pResult->NumArguments())
		pSelf->m_pController->DoPause(clamp(pResult->GetInteger(0), -1, 1000));
	else
		pSelf->m_pController->DoPause(pSelf->m_pController->IsGamePaused() ? 0 : IGameController::TIMER_INFINITE);
}

void CGameContext::ConChangeMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_pController->ChangeMap(pResult->NumArguments() ? pResult->GetString(0) : "");
}

void CGameContext::ConRestart(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pResult->NumArguments())
		pSelf->m_pController->DoWarmup(clamp(pResult->GetInteger(0), -1, 1000));
	else
		pSelf->m_pController->DoWarmup(0);
}

void CGameContext::ConSay(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SendChat(-1, CHAT_ALL, -1, pResult->GetString(0));
}

void CGameContext::ConBroadcast(IConsole::IResult* pResult, void* pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SendBroadcast(pResult->GetString(0), -1);
}

void CGameContext::ConSetTeam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = clamp(pResult->GetInteger(0), 0, (int)MAX_CLIENTS-1);
	int Team = clamp(pResult->GetInteger(1), -1, 1);
	int Delay = pResult->NumArguments()>2 ? pResult->GetInteger(2) : 0;
	if(!pSelf->m_apPlayers[ClientID] || !pSelf->m_pController->CanJoinTeam(Team, ClientID))
		return;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "moved client %d to team %d", ClientID, Team);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	pSelf->m_apPlayers[ClientID]->m_TeamChangeTick = pSelf->Server()->Tick()+pSelf->Server()->TickSpeed()*Delay*60;
	pSelf->m_pController->DoTeamChange(pSelf->m_apPlayers[ClientID], Team);
}

void CGameContext::ConSetTeamAll(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Team = clamp(pResult->GetInteger(0), -1, 1);

	pSelf->SendGameMsg(GAMEMSG_TEAM_ALL, Team, -1);

	for(int i = 0; i < MAX_CLIENTS; ++i)
		if(pSelf->m_apPlayers[i] && pSelf->m_pController->CanJoinTeam(Team, i))
			pSelf->m_pController->DoTeamChange(pSelf->m_apPlayers[i], Team, false);
}

void CGameContext::ConSwapTeams(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SwapTeams();
}

void CGameContext::ConShuffleTeams(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!pSelf->m_pController->IsTeamplay())
		return;

	int rnd = 0;
	int PlayerTeam = 0;
	int aPlayer[MAX_CLIENTS];

	for(int i = 0; i < MAX_CLIENTS; i++)
		if(pSelf->m_apPlayers[i] && pSelf->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
			aPlayer[PlayerTeam++]=i;

	pSelf->SendGameMsg(GAMEMSG_TEAM_SHUFFLE, -1);

	//creating random permutation
	for(int i = PlayerTeam; i > 1; i--)
	{
		rnd = random_int() % i;
		int tmp = aPlayer[rnd];
		aPlayer[rnd] = aPlayer[i-1];
		aPlayer[i-1] = tmp;
	}
	//uneven Number of Players?
	rnd = PlayerTeam % 2 ? random_int() % 2 : 0;

	for(int i = 0; i < PlayerTeam; i++)
		pSelf->m_pController->DoTeamChange(pSelf->m_apPlayers[aPlayer[i]], i < (PlayerTeam+rnd)/2 ? TEAM_RED : TEAM_BLUE, false);
}

void CGameContext::ConLockTeams(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_LockTeams ^= 1;
	pSelf->SendSettings(-1);
}

void CGameContext::ConForceTeamBalance(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_pController->ForceTeamBalance();
}

void CGameContext::ConAddVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pDescription = pResult->GetString(0);
	const char *pCommand = pResult->GetString(1);

	if(pSelf->m_NumVoteOptions == MAX_VOTE_OPTIONS)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "maximum number of vote options reached");
		return;
	}

	// check for valid option
	if(!pSelf->Console()->LineIsValid(pCommand) || str_length(pCommand) >= VOTE_CMD_LENGTH)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid command '%s'", pCommand);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}
	while(*pDescription && *pDescription == ' ')
		pDescription++;
	if(str_length(pDescription) >= VOTE_DESC_LENGTH || *pDescription == 0)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid option '%s'", pDescription);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	// check for duplicate entry
	CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "option '%s' already exists", pDescription);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return;
		}
		pOption = pOption->m_pNext;
	}

	// add the option
	++pSelf->m_NumVoteOptions;
	int Len = str_length(pCommand);

	pOption = (CVoteOptionServer *)pSelf->m_pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len);
	pOption->m_pNext = 0;
	pOption->m_pPrev = pSelf->m_pVoteOptionLast;
	if(pOption->m_pPrev)
		pOption->m_pPrev->m_pNext = pOption;
	pSelf->m_pVoteOptionLast = pOption;
	if(!pSelf->m_pVoteOptionFirst)
		pSelf->m_pVoteOptionFirst = pOption;

	str_copy(pOption->m_aDescription, pDescription, sizeof(pOption->m_aDescription));
	mem_copy(pOption->m_aCommand, pCommand, Len+1);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "added option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	// inform clients about added option
	CNetMsg_Sv_VoteOptionAdd OptionMsg;
	OptionMsg.m_pDescription = pOption->m_aDescription;
	pSelf->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, -1);
}

void CGameContext::ConRemoveVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pDescription = pResult->GetString(0);

	// check for valid option
	CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
			break;
		pOption = pOption->m_pNext;
	}
	if(!pOption)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "option '%s' does not exist", pDescription);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	// inform clients about removed option
	CNetMsg_Sv_VoteOptionRemove OptionMsg;
	OptionMsg.m_pDescription = pOption->m_aDescription;
	pSelf->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, -1);

	// remove the option
	--pSelf->m_NumVoteOptions;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "removed option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	CHeap *pVoteOptionHeap = new CHeap();
	CVoteOptionServer *pVoteOptionFirst = 0;
	CVoteOptionServer *pVoteOptionLast = 0;
	int NumVoteOptions = pSelf->m_NumVoteOptions;
	for(CVoteOptionServer *pSrc = pSelf->m_pVoteOptionFirst; pSrc; pSrc = pSrc->m_pNext)
	{
		if(pSrc == pOption)
			continue;

		// copy option
		int Len = str_length(pSrc->m_aCommand);
		CVoteOptionServer *pDst = (CVoteOptionServer *)pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len);
		pDst->m_pNext = 0;
		pDst->m_pPrev = pVoteOptionLast;
		if(pDst->m_pPrev)
			pDst->m_pPrev->m_pNext = pDst;
		pVoteOptionLast = pDst;
		if(!pVoteOptionFirst)
			pVoteOptionFirst = pDst;

		str_copy(pDst->m_aDescription, pSrc->m_aDescription, sizeof(pDst->m_aDescription));
		mem_copy(pDst->m_aCommand, pSrc->m_aCommand, Len+1);
	}

	// clean up
	delete pSelf->m_pVoteOptionHeap;
	pSelf->m_pVoteOptionHeap = pVoteOptionHeap;
	pSelf->m_pVoteOptionFirst = pVoteOptionFirst;
	pSelf->m_pVoteOptionLast = pVoteOptionLast;
	pSelf->m_NumVoteOptions = NumVoteOptions;
}

void CGameContext::ConClearVotes(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "cleared votes");
	CNetMsg_Sv_VoteClearOptions VoteClearOptionsMsg;
	pSelf->Server()->SendPackMsg(&VoteClearOptionsMsg, MSGFLAG_VITAL, -1);
	pSelf->m_pVoteOptionHeap->Reset();
	pSelf->m_pVoteOptionFirst = 0;
	pSelf->m_pVoteOptionLast = 0;
	pSelf->m_NumVoteOptions = 0;
}

void CGameContext::ConVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	// check if there is a vote running
	if(!pSelf->m_VoteCloseTime)
		return;

	if(str_comp_nocase(pResult->GetString(0), "yes") == 0)
		pSelf->m_VoteEnforce = CGameContext::VOTE_ENFORCE_YES;
	else if(str_comp_nocase(pResult->GetString(0), "no") == 0)
		pSelf->m_VoteEnforce = CGameContext::VOTE_ENFORCE_NO;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "forcing vote %s", pResult->GetString(0));
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CGameContext::ConchainSpecialMotdupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CGameContext *pSelf = (CGameContext *)pUserData;
		pSelf->SendMotd(-1);
	}
}

void CGameContext::ConchainSettingUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CGameContext *pSelf = (CGameContext *)pUserData;
		if(pSelf->Server()->MaxClients() < g_Config.m_SvPlayerSlots)
			g_Config.m_SvPlayerSlots = pSelf->Server()->MaxClients();
		pSelf->SendSettings(-1);
	}
}

void CGameContext::ConchainGameinfoUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CGameContext *pSelf = (CGameContext *)pUserData;
		if(pSelf->m_pController)
			pSelf->m_pController->CheckGameInfo();
	}
}

void CGameContext::OnConsoleInit()
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();

	// mod commands
	Console()->Register("makemod", "?i?i", CFGFLAG_SERVER, ConMakeMod, this, "makemod <ID> <1 - demote / 10000 - admin> - make a player moderator, admin or demote him");
	Console()->Register("showstats", "?i", CFGFLAG_SERVER, ConShowStats, this, "showstats <ID> - list the stats of an other player");
	Console()->Register("givemoney", "?i?i", CFGFLAG_SERVER, ConGiveMoney, this, "givemoney <ID> <amount> - give a player money");
	Console()->Register("resetacc", "?i", CFGFLAG_SERVER, ConResetAcc, this, "resetacc <ID> - reset a player's account");
	Console()->Register("idlist", "", CFGFLAG_SERVER, ConIdList, this, "idlist - show the IDs with names and username of all active players");
	Console()->Register("accupdate", "", CFGFLAG_SERVER, ConAccUpdate, this, "accupdate - update the accounts of all active players");
	Console()->Register("dummyadd", "?i", CFGFLAG_SERVER, ConDummyAdd, this, "dummyadd <amount> - add dummies to the game");
	Console()->Register("startevent", "?i?i", CFGFLAG_SERVER, ConStartEvent, this, "startevent <type> <duration (min)> - start an event for a duration");
	Console()->Register("cmdlist", "", CFGFLAG_SERVER, ConCmdList, this, "cmdlist - list all mod console commands");

	Console()->Register("tune", "si", CFGFLAG_SERVER, ConTuneParam, this, "Tune variable to value");
	Console()->Register("tune_reset", "", CFGFLAG_SERVER, ConTuneReset, this, "Reset tuning");
	Console()->Register("tune_dump", "", CFGFLAG_SERVER, ConTuneDump, this, "Dump tuning");
	Console()->Register("pause", "?i", CFGFLAG_SERVER|CFGFLAG_STORE, ConPause, this, "Pause/unpause game");
	Console()->Register("change_map", "?r", CFGFLAG_SERVER|CFGFLAG_STORE, ConChangeMap, this, "Change map");
	Console()->Register("restart", "?i", CFGFLAG_SERVER|CFGFLAG_STORE, ConRestart, this, "Restart in x seconds (0 = abort)");
	Console()->Register("say", "r", CFGFLAG_SERVER, ConSay, this, "Say in chat");
	Console()->Register("broadcast", "r", CFGFLAG_SERVER, ConBroadcast, this, "Broadcast message");
	Console()->Register("set_team", "ii?i", CFGFLAG_SERVER, ConSetTeam, this, "Set team of player to team");
	Console()->Register("set_team_all", "i", CFGFLAG_SERVER, ConSetTeamAll, this, "Set team of all players to team");
	Console()->Register("swap_teams", "", CFGFLAG_SERVER, ConSwapTeams, this, "Swap the current teams");
	Console()->Register("shuffle_teams", "", CFGFLAG_SERVER, ConShuffleTeams, this, "Shuffle the current teams");
	Console()->Register("lock_teams", "", CFGFLAG_SERVER, ConLockTeams, this, "Lock/unlock teams");
	Console()->Register("force_teambalance", "", CFGFLAG_SERVER, ConForceTeamBalance, this, "Force team balance");

	Console()->Register("add_vote", "sr", CFGFLAG_SERVER, ConAddVote, this, "Add a voting option");
	Console()->Register("remove_vote", "s", CFGFLAG_SERVER, ConRemoveVote, this, "remove a voting option");
	Console()->Register("clear_votes", "", CFGFLAG_SERVER, ConClearVotes, this, "Clears the voting options");
	Console()->Register("vote", "r", CFGFLAG_SERVER, ConVote, this, "Force a vote to yes/no");
}

void CGameContext::OnInit()
{
	// create all required folders
	char folderpaths[9][256] =
	{
		FOLDERPATH_ACCOUNTS,
		FOLDERPATH_TOPTEN,
		FOLDERPATH_REDEEMCODES,
		FOLDERPATH_LOGS,
		FOLDERPATH_CHATLOG_1,
		FOLDERPATH_CHATLOG_2,
		FOLDERPATH_CHATLOG_3,
		FOLDERPATH_CHATLOG_4,
		FOLDERPATH_CHATLOG_5
	};

	// init everything
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_World.SetGameServer(this);
	m_Events.SetGameServer(this);

	for(int i = 0; i < NUM_NETOBJTYPES; i++)
		Server()->SnapSetStaticsize(i, m_NetObjHandler.GetObjSize(i));

	m_Layers.Init(Kernel());
	m_Collision.Init(&m_Layers);

	// select gametype
	if(str_comp_nocase(g_Config.m_SvGametype, "mod") == 0)
		m_pController = new CGameControllerMOD(this);
	else if(str_comp_nocase(g_Config.m_SvGametype, "ctf") == 0)
		m_pController = new CGameControllerCTF(this);
	else if(str_comp_nocase(g_Config.m_SvGametype, "lms") == 0)
		m_pController = new CGameControllerLMS(this);
	else if(str_comp_nocase(g_Config.m_SvGametype, "lts") == 0)
		m_pController = new CGameControllerLTS(this);
	else if(str_comp_nocase(g_Config.m_SvGametype, "tdm") == 0)
		m_pController = new CGameControllerTDM(this);
	else
		m_pController = new CGameControllerDM(this);

	// create all entities from the game layer
	CMapItemLayerTilemap *pTileMap = m_Layers.GameLayer();
	CTile *pTiles = (CTile *)Kernel()->RequestInterface<IMap>()->GetData(pTileMap->m_Data);
	for(int y = 0; y < pTileMap->m_Height; y++)
	{
		for(int x = 0; x < pTileMap->m_Width; x++)
		{
			int Index = pTiles[y*pTileMap->m_Width+x].m_Index;

			if(Index >= ENTITY_OFFSET)
			{
				vec2 Pos(x*32.0f+16.0f, y*32.0f+16.0f);
				m_pController->OnEntity(Index-ENTITY_OFFSET, Pos);
			}
		}
	}

	Console()->Chain("sv_motd", ConchainSpecialMotdupdate, this);

	Console()->Chain("sv_vote_kick", ConchainSettingUpdate, this);
	Console()->Chain("sv_vote_kick_min", ConchainSettingUpdate, this);
	Console()->Chain("sv_vote_spectate", ConchainSettingUpdate, this);
	Console()->Chain("sv_teambalance_time", ConchainSettingUpdate, this);
	Console()->Chain("sv_player_slots", ConchainSettingUpdate, this);

	Console()->Chain("sv_scorelimit", ConchainGameinfoUpdate, this);
	Console()->Chain("sv_timelimit", ConchainGameinfoUpdate, this);
	Console()->Chain("sv_matches_per_map", ConchainGameinfoUpdate, this);

	// clamp sv_player_slots to 0..MaxClients
	if(Server()->MaxClients() < g_Config.m_SvPlayerSlots)
		g_Config.m_SvPlayerSlots = Server()->MaxClients();
	
	// create all required folders
	for (int i = 0; i < sizeof(folderpaths) / sizeof(folderpaths[0]); ++i)
	{
		if (!GetDirExists(folderpaths[i]))
		{
			if (!CreateDirectoryA(folderpaths[i], NULL))
			{
				printf("Error creating folder: %s\n", folderpaths[i]);
				return;
			}
		}
	}

	// private version of the server gets treated differently
	if (IS_PRIVATE_VERSION)
	{
		if (g_Config.m_SvRegister != 0)
		{
			system("CLS");
			printf("************************************************************************************\n");
			printf("* This is a private version of the LUM|lvl mod, as such it cannot be hosted online\n");
			printf("* or registered on the master servers.\n");
			printf("* The server can be hosted in your Local Area Network (LAN) without restrictions.\n");
			printf("* in order to do so, please make sure that \"sv_register\" is set to \"0\" in your\n");
			printf("* \"autoexec.cfg\" file.\n");
			printf("************************************************************************************\n");
			printf("* Dies ist eine private Version des LUM|lvl mods, als solche kann dieser nicht\n");
			printf("* online gehostet / bei den Masterservern registriert werden.\n");
			printf("* Der Server kann ohne Beschraenkung in ihrem Local Area Network (LAN) gehostet werden.\n");
			printf("* Um dies zu erreichen, stellen sie sicher dass in ihrer \"autoexec.cfg\" Datei\n");
			printf("* \"sv_register\" auf \"0\" gesetzt ist.\n");
			printf("************************************************************************************\n");
			system("pause");
			exit(0);
		}
	}

#ifdef CONF_DEBUG
	if(g_Config.m_DbgDummies)
	{
		for(int i = 0; i < g_Config.m_DbgDummies ; i++)
			OnClientConnected(Server()->MaxClients() -i-1, true);
	}
#endif
}

void CGameContext::OnShutdown()
{
	delete m_pController;
	m_pController = 0;
	Clear();
}

void CGameContext::OnSnap(int ClientID)
{
	// add tuning to demo
	CTuningParams StandardTuning;
	if(ClientID == -1 && Server()->DemoRecorder_IsRecording() && mem_comp(&StandardTuning, &m_Tuning, sizeof(CTuningParams)) != 0)
	{
		CNetObj_De_TuneParams *pTuneParams = static_cast<CNetObj_De_TuneParams *>(Server()->SnapNewItem(NETOBJTYPE_DE_TUNEPARAMS, 0, sizeof(CNetObj_De_TuneParams)));
		if(!pTuneParams)
			return;

		mem_copy(pTuneParams->m_aTuneParams, &m_Tuning, sizeof(pTuneParams->m_aTuneParams));
	}

	m_World.Snap(ClientID);
	m_pController->Snap(ClientID);
	m_Events.Snap(ClientID);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
			m_apPlayers[i]->Snap(ClientID);
	}
}
void CGameContext::OnPreSnap() {}
void CGameContext::OnPostSnap()
{
	m_World.PostSnap();
	m_Events.Clear();
}

bool CGameContext::IsClientReady(int ClientID) const
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_IsReadyToEnter;
}

bool CGameContext::IsClientPlayer(int ClientID) const
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS ? false : true;
}

const char *CGameContext::GameType() const { return m_pController && m_pController->GetGameType() ? m_pController->GetGameType() : ""; }
const char *CGameContext::Version() const { return GAME_VERSION; }
const char *CGameContext::NetVersion() const { return GAME_NETVERSION; }

IGameServer *CreateGameServer() { return new CGameContext; }
