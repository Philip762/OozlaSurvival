#include <libdl/game.h>
#include <libdl/moby.h>
#include "shared.h"
#include "gate.h"

const float HEALTH_PERCENT_TABLE[GAME_MAX_PLAYERS] = {
    [0] = 0,
    [1] = 0,
    [2] = 0.05,
    [3] = 0.10,
    [4] = 0.15,
    [5] = 0.20,
    [6] = 0.30,
    [7] = 0.40,
    [8] = 0.50,
    [9] = 0.60
};

const float DAMAGE_TABLE[GAME_MAX_PLAYERS] = {
	[0] = 0,
  	[1] = 0,
  	[2] = 0.25,
  	[3] = 0.50,
  	[4] = 0.75,
  	[5] = 1.00,
  	[6] = 1.25,
  	[7] = 1.50,
  	[8] = 1.75,
	[9] = 2.00
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
