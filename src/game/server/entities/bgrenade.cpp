/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>

#include "character.h"
#include "bgrenade.h"

CBgrenade::CBgrenade(CGameWorld *pGameWorld, int Type, int Owner, vec2 Pos, vec2 Dir, int Span,
		int Damage, bool Explosive, float Force, int SoundImpact, int Weapon, int Bounces)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_PROJECTILE, Pos)
{
	m_Type = Type;
	m_Pos = Pos;
	m_Direction = Dir;
	m_LifeSpan = Span;
	m_Owner = Owner;
	m_Force = Force;
	m_Damage = Damage;
	m_SoundImpact = SoundImpact;
	m_Weapon = Weapon;
	m_StartTick = Server()->Tick();
	m_Explosive = Explosive;
	m_Bounces = Bounces;

	GameWorld()->InsertEntity(this);
}

void CBgrenade::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

vec2 CBgrenade::GetPos(float Time)
{
	float Curvature = 0;
	float Speed = 0;

	switch(m_Type)
	{
		case WEAPON_GRENADE:
			Curvature = GameServer()->Tuning()->m_GrenadeCurvature;
			Speed = GameServer()->Tuning()->m_GrenadeSpeed;
			break;

		case WEAPON_SHOTGUN:
			Curvature = GameServer()->Tuning()->m_ShotgunCurvature;
			Speed = GameServer()->Tuning()->m_ShotgunSpeed;
			break;

		case WEAPON_GUN:
			Curvature = GameServer()->Tuning()->m_GunCurvature;
			Speed = GameServer()->Tuning()->m_GunSpeed;
			break;
	}

	return CalcPos(m_Pos, m_Direction, Curvature, Speed, Time);
}

void CBgrenade::Tick()
{
	float Pt = (Server()->Tick()-m_StartTick-1)/(float)Server()->TickSpeed();
	float Ct = (Server()->Tick()-m_StartTick)/(float)Server()->TickSpeed();
	vec2 PrevPos = GetPos(Pt);
	vec2 CurPos = GetPos(Ct);
	vec2 CurVel = CurPos - PrevPos;
	int Collide = GameServer()->Collision()->IntersectLine(PrevPos, CurPos, &CurPos, 0);
	CCharacter *OwnerChar = GameServer()->GetPlayerChar(m_Owner);
	CCharacter *TargetChr = GameServer()->m_World.IntersectCharacter(PrevPos, CurPos, 6.0f, CurPos, OwnerChar);

	m_LifeSpan--;

	if(TargetChr || Collide || m_LifeSpan < 0 || GameLayerClipped(CurPos))
	{
		// push out of wall
		vec2 Bpos = CurPos;
		vec2 Bvel = CurVel;

		while (GameServer()->Collision()->GetCollisionAt(Bpos.x, Bpos.y))
		{
			Bpos -= normalize(CurVel);
		}

		if (m_Bounces > 0 && m_LifeSpan >= 0)
		{
			m_Bounces--;

			// check where terrain is
			if (GameServer()->Collision()->GetCollisionAt(Bpos.x + 2 * sign(Bvel.x), Bpos.y))
				Bvel.x *= -1;

			if (GameServer()->Collision()->GetCollisionAt(Bpos.x, Bpos.y + 2 * sign(Bvel.y)))
				Bvel.y *= -1;

			if (GameServer()->Collision()->GetCollisionAt(Bpos.x + 2 * sign(Bvel.x), Bpos.y + 2 * sign(Bvel.y)) ||
				TargetChr)
			{
				Bvel.x *= -1;
				Bvel.y *= -1;
			}

			CBgrenade *pProj = new CBgrenade(GameWorld(), WEAPON_GRENADE,
				m_Owner,
				Bpos,
				normalize(vec2(Bvel.x, Bvel.y)),
				(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GrenadeLifetime),
				m_Damage, true, 0, SOUND_GRENADE_EXPLODE, WEAPON_GRENADE, m_Bounces);

			// pack the Projectile and send it to the client Directly
			CNetObj_Projectile p;
			pProj->FillInfo(&p);

			CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
			Msg.AddInt(1);
			for (unsigned i = 0; i < sizeof(CNetObj_Projectile) / sizeof(int); i++)
				Msg.AddInt(((int *)&p)[i]);
			Server()->SendMsg(&Msg, 0, m_Owner);
		}
		
		if (m_Explosive)
			GameServer()->CreateExplosionExt(Bpos, m_Owner, m_Weapon, m_Damage, (int)m_Force, 1, true);
		else if (TargetChr)
		{
			TargetChr->ImpulseAdd(m_Direction * max(0.001f, m_Force));
			TargetChr->TakeDamage(m_Damage, m_Owner, m_Weapon);
		}

		GameServer()->m_World.DestroyEntity(this);
	}
}

void CBgrenade::TickPaused()
{
	++m_StartTick;
}

void CBgrenade::FillInfo(CNetObj_Projectile *pProj)
{
	pProj->m_X = (int)m_Pos.x;
	pProj->m_Y = (int)m_Pos.y;
	pProj->m_VelX = (int)(m_Direction.x*100.0f);
	pProj->m_VelY = (int)(m_Direction.y*100.0f);
	pProj->m_StartTick = m_StartTick;
	pProj->m_Type = m_Type;
}

void CBgrenade::Snap(int SnappingClient)
{
	float Ct = (Server()->Tick()-m_StartTick)/(float)Server()->TickSpeed();

	if(NetworkClipped(SnappingClient, GetPos(Ct)))
		return;

	CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, GetID(), sizeof(CNetObj_Projectile)));
	if(pProj)
		FillInfo(pProj);
}
