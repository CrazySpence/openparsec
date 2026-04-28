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

	std::vector<MasterServerItem>		ServerList;
	std::vector<PlayerRecord>			PlayerRecords;
private:
	int last_check;
};

#endif /* MASTERSERVER_H_ */
