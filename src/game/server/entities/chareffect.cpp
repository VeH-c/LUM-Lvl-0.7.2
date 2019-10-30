/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <generated/server_data.h>
#include <game/server/gamecontext.h>

#include "chareffect.h"

CEff::CEff(CGameWorld *pGameWorld, vec2 Pos)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, Pos)
{
	m_Pos = Pos;
	m_EvalTick = 0;

	GameWorld()->InsertEntity(this);
}

void CEff::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

void CEff::Tick()
{
	// ---
}

void CEff::TickPaused()
{
	++m_EvalTick;
}

void CEff::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient) && NetworkClipped(SnappingClient, m_Pos))//m_From))
		return;

	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, GetID(), sizeof(CNetObj_Laser)));
	if(!pObj)
		return;

	pObj->m_X = (int)m_Pos.x;
	pObj->m_Y = (int)m_Pos.y;
	pObj->m_FromX = (int)m_Pos.x;// (int)m_From.x;
	pObj->m_FromY = (int)m_Pos.y;// (int)m_From.y;
	pObj->m_StartTick = m_EvalTick;
}
