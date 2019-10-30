/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>

#include <game/collision.h>
#include "character.h"
#include "spawnprotecteff.h"

CSeff::CSeff(CGameWorld *pGameWorld, vec2 Pos, int Type)
	: CEntity(pGameWorld, CGameWorld::ENTTYPE_PICKUP, Pos, 0)
{
	m_Type = Type;
	m_Pos = Pos;

	GameWorld()->InsertEntity(this);
}

void CSeff::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

void CSeff::Tick()
{
	// ---
}

void CSeff::TickPaused()
{
	// ---
}

void CSeff::Snap(int SnappingClient)
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
