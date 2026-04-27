/*
 * MasterServer.cpp
 *
 *  Created on: Jan 2, 2013
 *      Author: jasonw
 */
// C library
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/timeb.h>

// C library
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h> 
#include <sys/timeb.h>

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

// proprietary module headers
#include "con_arg.h"
#include "con_aux_sv.h"
#include "con_com_sv.h"
#include "con_main_sv.h"
#include "e_colldet.h"
#include "g_extra.h"
#include "inp_main_sv.h"
#include "net_csdf.h"
#include "net_limits.h"
#include "net_udpdriver.h"
#include "net_util.h"
#include "net_packetdriver.h"
#include "obj_clas.h"
//#include "e_stats.h"
#include "g_main_sv.h"
#include "e_connmanager.h"
#include "e_packethandler.h"
#include "e_simulator.h"
#include "e_simnetinput.h"
#include "e_simnetoutput.h"
#include "sys_refframe_sv.h"
#include "sys_util_sv.h"

#include "gd_help.h"

#include "MasterServer.h"

MasterServer::MasterServer() {
	_init();
}

MasterServer::MasterServer(E_GameServer* gameserver) {
	_init();

}

MasterServer::~MasterServer() {
	// TODO Auto-generated destructor stub
}


void MasterServer::_init(){
	last_check=0;
	ServerList.clear();
	ServerList.resize(0);
	PlayerRecords.clear();
}


// save (or refresh) a player transit record ----------------------------------
//
void MasterServer::SavePlayerRecord( const PlayerRecord& rec )
{
	// update existing record if player already has one
	for ( std::vector<PlayerRecord>::iterator it = PlayerRecords.begin();
		  it != PlayerRecords.end(); ++it ) {
		if ( strncmp( it->name, rec.name, MAX_PLAYER_NAME ) == 0 ) {
			*it = rec;
			MSGOUT( "transit: updated record for %s", rec.name );
			return;
		}
	}
	// new record
	PlayerRecords.push_back( rec );
	MSGOUT( "transit: stored record for %s", rec.name );
}


// claim (retrieve and delete) a player transit record -------------------------
//
bool MasterServer::ClaimPlayerRecord( const char* name, PlayerRecord* out )
{
	ASSERT( name != NULL );
	ASSERT( out  != NULL );

	for ( std::vector<PlayerRecord>::iterator it = PlayerRecords.begin();
		  it != PlayerRecords.end(); ++it ) {
		if ( strncmp( it->name, name, MAX_PLAYER_NAME ) == 0 ) {
			*out = *it;
			PlayerRecords.erase( it );
			MSGOUT( "transit: claimed record for %s", name );
			return true;
		}
	}
	return false;
}


// remove records older than 5 minutes ----------------------------------------
//
void MasterServer::RemoveStalePlayerRecords()
{
	time_t now = time( NULL );
	std::vector<PlayerRecord>::iterator it = PlayerRecords.begin();
	while ( it != PlayerRecords.end() ) {
		if ( now - it->timestamp > 300 ) {
			MSGOUT( "transit: expiring stale record for %s", it->name );
			it = PlayerRecords.erase( it );
		} else {
			++it;
		}
	}
}

int MasterServer::RemoveStaleEntries(){

	int curr_check = (int)time(NULL);
	if ( last_check + 60 < curr_check ) {
		std::vector<MasterServerItem>::iterator it = ServerList.begin();
		while ( it != ServerList.end() ) {
			int entry_time = (int)it->GetMTime();

			// expire entries that have not sent a heartbeat in 60 seconds
			if ( ( curr_check > entry_time + 60 ) && ( entry_time >= 1 ) ) {
				char srv_name[ MAX_SERVER_NAME ];
				it->GetServerName( srv_name, MAX_SERVER_NAME - 1 );
				MSGOUT( "Expiring %s due to lack of heartbeat.", srv_name );
				it = ServerList.erase( it );   // erase() returns next valid iterator
			} else {
				++it;
			}
		}
		last_check = curr_check;
	}
	return 0;
}
