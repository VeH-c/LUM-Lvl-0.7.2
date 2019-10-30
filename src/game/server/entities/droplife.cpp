/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>

#include <game/collision.h>
#include "character.h"
#include "droplife.h"

CDropLife::CDropLife(CGameWorld *pGameWorld, vec2 Pos, vec2 Pushdir, float Amount, int Type)
	: CEntity(pGameWorld, CGameWorld::ENTTYPE_PICKUP, Pos, PickupPhysSize)
{
	m_Type = Type;
	m_Pushdir = Pushdir;
	m_Amount = Amount;
	m_Falling = 1;
	m_Gravity = GameServer()->m_DropLifeGravity;
	m_FloorTol = 15;
	m_PushTol = m_FloorTol;

	m_Pos = Pos;

	m_LifeTime = Server()->TickSpeed() * GameServer()->m_DropLifeLifeTime;

	GameWorld()->InsertEntity(this);
}

void CDropLife::Tick()
{
	bool Collide = false;

	// Check if a player intersected us
	CCharacter *pChr = (CCharacter *)GameServer()->m_World.ClosestEntity(m_Pos, 20.0f, CGameWorld::ENTTYPE_CHARACTER, 0);

	if(pChr && pChr->IsAlive())
	{
//		char amt[256] = { 0 };
//		str_format(amt, sizeof(amt), "%.2f", m_Amount);
		// player picked us up, is someone was hooking us, let them go
		switch (m_Type)
		{
			case PICKUP_HEALTH:
				if(pChr->IncreaseHealth(m_Amount))
				{
					GameServer()->CreateSound(m_Pos, SOUND_PICKUP_HEALTH);
					GameWorld()->DestroyEntity(this);
//					GameServer()->SendChatTarget(0, amt);
				}
				break;

			case PICKUP_ARMOR:
				if(pChr->IncreaseArmor(m_Amount))
				{
					GameServer()->CreateSound(m_Pos, SOUND_PICKUP_ARMOR);
					GameWorld()->DestroyEntity(this);
//					GameServer()->SendChatTarget(0, amt);
				}
				break;

			default:
				break;
		};
	}

	if (m_Falling == 1)
	{
		while (1)
		{
			Collide = GameServer()->Collision()->GetCollisionAt(m_Pos.x - normalize(m_Pushdir).x * m_PushTol, m_Pos.y - normalize(m_Pushdir).y * m_PushTol);

			if (Collide)
				m_Pos += m_Pushdir;
			else
				break;
		}

		if (sign(m_Velocity.y) != -1)
			Collide = GameServer()->Collision()->GetCollisionAt(m_Pos.x, m_Pos.y + m_FloorTol + m_Velocity.y);

		// drop from gravity
		if (!Collide)
		{
			m_Pos += m_Velocity;
			m_Velocity.y += m_Gravity;
		}
		else
		{
			// feel for surface
			for (int i = 0; i < ceil(m_Velocity.y + 1); ++i)
			{
				Collide = GameServer()->Collision()->GetCollisionAt(m_Pos.x, m_Pos.y + m_FloorTol + i);

				if (Collide)
				{
					m_Pos.y += i;
					break;
				}
			}

			m_Velocity.y = -m_Velocity.y * (GameServer()->m_DropLifeBounceForce / 100.f);
			
			if (abs(m_Velocity.y) <= 1)
				m_Falling = 2;
		}
	}
	
	// push out of floor decently once in the end
	if (m_Falling == 2)
	{
		for (int i = 0; i < 50; ++i)
		{
			// floor
			Collide = GameServer()->Collision()->GetCollisionAt(m_Pos.x, m_Pos.y + m_FloorTol);
			if (Collide)
				m_Pos.y--;
			else
				break;
		}

		m_Falling = 0;
	}

	// destroy after certain time, leaving game layer, touching death tiles
	m_LifeTime--;

	if (m_LifeTime <= 0 || GameLayerClipped(m_Pos) ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x, m_Pos.y + m_FloorTol + 1)&CCollision::COLFLAG_DEATH)
	{
		GameWorld()->DestroyEntity(this);
	}
}

void CDropLife::TickPaused()
{
	++m_LifeTime;
}

void CDropLife::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, GetID(), sizeof(CNetObj_Pickup)));
	if(!pP)
		return;

	pP->m_X = (int)m_Pos.x;
	pP->m_Y = (int)m_Pos.y;
	pP->m_Type = m_Type;
}
