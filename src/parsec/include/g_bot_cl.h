#ifndef G_BOT_CL_H_
#define G_BOT_CL_H_

// shared bot types (object_control_s, agentmode_t, UTL_LocomotionController)
#include "g_bot_common.h"

extern int headless_bot;

// simple timeout handling ----------------------------------------------------
//
class UTL_RefFrameTimeout
{
protected:
	refframe_t m_RefFrames;
	refframe_t m_Base;
	refframe_t m_Timeout;

public:
	// ctors
	UTL_RefFrameTimeout()
	{
		m_Timeout   = 0;
		Reset();
	}
	UTL_RefFrameTimeout( refframe_t _Timeout )
	{
		Set( _Timeout );
	}

	// set the timeout value
	void Set( refframe_t _Timeout )
	{
		m_Timeout   = _Timeout;
		Reset();
	}

	// reset timeout
	void Reset()
	{
		m_RefFrames = m_Timeout;
		m_Base      = SYSs_GetRefFrameCount();
	}

	// check for timeout
	bool_t IsTimeout()
	{
		refframe_t diff = SYSs_GetRefFrameCount() - m_Base;
		m_RefFrames -= diff;
		if ( m_RefFrames < 0 ) {
			m_RefFrames += m_Timeout;
			return true;
		} else {
			return false;
		}
	}
};


// do the desired object control (client-side: calls INP_User* input functions)
int	OCT_DoControl( object_control_s* objctl );

// dump an object control to the console
void OCT_Dump( object_control_s* objctl );


// bot character properties ---------------------------------------------------
//
class BOT_Character
{
protected:
	float		m_fPlanInterval;			// planning interval in secs 
	float		m_fGoalCheckInterval;		// goal checking interval in secs
	float		m_fInputChangeInterval;		// input change interval in secs
	float		m_emp_delay;
	float       m_fire_delay;
	float       m_missile_delay;

public:

	//ctor
	BOT_Character();

	// reset to defaults
	void Reset();

	// select the attack target
	ShipObject* SelectAttackTarget( ShipObject* pAttacker );
	ExtraObject* SelectEnergyObject();
	ExtraObject* SelectRepairObject();
	ExtraObject* SelectHomingMissileObject();

	// getters
	float GetPlanInterval_sec() const			{ return m_fPlanInterval; }
	float GetGoalCheckInterval_sec() const		{ return m_fGoalCheckInterval; }
	float GetInputChangeInterval_sec() const	{ return m_fInputChangeInterval; }
	float GetEMPDelay() { return m_emp_delay; };
	void  SetEMPDelay(float emp_delay) {m_emp_delay = emp_delay;};
	float GetFireDelay() { return m_fire_delay; };
	void  SetFireDelay(float fire_delay) { m_fire_delay = fire_delay; };
	float GetMissileDelay() { return m_missile_delay; };
	void  SetMissileDelay(float missile_delay) { m_missile_delay = missile_delay; };

	// friend functions
	friend void RealizeBotChar();
};

// bot goal -------------------------------------------------------------------
//
class BOT_Goal
{
protected:
	Vector3		m_Pos;
	GenObject*  m_pObject;

public:
	BOT_Goal()
	{
		Reset();
	}

	void Reset()
	{
		memset( &m_Pos, 0, sizeof( Vector3 ) );
		m_pObject = NULL;
	}

	// get the position the goal is at
	Vector3* GetGoalPosition()
	{ 
		return &m_Pos; 
	}

	// set/get target object
	void		SetTargetObject( GenObject* pObject ) { m_pObject = pObject; }
	GenObject*	GetTargetObject() const { return m_pObject; }
};


// current state of the bot ---------------------------------------------------
//
class BOT_State
{
protected:
	BOT_Goal			m_CurGoal;
	object_control_s	m_oc;
public:

	BOT_State()
	{
	}

	// reset the dynamic state of the bot
	void Reset( ShipObject* pShip )
	{
		m_CurGoal.Reset();
		memset( &m_oc, 0, sizeof( object_control_s ) );
		m_oc.pShip = pShip;
	}

	BOT_Goal*			GetCurGoal()		{ return &m_CurGoal; }
	object_control_s*	GetObjectControl()	{ return &m_oc; }
};


// main class for handling the bot AI -----------------------------------------
//
class BOT_AI
{
protected:
	agentmode_t			m_nAgentMode;
	BOT_State			m_State;
	BOT_Character		m_Character;
	ShipObject*			m_pShip;

	UTL_RefFrameTimeout	m_PlanTimeout;
	UTL_RefFrameTimeout	m_GoalCheckTimeout;
	UTL_RefFrameTimeout	m_InputTimeout;

	Vector3				m_AgentPos;

public:

	BOT_AI()
	{
		Reset();
	}

	// reset the bot AI
	void Reset()
	{
		//m_nAgentMode	= AGENTMODE_IDLE;
		m_nAgentMode	= AGENTMODE_ATTACK;
		m_pShip         = NULL;
		m_State.Reset( m_pShip );

		m_PlanTimeout.Set	( (refframe_t)m_Character.GetPlanInterval_sec()        * FRAME_MEASURE_TIMEBASE );
		m_GoalCheckTimeout.Set	( (refframe_t)m_Character.GetGoalCheckInterval_sec()   * FRAME_MEASURE_TIMEBASE );
		m_InputTimeout.Set	( (refframe_t)m_Character.GetInputChangeInterval_sec() * FRAME_MEASURE_TIMEBASE );
	}

	// set the ship object we want to control
	void SetShipObject( ShipObject* pShip ) 
	{
		m_pShip = pShip;
		m_State.Reset( pShip );
	}



	// main think function, called every render or sim frame ( client or server )
	void DoThink();

	// get the character of the bot
	BOT_Character* GetCharacter() { return &m_Character; }

protected:

	void _DoPlan();

	void _GoalCheck_AgentMode_Idle();
	void _GoalCheck_AgentMode_Powerup();
	void _GoalCheck_AgentMode_Attack();
	void _GoalCheck_AgentMode_Retreat();
	int _TargetInRange( ShipObject *ship, ShipObject *target, geomv_t range );
	void _SteerToPosition( Vector3* targetPos, object_control_s* pObjctl );
};



// easy access to the singleton -----------------------------------------------
//
#define TheBot		( BOT_ClientSide::GetClientSideBot() )


// forward decls --------------------------------------------------------------
//
PRIVATE int BOT_CallThink( void* param );

// the timeout in refframes to wait between status checks ---------------------
//
#define BOT_STATUS_CHECK_TIMEOUT		DEFAULT_REFFRAME_FREQUENCY

// class for the client-side bot ----------------------------------------------
//
class BOT_ClientSide : public BOT_AI
{
protected:

	// standard ctor
	BOT_ClientSide() :
		 m_Started( FALSE ),
		 m_bWantToBeConnected( TRUE ),
		 m_bWantToBeJoined( TRUE ),
		 m_NextStatusCheckRefFrame( 0 )
	{
		memset( m_szServer, 0, MAX_SERVER_NAME * sizeof( char ) );
	}

	// standard dtor
	~BOT_ClientSide()
	{
	}

	// check whether current status (connected/joined etc. ) matches the desired one
	void _CheckStatus();

protected:
	int			m_Started;
	char		m_szServer[ MAX_SERVER_NAME ];

	int			m_bWantToBeConnected;
	int			m_bWantToBeJoined;

	// next refframe, we check for connect/disconnect/joined/unjoined
	refframe_t	m_NextStatusCheckRefFrame;			

public:
	// SINGLETON pattern
	static BOT_ClientSide* GetClientSideBot()
	{
		static BOT_ClientSide _TheClientSideBot;
		return &_TheClientSideBot;
	}

	// set/get of status flags
	int		GetDesiredConnStatus() const { return m_bWantToBeConnected; }
	int		GetDesiredJoinStatus() const { return m_bWantToBeJoined; }
	void	SetDesiredConnStatus( int bWantToBeConnected )	{ m_bWantToBeConnected = bWantToBeConnected; }
	void	SetDesiredJoinStatus( int bWantToBeJoined )		{ m_bWantToBeJoined = bWantToBeJoined; }

	// overwritten think method to allow special client-side things
	void DoThink();

	// start the bot
	void Start();

	// stop the bot
	void Stop();

	int IsStarted(){
		return m_Started;
	}


	// get the servername the client bot should connect to
	char* GetConnectServer()
	{
		return m_szServer;
	}

	// set the server we want to connect to
	void SetConnectServer( char* pszServer )
	{
		// trim left
		char *p = NULL;
		for( p = pszServer; *p == ' '; p++ );

		if ( strncmp( p, "\"\"", 2 ) == 0 ) {
			m_szServer[ 0 ] = '\0';
		} else {
			strncpy( m_szServer, p, MAX_SERVER_NAME );
		}
	}
};

#endif // G_BOT_CL_H_
