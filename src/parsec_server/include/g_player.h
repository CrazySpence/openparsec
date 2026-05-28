// class holding all GAME specific player information -------------------------
//
class G_Player
{
	//NOTE: this class mirrors all information/actions of a single client ( in the client code )

protected:

	int			m_nKills;				// # of ships this player killed this session
	int			m_nDeaths;				// # of deaths for this player this session
	int			m_nPoints;				// # of points for this player
	int			m_nUniverseKillsAtJoin;	// kills carried in from other universe servers
	int			m_nUniverseDeathsAtJoin;// deaths carried in from other universe servers
	bool		m_bUniverseKillsCredited; // true once UNIV_PLAYER has been applied this round
	int			m_nLastUnjoinFlag;		// the last unjoin flag
	int			m_nLastKiller;			// the playerid of last killer
	int			m_nLastWeapon;			// the weapon type used to kill this player

	refframe_t	m_FireDisableFrames;	// refframes gun-fire is disabled                  ( = G_GLOBAL::FireDisable	in old CLIENT code )
	refframe_t  m_MissileDisableFrames;	// refframes missile-fire is disabled              ( = G_GLOBAL::MissileDisable	in old CLIENT code )
    refframe_t	helix_refframes_delta;
	int 		m_CurGun;				// currently selected gun outlet
	int 		m_CurLauncher;			// currently selected missile outlet

	int			m_nClientID;

	E_SimPlayerInfo* m_pSimPlayerInfo;
protected:

	// create laser originating from player ship
	void _OBJ_ShootLaser();

    // create a dumb missle originating from player ship
	void _OBJ_LaunchMissile();
	
	// create a homing missle originating from player ship
	void _OBJ_LaunchHomingMissile( dword launcher, dword targetid );

	void _OBJ_LaunchMine();

	void _OBJ_LaunchSwarm( dword targetid );


public:
	G_Player()
	{
		Reset();
	}

	// reset all fields to defaults ( not connected )
	void Reset();

	// set the player status to connected
	void Connect( int nClientID );

	// set the player status to disconnected
	void Disconnect();

	// user fired laser 
	void FireLaser();
    
    // user fired Helix cannon
    void FireHelix();

    // user fired Lightning cannon
    void FireLightning();

    // user fired Photon cannon
    void FirePhoton();

	// user launched a dumb missle
	void LaunchMissile();
	
	// user launched a homping missle
	void LaunchHomingMissile(dword launcher, dword targetid);

	// user launched a mine
	void LaunchMine();

	// user launched a swarm
	void LaunchSwarm(dword targetid);

	// user fired emp.
	// broadcastToClients: send RE_CreateEmp multicast so all clients render
	// the visual effect. Pass true for server-internal bots (no network path
	// handles the broadcast for them). Pass false (default) when the caller
	// already multicasts the original RE — e.g. e_simnetinput.cpp.
	void FireEMP(byte Upgradelevel, bool broadcastToClients = false);

	// record a kill
	void RecordKill();

	// record a death
	void RecordDeath( int nClientID_Killer, int nWeapon );

	// reset any death info
	void ResetDeathInfo();

	// reset all game variables
	void ResetGameVars();

	// maintain weapon firing delays
	void MaintainWeaponDelays();

	// return the ship object assigned to the player
	ShipObject* GetShipObject();

	// return # of kills for the player
	int GetKills() { return m_nKills; }
	// total kills including kills carried from other universe servers
	int GetTotalUniverseKills()  { return m_nKills + m_nUniverseKillsAtJoin; }
	int GetTotalUniverseDeaths() { return m_nDeaths + m_nUniverseDeathsAtJoin; }
	// true once UNIV_PLAYER credit has been applied this round; prevents respawn re-query
	bool IsUniverseKillsCredited() const { return m_bUniverseKillsCredited; }

	// apply universe kills credited from master on join (once per round)
	void SetUniverseKillsAtJoin( int kills, int deaths )
	{
		m_nUniverseKillsAtJoin    = kills;
		m_nUniverseDeathsAtJoin   = deaths;
		m_bUniverseKillsCredited  = true;
	}

	// return the last unjoin flag
	int GetLastUnjoinFlag() { return m_nLastUnjoinFlag; }

	// return the player id that last killed this player
	int GetLastKiller() { return m_nLastKiller; }

	// return the weapon type used to kill this player last
	int GetLastWeapon() { return m_nLastWeapon; }
};

