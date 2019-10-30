/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/shared/config.h>

#include <generated/server_data.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/player.h>

#include "character.h"
#include "laser.h"
#include "projectile.h"

#include <corecrt_math_defines.h>
#include "hammermine.h"
#include "droplife.h"
#include "bgrenade.h"
#include "chareffect.h"
#include "spawnprotecteff.h"

//input count
struct CInputCount
{
	int m_Presses;
	int m_Releases;
};

CInputCount CountInput(int Prev, int Cur)
{
	CInputCount c = {0, 0};
	Prev &= INPUT_STATE_MASK;
	Cur &= INPUT_STATE_MASK;
	int i = Prev;

	while(i != Cur)
	{
		i = (i+1)&INPUT_STATE_MASK;
		if(i&1)
			c.m_Presses++;
		else
			c.m_Releases++;
	}

	return c;
}

MACRO_ALLOC_POOL_ID_IMPL(CCharacter, MAX_CLIENTS)

// Character, "physical" player's part
CCharacter::CCharacter(CGameWorld *pWorld)
: CEntity(pWorld, CGameWorld::ENTTYPE_CHARACTER, vec2(0, 0), ms_PhysSize)
{
	m_VHealth = 0;
	m_VArmor = 0;
	HandleVirtualHealth();

	m_TriggeredEvents = 0;
}

void CCharacter::Reset()
{
	Destroy();
}

bool CCharacter::Spawn(CPlayer *pPlayer, vec2 Pos)
{
	m_pPlayer = pPlayer;
	m_Pos = Pos;

	m_EmoteStop = -1;
	m_LastAction = -1;
	m_LastNoAmmoSound = -1;
	m_ActiveWeapon = m_pPlayer->m_Player_lastweapon;//switch to favorite weapon
	m_LastWeapon = m_pPlayer->m_Player_lastweapon != WEAPON_HAMMER ? WEAPON_HAMMER : WEAPON_GUN;
	m_QueuedWeapon = -1;

	m_Core.Reset();
	m_Core.Init(&GameServer()->m_World.m_Core, GameServer()->Collision());
	m_Core.m_Pos = m_Pos;
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = &m_Core;

	m_ReckoningTick = 0;
	mem_zero(&m_SendCore, sizeof(m_SendCore));
	mem_zero(&m_ReckoningCore, sizeof(m_ReckoningCore));

	GameServer()->m_World.InsertEntity(this);
	m_Alive = true;

	GameServer()->m_pController->OnCharacterSpawn(this);

	// start frozen
	if (m_pPlayer->m_Player_status == 1)
		FreezeSelf();

	// set emote
	SetEmote(m_pPlayer->m_aPlayer_util[2], Server()->Tick() + Server()->TickSpeed() * 604800);// set emote for one week straight

	// *dummy switch to preferred weapon
	if (pPlayer->IsDummy())
	{
		m_ActiveWeapon = m_pPlayer->m_dum_wprimpref;
		m_LastWeapon = m_pPlayer->m_dum_wsecpref;
	}

	// create spawn protect effect
//	pSeff = new CSeff(GameWorld(), m_Pos, PICKUP_ARMOR);
//	GameServer()->CreateSound(m_Pos, SOUND_PICKUP_ARMOR);

	return true;
}

void CCharacter::Destroy()
{
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	m_Alive = false;
}

void CCharacter::SetWeapon(int W)
{
	if(W == m_ActiveWeapon)
		return;

	m_LastWeapon = m_ActiveWeapon;
	m_QueuedWeapon = -1;
	m_ActiveWeapon = W;
	GameServer()->CreateSound(m_Pos, SOUND_WEAPON_SWITCH);

	if(m_ActiveWeapon < 0 || m_ActiveWeapon >= NUM_WEAPONS)
		m_ActiveWeapon = 0;

	//set favorite weapon
	m_pPlayer->m_Player_lastweapon = m_ActiveWeapon;
}

void CCharacter::GainAmmoBack(int Amount)
{
	if (m_ActiveWeapon > WEAPON_GUN)// hammer and gun dont gain ammo
		m_aWeapons[m_ActiveWeapon].m_Ammo += Amount;
}

bool CCharacter::IsGrounded()
{
	if(GameServer()->Collision()->CheckPoint(m_Pos.x+GetProximityRadius()/2, m_Pos.y+GetProximityRadius()/2+5))
		return true;
	if(GameServer()->Collision()->CheckPoint(m_Pos.x-GetProximityRadius()/2, m_Pos.y+GetProximityRadius()/2+5))
		return true;
	return false;
}

vec2 CCharacter::GetPos()
{
	return m_Pos;
}

void CCharacter::HandleNinja()
{
	if(m_ActiveWeapon != WEAPON_NINJA)
		return;

	if ((Server()->Tick() - m_Ninja.m_ActivationTick) > (g_pData->m_Weapons.m_Ninja.m_Duration * Server()->TickSpeed() / 1000))
	{
		// time's up, return
		m_aWeapons[WEAPON_NINJA].m_Got = false;
		m_ActiveWeapon = m_LastWeapon;
		
		// reset velocity
		if(m_Ninja.m_CurrentMoveTime > 0)
			m_Core.m_Vel = m_Ninja.m_ActivationDir*m_Ninja.m_OldVelAmount;

		SetWeapon(m_ActiveWeapon);
		return;
	}

	// force ninja Weapon
	SetWeapon(WEAPON_NINJA);

	m_Ninja.m_CurrentMoveTime--;

	if (m_Ninja.m_CurrentMoveTime == 0)
	{
		// reset velocity
		m_Core.m_Vel = m_Ninja.m_ActivationDir*m_Ninja.m_OldVelAmount;
	}

	if (m_Ninja.m_CurrentMoveTime > 0)
	{
		// Set velocity
		m_Core.m_Vel = m_Ninja.m_ActivationDir * g_pData->m_Weapons.m_Ninja.m_Velocity;
		vec2 OldPos = m_Pos;
		GameServer()->Collision()->MoveBox(&m_Core.m_Pos, &m_Core.m_Vel, vec2(GetProximityRadius(), GetProximityRadius()), 0.f);

		// reset velocity so the client doesn't predict stuff
		m_Core.m_Vel = vec2(0.f, 0.f);

		// check if we Hit anything along the way
		{
			CCharacter *aEnts[MAX_CLIENTS];
			vec2 Dir = m_Pos - OldPos;
			float Radius = GetProximityRadius() * 2.0f;
			vec2 Center = OldPos + Dir * 0.5f;
			int Num = GameServer()->m_World.FindEntities(Center, Radius, (CEntity**)aEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

			for (int i = 0; i < Num; ++i)
			{
				if (aEnts[i] == this)
					continue;

				// make sure we haven't Hit this object before
				bool bAlreadyHit = false;
				for (int j = 0; j < m_NumObjectsHit; j++)
				{
					if (m_apHitObjects[j] == aEnts[i])
						bAlreadyHit = true;
				}
				if (bAlreadyHit)
					continue;

				// check so we are sufficiently close
				if (distance(aEnts[i]->m_Pos, m_Pos) > (GetProximityRadius() * 2.0f))
					continue;

				// Hit a player, give him damage and stuffs...
				GameServer()->CreateSound(aEnts[i]->m_Pos, SOUND_NINJA_HIT);
				// set his velocity to fast upward (for now)
				if(m_NumObjectsHit < 10)
					m_apHitObjects[m_NumObjectsHit++] = aEnts[i];

				aEnts[i]->TakeDamage(g_pData->m_Weapons.m_Ninja.m_pBase->m_Damage, m_pPlayer->GetCID(), WEAPON_NINJA);
			}
		}

		return;
	}

	return;
}

void CCharacter::DoWeaponSwitch()
{
	// make sure we can switch
	if(m_ReloadTimer > 0 || m_QueuedWeapon == -1 || m_aWeapons[WEAPON_NINJA].m_Got)
		return;

	// switch Weapon
	SetWeapon(m_QueuedWeapon);
}

void CCharacter::HandleWeaponSwitch()
{
	int WantedWeapon = m_ActiveWeapon;
	if(m_QueuedWeapon != -1)
		WantedWeapon = m_QueuedWeapon;

	// select Weapon
	int Next = CountInput(m_LatestPrevInput.m_NextWeapon, m_LatestInput.m_NextWeapon).m_Presses;
	int Prev = CountInput(m_LatestPrevInput.m_PrevWeapon, m_LatestInput.m_PrevWeapon).m_Presses;

	if(Next < 128) // make sure we only try sane stuff
	{
		while(Next) // Next Weapon selection
		{
			WantedWeapon = (WantedWeapon+1)%NUM_WEAPONS;
			if(m_aWeapons[WantedWeapon].m_Got)
				Next--;
		}
	}

	if(Prev < 128) // make sure we only try sane stuff
	{
		while(Prev) // Prev Weapon selection
		{
			WantedWeapon = (WantedWeapon-1)<0?NUM_WEAPONS-1:WantedWeapon-1;
			if(m_aWeapons[WantedWeapon].m_Got)
				Prev--;
		}
	}

	// Direct Weapon selection
	if(m_LatestInput.m_WantedWeapon)
		WantedWeapon = m_Input.m_WantedWeapon-1;

	// check for insane values
	if(WantedWeapon >= 0 && WantedWeapon < NUM_WEAPONS && WantedWeapon != m_ActiveWeapon && m_aWeapons[WantedWeapon].m_Got)
		m_QueuedWeapon = WantedWeapon;

	DoWeaponSwitch();
}

void CCharacter::FireWeapon()
{
	if(m_ReloadTimer > 0)
		return;

	DoWeaponSwitch();
	vec2 Direction = normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));

	bool FullAuto = false;

	// check if we have auto hammer / gun yet
	switch (m_ActiveWeapon)
	{
	case WEAPON_HAMMER:
		FullAuto = m_pPlayer->m_aPlayer_stat[m_ActiveWeapon] >= GameServer()->m_Req_hammer_auto;
		break;
	case WEAPON_GUN:
		FullAuto = m_pPlayer->m_aPlayer_stat[m_ActiveWeapon] >= GameServer()->m_Req_gun_auto;
		break;
	default:// for special weapons
		FullAuto = true;
		break;
	}

	// check if we gonna fire
	bool WillFire = false;
	if (CountInput(m_LatestPrevInput.m_Fire, m_LatestInput.m_Fire).m_Presses)
		WillFire = true;

	if (FullAuto && (m_LatestInput.m_Fire & 1) && (m_aWeapons[m_ActiveWeapon].m_Ammo || m_ActiveWeapon == WEAPON_GUN))
		WillFire = true;

	// *dummy firing
	if (m_pPlayer->IsDummy() == true && m_dum_shoot)
	{
		// switch to preferred secondary weapon when out of ammo
		if (m_ActiveWeapon != WEAPON_GUN &&
			m_ActiveWeapon != WEAPON_HAMMER &&
			m_aWeapons[m_ActiveWeapon].m_Ammo <= 0)
			m_ActiveWeapon = m_pPlayer->m_dum_wsecpref;

		Direction = m_dum_direction;
		m_MoveDist = 0.f;
		m_MoveDir = Direction;

		WillFire = true;
	}

	if (!WillFire)
	{
		m_ReloadTimer = 0;
		return;
	}

	float DamageCurrent = GetDamage(m_ActiveWeapon);

	char aBuf[256] = { 0 };
	str_format(aBuf, sizeof(aBuf), "%.2f", DamageCurrent);

	// check for ammo
	if (!m_aWeapons[m_ActiveWeapon].m_Ammo && m_ActiveWeapon != WEAPON_GUN)
	{
		GameServer()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);
		return;
	}

	// remove spawn protection
	m_pPlayer->m_SpawnProtectionMs = 0;

	vec2 ProjStartPos = m_Pos + Direction * GetProximityRadius() * 0.75f;

	switch(m_ActiveWeapon)
	{
		case WEAPON_HAMMER:
		{
			// reset objects Hit
			m_NumObjectsHit = 0;
			GameServer()->CreateSound(m_Pos, SOUND_HAMMER_FIRE);

			CCharacter *apEnts[MAX_CLIENTS];
			int Hits = 0;
			int Num = GameServer()->m_World.FindEntities(ProjStartPos, GetProximityRadius()*0.5f, (CEntity**)apEnts,
														MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

			for (int i = 0; i < Num; ++i)
			{
				CCharacter *pTarget = apEnts[i];

				if ((pTarget == this) || GameServer()->Collision()->IntersectLine(ProjStartPos, pTarget->m_Pos, NULL, NULL))
					continue;

				// set his velocity to fast upward (for now)
				if(length(pTarget->m_Pos-ProjStartPos) > 0.0f)
					GameServer()->CreateHammerHit(pTarget->m_Pos-normalize(pTarget->m_Pos-ProjStartPos)*GetProximityRadius()*0.5f);
				else
					GameServer()->CreateHammerHit(ProjStartPos);

				vec2 Dir;
				if (length(pTarget->m_Pos - m_Pos) > 0.0f)
					Dir = normalize(pTarget->m_Pos - m_Pos);
				else
					Dir = vec2(0.f, -1.f);

				// always knock up from melee
				pTarget->ImpulseAdd(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f);

				// add melee full damage only if not in flying mode (mine mode melee damage since you would not reach someone with your mines)
				if (m_pPlayer->m_aPlayer_stat[WEAPON_HAMMER] < GameServer()->m_Req_hammer_fly || m_pPlayer->m_aPlayer_option[0] != 0)
				{
					pTarget->TakeDamage(DamageCurrent,
						m_pPlayer->GetCID(), m_ActiveWeapon);
				}

				Hits++;
			}

			// at end of code to prevent double kill explosion bug
			if (m_pPlayer->m_aPlayer_stat[WEAPON_HAMMER] >= GameServer()->m_Req_hammer_fly)
			{
				// fly mode
				if (m_pPlayer->m_aPlayer_option[0] == 0)
				{
					// recalculate pos for smooth look
					ProjStartPos = m_Pos + m_MoveDir * m_MoveDist * abs(m_ReloadTimer);

					// add fly force
					float force_fhammer = 10;
					ImpulseAdd(Direction*force_fhammer);

					// add explosion (full damage)
					GameServer()->CreateExplosionExt(ProjStartPos, m_pPlayer->GetCID(), WEAPON_HAMMER, DamageCurrent, 1, 1, false);
				}
				else// create mines (full damage)
				{
					// recalculate pos for smooth look
	//				ProjStartPos = m_Pos + m_MoveDir * m_MoveDist * abs(m_ReloadTimer);

					// controlled mine placing
					ProjStartPos += m_MoveDir * m_MoveDist * abs(m_ReloadTimer);

					CMine *pProj = new CMine(GameWorld(), WEAPON_LASER,
						m_pPlayer->GetCID(),
						ProjStartPos,
						Direction,
						(int)(Server()->TickSpeed() * GameServer()->m_MineLifeTime),
						DamageCurrent, true, 0, SOUND_GRENADE_EXPLODE, WEAPON_HAMMER);
				}
			}
		}break;

		case WEAPON_GUN:
		{
			int num_projectiles = 1;
			int spread_projectiles = 7;

			// spread level
			if (m_pPlayer->m_aPlayer_stat[WEAPON_GUN] >= GameServer()->m_Req_gun_spread)
			{
				num_projectiles++;

				// switchmode gun tripleshot
				if (m_pPlayer->m_aPlayer_option[0] == 1)
					num_projectiles++;
			}

			for (int i = 0; i < num_projectiles; i++)
			{
				float angle_current = angle(Direction) - ((num_projectiles - 1)*spread_projectiles / 2)*(M_PI / 180);

				angle_current += i * (spread_projectiles*(M_PI / 180));

				// recalculate pos for smooth look
				ProjStartPos = m_Pos + vec2(cosf(angle_current), sinf(angle_current)) * GetProximityRadius() * 0.75f + normalize(vec2(cosf(angle_current), sinf(angle_current))) * (GameServer()->Tuning()->m_GunSpeed / Server()->TickSpeed()) * abs(m_ReloadTimer);

				CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_GUN,
					m_pPlayer->GetCID(),
					ProjStartPos,
					vec2(cosf(angle_current), sinf(angle_current)),
					(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GunLifetime),
					DamageCurrent, 0, 0, -1, WEAPON_GUN);
			}

			GameServer()->CreateSound(m_Pos, SOUND_GUN_FIRE);
		} break;

		case WEAPON_SHOTGUN:
		{
			int BulletCap = GameServer()->twep_shotgun_bulletcap;
			float SpreadBase = GameServer()->twep_shotgun_spreadbase;
			float num_projectiles = 0;
			float speeddiff_max = GameServer()->twep_shotgun_speeddiff;
			int lifetime_add = 0;// lifetime increase
			int lifetime_penalty = 0;// lifetime

			// spread calculation (switchmode double spread)
			float spread_projectiles = SpreadBase * (m_pPlayer->m_aPlayer_option[0] == 0 ? 1 : 2);
			
			// number projectiles calculation
			for (int i = 0; i < m_pPlayer->m_aPlayer_stat[WEAPON_SHOTGUN]; ++i)
			{
				num_projectiles += min(1.f, 1.5f / num_projectiles);
			}

			if (num_projectiles > BulletCap)
				num_projectiles = BulletCap;

			// lifetime bonus calculation
			lifetime_add += (float)m_pPlayer->m_aPlayer_stat[WEAPON_SHOTGUN] * (GameServer()->twep_shotgun_rangegain / 100);
			// lifetime penalty calculation (switchmode half base duration)
			lifetime_penalty = (float)Server()->TickSpeed()*GameServer()->Tuning()->m_ShotgunLifetime * (m_pPlayer->m_aPlayer_option[0] == 0 ? 0 : 0.5);

			for (int i = 0; i < floor(num_projectiles); i++)
			{
				float angle_current = angle(Direction) - (((floor(num_projectiles) - 1) / 2) * spread_projectiles)*(M_PI / 180);
				float speeddiff = abs(2 * ((-(num_projectiles - 1) / 2 + i) / (int)num_projectiles)) * (speeddiff_max / 100);
				
				angle_current += i * (spread_projectiles * (M_PI / 180));

				// recalculate pos for smooth look
				ProjStartPos = m_Pos + Direction * 15 + vec2(cosf(angle_current), sinf(angle_current)) * GetProximityRadius() * 0.75f + normalize(vec2(cosf(angle_current), sinf(angle_current))) * (GameServer()->Tuning()->m_ShotgunSpeed * (1 - speeddiff) / Server()->TickSpeed()) * abs(m_ReloadTimer);

				CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_SHOTGUN,
					m_pPlayer->GetCID(),
					ProjStartPos,
					vec2(cosf(angle_current), sinf(angle_current)) * (1 - speeddiff),
					(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_ShotgunLifetime + lifetime_add - lifetime_penalty),
					DamageCurrent, 0, 0, -1, WEAPON_SHOTGUN);
			}

			GameServer()->CreateSound(m_Pos, SOUND_SHOTGUN_FIRE);
		} break;

		case WEAPON_GRENADE:
		{
			int bounces = 0;
			int num_projectiles = 1;// number of projectiles
			int spread_projectiles = 5;// spread of projectiles

			// bounce level x1 (switchmode +1 projectile instead)
			if (m_pPlayer->m_aPlayer_stat[WEAPON_GRENADE] >= GameServer()->m_Req_grenade_bounce)
			{
				if (m_pPlayer->m_aPlayer_option[0] == 0)
					bounces++;
				else
					num_projectiles++;
			}

			// bounce level x2 (switchmode +1 projectile instead)
			if (m_pPlayer->m_aPlayer_stat[WEAPON_GRENADE] >= GameServer()->m_Req_grenade_bounce2)
			{
				if (m_pPlayer->m_aPlayer_option[0] == 0)
					bounces++;
				else
					num_projectiles++;
			}

			// recalculate pos for smooth look
			ProjStartPos = m_Pos + Direction * GetProximityRadius() * 0.75f + normalize(Direction) * (GameServer()->Tuning()->m_GrenadeSpeed / Server()->TickSpeed()) * abs(m_ReloadTimer);

			for (int i = 0; i < num_projectiles; i++)
			{
				float angle_current = angle(Direction) - ((num_projectiles - 1)*spread_projectiles / 2)*(M_PI / 180);

				angle_current += i * (spread_projectiles*(M_PI / 180));

				// recalculate pos for smooth look
				ProjStartPos = m_Pos + vec2(cosf(angle_current), sinf(angle_current)) * GetProximityRadius() * 0.75f + normalize(vec2(cosf(angle_current), sinf(angle_current))) * (GameServer()->Tuning()->m_GrenadeSpeed / Server()->TickSpeed()) * abs(m_ReloadTimer);

				CBgrenade *pProj = new CBgrenade(GameWorld(), WEAPON_GRENADE,
					m_pPlayer->GetCID(),
					ProjStartPos,
					vec2(cosf(angle_current), sinf(angle_current)),
					(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GrenadeLifetime),
					DamageCurrent, true, (m_pPlayer->m_aPlayer_option[0] == 1 && i != 1), SOUND_GRENADE_EXPLODE, WEAPON_GRENADE, bounces);
			}

			GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
		} break;

		case WEAPON_LASER:
		{
			int num_lasers = 1;
			int spread_laser = 5;
			bool range_up = false;
			bool do_explode = false;
			bool do_knockback = false;

			// dual
			if (m_pPlayer->m_aPlayer_stat[WEAPON_LASER] >= GameServer()->m_Req_rifle_dual)
			{
				num_lasers = 2;
				spread_laser = 3;
				do_explode = true;// deactivated
			}

			// explode
//			if (m_pPlayer->m_aPlayer_stat[WEAPON_LASER] >= GameServer()->m_Req_rifle_exp)
//				do_explode = true;

			// range up
			if (m_pPlayer->m_aPlayer_stat[WEAPON_LASER] >= GameServer()->m_Req_rifle_range)
				range_up = true;

			// spread
			if (m_pPlayer->m_aPlayer_stat[WEAPON_LASER] >= GameServer()->m_Req_rifle_triple)
			{
				num_lasers = 3;
				spread_laser = 5;
			}

			// switchmode concentrated firepower
			if (m_pPlayer->m_aPlayer_option[0] == 1)
			{
				spread_laser = 0;
				num_lasers = 1;
			}

			for (int i = 0; i < num_lasers; i++)
			{
				do_knockback = false;

				if (m_pPlayer->m_aPlayer_stat[WEAPON_LASER] < GameServer()->m_Req_rifle_triple)
				{
					if (i == 0)
						do_knockback = true;
				}
				else if (i == 1)
					do_knockback = true;

				// special firemode knockback
				if (m_pPlayer->m_aPlayer_option[0] == 1)
					do_knockback = true;

				float angle_current = angle(Direction) - ((num_lasers - 1)*spread_laser / 2)*(M_PI / 180);

				angle_current += i * (spread_laser*(M_PI / 180));

				new CLaser(GameWorld(), m_Pos, vec2(cosf(angle_current), sinf(angle_current)), GameServer()->Tuning()->m_LaserReach + range_up * GameServer()->swep_rifle_rangegain, m_pPlayer->GetCID(), DamageCurrent, do_explode, do_knockback);
			}

			GameServer()->CreateSound(m_Pos, SOUND_LASER_FIRE);
		} break;
	}

	m_AttackTick = Server()->Tick();

	if (m_aWeapons[m_ActiveWeapon].m_Ammo > 0 && m_ActiveWeapon != WEAPON_GUN && !GameServer()->m_EvtNoclip)
		m_aWeapons[m_ActiveWeapon].m_Ammo--;

	m_ReloadTimer += GetFireRate();

	// make gun and hammer shootable faster if not automatic yet (huge fun factor - it's the small things after all)
	if (!FullAuto)
	{
		if (m_ActiveWeapon == WEAPON_GUN || m_ActiveWeapon == WEAPON_HAMMER)
			m_ReloadTimer /= 2;
	}
}

void CCharacter::HandleWeapons()
{
	vec2 PosTrans = m_Pos;

	/* controlled mine placing */
	if (m_ActiveWeapon == WEAPON_HAMMER)// for hammer in mine mode make it really smooth
	{
		vec2 Direction = normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));
		PosTrans += Direction * GetProximityRadius()*0.75f;
	}

	m_MoveDir = normalize(vec2(m_PrevPos.x - PosTrans.x, m_PrevPos.y - PosTrans.y));
	m_MoveDist = length(vec2(m_PrevPos.x - PosTrans.x, m_PrevPos.y - PosTrans.y));

	if (m_MoveDist == 0)
		m_MoveDir = vec2(0.f, 1.f);

	m_PrevPos = PosTrans;

	if (m_ReloadTimer)
		m_ReloadTimer--;
	while (m_ReloadTimer < 0)
		FireWeapon();
	return;
}

float CCharacter::GetDamage(int Weapon)
{
	float damage = 0;

	if (Weapon < 0 || Weapon > WEAPON_LASER)
		return 0;

	damage = (GameServer()->m_aBaseDmg[Weapon]) + (float)GameServer()->m_aBaseDmg[Weapon] * (GameServer()->m_DamageScaling / 100.0) * (m_pPlayer->m_aPlayer_stat[Weapon] - 1);

	switch (m_ActiveWeapon)
	{
	case WEAPON_HAMMER:// increase hammer damage in fly mode
		if (m_pPlayer->m_aPlayer_option[0] != 1 && m_pPlayer->m_aPlayer_stat[WEAPON_HAMMER] >= GameServer()->m_Req_hammer_fly)
		damage *= GameServer()->swep_hammer_dmgscale;
		break;

	case WEAPON_GUN:
		damage *= GameServer()->swep_gun_dmgscale;
		break;

	case WEAPON_SHOTGUN:
		damage *= GameServer()->swep_shotgun_dmgscale;
		break;

	case WEAPON_GRENADE:
		damage *= GameServer()->swep_grenade_dmgscale;
		break;

	case WEAPON_LASER:
		damage *= GameServer()->swep_rifle_dmgscale;
		break;
	}

	//	GameServer()->ServerMessage(0, "%.2f", damage);
	
		return damage;
}

float CCharacter::GetFireRate()
{
	// show effect only each 10 steps
	// m_ReloadTimer = (AsBase / (1 + (1 * floor((float)m_pPlayer->m_aPlayer_stat[6] / 10) * (AsPerTen / 100.0))))// increase only each 10 handle

	float Retval = 0;
	float AsPerTen = 50;// % attackspeed increase each 10 level (smooth increase)
	float AsBase = g_pData->m_Weapons.m_aId[m_ActiveWeapon].m_Firedelay;

	Retval = (AsBase / (1 + (1 * (((float)m_pPlayer->m_aPlayer_stat[6] + GameServer()->m_EvtBonusHandle) / 10) * (AsPerTen / 100.0))))
		* Server()->TickSpeed() / 1000;

	if (m_ActiveWeapon == WEAPON_HAMMER && m_pPlayer->m_aPlayer_stat[WEAPON_HAMMER] >= GameServer()->m_Req_hammer_fly)
	{
		// double firerate for fly hammer
		if (m_pPlayer->m_aPlayer_option[0] == 0)
			Retval *= (1.f - 0.2);
		else// half firerate for mine hammer
			Retval *= 2;
	}
	
	// switchmode gun one third firerate decrease
	if (m_ActiveWeapon == WEAPON_GUN && m_pPlayer->m_aPlayer_option[0] == 1 && m_pPlayer->m_aPlayer_stat[WEAPON_GUN] >= GameServer()->m_Req_gun_spread)
	{
		Retval *= (1.f + 1.f/3);
	}

	// switchmode rifle *amount spread firerate
	if (m_ActiveWeapon == WEAPON_LASER && m_pPlayer->m_aPlayer_option[0] == 1)
	{
		// double spread to double firerate
		if (m_pPlayer->m_aPlayer_stat[WEAPON_LASER] >= GameServer()->m_Req_rifle_dual)
			Retval /= 2;
		// triple spread to triple firerate
		if (m_pPlayer->m_aPlayer_stat[WEAPON_LASER] >= GameServer()->m_Req_rifle_triple)
			Retval /= 2;
	}

	return Retval;
}

bool CCharacter::GiveWeapon(int Weapon, int Ammo)
{
	// no limits
	m_aWeapons[Weapon].m_Got = true;
	m_aWeapons[Weapon].m_Ammo += Ammo;
	return true;
}

void CCharacter::FreezeSelf()
{
	m_aWeapons[WEAPON_NINJA].m_Got = true;
	m_aWeapons[WEAPON_NINJA].m_Ammo = 0;

	if (m_ActiveWeapon != WEAPON_NINJA)
		m_LastWeapon = m_ActiveWeapon;
	m_ActiveWeapon = WEAPON_NINJA;

	m_VHealth = 1;
	m_VArmor = 0;
	HandleVirtualHealth();
}

void CCharacter::SetEmote(int Emote, int Tick)
{
	m_EmoteType = Emote;
	m_EmoteStop = Tick;
}

void CCharacter::GiveNinja()
{
	// freezingly cold
}

void CCharacter::OnPredictedInput(CNetObj_PlayerInput *pNewInput)
{
	// frozen
	if (m_ActiveWeapon == WEAPON_NINJA)
		return;

	// check for changes
	if(mem_comp(&m_Input, pNewInput, sizeof(CNetObj_PlayerInput)) != 0)
		m_LastAction = Server()->Tick();

	// copy new input
	mem_copy(&m_Input, pNewInput, sizeof(m_Input));
	m_NumInputs++;

	// it is not allowed to aim in the center
	if(m_Input.m_TargetX == 0 && m_Input.m_TargetY == 0)
		m_Input.m_TargetY = -1;
}

void CCharacter::OnDirectInput(CNetObj_PlayerInput *pNewInput)
{
	// frozen
	if (m_ActiveWeapon == WEAPON_NINJA)
		return;

	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
	mem_copy(&m_LatestInput, pNewInput, sizeof(m_LatestInput));

	// it is not allowed to aim in the center
	if(m_LatestInput.m_TargetX == 0 && m_LatestInput.m_TargetY == 0)
		m_LatestInput.m_TargetY = -1;

	if(m_NumInputs > 2 && m_pPlayer->GetTeam() != TEAM_SPECTATORS)
	{
		HandleWeaponSwitch();
		FireWeapon();
	}

	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
}

void CCharacter::ResetInput()
{
	m_Input.m_Direction = 0;
	m_Input.m_Hook = 0;
	// simulate releasing the fire button
	if((m_Input.m_Fire&1) != 0)
		m_Input.m_Fire++;
	m_Input.m_Fire &= INPUT_STATE_MASK;
	m_Input.m_Jump = 0;
	m_LatestPrevInput = m_LatestInput = m_Input;
}

void CCharacter::Tick()
{
	// *dummy brain
	if (m_pPlayer->IsDummy())
	{
		HandleDummyTick();
	}

	m_Core.m_Input = m_Input;
	m_Core.Tick(true);

	// handle death-tiles and leaving gamelayer
	if(GameServer()->Collision()->GetCollisionAt(m_Pos.x+GetProximityRadius()/3.f, m_Pos.y-GetProximityRadius()/3.f)&CCollision::COLFLAG_DEATH ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x+GetProximityRadius()/3.f, m_Pos.y+GetProximityRadius()/3.f)&CCollision::COLFLAG_DEATH ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x-GetProximityRadius()/3.f, m_Pos.y-GetProximityRadius()/3.f)&CCollision::COLFLAG_DEATH ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x-GetProximityRadius()/3.f, m_Pos.y+GetProximityRadius()/3.f)&CCollision::COLFLAG_DEATH ||
		GameLayerClipped(m_Pos))
	{
		Die(m_pPlayer->GetCID(), WEAPON_WORLD);
	}

	// handle Weapons
	HandleWeapons();
	
	// *dummy firing input
	if (m_pPlayer->IsDummy())
	{
		HandleWeaponSwitch();
		FireWeapon();
	}

	// move marker
	int myoff = 30;
	int myYoff = -80;
	vec2 p[3];
	// orbiting
	vec2 markVec = GetMarkVec(0, myoff);
	m_mark_angle += 5;

	if (m_pPlayer->m_aPlayer_util[0] == 0)
	for (int i = 0; i < 3; ++i)
	{
		if (!pEff[i])
			continue;

		switch (i)
		{
		case 0: p[i] = vec2(pEff[i]->m_Pos.x + markVec.x, pEff[i]->m_Pos.y - myYoff + markVec.y); break;
		case 1:
			if (m_pPlayer->m_Player_status == 2)// moderator
				markVec = GetMarkVec(180, myoff);
			else if (m_pPlayer->m_Player_status == 3)// admin
				markVec = GetMarkVec(120, myoff);

			p[i] = vec2(pEff[i]->m_Pos.x + markVec.x, pEff[i]->m_Pos.y - myYoff + markVec.y); break;
		case 2:
			if (m_pPlayer->m_Player_status == 3)// admin
				markVec = GetMarkVec(240, myoff);

			p[i] = vec2(pEff[i]->m_Pos.x + markVec.x, pEff[i]->m_Pos.y - myYoff + markVec.y); break;}
//		}

		pEff[i]->m_Pos.x += (m_Pos.x - p[i].x) / 1.f;
		pEff[i]->m_Pos.y += (m_Pos.y - p[i].y) / 1.f;
	}

	// destroy marker
	if (m_pPlayer->m_aPlayer_util[0] == 1)
	for (int i = 0; i < 3; ++i)
	{
		if (!pEff[i])
			continue;

		pEff[i]->Reset();
		
		if (pEff[i])
			pEff[i] = NULL;
	}

	// destroy spawn protection marker
	if (m_pPlayer->m_SpawnProtectionMs <= 0)
	{
		if (pSeff)
		{
			pSeff->Reset();
			pSeff = NULL;
		}
	}

	// create marker
	int amt = 0;
	// moderator / admin indicator
	if (m_pPlayer->m_Player_status == 2)
		amt = 2;
	if (m_pPlayer->m_Player_status == 3)
		amt = 3;

	if (m_pPlayer->m_aPlayer_util[0] == 0 && IsAlive())
	for (int i = 0; i < amt; ++i)
	{
		if (!pEff[i])
			pEff[i] = new CEff(GameWorld(), m_Pos);
	}

	// move spawn protection effect
	if (pSeff)
	{
		pSeff->m_Pos = vec2(m_Pos.x, m_Pos.y - 70);
	}
}

void CCharacter::TickDefered()
{
	// advance the dummy
	{
		CWorldCore TempWorld;
		m_ReckoningCore.Init(&TempWorld, GameServer()->Collision());
		m_ReckoningCore.Tick(false);
		m_ReckoningCore.Move();
		m_ReckoningCore.Quantize();
	}

	//lastsentcore
	vec2 StartPos = m_Core.m_Pos;
	vec2 StartVel = m_Core.m_Vel;
	bool StuckBefore = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));

	m_Core.Move();
	bool StuckAfterMove = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Core.Quantize();
	bool StuckAfterQuant = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Pos = m_Core.m_Pos;

	if(!StuckBefore && (StuckAfterMove || StuckAfterQuant))
	{
		// Hackish solution to get rid of strict-aliasing warning
		union
		{
			float f;
			unsigned u;
		}StartPosX, StartPosY, StartVelX, StartVelY;

		StartPosX.f = StartPos.x;
		StartPosY.f = StartPos.y;
		StartVelX.f = StartVel.x;
		StartVelY.f = StartVel.y;

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "STUCK!!! %d %d %d %f %f %f %f %x %x %x %x",
			StuckBefore,
			StuckAfterMove,
			StuckAfterQuant,
			StartPos.x, StartPos.y,
			StartVel.x, StartVel.y,
			StartPosX.u, StartPosY.u,
			StartVelX.u, StartVelY.u);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}
	
	m_TriggeredEvents |= m_Core.m_TriggeredEvents;

	if(m_pPlayer->GetTeam() == TEAM_SPECTATORS)
	{
		m_Pos.x = m_Input.m_TargetX;
		m_Pos.y = m_Input.m_TargetY;
	}

	// update the m_SendCore if needed
	{
		CNetObj_Character Predicted;
		CNetObj_Character Current;
		mem_zero(&Predicted, sizeof(Predicted));
		mem_zero(&Current, sizeof(Current));
		m_ReckoningCore.Write(&Predicted);
		m_Core.Write(&Current);

		// only allow dead reackoning for a top of 3 seconds
		if(m_ReckoningTick+Server()->TickSpeed()*3 < Server()->Tick() || mem_comp(&Predicted, &Current, sizeof(CNetObj_Character)) != 0)
		{
			m_ReckoningTick = Server()->Tick();
			m_SendCore = m_Core;
			m_ReckoningCore = m_Core;
		}
	}
}

void CCharacter::TickPaused()
{
	++m_AttackTick;
	++m_Ninja.m_ActivationTick;
	++m_ReckoningTick;
	if(m_LastAction != -1)
		++m_LastAction;
	if(m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart > -1)
		++m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart;
	if(m_EmoteStop > -1)
		++m_EmoteStop;
}

bool CCharacter::IncreaseHealth(float Amount)
{
	m_VHealth += Amount;
	HandleVirtualHealth();
	return true;
}

bool CCharacter::IncreaseArmor(float Amount)
{
	m_VArmor += Amount;
	HandleVirtualHealth();
	return true;
}

bool CCharacter::ImpulseAdd(vec2 Force)
{
	m_Core.m_Vel += Force;
	return true;
}

void CCharacter::Die(int Killer, int Weapon)
{
	float amt_health = max(1.0f, (GameServer()->m_apPlayers[m_pPlayer->GetCID()]->m_aPlayer_stat[5] * GameServer()->m_Per_health_max * GameServer()->m_DropLifeRewardRatio) / 100);
	float amt_armor = max(1.0f, (GameServer()->m_apPlayers[m_pPlayer->GetCID()]->m_aPlayer_stat[5] * GameServer()->m_Per_armor_max * GameServer()->m_DropLifeRewardRatio) / 100);
	float s_radius = 35;// radius in where the health can spawn
	float s_angle = 0;// angle where it spawns

	// give killer stats upgrade
	GameServer()->HandleKill(Killer, m_pPlayer->GetCID(), Weapon);

	// handle getting killed
	GameServer()->HandleDeath(m_pPlayer->GetCID(), Killer, Weapon);

	// drop life if killed by weapon
	if (Weapon >= WEAPON_HAMMER && Weapon <= WEAPON_NINJA && Weapon != WEAPON_GAME)
	{
		s_angle = 360 * frandom();
		new CDropLife(GameWorld(), m_Pos + vec2(cosf(s_angle), sinf(s_angle)) * s_radius, -vec2(cosf(s_angle), sinf(s_angle)), amt_health, PICKUP_HEALTH);
		new CDropLife(GameWorld(), m_Pos + vec2(cosf(s_angle + 180), sinf(s_angle + 180)) * s_radius, -vec2(cosf(s_angle + 180), sinf(s_angle + 180)), amt_armor, PICKUP_ARMOR);
	}

	// we got to wait 0.5 secs before respawning
	m_Alive = false;
	m_pPlayer->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
	int ModeSpecial = GameServer()->m_pController->OnCharacterDeath(this, GameServer()->m_apPlayers[Killer], Weapon);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "kill killer='%d:%s' victim='%d:%s' weapon=%d special=%d",
		Killer, Server()->ClientName(Killer),
		m_pPlayer->GetCID(), Server()->ClientName(m_pPlayer->GetCID()), Weapon, ModeSpecial);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	// send the kill message
	CNetMsg_Sv_KillMsg Msg;
	Msg.m_Killer = Killer;
	Msg.m_Victim = m_pPlayer->GetCID();
	Msg.m_Weapon = Weapon;
	Msg.m_ModeSpecial = ModeSpecial;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);

	// a nice sound
	GameServer()->CreateSound(m_Pos, SOUND_PLAYER_DIE);

	// this is for auto respawn after 3 secs
	m_pPlayer->m_DieTick = Server()->Tick();

	GameServer()->m_World.RemoveEntity(this);
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	GameServer()->CreateDeath(m_Pos, m_pPlayer->GetCID());

	// destroy marker
	for (int i = 0; i < 3; ++i)
	{
		if (!pEff[i])
			continue;

		pEff[i]->Reset();
	}

	// destroy spawnprotection marker
	if (pSeff)
		pSeff->Reset();
}

bool CCharacter::TakeDamage(float Dmg, int From, int Weapon)
{
	// spawnprotection - invulnerable
	if (m_pPlayer->m_SpawnProtectionMs > 0)
		return false;

	if(GameServer()->m_pController->IsFriendlyFire(m_pPlayer->GetCID(), From))
		return false;

	// m_pPlayer only inflicts half damage on self
	if (From == m_pPlayer->GetCID())
		Dmg = max(1.0f, Dmg / 2);

	float OldHealth = m_VHealth, OldArmor = m_VArmor;
	float Hdmg = 0, Admg = 0;

	if(Dmg)
	{
		if(m_VArmor)
		{
			if(Dmg > 1)
			{
				m_VHealth--;
				HandleVirtualHealth();
				Dmg--;
			}

			if(Dmg > m_VArmor)
			{
				Dmg -= m_VArmor;
				m_VArmor = 0;
				HandleVirtualHealth();
			}
			else
			{
				m_VArmor -= Dmg;
				HandleVirtualHealth();
				Dmg = 0;
			}
		}

		m_VHealth -= Dmg;
		HandleVirtualHealth();
	}

	Hdmg = OldHealth - m_VHealth;
	Admg = OldArmor - m_VArmor;

	// create healthmod indicator
	GameServer()->CreateDamage(m_Pos, m_pPlayer->GetCID(), m_Pos, min(9, (int)(floor(Hdmg))), min(9, (int)(floor(Admg))), From == m_pPlayer->GetCID());
	
	/*
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "%d", min(9, (int)(floor(Admg))));
	GameServer()->SendChat(TEAM_SPECTATORS, CHAT_NONE, 1, aBuf);
	*/

	// do damage Hit sound
	if(From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
	{
		int64 Mask = CmaskOne(From);
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameServer()->m_apPlayers[i] && (GameServer()->m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS ||  GameServer()->m_apPlayers[i]->m_DeadSpecMode) &&
				GameServer()->m_apPlayers[i]->GetSpectatorID() == From)
				Mask |= CmaskOne(i);
		}
		GameServer()->CreateSound(GameServer()->m_apPlayers[From]->m_ViewPos, SOUND_HIT, Mask);
	}

	// check for death
	if(m_Health <= 0.f)
	{
		Die(From, Weapon);

		// set attacker's face to happy (taunt!)
		if (From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
		{
			CCharacter *pChr = GameServer()->m_apPlayers[From]->GetCharacter();
			if (pChr)
			{
				pChr->m_EmoteType = EMOTE_HAPPY;
				pChr->m_EmoteStop = Server()->Tick() + Server()->TickSpeed();
			}
		}

		return false;
	}

	if (Dmg > 2)
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_LONG);
	else
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_SHORT);

	m_EmoteType = EMOTE_PAIN;
	m_EmoteStop = Server()->Tick() + 500 * Server()->TickSpeed() / 1000;

	return true;
}

void CCharacter::HandleVirtualHealth()
{
	m_Health = min(10.f, m_VHealth);
	m_Armor = min(10.f, m_VArmor);
}

void CCharacter::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CNetObj_Character *pCharacter = static_cast<CNetObj_Character *>(Server()->SnapNewItem(NETOBJTYPE_CHARACTER, m_pPlayer->GetCID(), sizeof(CNetObj_Character)));
	if(!pCharacter)
		return;

	// write down the m_Core
	if(!m_ReckoningTick || GameServer()->m_World.m_Paused)
	{
		// no dead reckoning when paused because the client doesn't know
		// how far to perform the reckoning
		pCharacter->m_Tick = 0;
		m_Core.Write(pCharacter);
	}
	else
	{
		pCharacter->m_Tick = m_ReckoningTick;
		m_SendCore.Write(pCharacter);
	}

	// set emote
	if (m_EmoteStop < Server()->Tick())
	{
		m_EmoteType = m_pPlayer->m_aPlayer_util[2];// set emote to configured emote
		m_EmoteStop = -1;
	}

	pCharacter->m_Emote = m_EmoteType;

	pCharacter->m_AmmoCount = 0;
	pCharacter->m_Health = 0;
	pCharacter->m_Armor = 0;
	pCharacter->m_TriggeredEvents = m_TriggeredEvents;

	pCharacter->m_Weapon = m_ActiveWeapon;
	pCharacter->m_AttackTick = m_AttackTick;

	pCharacter->m_Direction = m_Input.m_Direction;

	if(m_pPlayer->GetCID() == SnappingClient || SnappingClient == -1 ||
		(!g_Config.m_SvStrictSpectateMode && m_pPlayer->GetCID() == GameServer()->m_apPlayers[SnappingClient]->GetSpectatorID()))
	{
		pCharacter->m_Health = m_Health;
		pCharacter->m_Armor = m_Armor;
		if(m_ActiveWeapon == WEAPON_NINJA)
			pCharacter->m_AmmoCount = m_Ninja.m_ActivationTick + g_pData->m_Weapons.m_Ninja.m_Duration * Server()->TickSpeed() / 1000;
		else if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0)
			pCharacter->m_AmmoCount = m_aWeapons[m_ActiveWeapon].m_Ammo;
	}

	if(pCharacter->m_Emote == EMOTE_NORMAL)
	{
		if(250 - ((Server()->Tick() - m_LastAction)%(250)) < 5)
			pCharacter->m_Emote = EMOTE_BLINK;
	}
}

void CCharacter::PostSnap()
{
	m_TriggeredEvents = 0;
}

void CCharacter::HandleDummyTick()
{
	// if no human players are active
	if (!GameServer()->m_has_human_players)// || m_pPlayer->m_Player_status == 1
	{
		m_Input.m_Direction = 0;
		m_dum_shoot = false;
		return;
	}

	// if no human players or only spectators
	if (!GameServer()->m_has_human_active_players)
	{
		if (m_VHealth > 1)
			m_VHealth = 1;
		if (m_VArmor > 1)
			m_VArmor = 1;

		HandleVirtualHealth();
	}

	m_dum_range_vision = 800;
	m_dum_range_triggershooting = 0;

	// determine vision range depending on weapon
	switch (m_ActiveWeapon)
	{
	case WEAPON_HAMMER:
		if (m_pPlayer->m_aPlayer_stat[WEAPON_HAMMER] < GameServer()->m_Req_hammer_fly)
		{
			m_dum_range_triggershooting = m_dum_range_hook + 100;
		}
		else
			m_dum_range_triggershooting = 2000;
		break;
	case WEAPON_GUN:
		m_dum_range_triggershooting = 600;
		break;
	case WEAPON_SHOTGUN:
		m_dum_range_triggershooting = 400;
		break;
	case WEAPON_GRENADE:
		m_dum_range_triggershooting = 600;
		break;
	case WEAPON_LASER:
		m_dum_range_triggershooting = GameServer()->Tuning()->m_LaserReach - 300;
		break;
	}

	CEntity* ent = GameServer()->m_World.ClosestEntity(m_Pos, m_dum_range_vision, CGameWorld::ENTTYPE_CHARACTER, this);

	bool canstroll = true;
	bool canscan = true;// move weapon randomly

	// jumping
	int trigger_jump_range = 300;

	// moving
	int trigger_dirchange_range = ms_PhysSize + 10;

	// hook mood
	if (!m_dum_hookmoodtimer)
	{
		m_dum_hookmood = frandom() > 0.5 ? true : false;
		m_dum_hookmoodtimer = round(max(Server()->TickSpeed() * 1.f, frandom() * Server()->TickSpeed() * 5.f));
	}

	if (m_dum_hookmoodtimer)
		m_dum_hookmoodtimer--;

	// shoot / hook
	// reset shoot reaction when target switches
	if (lastent != ent)
	{
		lastent = ent;
		m_dum_shootreaction = 0;
	}

	m_dum_shoot = false;
	m_dum_hook = false;
	if (ent)
	{
		// shoot at nearest visible target (any)
		if (!GameServer()->Collision()->IntersectLine(m_Pos, ent->GetPos(), NULL, NULL))
		{
			// bullet drop calculation
			yoff_bdrop = abs(m_Pos.x - ent->GetPos().x) * wep_dropoff[m_ActiveWeapon];
			m_dum_direction = normalize(vec2(ent->GetPos().x - m_Pos.x + aoff.x, ent->GetPos().y - m_Pos.y + aoff.y));

			// shoot if in weapon range
			if (distance(m_Pos, ent->GetPos()) <= m_dum_range_triggershooting)
			{
				if (m_dum_shootreaction >= Server()->TickSpeed() * 0.5f)
				{
					m_dum_shoot = true;
					// set scanpos to pointing direction
					m_dum_scanpos = m_dum_direction;

					// lower firerate if gun is semi
					if (m_ActiveWeapon == WEAPON_GUN &&
						m_pPlayer->m_aPlayer_stat[WEAPON_GUN] < GameServer()->m_Req_gun_auto)
					{
						m_dum_shoot = false;

						if (!m_dum_gundelay)
						{
							m_dum_gundelay = round(Server()->TickSpeed() * 0.2);
							m_dum_shoot = true;
						}
					}

					// lower firerate if hammer is semi
					if (m_ActiveWeapon == WEAPON_HAMMER &&
						m_pPlayer->m_aPlayer_stat[WEAPON_HAMMER] < GameServer()->m_Req_hammer_auto)
					{
						m_dum_shoot = false;

						if (!m_dum_hammerdelay)
						{
							m_dum_hammerdelay = round(Server()->TickSpeed() * 0.15);
							m_dum_shoot = true;
						}
					}
				}
				else
				{
					m_dum_shootreaction++;
				}
			}
			else
			{
				m_dum_shootreaction = 0;
			}

			canstroll = false;
			canscan = false;

			// hook (for now only with hammer equipped and if not in flymode)
			if (m_ActiveWeapon == WEAPON_HAMMER && (m_pPlayer->m_aPlayer_stat[WEAPON_HAMMER] < GameServer()->m_Req_hammer_fly || m_pPlayer->m_aPlayer_option[0] == 1))
				m_dum_hook = true;

			if (0)// disabled cause its annoying
			{
				// hook with any other weapon than hammer
				if (m_ActiveWeapon != WEAPON_HAMMER)// && m_dum_hookmood)
				{
					m_dum_hook = true;
				}
			}

			if (distance(ent->GetPos(), m_Pos) <= m_dum_range_hook)
			{
				if (!m_dum_hooktimer)
				{
					m_dum_hooktimer = round(max(Server()->TickSpeed() * 0.2f, Server()->TickSpeed() * 1.5f * frandom()));
					m_dum_hook = false;
				}
			}
			else
				m_dum_hook = false;
		}
		else
			m_dum_shootreaction = 0;
	}
	else
		m_dum_shootreaction = 0;

	if(m_dum_hooktimer)
		m_dum_hooktimer--;

	if (m_dum_gundelay)
		m_dum_gundelay--;
	
	if (m_dum_hammerdelay)
		m_dum_hammerdelay--;

	// aim offset
	if (!m_dum_aoffdelay)
	{
		float aoff_max = 0;

		// scale with distance
		if (ent)
			aoff_max = distance(m_Pos, ent->GetPos()) / 3.f;

		// +80% unprecision with rifle
		if (m_ActiveWeapon == WEAPON_LASER)
			aoff_max *= 1.8f;

		m_dum_aoffdelay = max(Server()->TickSpeed() * 0.1f, Server()->TickSpeed() * 0.2f);

		aoff.x = -aoff_max / 2 + aoff_max * frandom();
		aoff.y = -aoff_max / 2 + aoff_max * frandom();
	}
	else
		m_dum_aoffdelay--;
	
	if (canstroll)// walk randomly
	{
		if (!m_dum_walktimer)
		{
			DumChooseNewStroll();
		}
		else
			m_dum_walktimer--;
	}
	else// chase enemy
	{
		m_dum_walktimer = 0;

		if (ent)
			m_dum_walkdir = ent->GetPos().x - m_Pos.x;
	}

	// handle jumping independently
	m_dum_jump = false;

	if (IsGrounded())
		m_dum_hasjumped = false;

	if (GameServer()->Collision()->GetCollisionAt(m_Pos.x + m_dum_walkdir * trigger_jump_range, m_Pos.y) ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x + m_dum_walkdir * trigger_dirchange_range, m_Pos.y))
	{
		if (GameServer()->Collision()->GetCollisionAt(m_Pos.x + m_dum_walkdir * trigger_dirchange_range, m_Pos.y) &&
			IsGrounded())
		{
			DumChooseNewStroll();
		}

		// jump
		if (!m_dum_jump_delay)
		{
			// let them fall down openings
			if (IsGrounded() || m_dum_hasjumped)
			{
				if (IsGrounded())
					m_dum_hasjumped = true;

				m_dum_jump_delay = round(max(Server()->TickSpeed() * 0.3f, Server()->TickSpeed() * 0.5f * frandom()));
				m_dum_jump = true;
			}
		}
	}

	if (m_dum_jump_delay)
		m_dum_jump_delay--;

	// move weapon randomly
	if (canscan)
	{
		float scanrange = 50.f;
		int xspd = 0;
		int yspd = 0;
		int div = 10;

		if (!m_dum_scan_delay)
		{
			m_dum_scan_delay = round(max(Server()->TickSpeed() * 1.f, Server()->TickSpeed() * 1.5f * frandom()));
			m_dum_scanpos.x = -scanrange + scanrange * 2 * frandom();
			m_dum_scanpos.y = -scanrange + scanrange * 2 * frandom();
		}
		else
			m_dum_scan_delay--;

		if (m_dum_scanpos.x > m_dum_direction.x - 1)
			xspd = (m_dum_scanpos.x - m_dum_direction.x) / div;
		else if (m_dum_scanpos.x < m_dum_direction.x + 1)
			xspd = -1;

		if (m_dum_scanpos.y > m_dum_direction.y - 1)
			yspd = 1;
		else if (m_dum_scanpos.y < m_dum_direction.y + 1)
			yspd = -1;

		m_dum_direction.x += xspd;
		m_dum_direction.y += yspd;

		if (m_dum_direction.x > scanrange)
			m_dum_direction.x = scanrange;
		if (m_dum_direction.y > scanrange)
			m_dum_direction.y = scanrange;
		if (m_dum_direction.x < -scanrange)
			m_dum_direction.x = -scanrange;
		if (m_dum_direction.y < -scanrange)
			m_dum_direction.y = -scanrange;
	}

	// point direction
	m_Input.m_TargetX = m_dum_direction.x * 10.f;
	m_Input.m_TargetY = m_dum_direction.y * 10.f - yoff_bdrop;
	m_dum_direction.y -= yoff_bdrop / 10.f;

	// walk direction
	m_Input.m_Direction = m_dum_walkdir;

	// jump input
	m_Input.m_Jump = m_dum_jump;

	// hook input
	m_Input.m_Hook = m_dum_hook;
}

void CCharacter::DumChooseNewStroll()
{
	m_dum_walktimer = round(max(Server()->TickSpeed() * 0.5f, Server()->TickSpeed() * 2.f * frandom()));

	m_dum_walkdir = 1;// walk right by default
	if (frandom() > 0.5f)
		m_dum_walkdir = -1;// walk left by chance
}

vec2 CCharacter::GetMarkVec(float Offset, int Length)
{
	float an = (m_mark_angle + Offset) * (M_PI / 180);
	return vec2(cosf(an), sinf(an) / 3) * Length;
}