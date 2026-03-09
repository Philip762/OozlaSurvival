#include <libdl/player.h>
#include <libdl/utils.h>
#include <libdl/game.h>
#include <libdl/moby.h>
#include <libdl/area.h>
#include <libdl/spawnpoint.h>
#include <libdl/stdio.h>
#include <libdl/string.h>
#include <libdl/weapon.h>
#include <libdl/cheats.h>
#include <libdl/dialog.h>
#include <libdl/stdlib.h>
#include <libdl/ui.h>
#include <libdl/net.h>
#include <libdl/gamesettings.h>
#include "utils.h"
#include "blip.h"
#include "shared.h"
#include "interop.h"
#include "gate.h"
#include "values.h"

#define CUSTOM_NET_MESSAGE_ID                   (190)

#define NIGHTMARE_ESCAPE_MODE_GATE_COST 		(150)
#define ESCAPE_MODE_GATE_COST 					(100)
#define ESCAPE_MODE_BOLT_COST 					(100000)
#define ESCAPE_MODE_GATE_UID					(7)
#define ESCAPE_MODE_RADAR_BLIP_UID				(9)
#define ESCAPE_MODE_AREA_IDX					(10)

// =================================== ESCAPE MODE GAMBIT ===================================
float mapGetCurrentDifficulty(void);
int gateCostPerPlayer = 0;
char playerEscapeStatus[GAME_MAX_PLAYERS] = {0,0,0,0,0,0,0,0,0,0};
const char *PLAYER_NOT_ESCAPED_MESSAGE = "\x11 Buy your freedom (%d bolts)";
const char *PLAYER_HAS_ESCAPED_MESSAGE = "%d out of %d player(s) remaining...";
typedef struct PlayerBuyEscapeMessage { char PlayerId; } PlayerBuyEscapeMessage_t;
int gambitsGetActiveValue(void);
//--------------------------------------------------------------------------
int getEscapedPlayerCount() {
    int count = 0;
    Player** players = playerGetAll();
    int i;
    for (i = 0; i < GAME_MAX_PLAYERS; i++) {
     	Player* player = players[i];
        if (!playerIsValid(player)) continue;
        if (playerEscapeStatus[player->PlayerId] == 1) count++;
    }
    return count;
}

//--------------------------------------------------------------------------
void playerInsideEscapeAreaTick(Player* player, int localPlayerIndex) {
	static char stringBuffer[4][64];
	struct SurvivalPlayer *playerData = &MapConfig.State->PlayerStates[player->PlayerId];

    if (playerEscapeStatus[player->PlayerId] == 1) {
        int totalPlayers = gameGetSettings()->PlayerCount;
        int remainingCount = totalPlayers - getEscapedPlayerCount();

        snprintf(stringBuffer[localPlayerIndex], sizeof(stringBuffer[localPlayerIndex]), PLAYER_HAS_ESCAPED_MESSAGE, remainingCount, totalPlayers);
	    uiShowPopup(localPlayerIndex, stringBuffer[localPlayerIndex]);
        playerData->MessageCooldownTicks = 2;
	} else {
        snprintf(stringBuffer[localPlayerIndex], sizeof(stringBuffer[localPlayerIndex]), PLAYER_NOT_ESCAPED_MESSAGE, ESCAPE_MODE_BOLT_COST);
	    uiShowPopup(localPlayerIndex, stringBuffer[localPlayerIndex]);
	    playerData->MessageCooldownTicks = 2;

	    if (playerData->State.Bolts >= ESCAPE_MODE_BOLT_COST && padGetButtonDown(localPlayerIndex, PAD_CIRCLE) > 0) {
            playerEscapeStatus[(int)player->PlayerId] = 1;
            playerData->State.Bolts -= (int)ESCAPE_MODE_BOLT_COST;
    	    playPaidSound(player);
            PlayerBuyEscapeMessage_t message = { .PlayerId = player->PlayerId };
            netBroadcastCustomAppMessage(NET_DELIVERY_CRITICAL, netGetDmeServerConnection(), CUSTOM_NET_MESSAGE_ID, sizeof(PlayerBuyEscapeMessage_t), &message);
	    }
    }
}

//--------------------------------------------------------------------------
int onPlayerBuyEscapeRemote(void* connection, void* data) {
    PlayerBuyEscapeMessage_t* message = (PlayerBuyEscapeMessage_t*)data;
    DPRINTF("Received escape purchase message from player %d\n", message->PlayerId);
    playerEscapeStatus[(int)message->PlayerId] = 1;
    Player* player = playerGetAll()[(int)message->PlayerId];
    playPaidSound(player);
    return sizeof(PlayerBuyEscapeMessage_t);
}

//--------------------------------------------------------------------------
void checkForEscapeModeCompletion(void) {
    static int tickCounter = 180;
    static int state = 0;

    // Step 1: detect completion
    if (state == 0) {
        if (getEscapedPlayerCount() == gameGetSettings()->PlayerCount) {
            state = 1;
            mapSendSendGambitCompletedMessage(gambitsGetActiveValue());
            DPRINTF("All players have escaped!\n");
        }
        return;
    }

    // Step 2: wait delay
    if (state == 1) {
        if (--tickCounter <= 0) {
            state = 2;
            MapConfig.State->GameOver = 1;
            //gameSetWinner(10, 1);
        }
    }
}

//--------------------------------------------------------------------------
void populateSpawnArgsFromConfigUncapped(struct MobSpawnEventArgs* output, struct MobConfig* config, int spawnParamsIdx, int isBaseConfig, int spawnFlags) {
  GameSettings* gs = gameGetSettings();
  if (!gs) return;

  int playerCount = gs->PlayerCount;
  float damage = config->Damage + DAMAGE_TABLE[playerCount];
  float speed = config->Speed;
  float health = config->Health * (1 + HEALTH_PERCENT_TABLE[playerCount]);

  float difficulty = mapGetCurrentDifficulty();
  if (isBaseConfig) {
    damage = damage * (1 + (MOB_BASE_DAMAGE_SCALE * config->DamageScale * difficulty));
    speed = speed * (1 + (MOB_BASE_SPEED_SCALE * config->SpeedScale * difficulty));
    health = health * powf(1 + (MOB_BASE_HEALTH_SCALE * config->HealthScale * difficulty), 2);
  }

  output->SpawnParamsIdx = spawnParamsIdx;
  output->Bolts = config->Bolts;
  output->Xp = config->Xp;
  output->StartHealth = health;
  output->Bangles = (u16)config->Bangles;
  output->Damage = (u16)damage;
  output->AttackRadiusEighths = (u8)(config->AttackRadius * 8);
  output->HitRadiusEighths = (u8)(config->HitRadius * 8);
  output->CollRadiusEighths = (u8)(config->CollRadius * 8);
  output->SpeedEighths = (u16)(speed * 8);
  output->ReactionTickCount = (u8)config->ReactionTickCount;
  output->AttackCooldownTickCount = (u8)config->AttackCooldownTickCount;
  output->DamageCooldownTickCount = (u16)config->DamageCooldownTickCount;
  output->MobAttribute = config->MobAttribute;
  output->Behavior = config->Behavior;
}

int assumedPlayerCount = 0;
void checkForPlayerLeft() {
    static int ticksSinceLastCheck = 0;
    ticksSinceLastCheck++;
    if (ticksSinceLastCheck < TPS) return;

    ticksSinceLastCheck = 0;
    int playersLeft = assumedPlayerCount - gameGetSettings()->PlayerCount;
    if (playersLeft == 0) return;

    assumedPlayerCount = assumedPlayerCount - playersLeft;
    Moby *gate = mobyFindByUID(7);
    if (!gate || gate->State == GATE_STATE_DEACTIVATED) return;

    int amountToReduce = playersLeft * gateCostPerPlayer;
    struct GatePVar* pvars = (struct GatePVar*)gate->PVar;
    if (pvars->CurrentCost > amountToReduce) {
        pvars->CurrentCost = pvars->CurrentCost - amountToReduce;
    } else {
        disableGate(gate);
    }
}

//--------------------------------------------------------------------------
void gambitsEscapeModeInit(void) {
    gateCostPerPlayer = ESCAPE_MODE_GATE_COST;
    netInstallCustomMsgHandler(CUSTOM_NET_MESSAGE_ID, &onPlayerBuyEscapeRemote);
    assumedPlayerCount = gameGetSettings()->PlayerCount;

	disableAllGates();
	Moby *gate = mobyFindByUID(7);
    if (gate) {
        enableGate(gate);
        struct GatePVar* pvars = (struct GatePVar*)gate->PVar;
        if (assumedPlayerCount == 1) {
            pvars->CurrentCost = 125;
        } else {
            pvars->CurrentCost = gateCostPerPlayer * assumedPlayerCount;
        }
    };

    Moby *blip = mobyFindByUID(ESCAPE_MODE_RADAR_BLIP_UID);
    if (blip) {
        mobySetState(blip, 1, -1);
    }
    
	DPRINTF("Initialized escape mode gambit\n");
}

//--------------------------------------------------------------------------
void gambitsNightmareEscapeModeInit(void) {
    gateCostPerPlayer = NIGHTMARE_ESCAPE_MODE_GATE_COST;
    netInstallCustomMsgHandler(CUSTOM_NET_MESSAGE_ID, &onPlayerBuyEscapeRemote);
    assumedPlayerCount = gameGetSettings()->PlayerCount;

	disableAllGates();
	Moby *gate = mobyFindByUID(7);
    if (gate) {
        enableGate(gate);
        struct GatePVar* pvars = (struct GatePVar*)gate->PVar;
        if (assumedPlayerCount == 1) {
            pvars->CurrentCost = 175;
        } else {
            pvars->CurrentCost = gateCostPerPlayer * assumedPlayerCount;
        }
    };

    Moby *blip = mobyFindByUID(ESCAPE_MODE_RADAR_BLIP_UID);
    if (blip) {
        mobySetState(blip, 1, -1);
    }
    
	DPRINTF("Initialized escape mode gambit\n");
}

//--------------------------------------------------------------------------
void gambitsEscapeModeTick(void) {
	checkForEscapeModeCompletion();
    checkForPlayerLeft();

	static int init = 0;
	if (!init && MapConfig.ClientsReady) {
		init = 1;
		MapConfig.Functions.ModePopulateSpawnArgsFunc = &populateSpawnArgsFromConfigUncapped;
	}

	Area_t area;
	if (!areaGetArea(ESCAPE_MODE_AREA_IDX, &area)) return;
	if (!area.Cuboids || area.CuboidCount != 1) return;
	SpawnPoint* boxCuboid = spawnPointGet(area.Cuboids[0]);

	int i;
	for (i = 0; i < GAME_MAX_LOCALS; i++) {
		Player* player = playerGetFromSlot(i);
		if (!playerIsValid(player)) continue;
		if (spawnPointIsPointInside(boxCuboid, player->PlayerPosition, NULL)) {
			playerInsideEscapeAreaTick(player, i);
		}
	}
}
