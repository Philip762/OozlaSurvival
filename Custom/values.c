#include <libdl/game.h>
#include "shared.h"

const float HEALTH_PERCENT_TABLE[GAME_MAX_PLAYERS] = {
	[0] = 0,
  	[1] = 0,
  	[2] = 0,
  	[3] = 0.05,
  	[4] = 0.10,
  	[5] = 0.15,
  	[6] = 0.20,
  	[7] = 0.30,
  	[8] = 0.40,
	[9] = 0.50
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
