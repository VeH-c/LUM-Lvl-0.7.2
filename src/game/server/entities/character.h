/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_CHARACTER_H
#define GAME_SERVER_ENTITIES_CHARACTER_H

#include <generated/protocol.h>

#include <game/gamecore.h>
#include <game/server/entity.h>


class CCharacter : public CEntity
{
	MACRO_ALLOC_POOL_ID()

public:
	//character's size
	static const int ms_PhysSize = 28;

	CCharacter(CGameWorld *pWorld);

	virtual void Reset();
	virtual void Destroy();
	virtual void Tick();
	virtual void TickDefered();
	virtual void TickPaused();
	virtual void Snap(int SnappingClient);
	virtual void PostSnap();

	bool IsGrounded();

	void SetWeapon(int W);
	void HandleWeaponSwitch();
	void DoWeaponSwitch();

	void HandleWeapons();
	void HandleNinja();

	void OnPredictedInput(CNetObj_PlayerInput *pNewInput);
	void OnDirectInput(CNetObj_PlayerInput *pNewInput);
	void ResetInput();
	void FireWeapon();

	void Die(int Killer, int Weapon);
	bool TakeDamage(float Dmg, int From, int Weapon);

	bool Spawn(class CPlayer *pPlayer, vec2 Pos);
	bool Remove();

	bool IncreaseHealth(float Amount);
	bool IncreaseArmor(float Amount);

	bool GiveWeapon(int Weapon, int Ammo);
	void GiveNinja();

	void SetEmote(int Emote, int Tick);

	vec2 GetPos();

	bool IsAlive() const { return m_Alive; }
	class CPlayer *GetPlayer() { return m_pPlayer; }

	// mod methods
	bool ImpulseAdd(vec2 Force);
	void GainAmmoBack(int Amount);
	void FreezeSelf();

	float GetFireRate();
	float GetDamage(int Weapon);

	int m_ActiveWeapon;

	// *dummy variables
	void HandleDummyTick();

	vec2 m_dum_direction;
	CEntity *lastent;
	int m_dum_range_vision;
	int m_dum_range_triggershooting;
	int m_dum_range_hook = 400;
	bool m_dum_has_vision;
	bool m_dum_shoot;
	int m_dum_shootreaction;
	int m_dum_gundelay;
	int m_dum_hammerdelay;
	bool m_dum_hook;
	int m_dum_hooktimer;
	int m_dum_walkdir;
	int m_dum_walktimer;
	int m_dum_jump;
	bool m_dum_hasjumped;
	int m_dum_jump_delay;
	int m_dum_scan_delay;
	vec2 m_dum_scanpos;
	int m_dum_aoffdelay;
	vec2 aoff;
	int m_dum_hookmood;
	int m_dum_hookmoodtimer;
	float yoff_bdrop;
	float wep_dropoff[5] = { 0.f, 0.0016f, 0.f, 0.00627f, 0.f };

	void DumChooseNewStroll();

	// moderator / admin maker effect
	float m_mark_angle = 0;

	vec2 GetMarkVec(float Offset, int Length);

private:
	// player controlling this character
	class CPlayer *m_pPlayer;
	class CEff *pEff[3];
	class CSeff *pSeff;

	bool m_Alive;

	// weapon info
	CEntity *m_apHitObjects[10];
	int m_NumObjectsHit;

	struct WeaponStat
	{
		int m_AmmoRegenStart;
		int m_Ammo;
		bool m_Got;

	} m_aWeapons[NUM_WEAPONS];

	int m_LastWeapon;
	int m_QueuedWeapon;

	float m_ReloadTimer;
	int m_AttackTick;

	int m_EmoteType;
	signed long m_EmoteStop;

	// last tick that the player took any action ie some input
	int m_LastAction;
	int m_LastNoAmmoSound;

	// these are non-heldback inputs
	CNetObj_PlayerInput m_LatestPrevInput;
	CNetObj_PlayerInput m_LatestInput;

	// input
	CNetObj_PlayerInput m_Input;
	int m_NumInputs;
	int m_Jumped;

	float m_Health;
	float m_Armor;
	float m_VHealth;
	float m_VArmor;

	void HandleVirtualHealth();

	// movement variables
	vec2 m_PrevPos;
	vec2 m_MoveDir;
	float m_MoveDist;

	int m_TriggeredEvents;

	// ninja
	struct
	{
		vec2 m_ActivationDir;
		int m_ActivationTick;
		int m_CurrentMoveTime;
		int m_OldVelAmount;
	} m_Ninja;

	// the player core for the physics
	CCharacterCore m_Core;

	// info for dead reckoning
	int m_ReckoningTick; // tick that we are performing dead reckoning From
	CCharacterCore m_SendCore; // core that we should send
	CCharacterCore m_ReckoningCore; // the dead reckoning core

};

#endif
