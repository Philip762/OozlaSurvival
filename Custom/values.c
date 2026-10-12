#include <libdl/game.h>
#include <libdl/moby.h>

#include "mobs/mob.h"
#include "gate.h"

const float HEALTH_PERCENT_TABLE[GAME_MAX_PLAYERS] = {
	[0] = 0,
    [1] = 0,
    [2] = 0,
    [3] = 0.05,
    [4] = 0.10,
    [5] = 0.15,
    [6] = 0.20,
    [7] = 0.25,
    [8] = 0.30,
    [9] = 0.35,
};

const float DAMAGE_TABLE[GAME_MAX_PLAYERS] = {
	[0] = 0,
  	[1] = 0,
  	[2] = 0,
  	[3] = 0,
  	[4] = 0,
  	[5] = 0,
  	[6] = 0,
  	[7] = 0,
  	[8] = 0,
	[9] = 0,
};

void disableAllGates(void) {
  Moby* m = mobyListGetStart();
  Moby* mEnd = mobyListGetEnd();
  while (m < mEnd) {
    m = mobyFindNextByOClass(m, GATE_OCLASS);
    if (!m) break;
    mobySetState(m, GATE_STATE_DEACTIVATED, -1);
    ++m;
  }
}
