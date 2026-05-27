/*
 * PARSEC - Main Server Functions
 *
 * $Author: uberlinuxguy $ - $Date: 2004/09/26 03:43:46 $
 *
 * Orginally written by:
 *   Copyright (c) Clemens Beer        <cbx@parsec.org>   2001
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */ 

// C library
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/timeb.h>
#include <unistd.h>
#include <math.h>

// compilation flags/debug support
#include "config.h"
#include "debug.h"

// general definitions
#include "general.h"
#include "objstruc.h"

// global externals
#include "globals.h"

// subsystem headers
#include "net_defs.h"
//FIXME: ????
#include "sys_refframe_sv.h"

// UNP header
#include "net_wrap.h"

// server defs
#include "e_defs.h"

// net game header
#include "net_game_sv.h"

// mathematics header
#include "utl_math.h"

// local module header
#include "e_gameserver.h"
// MasterServer object
#include "MasterServer.h"


// proprietary module headers
#include "con_arg.h"
#include "con_aux_sv.h"
#include "con_com_sv.h"
#include "con_main_sv.h"
#include "e_colldet.h"
#include "g_extra.h"
#include "inp_main_sv.h"
#include "net_csdf.h"
#include "net_udpdriver.h"
#include "net_util.h"
#include "net_packetdriver.h"
#include "obj_clas.h"
//#include "e_stats.h"
#include "g_main_sv.h"
#include "e_connmanager.h"
#include "e_packethandler.h"
#include "e_simulator.h"
#include "e_simplayerinfo.h"
#include "g_player.h"
#include "e_relist.h"
#include "e_simnetinput.h"
#include "e_simnetoutput.h"
#include "sys_refframe_sv.h"
#include "sys_util_sv.h"


// defaults -------------------------------------------------------------------
//
#define DEFAULT_SIM_FREQUENCY					100
#define DEFAULT_SERVER_TO_CLIENT_HEARTBEAT		FRAME_MEASURE_TIMEBASE * 2
#define DEFAULT_MASTERSERVER_INTERVAL			DEFAULT_REFFRAME_FREQUENCY * 10
#define DEFAULT_MASTERSERVER_NAME				"master.openparsec.com"
//#define DEFAULT_MASTERSERVER_NAME				"drax"
//#define DEFAULT_MASTERSERVER_NAME				"192.168.1.102"


// console texts --------------------------------------------------------------
//
static char error_resolving_masterserver[]      = "error resolving masterserver hostname.";


// global accessor for the server-side bot manager ----------------------------
//
E_BotManager* SV_GetBotManager()
{
	return E_GameServer::GetGameServer()->GetBotManager();
}


// ----------------------------------------------------------------------------
// ServerConfig methods 
// ----------------------------------------------------------------------------

// standard ctor --------------------------------------------------------------
//
ServerConfig::ServerConfig()
{
	SetServername( "Unnamed" );
	m_SimFrequency		= 0;
	m_nMaxNumClients	= 0;
	m_ServerIsMaster    = 0;
	m_ClientUpdateHeartbeat = 2 * DEFAULT_REFFRAME_FREQUENCY;
}

void ServerConfig::SetServerIsMaster(bool isMaster){
	m_ServerIsMaster = isMaster;

}
bool ServerConfig::GetServerIsMaster(){
	return m_ServerIsMaster;
}

// set the # of max players ---------------------------------------------------
// 
bool_t ServerConfig::SetMaxNumClients( int nMaxNumClients )		
{ 
	// we only accept setting the max # of clients once
	if ( m_nMaxNumClients == 0 ) {
		ASSERT( m_nMaxNumClients <= MAX_NET_ALLOC_SLOTS );
		m_nMaxNumClients = nMaxNumClients;

		// reset the simulation engine
		TheSimulator->Reset();

		return true;
	} else {
		return false;
	}
}


// return the max # of players ------------------------------------------------
//
int ServerConfig::GetMaxNumClients()
{
	if ( m_nMaxNumClients == 0 ) {
		// defaults to the upper bound 
		return MAX_NET_ALLOC_SLOTS;
	}
	return m_nMaxNumClients;
}


// set the simulation frequency -----------------------------------------------
//
bool_t ServerConfig::SetSimFrequency( int nSimFrequency ) 
{ 
	// we only accept setting the max # of clients once
	if ( m_SimFrequency == 0 ) {
		m_SimFrequency = nSimFrequency; 
		return true;
	} else {
		return false;
	}
}


// return the simulation frequency --------------------------------------------
//
int	ServerConfig::GetSimFrequency() 
{ 
	// ensure this is set to the default
	if ( m_SimFrequency == 0 ) {
		m_SimFrequency = DEFAULT_SIM_FREQUENCY;
	}
	return m_SimFrequency; 
}


// ----------------------------------------------------------------------------
// E_GameServer methods 
// ----------------------------------------------------------------------------

// default ctor ---------------------------------------------------------------
//
E_GameServer::E_GameServer() :
m_bQuit( false )
{
	m_nTransitPending    = 0;
	m_nUnivQueryPending  = 0;
}

// default dtor ---------------------------------------------------------------
//
E_GameServer::~E_GameServer()
{
}

// init data prior to running the console script ------------------------------
//
int E_GameServer::_InitPreConsoleScript()
{
	//NOTE: init all vars that are not modifiable through the console script,
	//      or set the defaults for the ones that are.

	// default values for user configurable values
	m_nInterface				= 0;
	m_MaintainFrequency			= 10;
	m_MasterServer_FrameTime	= DEFAULT_MASTERSERVER_INTERVAL;
	m_nPacketAverageSecs		= 3;
	strncpy( m_MasterServer_Hostname, DEFAULT_MASTERSERVER_NAME, MAX_MASTERSERVER_NAME );
	m_MasterServer_Hostname[ MAX_MASTERSERVER_NAME ] = 0;
	m_nNumServerLinks			= 0;
	m_nNumTeleporters			= 0;

	// non modifiable
	m_nServerFrame				= 0;
	m_CurServerRefFrame			= REFFRAME_INVALID;
	m_LastServerRefFrame		= REFFRAME_INVALID;

	return TRUE;
}


// init data post running the console script ----------------------------------
//
int	E_GameServer::_InitPostConsoleScript()
{
	//NOTE: init all vars, that depend on settings set via the boot_sv.con 
	//      console script

	// idle for max. one simframe length
	m_ServerIdleTime_msec		= ( 1000 / GetSimFrequency() );

	// must be done before g_RefFrameCount is used the first time ( FRAME_MEASURE_TIMEBASE )
	SYSs_InitRefFrameCount();

	// default values for internal data
	m_Maintain_FrameTime		= FRAME_MEASURE_TIMEBASE / m_MaintainFrequency;
	m_SimTick_FrameTime			= FRAME_MEASURE_TIMEBASE / GetSimFrequency();
	
	m_MaintainFrameBase			= SYSs_GetRefFrameCount();
	m_MasterServerFrameBase		= SYSs_GetRefFrameCount();

	return TRUE;
}


// initialize the server components -------------------------------------------
//
int	E_GameServer::Init()
{
	//NOTE: some of the globals must already exist when parsing the console scripts

	// init all the modules
	TheModuleManager->InitAllModules();

	// Only init the game if I am not a master server...
	if(!this->GetServerIsMaster()){
		// init the Game globals
		TheGame				= G_Main::GetGame();
		TheGameInput		= G_Input::GetGameInput();
		TheGameExtraManager	= G_ExtraManager::GetExtraManager();
		TheGameCollDet		= G_CollDet::GetGameCollDet();

		// get the World global
		TheWorld = E_World::GetWorld();
	} 

	// init the network simulation engine, needed for master and game server
	TheSimNetInput	= E_SimNetInput::GetSimNetInput();
	TheSimNetOutput	= E_SimNetOutput::GetSimNetOutput();

	// register all AUX/SV vars (needed on both game server and master server)
	CON_AUX_SV_Register();

	// init the simulation engine only if we are not running a master server.
	if(!this->GetServerIsMaster()){
		TheSimulator	= E_Simulator::GetSimulator();

		// register server-side bot console commands
		SV_BotManager_RegisterCommands();
	}
#ifndef _DISABLE_SCREEN_OUTPUT
	// init CURSES
	CON_InitCurses();
#endif
	// init the input system
	INP_Init();

	// init all data not depending on the console script to be run
	_InitPreConsoleScript();

	// print the copyright
	PrintCopyRight();

	// init the console
	CON_InitConsole();

	if(this->GetServerIsMaster()) {
		MSGOUT("--- Open Parsec Master Server Running");
	}

	// init all data depending on console modifiable vars
	_InitPostConsoleScript();

	// if we are in master server mode, set some default values.
	if(this->GetServerIsMaster()){
		char tmp_srvname[MAX_SERVER_NAME + 1] = "";
		gethostname(tmp_srvname, MAX_SERVER_NAME);
		SV_SERVERID = 1; //master server is always 1
		SV_NETCONF_PORT = 6580; // master server port
		SV_MASTERSERVER_SENDHEARTBEAT = FALSE; // disable heartbeat sending.
	}

	// init the UDP driver
	TheUDPDriver = NET_UDPDriver::GetUDPDriver();
	TheUDPDriver->InitDriver( NULL, SV_NETCONF_PORT );

	// init the packet driver
	ThePacketDriver = NET_PacketDriver::GetPacketDriver();
	
	// init the packet handler
	ThePacketHandler = E_PacketHandler::GetPacketHandler();

	// init the connection manager
	TheConnManager = E_ConnManager::GetConnManager();

	// init the statistics manager
	//E_StatsManager* pStatsManager = TheStatsManager;

	// set to illegal challenge 
	m_MasterServer_Challenge = 0;

	// master server not yet resolved
	m_bMasterServer_NodeValid = false;

	MSGOUT("\n\nOpenParsec Server\n");
	MSGOUT("Network Protocol: %i.%i", CLSV_PROTOCOL_MAJOR, CLSV_PROTOCOL_MINOR);

	// init the game, only if in game server mode.
	if(!this->GetServerIsMaster()){
		TheGame->Init();
		TheWorld->InitParticleSystem();
	}
	return TRUE;
}

// kill the server ------------------------------------------------------------
//
int	E_GameServer::Kill()
{
	// kill the console
	CON_KillConsole();

	// kill the input system
	INP_Kill();

	// kill CURSES
	CON_KillCurses();

	return TRUE;
}

// parse relevant commands from commandline -----------------------------------
//
int	E_GameServer::ParseCommandLine( int argc, char** argv )
{
	// set the programname
	sys_ProgramName = argv[ 0 ];

	// Parse the incoming commandline options and do the stuff.
	opterr = 0;
	int optchar= getopt(argc, argv, "m");

	while(optchar != -1){

		switch(optchar){
			case 'm':
				// TODO: set the option in server config to say we are a master server
				this->m_ServerIsMaster = 1;
				break;
			case '?':
				// unknown option.
				// TODO: print error(maybe) but continue...
				break;
			default:
				break;
		}
		optchar = getopt(argc, argv, "m");

	}

	return TRUE;
}


// ----------------------------------------------------------------------------
//
int	E_GameServer::PrintCopyRight()
{
	MSGOUT( "\n" );
	MSGOUT( " PARSEC SERVER build " SERVER_BUILD_NUMBER );
	MSGOUT( "\n" );
	MSGOUT( "See LICENSE file for licensing information.         ");
	/*MSGOUT( " Copyright (c) 1996-2002 by Alex Mastny, Andreas Varga," );
	MSGOUT( " Clemens Beer, Markus Hadwiger, Stefan Poiss, Michael Woegerbauer." );
	MSGOUT( " All Rights Reserved." );*/
	MSGOUT( "-------------------------------------------------------" );
	MSGOUT( "               http://www.openparsec.com               " );
	MSGOUT( "-------------------------------------------------------" );
	MSGOUT( "\n" );

	return TRUE;
}


// ----------------------------------------------------------------------------
//
int	E_GameServer::PrintUsage()
{
	PrintCopyRight();

	return TRUE;
}


//FIXME_OSS: move this to seperate module as this is not yet used

// constant tick function pointer type ----------------------------------------
//
typedef int (*ConstTickedFunction)( refframe_t now );

// class for ensuring constant calls to a tick-function -----------------------
//
class PU_ConstTicker
{
protected:
	refframe_t			m_CurRefFrame;
	refframe_t			m_LastRefFrame;
	refframe_t			m_TickRefFrames;
	ConstTickedFunction	m_pTickedFunction;

public:

	// standard ctor
	//
	PU_ConstTicker( refframe_t _TickRefFrames, ConstTickedFunction _TickedFunction )
	{
		ASSERT( _TickedFunction != NULL );
		ASSERT( _TickRefFrames  > 0 );

		m_CurRefFrame		= REFFRAME_INVALID;
		m_LastRefFrame		= REFFRAME_INVALID;
		m_TickRefFrames		= _TickRefFrames;

		m_pTickedFunction	= _TickedFunction;
	}

	// maintain the tick function to achieve as many ticks as necessary between last call and current call
	//
	void MaintainTickFunction()
	{
		// first call ?
		if ( m_LastRefFrame == REFFRAME_INVALID ) {
			m_LastRefFrame = m_CurRefFrame;
		}

		// try to catch up as many ticks as we need to get under the frametime for one tick
		// the excess is left in m_LastRefFrame and accumulated in the next tick
		while( ( m_CurRefFrame - m_LastRefFrame ) >= m_TickRefFrames ) {
			m_LastRefFrame += m_TickRefFrames;

			// call the tick function
			m_pTickedFunction( m_LastRefFrame );
		}
	}

	// set the current refframe
	//
	void SetNow( refframe_t now )
	{
		m_CurRefFrame = now;
	}
};


// maintain the simulation ----------------------------------------------------
//
void E_GameServer::_MaintainSimulation()
{
	if ( m_LastServerRefFrame == REFFRAME_INVALID ) {
		m_LastServerRefFrame = m_CurServerRefFrame;
	}

	// catch up m_LastServerRefFrame in m_SimTick_FrameTime steps and run the simulation
	// remainging excess will be stored in m_LastServerRefFrame
	while ( ( m_CurServerRefFrame - m_LastServerRefFrame ) >= m_SimTick_FrameTime ) {
		m_LastServerRefFrame += m_SimTick_FrameTime;
		
		//MSGOUT( "%d: TheSimulator->DoSim( %d ), cur: %d diff: %d", TheSimulator->GetSimFrame(), m_LastServerRefFrame, m_CurServerRefFrame, m_CurServerRefFrame - m_LastServerRefFrame );
		// run the simulation frame/tick
		TheSimulator->DoSim( m_LastServerRefFrame );
		//LOGOUT(( "DoSim(): SimFrame:%d m_LastServerRefFrame: %d, m_nServerFrame: %d", TheSimulator->GetSimFrame(), m_LastServerRefFrame, m_nServerFrame ));
	}
}


// add a serverlink -----------------------------------------------------------
// 
int E_GameServer::AddServerLink( int serverid, Vector3* pos_spec, Vector3* dir_spec )
{
	ASSERT( serverid > 0 );
	ASSERT( pos_spec != NULL );
	ASSERT( dir_spec != NULL );

	if ( m_nNumServerLinks >= MAX_NUM_LINKS ) {
		return FALSE;
	}

	// norm the direction
	NormVctX( dir_spec );

	m_ServerLinks[ m_nNumServerLinks ].m_serverid = serverid;

	m_ServerLinks[ m_nNumServerLinks ].m_pos.X = pos_spec->X;
	m_ServerLinks[ m_nNumServerLinks ].m_pos.Y = pos_spec->Y;
	m_ServerLinks[ m_nNumServerLinks ].m_pos.Z = pos_spec->Z;

	m_ServerLinks[ m_nNumServerLinks ].m_dir.X = dir_spec->X;
	m_ServerLinks[ m_nNumServerLinks ].m_dir.Y = dir_spec->Y;
	m_ServerLinks[ m_nNumServerLinks ].m_dir.Z = dir_spec->Z;

	// create the corresponding stargate
	TheGame->CreateStargate( serverid, pos_spec, dir_spec );

	m_nNumServerLinks++;

	return TRUE;
}

// add a Teleporter -----------------------------------------------------------
//
int E_GameServer::AddTeleporter( Vector3* pos_spec,Vector3* expos_spec, float start_rot_phi, float start_rot_theta, float exit_rot_phi, float exit_rot_theta )
{
	ASSERT( pos_spec != NULL );

	ASSERT( expos_spec != NULL );



	if ( m_nNumTeleporters >= MAX_NUM_TELEP ) {
		return FALSE;
	}

	// create the corresponding teleporter
	Teleporter *teleporter = TheGame->CreateTeleporter( m_nNumTeleporters, pos_spec, expos_spec, start_rot_phi, start_rot_theta,  exit_rot_phi,  exit_rot_theta );
	if(teleporter != NULL){
		m_Teleporters[m_nNumTeleporters] = teleporter;
		MSGOUT("Created Teleporter with ID %i", m_nNumTeleporters);
		m_nNumTeleporters++;
		return TRUE;
	}
	return FALSE;
}

// Mod a Teleporter -----------------------------------------------------------
//
int E_GameServer::ModTeleporter( int id,  Vector3* pos_spec, Vector3* expos_spec, float start_rot_phi, float start_rot_theta, float exit_rot_phi, float exit_rot_theta )
{
	if ( id < 0 ) {
		return FALSE;
	}

	// create the corresponding teleporter
	Teleporter *teleporter = this->m_Teleporters[id];


	if(teleporter != NULL){

		// change the stuff
		if(pos_spec != NULL) {
			teleporter->start.X = pos_spec->X;
			teleporter->start.Y = pos_spec->Y;
			teleporter->start.Z = pos_spec->Z;

		}

		if(expos_spec != NULL) {
			teleporter->exit_delta_x = expos_spec->X;
			teleporter->exit_delta_y = expos_spec->Y;
			teleporter->exit_delta_z = expos_spec->Z;

		}

		// change the direction rotations of the start and end points, if needed.
		if(exit_rot_phi >= 0)
			teleporter->exit_rot_phi = exit_rot_phi;
		if(exit_rot_theta >= 0)
			teleporter->exit_rot_theta = exit_rot_theta;
		if(start_rot_phi >= 0)
			teleporter->start_rot_phi = start_rot_phi;
		if(start_rot_theta >= 0)
			teleporter->start_rot_theta = start_rot_theta;

		TeleporterPropsChanged(teleporter);


		// attach the created E_Distributable for the engine object
		// stargates are to be delivered reliable
		teleporter->pDist = TheSimNetOutput->CreateDistributable( teleporter, TRUE );
		/*
		// modify the teleporter.
		TheGame->ModTeleporter(Teleporter teleporter );*/
		MSGOUT("Modified Teleporter with ID %i", m_nNumTeleporters);
		return TRUE;
	}
	return FALSE;
}


// set the new masterserver info ----------------------------------------------
//
void E_GameServer::SetMasterServerInfo( char* pHostname, refframe_t _MasterServer_FrameTime )
{
	ASSERT( pHostname != NULL );
	ASSERT( _MasterServer_FrameTime >= 0 );

	m_MasterServer_FrameTime = _MasterServer_FrameTime;
	strncpy( m_MasterServer_Hostname, pHostname, MAX_MASTERSERVER_NAME );
	m_MasterServer_Hostname[ MAX_MASTERSERVER_NAME ] = 0;
}


// check whether the node is the master server node ---------------------------
//
int	E_GameServer::IsMasterServerNode( node_t* node )
{
	return ( NODE_Compare( node, &m_MasterServer_Node ) == NODECMP_EQUAL );
}

// set the new MASV challenge and enforce a new announcement to the MASV ------
//
void E_GameServer::SetMasterServerChallenge( int nMASVChallenge ) 
{ 
	m_MasterServer_Challenge = nMASVChallenge; 
	// enforce immediate resend of server info
	m_MasterServerFrameBase = SYSs_GetRefFrameCount() - m_MasterServer_FrameTime;
}


// maintain communication to master server ------------------------------------
//
int E_GameServer::_MaintainMasterServer()
{
	// check whether to skip heartbeat sending
	if ( !SV_MASTERSERVER_SENDHEARTBEAT )
		return FALSE;

	refframe_t CurMasterServerRefFrames = SYSs_GetRefFrameCount() - m_MasterServerFrameBase;

	// check whether maintainance is necessary
	if ( CurMasterServerRefFrames >= m_MasterServer_FrameTime ) {

		// advance base
		m_MasterServerFrameBase += CurMasterServerRefFrames;

		// try to resolve master server node
		if ( !m_bMasterServer_NodeValid ) {
			if ( TheUDPDriver->ResolveHostName( m_MasterServer_Hostname, &m_MasterServer_Node ) ) {
				NODE_StorePort( &m_MasterServer_Node, DEFAULT_MASTERSERVER_UDP_PORT );
				m_bMasterServer_NodeValid = true;
			} else {
				CON_AddLine( error_resolving_masterserver );
			}
		}

		// send challenge/info packet to masterserver
		if ( m_bMasterServer_NodeValid ) {

			// build command
			char szBuffer[ MAX_RE_COMMANDINFO_COMMAND_LEN + 1 ];
			int xpos_out = ( SV_MAP_X >= 0 ) ? SV_MAP_X : (int)SV_SERVERID;
			int ypos_out = ( SV_MAP_Y >= 0 ) ? SV_MAP_Y : (int)SV_SERVERID;
			snprintf( szBuffer, MAX_RE_COMMANDINFO_COMMAND_LEN,
						MASV_CHALLSTRING,
						CLSV_PROTOCOL_MAJOR, CLSV_PROTOCOL_MINOR,
						m_MasterServer_Challenge,
						m_szServername,
						TheConnManager->GetNumConnected(),
						MAX_NUM_CLIENTS,
						SV_SERVERID,
						CPU_VENDOR_OS,
						SV_NETCONF_PORT,
						xpos_out,
						ypos_out,
						(int)SV_UNIVERSE_ENABLED
					);

			// append a remote event containing the command
			E_REList* pUnreliable = E_REList::CreateAndAddRef( RE_LIST_MAXAVAIL );
			int rc = pUnreliable->NET_Append_RE_CommandInfo( szBuffer );
			ASSERT( rc == TRUE );

			// only send serverlinks if challenge is valid
			if ( m_MasterServer_Challenge != 0 ) {

				// append all links or until packet full
				for( int nLink = 0; nLink < m_nNumServerLinks; nLink++ ) {
					if ( !pUnreliable->NET_Append_RE_ServerLinkInfo( SV_SERVERID, m_ServerLinks[ nLink ].m_serverid, SERVERLINKINFO_1_TO_2 ) ) {
						break;
					}
				}
			}

			// send a datagram
			ThePacketHandler->Send_STREAM_Datagram( pUnreliable, &m_MasterServer_Node, PLAYERID_MASTERSERVER );

			// release the RE list from here
			pUnreliable->Release();

			// if this server participates in the universe game, send current kill
			// totals for all joined players so the master has fresh stats even for
			// players who are still alive and have never triggered PerformUnjoin.
			// Each player gets its own command packet because the master handler
			// dispatches on the first RE_CommandInfo per packet.
			if ( SV_UNIVERSE_ENABLED ) {
				for ( int nSlot = 0; nSlot < MAX_NUM_CLIENTS; nSlot++ ) {
					E_SimPlayerInfo* pSPI = TheSimulator->GetSimPlayerInfo( nSlot );
					if ( pSPI == NULL || !pSPI->IsPlayerJoined() )
						continue;
					const char* pname = TheConnManager->GetClientName( nSlot );
					if ( pname == NULL || pname[0] == '\0' )
						continue;
					G_Player* pPlayer = TheGame->GetPlayer( nSlot );
					if ( pPlayer == NULL )
						continue;

					char szSave[ MAX_RE_COMMANDINFO_COMMAND_LEN + 1 ];
					snprintf( szSave, sizeof(szSave), "UNIV_SAVE %s %d %d",
					          pname,
					          pPlayer->GetTotalUniverseKills(),
					          pPlayer->GetTotalUniverseDeaths() );
					E_REList* pStats = E_REList::CreateAndAddRef( RE_LIST_MAXAVAIL );
					pStats->NET_Append_RE_CommandInfo( szSave );
					ThePacketHandler->Send_STREAM_Datagram( pStats, &m_MasterServer_Node, PLAYERID_MASTERSERVER );
					pStats->Release();
				}
			}
		}
	}

	return TRUE;
}



// run housekeeping -----------------------------------------------------------
//
int E_GameServer::_MaintainHousekeeping()
{
	refframe_t CurMaintainRefFrames = SYSs_GetRefFrameCount() - m_MaintainFrameBase;
	
	// check whether maintainance is necessary
	if ( CurMaintainRefFrames >= m_Maintain_FrameTime ) {
		
		// advance base for frame measurement
		m_MaintainFrameBase += CurMaintainRefFrames;
		
		// check whether clients are alive and timeout if needed
		TheConnManager->CheckAliveStatus();

		// recalulate the average packet sizes sent to each client
		TheSimNetOutput->RecalcAveragePacketSizes();

		// cleanup all zombie distributables
		//FIXME: we could call this function after timeout of max. RTT_OF_ALL_CLIENTS
		TheSimNetOutput->CleanupZombieDistributables();
	}

	// auto-expire the universe game when the master's timer runs out
	if ( GetServerIsMaster() && TheMaster->m_bUniverseActive ) {
		if ( TheMaster->GetUniverseTimeRemaining() == GAME_FINISHED_TIME ) {
			SV_UNIVERSE_ACTIVE = 0;
			TheMaster->EndUniverseGame();
			MSGOUT( "Universe game timer expired — game ended automatically." );
		}
	}

	return TRUE;
}

// send an RE list as a datagram to the master server -------------------------
//
void E_GameServer::SendToMaster( E_REList* relist )
{
	if ( !m_bMasterServer_NodeValid )
		return;
	ThePacketHandler->Send_STREAM_Datagram( relist, &m_MasterServer_Node, PLAYERID_SERVER );
}


// register that we are waiting for a transit record for this player ----------
//
void E_GameServer::RegisterPendingTransit( const char* name, int nClientID )
{
	ASSERT( name != NULL );

	// update if already present
	for ( int i = 0; i < m_nTransitPending; i++ ) {
		if ( strncmp( m_TransitPending[ i ].name, name, MAX_PLAYER_NAME ) == 0 ) {
			m_TransitPending[ i ].nClientID = nClientID;
			return;
		}
	}

	// add new entry if space available
	if ( m_nTransitPending < MAX_TRANSIT_PENDING ) {
		strncpy( m_TransitPending[ m_nTransitPending ].name, name, 31 );
		m_TransitPending[ m_nTransitPending ].name[ 31 ] = '\0';
		m_TransitPending[ m_nTransitPending ].nClientID = nClientID;
		m_nTransitPending++;
	}
}


// consume (look up and remove) a pending transit entry -----------------------
// returns nClientID, or -1 if not found
//
int E_GameServer::ConsumePendingTransit( const char* name )
{
	ASSERT( name != NULL );
	for ( int i = 0; i < m_nTransitPending; i++ ) {
		if ( strncmp( m_TransitPending[ i ].name, name, MAX_PLAYER_NAME ) == 0 ) {
			int nClientID = m_TransitPending[ i ].nClientID;
			// remove by swapping with last entry
			m_TransitPending[ i ] = m_TransitPending[ --m_nTransitPending ];
			return nClientID;
		}
	}
	return -1;
}


// register that we are waiting for a universe kill response for this player --
//
void E_GameServer::RegisterPendingUniverseQuery( const char* name, int nClientID )
{
	ASSERT( name != NULL );

	for ( int i = 0; i < m_nUnivQueryPending; i++ ) {
		if ( strncmp( m_UnivQueryPending[ i ].name, name, MAX_PLAYER_NAME ) == 0 ) {
			m_UnivQueryPending[ i ].nClientID = nClientID;
			return;
		}
	}
	if ( m_nUnivQueryPending < MAX_TRANSIT_PENDING ) {
		strncpy( m_UnivQueryPending[ m_nUnivQueryPending ].name, name, MAX_PLAYER_NAME );
		m_UnivQueryPending[ m_nUnivQueryPending ].name[ MAX_PLAYER_NAME ] = '\0';
		m_UnivQueryPending[ m_nUnivQueryPending ].nClientID = nClientID;
		m_nUnivQueryPending++;
	}
}


// consume (look up and remove) a pending universe kill query -----------------
// returns nClientID, or -1 if not found
//
int E_GameServer::ConsumePendingUniverseQuery( const char* name )
{
	ASSERT( name != NULL );
	for ( int i = 0; i < m_nUnivQueryPending; i++ ) {
		if ( strncmp( m_UnivQueryPending[ i ].name, name, MAX_PLAYER_NAME ) == 0 ) {
			int nClientID = m_UnivQueryPending[ i ].nClientID;
			m_UnivQueryPending[ i ] = m_UnivQueryPending[ --m_nUnivQueryPending ];
			return nClientID;
		}
	}
	return -1;
}


// run the SERVER frame -------------------------------------------------------
//
refframe_t E_GameServer::ServerFrame()
{
	m_CurServerRefFrame = SYSs_GetRefFrameCount();
	//MSGOUT( "E_GameServer::ServerFrame(): %d", m_CurServerRefFrame );

	// run packet chain processing function ( reads network, filters out invalid events, fills input queue ) 
	ThePacketDriver->NET_ProcessPacketChain();

	// if we are not a master server, do the game server stuff...
	if(!this->GetServerIsMaster()){
		// process queue with input from all clients
		TheSimNetInput->ProcessInputREList();

		// Create helix collision particles from RE_HelixParticle events that were
		// buffered during ProcessInputREList().  Must run AFTER ProcessInputREList()
		// (so all REs are collected) but BEFORE _MaintainSimulation() (so
		// PAN_AnimateParticles picks them up in the same frame they arrive).
		TheSimNetInput->FlushPendingHelixParticles();

		// run server-side bot AI
		m_BotManager.Tick( m_SimTick_FrameTime );

		// maintain the simulation
		_MaintainSimulation();

		// maintain housekeeping
		_MaintainHousekeeping();

		// maintain info to master server
		_MaintainMasterServer();

		// update clients if necessary
		TheSimNetOutput->DoClientUpdates();
	} else {
		// TODO: Master server stuff....
		TheMaster->RemoveStaleEntries();
		TheMaster->RemoveStalePlayerRecords();
	}
	// increment the serverframe counter
	//MSGOUT( "m_nServerFrame++" );
	m_nServerFrame++;

	// return the duration of the server frame
	return SYSs_GetRefFrameCount() - m_CurServerRefFrame;
}



// run the SERVER mainloop ----------------------------------------------------
//
int	E_GameServer::MainLoop()
{
	for( ; !m_bQuit ; ) {

		//LOGOUT(( "Before select()." ));
		
		// check whether we have a network input for at most m_ServerIdleTime
		int netInput = TheUDPDriver->SleepUntilNetInput( m_ServerIdleTime_msec );
		if ( netInput < 0 ) {
			// EINTR is harmless — a signal interrupted select(); just continue
			if ( errno != EINTR ) {
				MSGOUT( "E_GameServer: error checking for network input (errno %d: %s)",
					errno, strerror( errno ) );
			}
		}
		// check input
		INP_HandleInput();

		//Run Server Frame
		ServerFrame();

		// process console input
		CON_ConsoleMain();
	}

	return TRUE;
}


// key table for MASTERSERVER command -----------------------------------------
//
key_value_s masterserver_key_value[] = {

	{ "name",		NULL,	KEYVALFLAG_PARENTHESIZE		},
	{ "interval",	NULL,	KEYVALFLAG_NONE				},

	{ NULL,			NULL,	KEYVALFLAG_NONE				},
};

enum {

	KEY_MASTERSERVER_NAME,
	KEY_MASTERSERVER_INTERVAL
};

// min/max interval for heartbeats to the masterserve -------------------------
//
#define MASTERSERVER_INTERVAL_MIN	5			
#define MASTERSERVER_INTERVAL_MAX   86400		


// console command for the masterserver configuration -------------------------
//
PRIVATE
int Cmd_MASTERSERVER( char* masv_command )
{
	//NOTE:
	//CONCOM:
	// masterserver_command	::= 'sv.masterserver.conf' <name_spec> [<interval_spec>]
	// name_spec			::= 'name' <masterservername>
	// interval_spec		::= 'interval' <sec>

	ASSERT( masv_command != NULL );
	HANDLE_COMMAND_DOMAIN( masv_command );

	// scan out all values to keys
	if ( !ScanKeyValuePairs( masterserver_key_value, masv_command ) )
		return TRUE;

	char* pName = masterserver_key_value[ KEY_MASTERSERVER_NAME ].value;

	refframe_t _MasterServer_FrameTime = TheServer->GetMasterServerFrameTime();

	// get the interval and adjust for refframes
	if ( masterserver_key_value[ KEY_MASTERSERVER_INTERVAL ].value != NULL ) {
	
		int interval;
		ScanKeyValueInt( &masterserver_key_value[ KEY_MASTERSERVER_INTERVAL ], &interval );

		if ( interval < MASTERSERVER_INTERVAL_MIN ) {
			interval = MASTERSERVER_INTERVAL_MIN;
			CON_AddLine( "sv.masterserver interval clamped to min" );
		} else if ( interval > MASTERSERVER_INTERVAL_MAX ) {
			interval = MASTERSERVER_INTERVAL_MAX;
			CON_AddLine( "sv.masterserver interval clamped to max" );
		}

		_MasterServer_FrameTime = interval * DEFAULT_REFFRAME_FREQUENCY;
	}

	// set the new masterserver info
	TheServer->SetMasterServerInfo( pName, _MasterServer_FrameTime );

	return TRUE;
}


// key table for "SV.CONF" command --------------------------------------------
//
key_value_s sv_conf_key_value[] = {

	{ "name",		NULL,	KEYVALFLAG_PARENTHESIZE		},
	{ "maxplayers",	NULL,	KEYVALFLAG_NONE				},
	{ "simfreq",	NULL,	KEYVALFLAG_NONE				},

	{ NULL,			NULL,	KEYVALFLAG_NONE				},
};

enum {

	KEY_SERVER_NAME,
	KEY_SERVER_MAXPLAYERS,
	KEY_SERVER_SIMFREQ
};

// min/max interval simulation frequency --------------------------------------
//
#define SIMFREQ_MIN		5			
#define SIMFREQ_MAX		DEFAULT_REFFRAME_FREQUENCY


// console command for configuring the server ---------------------------------
//
PRIVATE
int Cmd_SV_CONF( char* sv_conf_command )
{
	//NOTE:
	//CONCOM:
	// sv_conf_command	::= 'sv.conf' [<name_spec>] [<maxplayer_spec>] [<simfreq_spec>]
	// name_spec		::= 'name' <servername>
	// maxplayers_spec	::= 'maxplayers' <maxplayers>
	// simfreq_spec     ::= 'simfreq' <simfreq>

	ASSERT( sv_conf_command != NULL );
	HANDLE_COMMAND_DOMAIN( sv_conf_command );

	// scan out all values to keys
	if ( !ScanKeyValuePairs( sv_conf_key_value, sv_conf_command ) )
		return TRUE;

	// name specified ?
	if ( sv_conf_key_value[ KEY_SERVER_NAME ].value != NULL ) {
		TheServer->SetServername( sv_conf_key_value[ KEY_MASTERSERVER_NAME ].value );
	}

	// maxplayer specified ?
	if ( sv_conf_key_value[ KEY_SERVER_MAXPLAYERS ].value != NULL ) {
		int maxplayers;
		ScanKeyValueInt( &sv_conf_key_value[ KEY_SERVER_MAXPLAYERS ], &maxplayers );

		// set the current # of max. players
		if ( !TheServer->SetMaxNumClients( maxplayers ) ) {
			CON_AddLine( "the max # of players can only be set once. please restart the server." );
		}
	}

	// simfreq specified ?
	if ( sv_conf_key_value[ KEY_SERVER_SIMFREQ ].value != NULL ) {
		int simfreq;
		ScanKeyValueInt( &sv_conf_key_value[ KEY_SERVER_SIMFREQ ], &simfreq );

		if ( simfreq < SIMFREQ_MIN ) {
			CON_AddLine( "simfrequency clamped to min" );
			simfreq = SIMFREQ_MIN;
		} else if ( simfreq > SIMFREQ_MAX ) {
			CON_AddLine( "simfrequency clamped to max" );
			simfreq = SIMFREQ_MAX;
		}

		if ( !TheServer->SetSimFrequency( simfreq ) ) {
			CON_AddLine( "the simulation frequency can only be set once. please restart the server." );
		}
	}

	return TRUE;
}

// key table for "SV.LINK" command --------------------------------------------
//
key_value_s sv_link_key_value[] = {

	{ "serverid",	NULL,	KEYVALFLAG_MANDATORY		},
	{ "pos",		NULL,	KEYVALFLAG_PARENTHESIZE		},
	{ "dir",		NULL,	KEYVALFLAG_PARENTHESIZE		},

	{ NULL,			NULL,	KEYVALFLAG_NONE				},
};

enum {

	KEY_SERVERLINK_SERVERID,
	KEY_SERVERLINK_POS,
	KEY_SERVERLINK_DIR
};


// key table for "SV.PLANET" command ------------------------------------------
//
key_value_s sv_planet_key_value[] = {

	{ "pos",		NULL,	KEYVALFLAG_PARENTHESIZE		},
	{ "rotspeed",	NULL,	KEYVALFLAG_NONE				},
	{ "ring",		NULL,	KEYVALFLAG_NONE				},
	{ "size",		NULL,	KEYVALFLAG_NONE				},
	{ "tex",		NULL,	KEYVALFLAG_NONE				},
	{ "ringtex",	NULL,	KEYVALFLAG_NONE				},
	{ "ringinner",	NULL,	KEYVALFLAG_NONE				},
	{ "ringouter",	NULL,	KEYVALFLAG_NONE				},
	{ "ringtiltx",		NULL,	KEYVALFLAG_NONE				},
	{ "ringtiltz",		NULL,	KEYVALFLAG_NONE				},
	{ "hostid",			NULL,	KEYVALFLAG_NONE				},
	{ "orbitspeed",		NULL,	KEYVALFLAG_NONE				},
	{ "orbitradius",	NULL,	KEYVALFLAG_NONE				},
	{ "orbitshape",		NULL,	KEYVALFLAG_NONE				},
	{ "orbitparentid",	NULL,	KEYVALFLAG_NONE				},

	{ NULL,				NULL,	KEYVALFLAG_NONE				},
};

enum {

	KEY_PLANET_POS,
	KEY_PLANET_ROTSPEED,
	KEY_PLANET_RING,
	KEY_PLANET_SIZE,
	KEY_PLANET_TEX,
	KEY_PLANET_RINGTEX,
	KEY_PLANET_RINGINNER,
	KEY_PLANET_RINGOUTER,
	KEY_PLANET_RINGTILTX,
	KEY_PLANET_RINGTILTZ,
	KEY_PLANET_HOSTID,
	KEY_PLANET_ORBITSPEED,
	KEY_PLANET_ORBITRADIUS,
	KEY_PLANET_ORBITSHAPE,
	KEY_PLANET_ORBITPARENTID
};


// console command for specifying server links --------------------------------
//
PRIVATE
int Cmd_SV_LINK( char* sv_link_command )
{
	//NOTE:
	//CONCOM:
	// sv_link_command	::= 'sv.link' <serverid_spec> [<pos_spec>] [<dir_spec>]
	// serverid_spec	::= 'serverid' <serverid>
	// pos_spec			::= 'pos' '(' <float> <float> <float> ')'
	// dir_spec			::= 'dir' '(' <float> <float> <float> ')'

	ASSERT( sv_link_command != NULL );
	HANDLE_COMMAND_DOMAIN( sv_link_command );

	// scan out all values to keys
	if ( !ScanKeyValuePairs( sv_link_key_value, sv_link_command ) ) {
		return TRUE;
	}
	
	ASSERT( sv_link_key_value[ KEY_SERVERLINK_SERVERID ].value != NULL );
	int serverid;
	ScanKeyValueInt( &sv_link_key_value[ KEY_SERVERLINK_SERVERID ], &serverid );
	if ( serverid == 0 ) {
		CON_AddLine( "invalid server id specified" );
	}

	// parse position
	Vector3 pos_spec;
	if ( sv_link_key_value[ KEY_SERVERLINK_POS ].value != NULL ) {
		if ( !ScanKeyValueFloatList( &sv_link_key_value[ KEY_SERVERLINK_POS ], (float*)&pos_spec.X, 3, 3 ) ) {
			CON_AddLine( "position invalid" );
			return TRUE;
		}
	} else {
		//FIXME: constants
		pos_spec.X = ( RAND() % 1000 ) - 500;
		pos_spec.Y = ( RAND() % 1000 ) - 500;
		pos_spec.Z = ( RAND() % 1000 ) - 500;
		pos_spec.VisibleFrame = 0;
	}

	// parse direction
	Vector3 dir_spec;
	if ( sv_link_key_value[ KEY_SERVERLINK_DIR ].value != NULL ) {
		if ( !ScanKeyValueFloatList( &sv_link_key_value[ KEY_SERVERLINK_DIR ], (float*)&dir_spec.X, 3, 3 ) ) {
			CON_AddLine( "direction invalid" );
			return TRUE;
		}
	} else {
		// default to point in z direction
		dir_spec.X = 0.0f;
		dir_spec.Y = 0.0f;
		dir_spec.Z = 1.0f;
		dir_spec.VisibleFrame = 0;
	}

	// add the serverlink
	TheServer->AddServerLink( serverid, &pos_spec, &dir_spec );

	return TRUE;
}



// console command for spawning a planet --------------------------------------
//
PRIVATE
int Cmd_SV_PLANET( char* sv_planet_command )
{
	//NOTE:
	//CONCOM:
	// sv_planet_command	::= 'sv.planet' [<pos_spec>] [<rotspeed_spec>] [<ring_spec>] [<size_spec>] [<tex_spec>]
	// pos_spec				::= 'pos' '(' <float> <float> <float> ')'
	// rotspeed_spec		::= 'rotspeed' <int>
	// ring_spec			::= 'ring' <0|1>
	// size_spec			::= 'size' <float>      (visual radius; 0 = use class default)
	// tex_spec				::= 'tex' <texname>     (surface texture name without extension)
	// ringtex_spec			::= 'ringtex' <texname> (ring texture name without extension)
	// ringinner_spec		::= 'ringinner' <float> (ring inner radius; 0 = use default)
	// ringouter_spec		::= 'ringouter' <float> (ring outer radius; 0 = use default)
	// ringtiltx_spec		::= 'ringtiltx' <float> (ring tilt around X axis, degrees; 0 = flat)
	// ringtiltz_spec		::= 'ringtiltz' <float> (ring tilt around Z axis, degrees; 0 = flat)

	ASSERT( sv_planet_command != NULL );
	HANDLE_COMMAND_DOMAIN( sv_planet_command );

	// scan out all values to keys
	if ( !ScanKeyValuePairs( sv_planet_key_value, sv_planet_command ) )
		return TRUE;

	// parse position
	Vector3 pos_spec;
	if ( sv_planet_key_value[ KEY_PLANET_POS ].value != NULL ) {
		if ( !ScanKeyValueFloatList( &sv_planet_key_value[ KEY_PLANET_POS ], (float*)&pos_spec.X, 3, 3 ) ) {
			CON_AddLine( "position invalid" );
			return TRUE;
		}
	} else {
		pos_spec.X = ( RAND() % 1000 ) - 500;
		pos_spec.Y = ( RAND() % 1000 ) - 500;
		pos_spec.Z = ( RAND() % 1000 ) - 500;
		pos_spec.VisibleFrame = 0;
	}

	// parse rotation speed (BAMS units, default 0x0010)
	bams_t rotspeed = 0x0010;
	if ( sv_planet_key_value[ KEY_PLANET_ROTSPEED ].value != NULL ) {
		int rotspeed_int;
		ScanKeyValueInt( &sv_planet_key_value[ KEY_PLANET_ROTSPEED ], &rotspeed_int );
		rotspeed = (bams_t) rotspeed_int;
	}

	// parse ring flag
	int hasring = 0;
	if ( sv_planet_key_value[ KEY_PLANET_RING ].value != NULL )
		ScanKeyValueInt( &sv_planet_key_value[ KEY_PLANET_RING ], &hasring );

	// parse visual radius (0 = keep OD2 class default)
	geomv_t size = GEOMV_0;
	if ( sv_planet_key_value[ KEY_PLANET_SIZE ].value != NULL ) {
		float size_f = 0.0f;
		ScanKeyValueFloat( &sv_planet_key_value[ KEY_PLANET_SIZE ], &size_f );
		size = FLOAT_TO_GEOMV( size_f );
	}

	// parse surface texture name
	const char *surtexname = NULL;
	if ( sv_planet_key_value[ KEY_PLANET_TEX ].value != NULL )
		surtexname = sv_planet_key_value[ KEY_PLANET_TEX ].value;

	// parse ring texture name
	const char *ringtexname = NULL;
	if ( sv_planet_key_value[ KEY_PLANET_RINGTEX ].value != NULL )
		ringtexname = sv_planet_key_value[ KEY_PLANET_RINGTEX ].value;

	// parse ring inner/outer radii
	float ringinner = 0.0f, ringouter = 0.0f;
	if ( sv_planet_key_value[ KEY_PLANET_RINGINNER ].value != NULL )
		ScanKeyValueFloat( &sv_planet_key_value[ KEY_PLANET_RINGINNER ], &ringinner );
	if ( sv_planet_key_value[ KEY_PLANET_RINGOUTER ].value != NULL )
		ScanKeyValueFloat( &sv_planet_key_value[ KEY_PLANET_RINGOUTER ], &ringouter );

	// parse ring tilt angles (degrees)
	float ringtiltx_deg = 0.0f, ringtiltz_deg = 0.0f;
	if ( sv_planet_key_value[ KEY_PLANET_RINGTILTX ].value != NULL )
		ScanKeyValueFloat( &sv_planet_key_value[ KEY_PLANET_RINGTILTX ], &ringtiltx_deg );
	if ( sv_planet_key_value[ KEY_PLANET_RINGTILTZ ].value != NULL )
		ScanKeyValueFloat( &sv_planet_key_value[ KEY_PLANET_RINGTILTZ ], &ringtiltz_deg );

	// create the planet
	Planet *planet = TheGame->CreatePlanet( &pos_spec, rotspeed, hasring, size, surtexname );

	// apply ring params on returned object
	if ( planet != NULL ) {
		if ( ringtexname != NULL ) {
			strncpy( planet->RingTexName, ringtexname, MAX_RING_TEXNAME );
			planet->RingTexName[ MAX_RING_TEXNAME ] = '\0';
		}
		if ( ringinner > 0.0f )
			planet->RingInnerRadius = FLOAT_TO_GEOMV( ringinner );
		if ( ringouter > 0.0f )
			planet->RingOuterRadius = FLOAT_TO_GEOMV( ringouter );
		planet->RingTiltX = DEG_TO_BAMS( ringtiltx_deg );
		planet->RingTiltZ = DEG_TO_BAMS( ringtiltz_deg );

		// override HostObjNumber if hostid key was supplied
		if ( sv_planet_key_value[ KEY_PLANET_HOSTID ].value != NULL ) {
			int hostid_int = 0;
			ScanKeyValueInt( &sv_planet_key_value[ KEY_PLANET_HOSTID ], &hostid_int );
			if ( hostid_int > 0 )
				planet->HostObjNumber = (dword)hostid_int;
		}

		// apply orbit parameters if supplied
		if ( sv_planet_key_value[ KEY_PLANET_ORBITSPEED ].value != NULL ) {
			int orbitspeed_int = 0;
			ScanKeyValueInt( &sv_planet_key_value[ KEY_PLANET_ORBITSPEED ], &orbitspeed_int );
			planet->OrbitSpeed = (bams_t)orbitspeed_int;
		}
		if ( sv_planet_key_value[ KEY_PLANET_ORBITRADIUS ].value != NULL ) {
			float orbitradius_f = 0.0f;
			ScanKeyValueFloat( &sv_planet_key_value[ KEY_PLANET_ORBITRADIUS ], &orbitradius_f );
			planet->OrbitRadius = FLOAT_TO_GEOMV( orbitradius_f );
		}
		if ( sv_planet_key_value[ KEY_PLANET_ORBITSHAPE ].value != NULL ) {
			int orbitshape_int = 0;
			ScanKeyValueInt( &sv_planet_key_value[ KEY_PLANET_ORBITSHAPE ], &orbitshape_int );
			if ( orbitshape_int < 0 ) orbitshape_int = 0;
			if ( orbitshape_int > 100 ) orbitshape_int = 100;
			planet->OrbitShape = orbitshape_int;
		}
		if ( sv_planet_key_value[ KEY_PLANET_ORBITPARENTID ].value != NULL ) {
			int orbitparentid_int = 0;
			ScanKeyValueInt( &sv_planet_key_value[ KEY_PLANET_ORBITPARENTID ], &orbitparentid_int );
			planet->OrbitParentId = (dword)orbitparentid_int;
		}

		// update last-summoned id so "propo 0.xxx" works after this command
		TheWorld->SetLastSummonedObjectID( planet->ObjectNumber );

		// always print assigned IDs so they can be referenced in scripts
		MSGOUT( "sv.planet created: ObjectNumber %u, HostObjNumber %u",
			(unsigned int)planet->ObjectNumber,
			(unsigned int)planet->HostObjNumber );
	}

	return TRUE;
}


// key table for "SV.ASTEROID" command ----------------------------------------
//
key_value_s sv_asteroid_key_value[] = {

	{ "pos",			NULL,	KEYVALFLAG_PARENTHESIZE	},
	{ "count",			NULL,	KEYVALFLAG_NONE			},
	{ "density",		NULL,	KEYVALFLAG_NONE			},
	{ "size",			NULL,	KEYVALFLAG_NONE			},
	{ "tex",			NULL,	KEYVALFLAG_NONE			},
	{ "rotspdx",		NULL,	KEYVALFLAG_NONE			},
	{ "rotspdy",		NULL,	KEYVALFLAG_NONE			},
	{ "rotspdz",		NULL,	KEYVALFLAG_NONE			},
	{ "orbitspeed",		NULL,	KEYVALFLAG_NONE			},
	{ "orbitradius",	NULL,	KEYVALFLAG_NONE			},
	{ "orbitshape",		NULL,	KEYVALFLAG_NONE			},
	{ "orbitparentid",	NULL,	KEYVALFLAG_NONE			},

	{ NULL,				NULL,	KEYVALFLAG_NONE			},
};

enum {

	KEY_ASTEROID_POS,
	KEY_ASTEROID_COUNT,
	KEY_ASTEROID_DENSITY,
	KEY_ASTEROID_SIZE,
	KEY_ASTEROID_TEX,
	KEY_ASTEROID_ROTSPDX,
	KEY_ASTEROID_ROTSPDY,
	KEY_ASTEROID_ROTSPDZ,
	KEY_ASTEROID_ORBITSPEED,
	KEY_ASTEROID_ORBITRADIUS,
	KEY_ASTEROID_ORBITSHAPE,
	KEY_ASTEROID_ORBITPARENTID,
};


// console command for spawning an asteroid field ------------------------------
//
PRIVATE
int Cmd_SV_ASTEROID( char* sv_asteroid_command )
{
	//NOTE:
	//CONCOM:
	// sv_asteroid_command ::= 'sv.asteroid' [<pos_spec>] [<count_spec>] [<density_spec>]
	//                          [<size_spec>] [<tex_spec>] [<rotspd_specs>] [<orbit_specs>]
	// pos_spec         ::= 'pos' '(' <float> <float> <float> ')'
	// count_spec       ::= 'count' <int>       (number of asteroids; default 1)
	// density_spec     ::= 'density' <float>   (scatter radius around pos; default 0=exact pos)
	// size_spec        ::= 'size' <float>       (visual radius; 0 = use class default)
	// tex_spec         ::= 'tex' <texname>
	// rotspdx_spec     ::= 'rotspdx' <int>     (BAMS rotation speed around X axis)
	// rotspdy_spec     ::= 'rotspdy' <int>      (BAMS rotation speed around Y axis)
	// rotspdz_spec     ::= 'rotspdz' <int>      (BAMS rotation speed around Z axis)
	// orbitspeed_spec  ::= 'orbitspeed' <int>
	// orbitradius_spec ::= 'orbitradius' <float>
	// orbitshape_spec  ::= 'orbitshape' <int>  (0=circle, 100=sharp ellipse)
	// orbitparentid    ::= 'orbitparentid' <int>

	ASSERT( sv_asteroid_command != NULL );
	HANDLE_COMMAND_DOMAIN( sv_asteroid_command );

	// scan out all values to keys
	if ( !ScanKeyValuePairs( sv_asteroid_key_value, sv_asteroid_command ) )
		return TRUE;

	// parse centre position
	Vector3 pos_spec;
	if ( sv_asteroid_key_value[ KEY_ASTEROID_POS ].value != NULL ) {
		if ( !ScanKeyValueFloatList( &sv_asteroid_key_value[ KEY_ASTEROID_POS ], (float*)&pos_spec.X, 3, 3 ) ) {
			CON_AddLine( "position invalid" );
			return TRUE;
		}
	} else {
		pos_spec.X = 0.0f;
		pos_spec.Y = 0.0f;
		pos_spec.Z = 0.0f;
		pos_spec.VisibleFrame = 0;
	}

	// parse count (default 1)
	int count = 1;
	if ( sv_asteroid_key_value[ KEY_ASTEROID_COUNT ].value != NULL )
		ScanKeyValueInt( &sv_asteroid_key_value[ KEY_ASTEROID_COUNT ], &count );
	if ( count < 1 ) count = 1;
	if ( count > 500 ) count = 500;   // hard safety cap

	// parse density (scatter radius; 0 = all at exact pos)
	float density = 0.0f;
	if ( sv_asteroid_key_value[ KEY_ASTEROID_DENSITY ].value != NULL )
		ScanKeyValueFloat( &sv_asteroid_key_value[ KEY_ASTEROID_DENSITY ], &density );

	// parse visual radius (0 = keep OD2 class default)
	geomv_t size = GEOMV_0;
	if ( sv_asteroid_key_value[ KEY_ASTEROID_SIZE ].value != NULL ) {
		float size_f = 0.0f;
		ScanKeyValueFloat( &sv_asteroid_key_value[ KEY_ASTEROID_SIZE ], &size_f );
		size = FLOAT_TO_GEOMV( size_f );
	}

	// parse surface texture name
	const char *surtexname = NULL;
	if ( sv_asteroid_key_value[ KEY_ASTEROID_TEX ].value != NULL )
		surtexname = sv_asteroid_key_value[ KEY_ASTEROID_TEX ].value;

	// parse per-axis rotation speeds (optional; randomised if not given)
	int rotspdx_given = 0, rotspdy_given = 0, rotspdz_given = 0;
	bams_t rotspdx = 0, rotspdy = 0, rotspdz = 0;
	if ( sv_asteroid_key_value[ KEY_ASTEROID_ROTSPDX ].value != NULL ) {
		int v = 0; ScanKeyValueInt( &sv_asteroid_key_value[ KEY_ASTEROID_ROTSPDX ], &v );
		rotspdx = (bams_t)v; rotspdx_given = 1;
	}
	if ( sv_asteroid_key_value[ KEY_ASTEROID_ROTSPDY ].value != NULL ) {
		int v = 0; ScanKeyValueInt( &sv_asteroid_key_value[ KEY_ASTEROID_ROTSPDY ], &v );
		rotspdy = (bams_t)v; rotspdy_given = 1;
	}
	if ( sv_asteroid_key_value[ KEY_ASTEROID_ROTSPDZ ].value != NULL ) {
		int v = 0; ScanKeyValueInt( &sv_asteroid_key_value[ KEY_ASTEROID_ROTSPDZ ], &v );
		rotspdz = (bams_t)v; rotspdz_given = 1;
	}

	// parse orbit params (applied to all asteroids in field)
	bams_t  orbitspeed    = 0;
	float   orbitradius_f = 0.0f;
	int     orbitshape    = 0;
	dword   orbitparentid = 0;
	if ( sv_asteroid_key_value[ KEY_ASTEROID_ORBITSPEED ].value != NULL ) {
		int v = 0; ScanKeyValueInt( &sv_asteroid_key_value[ KEY_ASTEROID_ORBITSPEED ], &v );
		orbitspeed = (bams_t)v;
	}
	if ( sv_asteroid_key_value[ KEY_ASTEROID_ORBITRADIUS ].value != NULL )
		ScanKeyValueFloat( &sv_asteroid_key_value[ KEY_ASTEROID_ORBITRADIUS ], &orbitradius_f );
	if ( sv_asteroid_key_value[ KEY_ASTEROID_ORBITSHAPE ].value != NULL ) {
		ScanKeyValueInt( &sv_asteroid_key_value[ KEY_ASTEROID_ORBITSHAPE ], &orbitshape );
		if ( orbitshape < 0 ) orbitshape = 0;
		if ( orbitshape > 100 ) orbitshape = 100;
	}
	if ( sv_asteroid_key_value[ KEY_ASTEROID_ORBITPARENTID ].value != NULL ) {
		int v = 0; ScanKeyValueInt( &sv_asteroid_key_value[ KEY_ASTEROID_ORBITPARENTID ], &v );
		orbitparentid = (dword)v;
	}

	// create the field
	Asteroid *last_asteroid = NULL;
	for ( int i = 0; i < count; i++ ) {

		Vector3 apos = pos_spec;
		if ( density > 0.0f ) {
			// scatter within ±density around the centre
			apos.X += ( (float)( RAND() % 2001 ) - 1000.0f ) * ( density / 1000.0f );
			apos.Y += ( (float)( RAND() % 2001 ) - 1000.0f ) * ( density / 1000.0f );
			apos.Z += ( (float)( RAND() % 2001 ) - 1000.0f ) * ( density / 1000.0f );
		}

		// unique noise seed per asteroid so each has a different shape
		int noiseseed = (int)RAND();

		Asteroid *asteroid = TheGame->CreateAsteroid( &apos, size, noiseseed, surtexname );
		if ( asteroid == NULL )
			break;

		// apply rotation speeds (random slow tumble if not specified)
		// 0x08 max → ~16°/sec max per axis at target framerate — visible but not dizzying
		asteroid->RotSpeedX = rotspdx_given ? rotspdx : (bams_t)( RAND() % 0x08 );
		asteroid->RotSpeedY = rotspdy_given ? rotspdy : (bams_t)( RAND() % 0x08 );
		asteroid->RotSpeedZ = rotspdz_given ? rotspdz : (bams_t)( RAND() % 0x08 );

		// apply shared orbit params
		asteroid->OrbitSpeed    = orbitspeed;
		asteroid->OrbitRadius   = FLOAT_TO_GEOMV( orbitradius_f );
		asteroid->OrbitShape    = orbitshape;
		asteroid->OrbitParentId = orbitparentid;

		// Spread orbiting asteroids around the orbit path.
		// Without this every asteroid starts at CurOrbitPos=0 (the same angle),
		// snaps to the identical position on the first frame, and clumps in a line.
		if ( orbitspeed != 0 ) {
			asteroid->CurOrbitPos = (bams_t)( RAND() % 0x10000 );
		}

		last_asteroid = asteroid;
	}

	// update last-summoned id to the last created asteroid
	if ( last_asteroid != NULL ) {
		TheWorld->SetLastSummonedObjectID( last_asteroid->ObjectNumber );
		MSGOUT( "sv.asteroid: created %d asteroid(s); last ObjectNumber %u, HostObjNumber %u",
			count,
			(unsigned int)last_asteroid->ObjectNumber,
			(unsigned int)last_asteroid->HostObjNumber );
	}

	return TRUE;
}


REGISTER_MODULE( E_GAMESERVER )
{
	user_command_s regcom;
	memset( &regcom, 0, sizeof( user_command_s ) );

	// register "sv.masterserver.conf" command
	regcom.command	 = "sv.masterserver.conf";
	regcom.numparams = 1;
	regcom.execute	 = Cmd_MASTERSERVER;
	regcom.statedump = NULL;
	CON_RegisterUserCommand( &regcom );

	// register "sv.conf" command
	regcom.command	 = "sv.conf";
	regcom.numparams = 1;
	regcom.execute	 = Cmd_SV_CONF;
	regcom.statedump = NULL;
	CON_RegisterUserCommand( &regcom );

	// register "sv.link" command
	regcom.command	 = "sv.link";
	regcom.numparams = 0;
	regcom.execute	 = Cmd_SV_LINK;
	regcom.statedump = NULL;
	CON_RegisterUserCommand( &regcom );

	// register "sv.planet" command
	regcom.command	 = "sv.planet";
	regcom.numparams = 0;
	regcom.execute	 = Cmd_SV_PLANET;
	regcom.statedump = NULL;
	CON_RegisterUserCommand( &regcom );

	// register "sv.asteroid" command
	regcom.command	 = "sv.asteroid";
	regcom.numparams = 0;
	regcom.execute	 = Cmd_SV_ASTEROID;
	regcom.statedump = NULL;
	CON_RegisterUserCommand( &regcom );
}



