#ifndef E_SIMNETINPUT_H_
#define E_SIMNETINPUT_H_

// forward decls --------------------------------------------------------------
//
class E_REList;
//struct RE_Header;
//struct ShipRemInfo;

// Pending helix particle record — filled during ProcessInputREList(), flushed
// into the particle system by FlushPendingHelixParticles() just before
// _MaintainSimulation so that PRT_NewCluster runs at the normal frame time.
// Uses float directly (geomv_t = float) to avoid pulling in net_defs.h.
struct PendingHelixParticle {
	int   playerid;
	float x, y, z;
	float vx, vy, vz;
	int   rtt_ms;
};

#define MAX_PENDING_HELIX 64   // max helix REs buffered per frame


// class for handling the network input from the simulation -------------------
//
class E_SimNetInput {
protected:
	E_REList*	m_pInputREList;

	// helix particles collected during ProcessInputREList(), applied later
	PendingHelixParticle m_PendingHelix[ MAX_PENDING_HELIX ];
	int                  m_nPendingHelix;

protected:
	// default ctor/dtor
	E_SimNetInput();
	~E_SimNetInput();

public:
	// SINGLETON access
	static E_SimNetInput* GetSimNetInput()
	{
		static E_SimNetInput _TheSimNetInput;
		return &_TheSimNetInput;
	}

	// reset all data
	void Reset();

	// get the input remote event list
	//E_REList* GetInputREList() { return m_pInputREList; }

	// walk the input RE queue and apply events/states to simulation
	void ProcessInputREList();

	// create the helix collision particles that were buffered during
	// ProcessInputREList().  Call this just before _MaintainSimulation().
	void FlushPendingHelixParticles();

	// handle the network input from a client
	int HandleOneClient( int nClientID, RE_Header* relist );

protected:
	// check absolute movement bounds
	int _CheckAbsoluteMovementBounds( RE_PlayerAndShipStatus* pPAS );
};

#endif // !E_SIMNETINPUT_H_

