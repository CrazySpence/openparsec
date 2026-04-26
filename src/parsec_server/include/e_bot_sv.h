/*
 * PARSEC HEADER: e_bot_sv.h
 *
 * Server-side bot AI — drives a ship internally without a real network client.
 */

#ifndef E_BOT_SV_H_
#define E_BOT_SV_H_

#include "g_bot_common.h"   // object_control_s, agentmode_t, UTL_LocomotionController

// forward decls --------------------------------------------------------------
//
class G_Player;
class E_SimClientState;


// thresholds (mirror the client-side defines in g_bot_cl.cpp) ----------------
//
#define SV_BOT_REPAIR_LEVEL  0.9f   // retreat when damage exceeds 90 %
#define SV_BOT_ENERGY_LEVEL  0.1f   // retreat when energy drops below 10 %
#define SV_BOT_GMISSL_LEVEL  0.1f   // retreat when guided missiles drop below 10 %


// server-side bot goal -------------------------------------------------------
//
class SV_BotGoal
{
protected:
	Vector3    m_Pos;
	GenObject* m_pObject;

public:
	SV_BotGoal() { Reset(); }

	void Reset()
	{
		memset( &m_Pos, 0, sizeof( Vector3 ) );
		m_pObject = NULL;
	}

	Vector3*   GetGoalPosition()                { return &m_Pos; }
	void       SetTargetObject( GenObject* p )  { m_pObject = p; }
	GenObject* GetTargetObject() const          { return m_pObject; }
};


// per-bot AI state -----------------------------------------------------------
//
class E_BotPlayer
{
protected:

	// player slot and sim state pointers (not owned)
	int               m_nClientID;
	G_Player*         m_pPlayer;
	E_SimClientState* m_pSimState;
	ShipObject*       m_pShip;

	// AI state
	agentmode_t       m_nAgentMode;
	SV_BotGoal        m_Goal;

	// locomotion
	UTL_LocomotionController  m_Loco;
	object_control_s          m_oc;
	Vector3                   m_AgentPos;

	// bot name (for respawn)
	char    m_szName[ 32 ];

	// current tracked speed (fixed_t, managed by bot since sim state carries absolute speed)
	fixed_t m_fCurSpeed;

	// respawn delay: non-zero = refframe at which the bot died; 0 = alive / not waiting
	refframe_t m_DeathRefFrame;

	// per-bot debug logging flag (toggled by sv.bot.debug)
	bool    m_bDebug;
	int     m_nDebugCounter;    // throttle counter for periodic status output

	// weapon / targeting state
	dword   m_nTargetObjNumber;    // HostObjNumber of current target
	int     m_nCurLauncher;        // barrel counter (mirrors G_Player::m_CurLauncher)

	// weapon delay counters (seconds, count down toward 0)
	float   m_fFireDelay;
	float   m_fMissileDelay;
	float   m_fEMPDelay;

public:

	E_BotPlayer()
		: m_nClientID( -1 )
		, m_pPlayer( NULL )
		, m_pSimState( NULL )
		, m_pShip( NULL )
		, m_nAgentMode( AGENTMODE_ATTACK )
		, m_fCurSpeed( 0 )
		, m_DeathRefFrame( 0 )
		, m_bDebug( false )
		, m_nDebugCounter( 0 )
		, m_nTargetObjNumber( 0 )
		, m_nCurLauncher( 0 )
		, m_fFireDelay( 0.0f )
		, m_fMissileDelay( 0.0f )
		, m_fEMPDelay( 0.0f )
	{
		memset( &m_oc, 0, sizeof( object_control_s ) );
		memset( m_szName, 0, sizeof( m_szName ) );
	}

	// initialise the bot for a connected + joined player slot
	void Init( int nClientID, const char* name, G_Player* pPlayer, E_SimClientState* pSimState, ShipObject* pShip );

	// run one AI tick
	void DoThink( refframe_t refframes );

	// return the client slot this bot occupies
	int GetClientID() const { return m_nClientID; }

	// enable / disable debug logging for this bot
	void SetDebug( bool b ) { m_bDebug = b; }
	bool GetDebug() const   { return m_bDebug; }

	// keep alive counter ticking so the server doesn't time out the slot
	void KeepAlive();

protected:

	void _DoPlan();
	void _GoalCheck_Idle();
	void _GoalCheck_Powerup();
	void _GoalCheck_Attack();
	void _GoalCheck_Retreat();

	// convert object_control_s to E_SimClientState delta inputs
	void _ApplyControl( object_control_s* pObjctl, refframe_t refframes );

	// steer toward a world position
	void _SteerToPosition( Vector3* pTargetPos );

	// range check between two ships
	int  _TargetInRange( ShipObject* ship, ShipObject* target, geomv_t range );

	// target selection helpers
	ShipObject*  _SelectAttackTarget();
	ExtraObject* _SelectRepairObject();
	ExtraObject* _SelectEnergyObject();
	ExtraObject* _SelectMissileObject();
};


// manages all server-side bots -----------------------------------------------
//
class E_BotManager
{
protected:
	E_BotPlayer*  m_Bots;
	int           m_nNumBots;

	// how often the bot AI runs (every N sim frames)
	int           m_nThinkInterval;
	int           m_nFrameCounter;

public:
	E_BotManager()
		: m_Bots( NULL )
		, m_nNumBots( 0 )
		, m_nThinkInterval( 5 )    // think at ~10–20 Hz depending on sim rate
		, m_nFrameCounter( 0 )
	{}

	~E_BotManager() { delete[] m_Bots; }

	// add a bot with the given name; returns true on success
	bool AddBot( const char* name );

	// remove all bots in the given client slot
	bool RemoveBot( int nClientID );

	// called every server frame
	void Tick( refframe_t refframes );

	// return current bot count
	int  GetNumBots() const { return m_nNumBots; }

	// enable / disable debug output for the bot in a given client slot
	bool SetBotDebug( int nClientID, bool b );
};


// register sv.bot.add and sv.bot.remove console commands --------------------
// (called from E_GameServer::Init)
//
void SV_BotManager_RegisterCommands();


#endif // E_BOT_SV_H_
