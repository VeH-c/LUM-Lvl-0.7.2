/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>

#include "character.h"
#include "hammermine.h"

CMine::CMine(CGameWorld *pGameWorld, int Type, int Owner, vec2 Pos, vec2 Dir, int Span,
		int Damage, bool Explosive, float Force, int SoundImpact, int Weapon)
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
	m_Radius = GameServer()->m_MineRadius;

	GameWorld()->InsertEntity(this);
}

void CMine::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

void CMine::Tick()
{
	int Collide = GameServer()->Collision()->GetCollisionAt(m_Pos.x, m_Pos.y);

	CCharacter *OwnerChar = GameServer()->GetPlayerChar(m_Owner);
	// Check if a player intersected us
	CCharacter *TargetChr = (CCharacter *)GameServer()->m_World.ClosestEntity(m_Pos, 20.0f, CGameWorld::ENTTYPE_CHARACTER, OwnerChar);

	m_LifeSpan--;

	if(TargetChr || Collide || m_LifeSpan < 0 || GameLayerClipped(m_Pos))
	{
		if(m_Explosive)
			GameServer()->CreateExplosionExt(m_Pos, m_Owner, m_Weapon, m_Damage, 0, 1, true);
		else if (TargetChr)
		{
			TargetChr->ImpulseAdd(m_Direction * max(0.001f, m_Force));
			TargetChr->TakeDamage(m_Damage, m_Owner, m_Weapon);
		}

		GameServer()->m_World.DestroyEntity(this);
	}
}

void CMine::TickPaused()
{
	++m_StartTick;
}

void CMine::FillInfo(CNetObj_Projectile *pProj)
{
	pProj->m_X = (int)m_Pos.x;
	pProj->m_Y = (int)m_Pos.y;
	pProj->m_VelX = 0;
	pProj->m_VelY = 0;
	pProj->m_StartTick = m_StartTick;
	pProj->m_Type = m_Type;
}

void CMine::Snap(int SnappingClient)
{
	float Ct = (Server()->Tick()-m_StartTick)/(float)Server()->TickSpeed();

	if(NetworkClipped(SnappingClient, m_Pos))
		return;

	CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, GetID(), sizeof(CNetObj_Projectile)));
	if(pProj)
		FillInfo(pProj);
}
