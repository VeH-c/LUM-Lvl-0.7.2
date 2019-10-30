/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_PLAYER_H
#define GAME_SERVER_PLAYER_H

#include "alloc.h"


enum
{
	WEAPON_GAME = -3, // team switching etc
	WEAPON_SELF = -2, // console kill command
	WEAPON_WORLD = -1, // death tiles etc
};

// player object
class CPlayer
{
	MACRO_ALLOC_POOL_ID()

public:
	CPlayer(CGameContext *pGameServer, int ClientID, bool Dummy);
	~CPlayer();

	void Init(int CID);

	void TryRespawn();
	void Respawn();
	void SetTeam(int Team, bool DoChatMsg=true);
	int GetTeam() const { return m_Team; };
	int GetCID() const { return m_ClientID; };
	bool IsDummy() const { return m_Dummy; }

	void Tick();
	void PostTick();
	void Snap(int SnappingClient);

	void OnDirectInput(CNetObj_PlayerInput *NewInput);
	void OnPredictedInput(CNetObj_PlayerInput *NewInput);
	void OnDisconnect();

	void KillCharacter(int Weapon = WEAPON_GAME);
	CCharacter *GetCharacter();

	//---------------------------------------------------------
	// this is used for snapping so we know how we can clip the view for the player
	vec2 m_ViewPos;

	// states if the client is chatting, accessing a menu etc.
	int m_PlayerFlags;

	// used for snapping to just update latency if the scoreboard is active
	int m_aActLatency[MAX_CLIENTS];

	// used for spectator mode
	int GetSpectatorID() const { return m_SpectatorID; }
	bool SetSpectatorID(int SpecMode, int SpectatorID);
	bool m_DeadSpecMode;
	bool DeadCanFollow(CPlayer *pPlayer) const;
	void UpdateDeadSpecMode();

	bool m_IsReadyToEnter;
	bool m_IsReadyToPlay;

	bool m_RespawnDisabled;

	//
	int m_Vote;
	int m_VotePos;
	//
	int m_LastVoteCall;
	int m_LastVoteTry;
	int m_LastChat;
	int m_LastSetTeam;
	int m_LastSetSpectatorMode;
	int m_LastChangeInfo;
	int m_LastEmote;
	int m_LastKill;
	int m_LastReadyChange;

	// TODO: clean this up
	struct
	{
		char m_aaSkinPartNames[6][24];
		int m_aUseCustomColors[6];
		int m_aSkinPartColors[6];
	} m_TeeInfos;

	int m_RespawnTick;
	int m_DieTick;
	int m_Score;
	int m_ScoreStartTick;
	int m_LastActionTick;
	int m_TeamChangeTick;

	int m_InactivityTickCounter;

	struct
	{
		int m_TargetX;
		int m_TargetY;
	} m_LatestActivity;

	// network latency calculations
	struct
	{
		int m_Accum;
		int m_AccumMin;
		int m_AccumMax;
		int m_Avg;
		int m_Min;
		int m_Max;
	} m_Latency;

	// mod variables
	bool m_Player_logged;// if logged in
	int m_aPlayer_option[10];// additional options (0 - switchmode)
	char m_Player_nameraw[256];// raw name of player
	int m_Player_lastweapon = 1;// last weapon
	int m_Player_killcount;// counter for killstreaks

	char m_Player_username[MAX_LEN_REGSTR + 1] = { 0 };//username for rewriting the file
	char m_Player_password[MAX_LEN_REGSTR + 1];//password for rewriting the file
	int m_Player_status;//freezing cheaters
	int m_aPlayer_util[5];//reserved account settings (0 - undercover, 1 - showexp, 2 - emotes)
	int m_Player_level;//level of player
	int m_Player_experience;//experience of player
	int m_Player_money;//money of player
	int m_aPlayer_stat[7];//stats (hammer, gun, shotgun, grenade, laser, life, handle)
	char m_Player_charname[256] = { 0 };// name of player for account

	// reach maximum level of server logout delay crash safety
	int m_LogOutDelay = 0;

	// spawn protection
	int m_SpawnProtectionMs = 0;

	// *dummy variables
	int m_dum_wprimpref = floor(min((int)WEAPON_LASER, (int)floor(frandom() * 5)));
	int m_dum_wsecpref = frandom() > 0.5 ? WEAPON_GUN : WEAPON_HAMMER;

	char m_dum_name[MAX_NAME_LENGTH];
	char m_dum_clan[MAX_CLAN_LENGTH];
	int m_dum_country;

	int m_dum_chosenskin = 0;

private:
	CCharacter *m_pCharacter;
	CGameContext *m_pGameServer;

	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const;

	//
	bool m_Spawning;
	int m_ClientID;
	int m_Team;
	bool m_Dummy;

	// used for spectator mode
	int m_SpecMode;
	int m_SpectatorID;
	class CFlag *m_pSpecFlag;
	bool m_ActiveSpecSwitch;
};

#endif
