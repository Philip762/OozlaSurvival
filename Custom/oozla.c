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
#include <libdl/stdio.h>
#include <libdl/stdlib.h>
#include <libdl/random.h>
#include <libdl/ui.h>
#include <libdl/gamesettings.h>
#include "values.h"
#include "utils.h"
#include "shared.h"
#include "interop.h"
#include "gate.h"
#include "maputils.h"

#define WATER_DROWN_INVINCIBILIITY_SECONDS		(0.25f)

#define MEGACORP_STORE_AREA_IDX					(11)
#define OOB_AREA_LILY_PAD_COURSE_CHECKPOINT_IDX	(9)
#define OOB_AREA_LILY_PAD_COURSE_IDX			(1)
#define OOB_AREA_IDX_START						(2)
#define OOB_AREA_IDX_END						(8)

float mapGetCurrentDifficulty(void);

// =================================== GOING COMMANDO GAMBIT ===================================
char gambitsRandomWeapon[GAME_MAX_LOCALS] = {0};

//--------------------------------------------------------------------------
void gambitsUpYourArsenalOnRoundComplete(int roundNumber) {
  char buf[64];
  int i;
  for (i = 0; i < GAME_MAX_LOCALS; ++i) {
    int weaponId = weaponSlotToId(randRangeInt(WEAPON_SLOT_VIPERS, WEAPON_SLOT_COUNT-1));
    gambitsRandomWeapon[i] = weaponId;

    Player* player = playerGetFromSlot(i);
    if (!playerIsValid(player)) continue;

    // give max ammo
    if (player->GadgetBox->Gadgets[weaponId].Level < 0)
      playerGiveWeapon(player->GadgetBox, weaponId, 0, 1);
    else
      player->GadgetBox->Gadgets[weaponId].Ammo = playerGetWeaponMaxAmmo(player->GadgetBox, weaponId);

    snprintf(buf, sizeof(buf), "New Weapon \x0E%s\x08", uiMsgString(weaponGetDef(weaponId, 0)->basicQSTag));
    pushSnack(i, buf, TPS * 3);
  }
}

//--------------------------------------------------------------------------
void gambitsUpYourArsenalTick(void) {
  int i;

  for (i = 0; i < GAME_MAX_LOCALS; ++i) {
    if (gambitsRandomWeapon[i]) {
      mapLocalPlayerEnforceSingleWeaponRestriction(i, gambitsRandomWeapon[i], 0);
    }
  }
}

//--------------------------------------------------------------------------
void gambitsUpYourArsenalInit(void) {
	disableAllGates();
	gambitsUpYourArsenalOnRoundComplete(0); // initialize random weapons
}

// =================================== MAP ===================================
Moby *TeleporterMoby = NULL;
void mapReturnPlayersToMap(void); // Base survival function, called every tick
short LocalPlayerRespawnCuboid[GAME_MAX_PLAYERS]; // Respawn cuboid indices for every player

// Flags to track if a player has reached the prestige cave and should respawn there on drown
char PlayerCheckpointFlags[GAME_MAX_PLAYERS];

//--------------------------------------------------------------------------
int isPlayerInsideOfOutOfBoundsArea(Player* player, Area_t area) {
	int cuboidIdx = 1; // start from the second cuboid in the area
    for (; cuboidIdx < area.CuboidCount; cuboidIdx++) {
		SpawnPoint* boxCuboid = spawnPointGet(area.Cuboids[cuboidIdx]);
		if (spawnPointIsPointInside(boxCuboid, player->PlayerPosition, NULL)) {
			return 1;
		}
	}

	return 0;
}

//--------------------------------------------------------------------------
short getRespawnCuboidIdx(Player* player) {
	Area_t area;
	int areaIdx = OOB_AREA_IDX_START;
	for (; areaIdx <= OOB_AREA_IDX_END; areaIdx++) {
		if (!areaGetArea(areaIdx, &area)) continue;
		if (!area.Cuboids || area.CuboidCount <= 1) continue;
	
		if (isPlayerInsideOfOutOfBoundsArea(player, area)) {
			return (short)area.Cuboids[0]; // respawn cuboid is the first in list
		}
	}
	
	return -1; // No respawn cuboid found
}

//--------------------------------------------------------------------------
int isPlayerInsideMegacorpStore(Player* player) {
	Area_t area;
	if (!areaGetArea(MEGACORP_STORE_AREA_IDX, &area)) return 0;
	if (!area.Cuboids) return 0;

	int cuboidIdx = 0;
    for (; cuboidIdx < area.CuboidCount; cuboidIdx++) {
		SpawnPoint* boxCuboid = spawnPointGet(area.Cuboids[cuboidIdx]);
		if (spawnPointIsPointInside(boxCuboid, player->PlayerPosition, NULL)) {
			return 1;
		}
	}

	return 0;
}

//--------------------------------------------------------------------------
void oozlaDetermineRessurectionPoints(void) {
	int i;
	for (i = 0; i < GAME_MAX_LOCALS; i++) {
		Player* player = playerGetFromSlot(i);
		if (!playerIsValid(player)) continue;
		int playerId = player->PlayerId;
		if (player->PlayerState == PLAYER_STATE_QUICKSAND_SINK || player->PlayerState == PLAYER_STATE_QUICKSAND_JUMP) {
			player->timers.postHitInvinc = (int)(TPS * WATER_DROWN_INVINCIBILIITY_SECONDS);
			LocalPlayerRespawnCuboid[playerId] = getRespawnCuboidIdx(player);
		} else {
			LocalPlayerRespawnCuboid[playerId] = -1;
		}
	}

	VECTOR p, r, o;

	// if we're in between rounds, don't return
	if (!MapConfig.State) return;
	if (MapConfig.State->RoundEndTime) return;
	if (!TeleporterMoby) return;
	if (TeleporterMoby->State == 1) return;

	for (i = 0; i < GAME_MAX_LOCALS; ++i) {
		Player *player = playerGetFromSlot(i);
		if (!player || !player->SkinMoby) continue;
		if (isPlayerInsideMegacorpStore(player)) {
			// use player start
			playerGetSpawnpoint(player, p, r, 0);
			vector_fromyaw(o, (player->PlayerId / (float)GAME_MAX_PLAYERS) * MATH_TAU - MATH_PI);
			vector_scale(o, o, 2.5);
			vector_add(p, p, o);
			playerSetPosRot(player, p, r);
		}
	}
}

//--------------------------------------------------------------------------
int oozlaGetResurrectPoint(Player* player, VECTOR outPos, VECTOR outRot, int firstRes) {
	DPRINTF("oozlaGetResurrectPoint (player: %d firstRes: %d)\n", player->PlayerId, firstRes);
	if (!player->IsLocal) return 0;

	int i = player->PlayerId;
	if (LocalPlayerRespawnCuboid[i] >= 0) {
		DPRINTF("Respawn point set for player %d, going to cuboid with index: %d\n", i, LocalPlayerRespawnCuboid[i]);
		SpawnPoint* sp = spawnPointGet(LocalPlayerRespawnCuboid[i]);
		vector_copy(outPos, &sp->M0[12]);
		vector_copy(outRot, &sp->M1[12]);
		return 1;
	}

	DPRINTF("No respawn point set for player %d, going to spawn\n", i);
	return 0;
}

//--------------------------------------------------------------------------
void oozlaProcessLilyPadCourseDrowning(void) {
	int i;
	for (i = 0; i < GAME_MAX_LOCALS; i++) {
		Player* player = playerGetFromSlot(i);
		if (!playerIsValid(player)) continue;

		Area_t lilyPadCourseArea;
		if (!areaGetArea(OOB_AREA_LILY_PAD_COURSE_IDX, &lilyPadCourseArea)) continue;
		if (!lilyPadCourseArea.Cuboids || lilyPadCourseArea.CuboidCount <= 1) continue;

		if (!isPlayerInsideOfOutOfBoundsArea(player, lilyPadCourseArea)) {
			PlayerCheckpointFlags[player->PlayerId] = 0;
			continue;
		}

		Area_t checkPointArea;
		if (!areaGetArea(OOB_AREA_LILY_PAD_COURSE_CHECKPOINT_IDX, &checkPointArea)) continue;
		if (!checkPointArea.Cuboids || checkPointArea.CuboidCount <= 1) continue;
		if (isPlayerInsideOfOutOfBoundsArea(player, checkPointArea)) {
			PlayerCheckpointFlags[player->PlayerId] = 1;
		}

		if (player->PlayerState == PLAYER_STATE_QUICKSAND_SINK) {
			int respawnCuboidIdx = 0;
			float healthPercent = 0.0f;
			if (PlayerCheckpointFlags[player->PlayerId] == 1) {
				respawnCuboidIdx = (short)checkPointArea.Cuboids[0];
				healthPercent = 0.34f;
			} else {
				respawnCuboidIdx = (short)lilyPadCourseArea.Cuboids[0]; 
				healthPercent = 0.5f;
			}
			LocalPlayerRespawnCuboid[player->PlayerId] = respawnCuboidIdx;
			playerTeleportToSpawn(player, healthPercent);
		}
	}
}

//--------------------------------------------------------------------------
void oozlaUpdateTeleporter(void)
{
	if (!TeleporterMoby) return;
	
	if (MapConfig.State->RoundCompleteTime) {
		mobySetState(TeleporterMoby, 1, -1);
		TeleporterMoby->DrawDist = 64;
		TeleporterMoby->UpdateDist = 64;
		TeleporterMoby->CollActive = 0;
	} else {
		mobySetState(TeleporterMoby, 2, -1);
		TeleporterMoby->DrawDist = 0;
		TeleporterMoby->UpdateDist = 0;
		TeleporterMoby->CollActive = -1;
	}
}

//--------------------------------------------------------------------------
void oozlaPopulateSpawnArgsFromConfig(struct MobSpawnEventArgs* output, struct MobConfig* config, int spawnParamsIdx, int isBaseConfig, int spawnFlags) {
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

  // enforce max values
  if (config->MaxDamage > 0 && damage > config->MaxDamage) damage = config->MaxDamage;
  if (config->MaxSpeed > 0 && speed > config->MaxSpeed) speed = config->MaxSpeed;
  if (config->MaxHealth > 0 && health > config->MaxHealth) health = config->MaxHealth;
  
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

int gambitsGetActiveValue(void);
//--------------------------------------------------------------------------
void oozlaInit(void) {
	int i;
	for (i = 0; i < GAME_MAX_PLAYERS; i++) LocalPlayerRespawnCuboid[i] = -1;

	TeleporterMoby = mobyFindByUID(20);
	struct GadgetDef* mineLauncherDef = weaponGetDef(WEAPON_ID_MINE_LAUNCHER, 0);
	if (mineLauncherDef) mineLauncherDef->mpMaxAmmo = 15; // Set base max ammo

	MapConfig.Functions.OnPlayerGetResFunc = &oozlaGetResurrectPoint;
	HOOK_J_OP(&mapReturnPlayersToMap, &oozlaDetermineRessurectionPoints, 0);

	cheatsApplyWeather(WEATHER_LIGHT_RAIN_LIGHTNING);
	DPRINTF("OOZLA: Initialized\n");
	int activeGambit = gambitsGetActiveValue();
	DPRINTF("gambit: %d \n", activeGambit);
	if (activeGambit != 1 && activeGambit != 6) {
		Moby *gate = mobyFindByUID(7);
		if (gate) mobyDestroy(gate);
	} 
}

//--------------------------------------------------------------------------
void oozlaTick(void) {
	static int init = 0;
	if (!init && MapConfig.ClientsReady) {
		init = 1;
		MapConfig.Functions.ModePopulateSpawnArgsFunc = &oozlaPopulateSpawnArgsFromConfig;
	}
	oozlaUpdateTeleporter();
	oozlaProcessLilyPadCourseDrowning();
}
