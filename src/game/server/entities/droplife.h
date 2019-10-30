/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_DROPLIFE_H
#define GAME_SERVER_ENTITIES_DROPLIFE_H

#include <game/server/entity.h>

const int PickupPhysSize = 14;

class CDropLife : public CEntity
{
public:
	CDropLife(CGameWorld *pGameWorld, vec2 Pos, vec2 Pushdir, float Amount, int Type);

	virtual void Tick();
	virtual void TickPaused();
	virtual void Snap(int SnappingClient);

	float m_Amount;// amount of life / armor gained
	int m_Falling;// falling

private:
	vec2 m_Velocity;
	vec2 m_Pushdir;
	float m_Gravity;
	int m_Type;
	int m_LifeTime;
	int m_FloorTol;
	int m_PushTol;
};

#endif
