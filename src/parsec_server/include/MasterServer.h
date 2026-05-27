/*
 * MasterServer.h
 *
 *  Created on: Jan 2, 2013
 *      Author: jasonw
 */

#ifndef MASTERSERVER_H_
#define MASTERSERVER_H_

#include "e_gameserver.h"
#include "MasterServerItem.h"

// C++ STL
#include <vector>
#include <time.h>


// per-player transit loadout record ------------------------------------------
//
struct PlayerRecord {
	char     name[ MAX_PLAYER_NAME + 1 ];
	time_t   timestamp;
	word     NumMissls;
	word     NumHomMissls;
	word     NumPartMissls;
	word     NumMines;
	fixed_t  CurEnergy;
	geomv_t  CurShield;
	dword    Weapons;
	dword    Specials;
	int      CurDamage;
	// universe game stats (zero in non-universe games)
	int      UniverseKills;
	int      UniverseDeaths;
};

// per-player historical ladder stats -----------------------------------------
//
struct UniversePlayerStat {
	char   name[ MAX_PLAYER_NAME + 1 ];
	int    total_kills;
	int    total_deaths;
	int    games_played;
	time_t last_seen;
};


/*
 *
 */
class MasterServer: public E_GameServer {
public:
	MasterServer();
	MasterServer(E_GameServer *);
	virtual ~MasterServer();
	void _init();

	int RemoveStaleEntries();

	// player transit loadout registry
	void SavePlayerRecord( const PlayerRecord& rec );
	bool ClaimPlayerRecord( const char* name, PlayerRecord* out );
	void RemoveStalePlayerRecords();

	// universe game coordination
	bool m_bUniverseActive;
	time_t m_tUniverseStart;
	int  m_nUniverseDuration;              // seconds
	char m_szUniverseHtmlFile[ 256 ];      // path for HTML output

	int  GetUniverseTimeRemaining();       // seconds left, or GAME_FINISHED_TIME
	void StartUniverseGame();
	void EndUniverseGame();                // collect stats, write HTML, reset

	// universe kill tracking (name → kills/deaths this game, highest value wins)
	void UpdateUniverseKills( const char* name, int kills, int deaths );
	bool GetUniverseKills( const char* name, int* kills_out, int* deaths_out );

	// historical ladder stats
	std::vector<UniversePlayerStat> m_LadderStats;
	void LoadLadderStats();
	void WriteLadderStats();
	void UpdateLadderStats();              // merge current game into history
	void WriteHTMLLadder();

	std::vector<MasterServerItem>		ServerList;
	std::vector<PlayerRecord>			PlayerRecords;
private:
	int last_check;
};

#endif /* MASTERSERVER_H_ */
