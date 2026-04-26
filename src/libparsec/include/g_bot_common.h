/*
 * PARSEC HEADER: g_bot_common.h
 *
 * Shared bot AI types — included by both the client-side bot (g_bot_cl.h) and
 * the server-side bot (e_bot_sv.h).
 */

#ifndef G_BOT_COMMON_H_
#define G_BOT_COMMON_H_


// object control direction constants -----------------------------------------
//
#define OCT_PITCH_UP    -1
#define OCT_PITCH_DOWN  +1
#define OCT_YAW_LEFT    +1
#define OCT_YAW_RIGHT   -1
#define OCT_ROLL_LEFT   +1
#define OCT_ROLL_RIGHT  -1
#define OCT_ACCELERATE  +1
#define OCT_DECELERATE  -1


// object control structure ---------------------------------------------------
// Normalized inputs in range [-1, 1] for each axis plus accel/decel.
//
struct object_control_s
{
	ShipObject* pShip;

	float		rot_x;	// pitch:  -1 = divedown,  +1 = pullup
	float		rot_y;	// yaw:    -1 = left,      +1 = right
	float		rot_z;	// roll:   -1 = right,     +1 = left
	float		accel;	// speed:  +1 accelerate,  -1 decelerate

	bool_t IsPitch() const	{ return rot_x != 0.0f; }
	bool_t IsYaw()   const	{ return rot_y != 0.0f; }
	bool_t IsRoll()  const	{ return rot_z != 0.0f; }
	bool_t IsMove()  const	{ return accel != 0.0f; }
};


// agent mode enum ------------------------------------------------------------
//
enum agentmode_t {
	AGENTMODE_IDLE		= 1,
	AGENTMODE_POWERUP	= 2,
	AGENTMODE_ATTACK	= 3,
	AGENTMODE_RETREAT	= 4,

	AGENTMODE_MAX		= AGENTMODE_RETREAT
};


// locomotion controller — steers ship toward a desired velocity vector -------
//
class UTL_LocomotionController
{
protected:
	int		m_nRelaxedHeadingAngle;	// within this cone (deg) no corrective turn
	int		m_nFullSpeedHeading;	// within this cone (deg) go full speed
	float	m_fMinSpeedTurn;		// minimum speed to maintain during sharp turns

public:
	UTL_LocomotionController()
		: m_nRelaxedHeadingAngle( 5 )
		, m_nFullSpeedHeading( 30 )
		, m_fMinSpeedTurn( FIXED_TO_FLOAT( 500 ) )
	{}

	void SetRelaxedHeadingAngle( int n ) { m_nRelaxedHeadingAngle = n; }
	void SetFullSpeedHeading( int n )    { m_nFullSpeedHeading = n; }
	void SetMinSpeedTurn( fixed_t s )    { m_fMinSpeedTurn = FIXED_TO_FLOAT( s ); }

	// Fill pObjctl with rotation/accel commands to steer toward pDesiredVelocity
	// at _DesiredSpeed.  pObjctl->pShip must point to the ship being steered.
	void ControlOjbect( object_control_s* pObjctl,
	                    Vector3*          pDesiredVelocity,
	                    fixed_t           _DesiredSpeed );
};


#endif // G_BOT_COMMON_H_
