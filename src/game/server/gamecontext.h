/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMECONTEXT_H
#define GAME_SERVER_GAMECONTEXT_H

#define FOLDERPATH_ACCOUNTS "../../accounts"
#define FOLDERPATH_TOPTEN "../../topten"
#define FOLDERPATH_REDEEMCODES "../../redeemcodes"
#define FOLDERPATH_LOGS "../../logs"
#define FOLDERPATH_CHATLOG_1 FOLDERPATH_LOGS "/0 - 30"
#define FOLDERPATH_CHATLOG_2 FOLDERPATH_LOGS "/30 - 75"
#define FOLDERPATH_CHATLOG_3 FOLDERPATH_LOGS "/75 - 120"
#define FOLDERPATH_CHATLOG_4 FOLDERPATH_LOGS "/120 - 300"
#define FOLDERPATH_CHATLOG_5 FOLDERPATH_LOGS "/public"
#define FILEPATH_MODLOG FOLDERPATH_LOGS "/modlog.txt"
#define FILEPATH_TICKET FOLDERPATH_LOGS "/modtickets.txt"

#define FILEPATH_MODSETTINGS "modsettings.cfg"
#define FILEPATH_CHATLOG "chatlog.txt"
#define FILEPATH_EVENTTIMES "eventtimes.ini"

#define MAX_LINES_MODSETTINGS 50
#define MAX_LEN_REGSTR 24

#define IS_PRIVATE_VERSION 1

#include <engine/console.h>
#include <engine/server.h>

#include <game/layers.h>
#include <game/voting.h>

#include "eventhandler.h"
#include "gameworld.h"

/*
	Tick
		Game Context (CGameContext::tick)
			Game World (GAMEWORLD::tick)
				Reset world if requested (GAMEWORLD::reset)
				All entities in the world (ENTITY::tick)
				All entities in the world (ENTITY::tick_defered)
				Remove entities marked for deletion (GAMEWORLD::remove_entities)
			Game Controller (GAMECONTROLLER::tick)
			All players (CPlayer::tick)


	Snap
		Game Context (CGameContext::snap)
			Game World (GAMEWORLD::snap)
				All entities in the world (ENTITY::snap)
			Game Controller (GAMECONTROLLER::snap)
			Events handler (EVENT_HANDLER::snap)
			All players (CPlayer::snap)

*/
class CGameContext : public IGameServer
{
	IServer *m_pServer;
	class IConsole *m_pConsole;
	CLayers m_Layers;
	CCollision m_Collision;
	CNetObjHandler m_NetObjHandler;
	CTuningParams m_Tuning;

	static void ConTuneParam(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneReset(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneDump(IConsole::IResult *pResult, void *pUserData);
	static void ConPause(IConsole::IResult *pResult, void *pUserData);
	static void ConChangeMap(IConsole::IResult *pResult, void *pUserData);
	static void ConRestart(IConsole::IResult *pResult, void *pUserData);
	static void ConSay(IConsole::IResult *pResult, void *pUserData);
	static void ConBroadcast(IConsole::IResult *pResult, void *pUserData);
	static void ConSetTeam(IConsole::IResult *pResult, void *pUserData);
	static void ConSetTeamAll(IConsole::IResult *pResult, void *pUserData);
	static void ConSwapTeams(IConsole::IResult *pResult, void *pUserData);
	static void ConShuffleTeams(IConsole::IResult *pResult, void *pUserData);
	static void ConLockTeams(IConsole::IResult *pResult, void *pUserData);
	static void ConForceTeamBalance(IConsole::IResult *pResult, void *pUserData);
	static void ConAddVote(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveVote(IConsole::IResult *pResult, void *pUserData);
	static void ConClearVotes(IConsole::IResult *pResult, void *pUserData);
	static void ConVote(IConsole::IResult *pResult, void *pUserData);
	static void ConchainSpecialMotdupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainSettingUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainGameinfoUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	CGameContext(int Resetting);
	void Construct(int Resetting);

	bool m_Resetting;
public:
	IServer *Server() const { return m_pServer; }
	class IConsole *Console() { return m_pConsole; }
	CCollision *Collision() { return &m_Collision; }
	CTuningParams *Tuning() { return &m_Tuning; }

	CGameContext();
	~CGameContext();

	void Clear();

	CEventHandler m_Events;
	class CPlayer *m_apPlayers[MAX_CLIENTS];

	class IGameController *m_pController;
	CGameWorld m_World;

	// helper functions
	class CCharacter *GetPlayerChar(int ClientID);

	int m_LockTeams;

	// voting
	void StartVote(const char *pDesc, const char *pCommand, const char *pReason);
	void EndVote(int Type, bool Force);
	void ForceVote(int Type, const char *pDescription, const char *pReason);
	void SendVoteSet(int Type, int ToClientID);
	void SendVoteStatus(int ClientID, int Total, int Yes, int No);
	void AbortVoteOnDisconnect(int ClientID);
	void AbortVoteOnTeamChange(int ClientID);

	int m_VoteCreator;
	int m_VoteType;
	int64 m_VoteCloseTime;
	int64 m_VoteCancelTime;
	bool m_VoteUpdate;
	int m_VotePos;
	char m_aVoteDescription[VOTE_DESC_LENGTH];
	char m_aVoteCommand[VOTE_CMD_LENGTH];
	char m_aVoteReason[VOTE_REASON_LENGTH];
	int m_VoteClientID;
	int m_NumVoteOptions;
	int m_VoteEnforce;
	enum
	{
		VOTE_ENFORCE_UNKNOWN = 0,
		VOTE_ENFORCE_NO,
		VOTE_ENFORCE_YES,

		VOTE_TIME = 25,
		VOTE_CANCEL_TIME = 10,
	};
	class CHeap *m_pVoteOptionHeap;
	CVoteOptionServer *m_pVoteOptionFirst;
	CVoteOptionServer *m_pVoteOptionLast;

	// helper functions
	void CreateDamage(vec2 Pos, int Id, vec2 Source, int HealthAmount, int ArmorAmount, bool Self);
	//	void CreateExplosion(vec2 Pos, int Owner, int Weapon, int MaxDamage);
	void CreateHammerHit(vec2 Pos);
	void CreatePlayerSpawn(vec2 Pos);
	void CreateDeath(vec2 Pos, int Who);
	void CreateSound(vec2 Pos, int Sound, int64 Mask = -1);

	// network
	void SendChat(int ChatterClientID, int Mode, int To, const char *pText);
	void SendBroadcast(const char *pText, int ClientID);
	void SendEmoticon(int ClientID, int Emoticon);
	void SendWeaponPickup(int ClientID, int Weapon);
	void SendMotd(int ClientID);
	void SendSettings(int ClientID);

	void SendGameMsg(int GameMsgID, int ClientID);
	void SendGameMsg(int GameMsgID, int ParaI1, int ClientID);
	void SendGameMsg(int GameMsgID, int ParaI1, int ParaI2, int ParaI3, int ClientID);

	//
	void CheckPureTuning();
	void SendTuningParams(int ClientID);

	//
	void SwapTeams();

	// engine events
	virtual void OnInit();
	virtual void OnConsoleInit();
	virtual void OnShutdown();

	virtual void OnTick();
	virtual void OnPreSnap();
	virtual void OnSnap(int ClientID);
	virtual void OnPostSnap();

	virtual void OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID);

	virtual void OnClientConnected(int ClientID) { OnClientConnected(ClientID, false); }
	void OnClientConnected(int ClientID, bool Dummy);
	void OnClientTeamChange(int ClientID);
	virtual void OnClientEnter(int ClientID);
	virtual void OnClientDrop(int ClientID, const char *pReason);
	virtual void OnClientDirectInput(int ClientID, void *pInput);
	virtual void OnClientPredictedInput(int ClientID, void *pInput);

	virtual bool IsClientReady(int ClientID) const;
	virtual bool IsClientPlayer(int ClientID) const;

	virtual const char *GameType() const;
	virtual const char *Version() const;
	virtual const char *NetVersion() const;

	// general control
	int m_aTellDelay[MAX_CLIENTS] = { 0 };
	int m_aTicketDelay[MAX_CLIENTS] = { 0 };
	int m_aRedeemDelay[MAX_CLIENTS] = { 0 };
	int m_HelpBroadcastDelay = 0;
	int m_HelpBroadcastDelayDefault = 5;// default help broadcast time in seconds
	int m_TellDelayDefault = 10;// default tell waittime in seconds
	int m_TicketDelayDefault = 30;// default ticket waittime in seconds
	int m_RedeemDelayDefault = 10;// default redeem code waittime in seconds
	int m_EncryptLetterShift = 13;// letter encryption shift amount >>

	// format
	const char m_aSepLine[256] = { "--------------------------------------------------------------------" };//separator line

	// weapon requirement levels
	int m_Req_hammer_auto = 10;
	int m_Req_hammer_fly = 25;

	int m_Req_gun_auto = 10;
	int m_Req_gun_spread = 25;

	int m_Req_grenade_bounce = 40;
	int m_Req_grenade_bounce2 = 75;

	int m_Req_rifle_dual = 25;
	int m_Req_rifle_range = 40;
	int m_Req_rifle_triple = 55;

	// level variables
	float m_Per_health_max = 6;
	float m_Per_armor_max = 6;

	// general variables
	int m_MinAllowedLevel = -1;// minimum allowed level on server (-1 - no limits)
	int m_MaxAllowedLevel = -1;// maximum allowed level on server (-1 - no limits)

	int m_AmmoReward = 3;// ammo reward for killing player
	int m_StepKillstreak = 5;// how many kills are one killstreak step
	int m_BonusPerStreak = 3;// bonus for killing a player on streak
	int m_BonusPer100 = 2;// (unused) each 100% level difference from lvl 10 upwards gain this bonus
	int m_BonusPerDiff50 = 2;// each 50 level difference gain this bonus
	int m_ExpRatio = 1;// exp gain ratio 1x normal 2x event...

	int m_AccUpdateDelay = 50 * 1;// account update speed
	int m_AccUpdateTime;// account update time

	int m_ClUpdateTimeStart = 50 * 1;// client start update delay
	int m_ClUpdateSet = 50 * 60;// client waittime after update
	int m_ClUpdateTime[MAX_CLIENTS];// account update timer for each player (crash safety)

	// event variables (note: event time is added to current event time if an event is started)
	char m_aEventName[10][128] = { "Experience x2", "Low Gravity", "Rapid Fire" };

	int m_EvtTriggerTime = -1;// random event trigger timer
	int m_EvtTime[10] = { 0 };// event timers (0 - double exp)
	int m_EvtBroadcastTime = 0;// event broadcast refresh timer
	int m_EvtDurationBase = 60 * 15;// event duration in seconds
	int m_EvtFileUpdateTime = 0;// time until event times are saved in file
	int m_EvtFileUpdateTimeBase = 10;// default time in seconds until event times are saved
	// event depending
	int m_EvtBonusAmountBots = 0;// event bonus amount bots (deactivated)
	float m_EvtGravityScale = 0.5;// event low gravity scale
	int m_EvtBonusHandle = 0;// event rapidfire bonus handle
	int m_EvtNoclip = false;// event rapidfire noclip

	// hammer mines
	int m_MineLifeTime = 5;// mine lifetime in seconds
	int m_MineRadius = 15;// mine hitbox radius

	// drop life
	int m_DropLifeRewardRatio = 40;// % of max life dropped when dead
	int m_DropLifeLifeTime = 15;// lifetime of drop life in seconds
	float m_DropLifeGravity = 0.35;// gravity of drop life
	int m_DropLifeBounceForce = 30;// % bounce force

	// base damages
	int m_aBaseDmg[5];
	int m_DamageScaling = 25;// % damage gain per level

	// weapon specific level properties
	// hammer
	float swep_hammer_dmgscale = 3;// damage scaling for hammer in fly mode
	// gun
	float swep_gun_dmgscale = 1.65;
	// shotgun
	float swep_shotgun_dmgscale = 3;
	// grenade
	float swep_grenade_dmgscale = 1;
	// rifle
	float swep_rifle_dmgscale = 1.5;
	int swep_rifle_rangegain = 200;// range increase of rifle
	// katana
	int swep_katana_bonus_pickup = 8;// exp flat bonus for katana pickup
	float swep_katana_bonus_percLevel = 0.25;// exp bonus in % of own level for katana pickup

	// weapon specific tuning variables (modifiable in modsettings)
	// shotgun
	int twep_shotgun_bulletcap = 30;// bullet cap of shotgun
	int twep_shotgun_spreadcap = 180;// spread cap of shotgun
	float twep_shotgun_spreadbase = (float)twep_shotgun_spreadcap / ((float)twep_shotgun_bulletcap - 1);// ° spread base
	int twep_shotgun_speeddiff = 20;// % max speed diff on outer edge
	float twep_shotgun_rangegain = 0.5;// % level as range

	// character specific tune variables
	float m_SpawnProtectionBase = 1;// seconds spawn protection base duration

	// voting variables
	int m_AmountBotsDefault = 8;
	int m_Vote_AmountBots = m_AmountBotsDefault;

	// custom tuning variables
	// world
	float m_GravityDefault = 0.50;
	float m_AirControlSpeedDefault = 5.00;
	float m_AirControlAccelDefault = 1.50;
	float m_AirFrictionDefault = 0.95;
	// hook
	float m_HookDragSpeedDefault = 15.00;
	float m_HookLengthDefault = 380.00;
	// weapons
	float m_GunCurvatureDefault = 1.50;
	float m_ShotgunCurvatureDefault = 1.25;
	float m_GrenadeCurvatureDefault = 7.00;

	float m_GunSpeedDefault = 1700.00;
	float m_ShotgunSpeedDefault = 2750.00;
	float m_GrenadeSpeedDefault = 1000.00;

	float m_GunLifetimeDefault = 2.00;
	float m_ShotgunLifetimeDefault = 0.20;
	float m_GrenadeLifetimeDefault = 2.00;

	// mod functions
	int TuneModSettings(char *Filepath);// apply modsettings.cfg

	void UpgradeStats(int ClientID, char *pStat, char *pAmount);
	void ResetAccount(int ClientID);
	void ShowDefaultHelp(int ClientID);
	void ShowServerHelp(int ClientID);
	void ShowGameHelp(int ClientID);
	void ShowAccountHelp(int ClientID);
	void ShowEmoteHelp(int ClientID);
	void ShowModeratorHelp(int ClientID);
	void ShowStats(int ClientID, char* pParam);
	void ShowTopTen(int ClientID);
	void ToggleShowExp(int ClientID);
	void ToggleSwitchMode(int ClientID);

	void AccountRegister(int ClientID, char *Username, char *Password);
	void AccountLogIn(int ClientID, char *Username, char *Password);
	void AccountLogOut(int ClientID);
	void AccountChangePassword(int ClientID, char *Password, char *Newpassword, char *Newpasswordconfirm);
	void AccountUpdate(int ClientID);
	void SubmitTicket(int ClientID, char *Message);
	void RedeemCode(int ClientID, char *Code);

	bool CheckRegisterFormat(char *Text, int Length, int ClientID);
	bool CheckRedeemFormat(char *Text, int Length, int ClientID);

	void HandleKill(int ClientID, int Victim, int Weapon);
	void HandleDeath(int ClientID, int Killer, int Weapon);

	bool GetDirExists(const char* dirName);
	bool GetFileExists(char *Filepath);
	bool GetFromFile(int Position, char *Buffer, unsigned int Size, char *Filepath);

	void CreateExplosionExt(vec2 Pos, int Owner, int Weapon, float Damage, int KnockLvl, int DamageLvl, bool Sound);

	//	void GetTeeName(int ClientID, char* Buffer);
	void SetTeeEmote(int ClientID, int Emote);
	void SetTeeScore(int ClientID);

	// custom voting
	void HandleCustomVote(char *pVoteCommand);

	// event functions
	void HandleEventSystem();

	// admin commands
	static void ConMakeMod(IConsole::IResult *pResult, void *pUserData);
	static void ConShowStats(IConsole::IResult *pResult, void *pUserData);
	static void ConGiveMoney(IConsole::IResult *pResult, void *pUserData);
	static void ConResetAcc(IConsole::IResult *pResult, void *pUserData);
	static void ConIdList(IConsole::IResult *pResult, void *pUserData);
	static void ConCmdList(IConsole::IResult *pResult, void *pUserData);
	static void ConAccUpdate(IConsole::IResult *pResult, void *pUserData);
	static void ConDummyAdd(IConsole::IResult *pResult, void *pUserData);
	static void ConStartEvent(IConsole::IResult *pResult, void *pUserData);

	// other
	void WriteModLog(char *format, ...);
	void WriteChatLog(char *format, ...);
	void ServerMessage(int ClientID, char *format, ...);
	void PickUpKatana(int ClientID);

	// moderator commands
	bool GetIdStatus(int ID, int ClientID);
	void TreatPlayer(char *ID, int ClientID, int Effect, int Duration);
	void FreezePlayer(char *ID, int ClientID, int Reverse);
	void ShowIdList(int ClientID);
	void ShowPlayerStats(int ID, int ClientID);
	void ToggleUndercover(int ClientID);

	// *dummy functions
	bool m_has_human_players;
	bool m_has_human_active_players;
	int m_botchat_index = 0;
	int m_botchat_delay = 0;
	int m_dummy_wepswapdur;
	void HandleDummySystem();
	void DummyAdd(int Amount);
	void DummyRemove(int Amount);
	void DummyInit(int ClientID);
	void DummyDelete(int ClientID);
	bool DummyCharTaken(int ClientID, int CharID);
	void DummyAdaptLevel(int ClientID);
	int AmtHumanPlayers(void);
	int AmtDummies(void);

	char m_aBotChatLine[100][128] =
	{ "Hello I am a helper bot. Write '/help account' in the chat to join us",
		"Write: '/register Bob Bob' , and '/login Bob Bob', but replace Bob with an other name, don't forget the '/' before the message",
		"In this gamemode you can upgrade your weapon and character stats. Such as life and handle (fire rate)",
		"To upgrade something, use '/upgr <stat>', you can also write '/upgr <stat> <amount>'. View details with '/help game'",
		"It is a good advice to only level one weapon per account for the weapon to reach it's maximum potential",
		"Weapons become stronger each time you upgrade them, they also unlock special abilities at certain levels",
		"Each weapon has two firing modes, use '/switchmode' to switch between them",
		"Oh and there is also a top ten list, type '/topten' to see for yourself who wears the crown",
		"The game mode is split into 5 servers, every server covers a certain range of player levels",
		"You can see the range in the name of each server ([0 - 30], [30 - 75], [75 - 120], [120 - 300] and [public])",
		"If you cannot log in, this is probably not the [0 - 30] or [public] server. If so, you can find them in the browser :)",
		"That said, type '/help' to view all commands or '/help account' to see how to log in, have fun ^^"
	};

	// *dummy all skin settings
	const char m_dum_skinpartnames[16][6][24] =
	{ "kitty", "whisker", "", "standard", "standard", "standard",
		"standard","stripes","","standard","standard","standard",
		"bear","bear","hair","standard","standard","standard",
		"standard","cammo2","","standard","standard","standard",
		"standard","cammostripes","","standard","standard","standard",
		"standard","","","standard","standard","standard",
		"bear","bear","hair","standard","standard","standard",
		"kitty","whisker","","standard","standard","standard",
		"standard","whisker","","standard","standard","standard",
		"standard","donny","unibop","standard","standard","standard",
		"standard","stripe","","standard","standard","standard",
		"standard","saddo","","standard","standard","standard",
		"standard","toptri","","standard","standard","standard",
		"standard","duodonny","twinbopp","standard","standard","standard",
		"standard","twintri","","standard","standard","standard",
		"standard","warpaint","","standard","standard","standard"
	};
	int m_dum_skinpartcolors[16][6] =
	{ 8681144, -8229413, 65408, 7885547, 7885547, 65408,
		10187898, -16711808, 65408, 750848, 1944919, 65408,
		1082745, -15634776, 65408, 1082745, 1147174, 65408,
		5334342, -11771603, 65408, 750848, 1944919, 65408,
		5334342, -14840320, 65408, 750848, 1944919, 65408,
		1798004, -16711808, 65408, 1799582, 1869630, 65408,
		184, -15397662, 65408, 184, 9765959, 65408,
		4612803, -12229920, 65408, 3827951, 3827951, 65408,
		15911355, -801066, 65408, 15043034, 15043034, 65408,
		16177260, -16590390, 16177260, 16177260, 7624169, 65408,
		16307835, -16711808, 65408, 184, 9765959, 65408,
		7171455, -9685436, 65408, 3640746, 5792119, 65408,
		6119331, -16711808, 65408, 3640746, 5792119, 65408,
		15310519, -1600806, 15310519, 15310519, 37600, 65408,
		3447932, -14098717, 65408, 185, 9634888, 65408,
		1944919, -16711808, 65408, 750337, 1944919, 65408
	};
	int m_dum_usecustomcolors[16][6] =
	{ 1, 1, 0, 1, 1, 0,
		1, 0, 0, 1, 1, 0,
		1, 1, 0, 1, 1, 0,
		1, 1, 0, 1, 1, 0,
		1, 1, 0, 1, 1, 0,
		1, 0, 0, 1, 1, 0,
		1, 1, 0, 1, 1, 0,
		1, 1, 0, 1, 1, 0,
		1, 1, 0, 1, 1, 0,
		1, 1, 1, 1, 1, 0,
		1, 0, 0, 1, 1, 0,
		1, 1, 0, 1, 1, 0,
		1, 0, 0, 1, 1, 0,
		1, 1, 1, 1, 1, 0,
		1, 1, 0, 1, 1, 0,
		1, 0, 0, 1, 1, 0
	};
};

inline int64 CmaskAll() { return -1; }
inline int64 CmaskOne(int ClientID) { return 1<<ClientID; }
inline int64 CmaskAllExceptOne(int ClientID) { return CmaskAll()^CmaskOne(ClientID); }
inline bool CmaskIsSet(int64 Mask, int ClientID) { return (Mask&CmaskOne(ClientID)) != 0; }
#endif