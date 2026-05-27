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
#include "e_relist.h"
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

	// universe game state
	m_bUniverseActive    = false;
	m_tUniverseStart     = 0;
	m_nUniverseDuration  = 600;
	strncpy( m_szUniverseHtmlFile, "ladder.html", sizeof( m_szUniverseHtmlFile ) - 1 );
	m_szUniverseHtmlFile[ sizeof( m_szUniverseHtmlFile ) - 1 ] = '\0';
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


// ----------------------------------------------------------------------------
// Universe game coordination
// ----------------------------------------------------------------------------

// return seconds remaining in the universe game, or GAME_FINISHED_TIME -------
//
int MasterServer::GetUniverseTimeRemaining()
{
	if ( !m_bUniverseActive )
		return GAME_NOTSTARTEDYET;

	int elapsed = (int)( time( NULL ) - m_tUniverseStart );
	int remaining = m_nUniverseDuration - elapsed;
	return ( remaining > 0 ) ? remaining : GAME_FINISHED_TIME;
}


// leaderboard entry for sorting (file-scope to avoid local-type-template-args warning)
struct LBEntry { char name[ MAX_PLAYER_NAME + 1 ]; int kills; };


// start the universe game ----------------------------------------------------
//
void MasterServer::StartUniverseGame()
{
	m_bUniverseActive = true;
	m_tUniverseStart  = time( NULL );
	MSGOUT( "Universe game started: duration %d seconds.", m_nUniverseDuration );
	LoadLadderStats();
}


// end the universe game: collect stats, update history, write HTML -----------
//
void MasterServer::EndUniverseGame()
{
	m_bUniverseActive = false;
	MSGOUT( "Universe game ended.  Updating ladder stats." );
	UpdateLadderStats();
	WriteLadderStats();
	WriteHTMLLadder();

	// --- build and broadcast the universe leaderboard to all opted-in servers ---

	// collect players with any kills this game from PlayerRecords
	std::vector<LBEntry> lb;
	for ( std::vector<PlayerRecord>::iterator it = PlayerRecords.begin();
		  it != PlayerRecords.end(); ++it ) {
		if ( it->UniverseKills > 0 ) {
			LBEntry e;
			strncpy( e.name, it->name, MAX_PLAYER_NAME );
			e.name[ MAX_PLAYER_NAME ] = '\0';
			e.kills = it->UniverseKills;
			lb.push_back( e );
		}
	}

	// sort by kills descending (simple insertion sort — 16 entries max)
	for ( size_t i = 1; i < lb.size(); i++ ) {
		for ( size_t j = i; j > 0 && lb[j].kills > lb[j-1].kills; j-- ) {
			LBEntry tmp = lb[j]; lb[j] = lb[j-1]; lb[j-1] = tmp;
		}
	}

	// cap at UNI_LB_MAX
	int count = (int)lb.size();
	if ( count > UNI_LB_MAX ) count = UNI_LB_MAX;

	// build static arrays for the RE append function
	char lb_names[ UNI_LB_MAX ][ UNI_LB_NAMELEN + 1 ];
	int  lb_kills[ UNI_LB_MAX ];
	for ( int i = 0; i < count; i++ ) {
		strncpy( lb_names[ i ], lb[ i ].name, UNI_LB_NAMELEN );
		lb_names[ i ][ UNI_LB_NAMELEN ] = '\0';
		lb_kills[ i ] = lb[ i ].kills;
	}

	// send to all universe-enabled servers in the server list
	for ( std::vector<MasterServerItem>::iterator si = ServerList.begin();
		  si != ServerList.end(); ++si ) {
		if ( !si->GetUniverseEnabled() )
			continue;
		node_t srv_node;
		si->GetNode( &srv_node );

		E_REList* pRE = E_REList::CreateAndAddRef( RE_LIST_MAXAVAIL );
		if ( pRE->NET_Append_RE_UniverseLeaderboard( (byte)count,
		         (const char (*)[ UNI_LB_NAMELEN + 1 ])lb_names, lb_kills ) ) {
			ThePacketHandler->Send_STREAM_Datagram( pRE, &srv_node, PLAYERID_MASTERSERVER );
			MSGOUT( "universe: sent leaderboard (%d players) to server %s",
			        count, NODE_Print( &srv_node ) );
		}
		pRE->Release();
	}

	// clear per-game kill data from PlayerRecords so next game starts clean
	for ( std::vector<PlayerRecord>::iterator it = PlayerRecords.begin();
		  it != PlayerRecords.end(); ++it ) {
		it->UniverseKills  = 0;
		it->UniverseDeaths = 0;
	}
}


// update (or insert) a player's universe kills for the current game ----------
// called when a heartbeat from a universe server reports player stats
//
void MasterServer::UpdateUniverseKills( const char* name, int kills, int deaths )
{
	ASSERT( name != NULL );
	for ( std::vector<PlayerRecord>::iterator it = PlayerRecords.begin();
		  it != PlayerRecords.end(); ++it ) {
		if ( strncmp( it->name, name, MAX_PLAYER_NAME ) == 0 ) {
			// take the higher value so kills can't go backwards when
			// a player hops between servers
			if ( kills  > it->UniverseKills  ) it->UniverseKills  = kills;
			if ( deaths > it->UniverseDeaths ) it->UniverseDeaths = deaths;
			return;
		}
	}
	// new player seen this game — create a minimal record
	PlayerRecord rec;
	memset( &rec, 0, sizeof( rec ) );
	strncpy( rec.name, name, MAX_PLAYER_NAME );
	rec.name[ MAX_PLAYER_NAME ] = '\0';
	rec.timestamp      = time( NULL );
	rec.UniverseKills  = kills;
	rec.UniverseDeaths = deaths;
	PlayerRecords.push_back( rec );
}


// retrieve a player's current universe kills for this game ------------------
//
bool MasterServer::GetUniverseKills( const char* name, int* kills_out, int* deaths_out )
{
	ASSERT( name != NULL );
	for ( std::vector<PlayerRecord>::iterator it = PlayerRecords.begin();
		  it != PlayerRecords.end(); ++it ) {
		if ( strncmp( it->name, name, MAX_PLAYER_NAME ) == 0 ) {
			if ( kills_out  ) *kills_out  = it->UniverseKills;
			if ( deaths_out ) *deaths_out = it->UniverseDeaths;
			return true;
		}
	}
	if ( kills_out  ) *kills_out  = 0;
	if ( deaths_out ) *deaths_out = 0;
	return false;
}


// ----------------------------------------------------------------------------
// Ladder stats persistence
// ----------------------------------------------------------------------------

// derive the .dat companion path from the HTML path --------------------------
static void LadderDatPath( const char* htmlpath, char* datpath, size_t datsz )
{
	strncpy( datpath, htmlpath, datsz - 1 );
	datpath[ datsz - 1 ] = '\0';
	// replace or append .dat extension
	char* dot = strrchr( datpath, '.' );
	if ( dot && dot > strrchr( datpath, '/' ) && dot > strrchr( datpath, '\\' ) )
		strncpy( dot, ".dat", datsz - (size_t)(dot - datpath) - 1 );
	else
		strncat( datpath, ".dat", datsz - strlen( datpath ) - 1 );
}


// load ladder stats from <htmlfile>.dat (pipe-delimited) ---------------------
//
void MasterServer::LoadLadderStats()
{
	char datpath[ 256 ];
	LadderDatPath( m_szUniverseHtmlFile, datpath, sizeof( datpath ) );

	FILE* fp = fopen( datpath, "r" );
	if ( !fp ) {
		MSGOUT( "universe: no existing ladder stats at %s", datpath );
		return;
	}

	m_LadderStats.clear();
	char line[ 512 ];
	while ( fgets( line, sizeof( line ), fp ) ) {
		if ( line[ 0 ] == '#' || line[ 0 ] == '\n' ) continue;
		UniversePlayerStat s;
		memset( &s, 0, sizeof( s ) );
		long long ls;
		// format: name|kills|deaths|games|last_seen
		char namebuf[ MAX_PLAYER_NAME + 1 ];
		if ( sscanf( line, "%20[^|]|%d|%d|%d|%lld",
		             namebuf, &s.total_kills, &s.total_deaths,
		             &s.games_played, &ls ) == 5 ) {
			strncpy( s.name, namebuf, MAX_PLAYER_NAME );
			s.name[ MAX_PLAYER_NAME ] = '\0';
			s.last_seen = (time_t)ls;
			m_LadderStats.push_back( s );
		}
	}
	fclose( fp );
	MSGOUT( "universe: loaded %d ladder entries from %s",
	        (int)m_LadderStats.size(), datpath );
}


// save ladder stats to <htmlfile>.dat ----------------------------------------
//
void MasterServer::WriteLadderStats()
{
	char datpath[ 256 ];
	LadderDatPath( m_szUniverseHtmlFile, datpath, sizeof( datpath ) );

	FILE* fp = fopen( datpath, "w" );
	if ( !fp ) {
		MSGOUT( "universe: cannot write ladder stats to %s", datpath );
		return;
	}

	fprintf( fp, "# OpenParsec Universe Ladder stats\n" );
	fprintf( fp, "# name|kills|deaths|games|last_seen\n" );
	for ( size_t i = 0; i < m_LadderStats.size(); i++ ) {
		const UniversePlayerStat& s = m_LadderStats[ i ];
		fprintf( fp, "%s|%d|%d|%d|%lld\n",
		         s.name, s.total_kills, s.total_deaths,
		         s.games_played, (long long)s.last_seen );
	}
	fclose( fp );
	MSGOUT( "universe: wrote %d ladder entries to %s",
	        (int)m_LadderStats.size(), datpath );
}


// merge current game's PlayerRecord kills into historical ladder stats --------
//
void MasterServer::UpdateLadderStats()
{
	time_t now = time( NULL );
	for ( std::vector<PlayerRecord>::iterator it = PlayerRecords.begin();
		  it != PlayerRecords.end(); ++it ) {
		if ( it->UniverseKills == 0 && it->UniverseDeaths == 0 )
			continue; // didn't participate

		// find existing entry
		bool found = false;
		for ( size_t i = 0; i < m_LadderStats.size(); i++ ) {
			if ( strncmp( m_LadderStats[ i ].name, it->name, MAX_PLAYER_NAME ) == 0 ) {
				m_LadderStats[ i ].total_kills  += it->UniverseKills;
				m_LadderStats[ i ].total_deaths += it->UniverseDeaths;
				m_LadderStats[ i ].games_played += 1;
				m_LadderStats[ i ].last_seen     = now;
				found = true;
				break;
			}
		}
		if ( !found ) {
			UniversePlayerStat s;
			memset( &s, 0, sizeof( s ) );
			strncpy( s.name, it->name, MAX_PLAYER_NAME );
			s.name[ MAX_PLAYER_NAME ] = '\0';
			s.total_kills  = it->UniverseKills;
			s.total_deaths = it->UniverseDeaths;
			s.games_played = 1;
			s.last_seen    = now;
			m_LadderStats.push_back( s );
		}
	}

	// sort by total kills descending
	for ( size_t i = 0; i < m_LadderStats.size(); i++ ) {
		for ( size_t j = i + 1; j < m_LadderStats.size(); j++ ) {
			if ( m_LadderStats[ j ].total_kills > m_LadderStats[ i ].total_kills ) {
				UniversePlayerStat tmp = m_LadderStats[ i ];
				m_LadderStats[ i ]    = m_LadderStats[ j ];
				m_LadderStats[ j ]    = tmp;
			}
		}
	}
}


// write HTML ladder page -----------------------------------------------------
//
void MasterServer::WriteHTMLLadder()
{
	FILE* fp = fopen( m_szUniverseHtmlFile, "w" );
	if ( !fp ) {
		MSGOUT( "universe: cannot write HTML ladder to %s", m_szUniverseHtmlFile );
		return;
	}

	// timestamp for footer
	time_t now = time( NULL );
	char timebuf[ 64 ];
	struct tm* tm_info = localtime( &now );
	strftime( timebuf, sizeof( timebuf ), "%Y-%m-%d %H:%M:%S", tm_info );

	fprintf( fp,
		"<html>\n"
		"<head><title>OpenParsec Universe Ladder</title></head>\n"
		"<body bgcolor=\"#000022\" text=\"#FFFF00\" link=\"#00FFFF\" vlink=\"#FF00FF\">\n"
		"<center>\n"
		"<h1><font face=\"courier new\" color=\"#00FFFF\">"
		"&#9733; OPENPARSEC UNIVERSE LADDER &#9733;</font></h1>\n"
		"<font face=\"courier new\" color=\"#FF8800\" size=\"2\">"
		"FRAG OR BE FRAGGED &mdash; THE UNIVERSE IS WATCHING"
		"</font><br><br>\n"
	);

	fprintf( fp,
		"<table border=\"2\" bordercolor=\"#00FFFF\" cellpadding=\"4\" cellspacing=\"0\" "
		"bgcolor=\"#000033\">\n"
		"<tr bgcolor=\"#003366\">\n"
		"<th><font face=\"courier new\" color=\"#00FFFF\">RANK</font></th>\n"
		"<th><font face=\"courier new\" color=\"#00FFFF\">CALLSIGN</font></th>\n"
		"<th><font face=\"courier new\" color=\"#00FFFF\">KILLS</font></th>\n"
		"<th><font face=\"courier new\" color=\"#00FFFF\">DEATHS</font></th>\n"
		"<th><font face=\"courier new\" color=\"#00FFFF\">K:D</font></th>\n"
		"<th><font face=\"courier new\" color=\"#00FFFF\">GAMES</font></th>\n"
		"<th><font face=\"courier new\" color=\"#00FFFF\">LAST SEEN</font></th>\n"
		"</tr>\n"
	);

	for ( size_t i = 0; i < m_LadderStats.size(); i++ ) {
		const UniversePlayerStat& s = m_LadderStats[ i ];
		int rank = (int)i + 1;

		// K:D ratio as string
		char kd[ 16 ];
		if ( s.total_deaths == 0 )
			snprintf( kd, sizeof( kd ), "%.2f", (float)s.total_kills );
		else
			snprintf( kd, sizeof( kd ), "%.2f",
			          (float)s.total_kills / (float)s.total_deaths );

		// last seen as local time string
		char lastseen[ 32 ];
		struct tm* lm = localtime( &s.last_seen );
		strftime( lastseen, sizeof( lastseen ), "%Y-%m-%d", lm );

		// row colour: gold for rank 1, alternating dark blues for rest
		const char* rowbg = ( rank == 1 ) ? "#332200" :
		                    ( rank % 2 == 0 ) ? "#000044" : "#000033";
		const char* namecol = ( rank == 1 ) ? "#FFDD00" : "#FFFF88";

		fprintf( fp, "<tr bgcolor=\"%s\">\n", rowbg );

		// rank cell — blink for #1
		if ( rank == 1 ) {
			fprintf( fp,
				"<td align=\"center\"><font face=\"courier new\" color=\"#FFDD00\">"
				"<blink><b>#%d</b></blink></font></td>\n", rank );
		} else {
			fprintf( fp,
				"<td align=\"center\"><font face=\"courier new\" color=\"#88AAFF\">"
				"#%d</font></td>\n", rank );
		}

		fprintf( fp,
			"<td><font face=\"courier new\" color=\"%s\"><b>%s</b></font></td>\n"
			"<td align=\"right\"><font face=\"courier new\" color=\"#00FF88\">%d</font></td>\n"
			"<td align=\"right\"><font face=\"courier new\" color=\"#FF4444\">%d</font></td>\n"
			"<td align=\"right\"><font face=\"courier new\" color=\"#FFFFFF\">%s</font></td>\n"
			"<td align=\"right\"><font face=\"courier new\" color=\"#AAAAAA\">%d</font></td>\n"
			"<td align=\"center\"><font face=\"courier new\" color=\"#888888\">%s</font></td>\n"
			"</tr>\n",
			namecol, s.name,
			s.total_kills, s.total_deaths, kd,
			s.games_played, lastseen
		);
	}

	if ( m_LadderStats.empty() ) {
		fprintf( fp,
			"<tr><td colspan=\"7\" align=\"center\">"
			"<font face=\"courier new\" color=\"#888888\">NO PILOTS RECORDED YET</font>"
			"</td></tr>\n"
		);
	}

	fprintf( fp,
		"</table>\n"
		"<br>\n"
		"<font face=\"courier new\" color=\"#444488\" size=\"1\">"
		"Last updated: %s &nbsp;|&nbsp; Powered by OpenParsec"
		"</font>\n"
		"</center>\n"
		"</body>\n"
		"</html>\n",
		timebuf
	);

	fclose( fp );
	MSGOUT( "universe: wrote HTML ladder to %s", m_szUniverseHtmlFile );
}
