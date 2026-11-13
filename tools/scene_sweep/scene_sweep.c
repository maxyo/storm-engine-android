// Game-side driver of tools/scene_sweep (see README.md). run.py copies this file
// into PROGRAM/ of a throw-away copy of the game; an engine built with
// -DSTORM_SCENE_SWEEP=ON loads it after Main() when STORM_SCENE_SWEEP_DIR is set.
//
// It starts a new game through the main menu, then loads every scene the runner
// lists with the functions the game itself uses for reloads (quest reload to a
// location, quest sea login, exit to the world map, world map storm, boarding),
// lets each scene render and hands the frame to the runner.
//
// Scenes ("kind:id", one per line of scenes.txt):
//   menu:main      main menu with its 3D back scene (always first, implicit)
//   newgame:intro  new game up to the opening dialogue (always second, implicit)
//   loc:<id>       location <id> at noon
//   night:<id>     location <id> at midnight
//   combat:<id>    location <id> with a sword fight against a spawned enemy
//   surface:<id>   underwater location <id> at noon, the camera looking up at the sea surface
//   sea:<island>   sea around <island> ("sea:" is open sea)
//   dusk:open      the open sea at sunset (19:00, the "19 Hour" weather: its sunset sky), heading towards the sun
//   dawn:open      the open sea at sunrise (7:00, the "7 Hour" weather), heading into the low sun (in view)
//   ship:night     the open sea at 0:00 (the "0 Hours" weather), the ship camera close and looking down at the hero's
//                  ship: rigging against the water, the bow wave and the hull at the waterline
//   battle:<island> sea around <island> with an attacking enemy ship, captured once it has fired
//   boarding:<island> boarding of that enemy ship (deck fight)
//   map:world      world map
//   storm:world    storm entered from the world map

native string SceneSweepNext();
native void SceneSweepEvent(string text);
native void SceneSweepCapture();
native int SceneSweepSettleMs();
native int SceneSweepVariants();
native void SceneSweepVariant(int index);
native int SceneSweepVariantReady();
#libriary "SceneSweepLibrary"

#event_handler("frame", "SceneSweep_Frame");

#define SS_BOOT		0	// waiting for the main menu
#define SS_LOAD		1	// waiting for the current step of the scene to finish loading
#define SS_SETTLE	2	// scene loaded, rendering before the capture
#define SS_NEWGAME	3	// character selection, then the new game itself
#define SS_DONE		4	// leaving the game

#define SS_SETTLE_FRAMES	5	// after the fader is gone; scene loading is synchronous
#define SS_VARIANT_WARM	4	// --variants: frames after the variant draws (it is prepared first) before the measured window
#define SS_VARIANT_READY_TIMEOUT_MS	60000	// the renderer links the variant's programs and self-tests it first
#define SS_VARIANT_FRAMES	12	// --variants: frames in the measured window
#define SS_FADE_TIMEOUT_MS	30000
#define SS_LOAD_TIMEOUT_MS	360000
#define SS_ENEMY_FIRE_TIMEOUT_MS	90000	// game time at sea before the battle enemy must have fired
#define SS_ENEMY_DISTANCE	350.0	// metres from the hero to the battle enemy; cannons reach ~700
#define SS_ENEMY_GROUP		"SceneSweepEnemy"
#define SS_ENEMY_CAPTAIN	"SceneSweepCaptain"

int ssState = SS_BOOT;
int ssFrames = 0;
int ssTime = 0;
int ssStep = 0;
int ssSettleTime = 0;
bool ssSteady = false;		// the fader is gone: the measured window has started
bool ssFrozen = false;		// --variants in a location: the game's time stands still for every variant
int ssVariant = 0;		// graphics settings variant of the next capture (--variants)
bool ssViaHome = false;
string ssHome = "";
string ssHomeGroup = "";
string ssHomeLocator = "";
string ssScene = "";
string ssKind = "";
string ssId = "";
string ssGroup = "";		// locator the hero enters the current location at
string ssLocator = "";
int ssEnemyBalls = 0;		// cannons the battle enemy has fired
int ssAtSeaTime = 0;		// game time at sea while waiting for them

void SceneSweep_SetState(int state)
{
	ssState = state;
	ssFrames = 0;
	ssTime = 0;
}

void SceneSweep_Begin(string scene)
{
	int split = findSubStr(scene, ":", 0);
	ssScene = scene;
	ssKind = strcut(scene, 0, split - 1);
	ssId = "";
	if(split + 1 < strlen(scene)) ssId = strcut(scene, split + 1, strlen(scene) - 1);
	ssStep = 0;
	ssViaHome = false;
	ssEnemyBalls = 0;
	ssAtSeaTime = 0;
	SceneSweepEvent("begin " + scene);
	SceneSweep_SetState(SS_LOAD);
}

void SceneSweep_Fail(string reason)
{
	SceneSweepEvent("fail " + ssScene + " " + reason);
	if(ssKind == "newgame")
	{
		// No game to load scenes into.
		SceneSweepEvent("abort");
		SceneSweep_SetState(SS_DONE);
		SetEventHandler("frame", "SceneSweep_Quit", 0);
		return;
	}
	SceneSweep_Next();
}

void SceneSweep_Next()
{
	string scene = SceneSweepNext();
	if(scene == "")
	{
		SceneSweepEvent("done");
		SceneSweep_SetState(SS_DONE);
		SetEventHandler("frame", "SceneSweep_Quit", 0);
		return;
	}
	SceneSweep_Begin(scene);
	SceneSweep_Start();
}

// Leaves the way the main menu's Exit does: from its own frame handler.
void SceneSweep_Quit()
{
	DelEventHandler("frame", "SceneSweep_Quit");
	ExitProgram();
}

// Every variant of a location shows the same picture: the game's time stands still from the first variant on (the
// follow camera, the hero's idle turns, people, sea and sky would move between them), the renderer goes on drawing.
// While it stands still the sweep's clock counts SS_FROZEN_FRAME_MS a frame (the game's delta time is 0 then).
#define SS_FROZEN_FRAME_MS	100
void SceneSweep_Freeze(bool frozen)
{
	if(frozen == ssFrozen) return;
	ssFrozen = frozen;
	if(frozen) SetTimeScale(0.0);
	else SetTimeScale(1.0);
}

// Graphics variants (--variants) apply to game scenes; the boot scenes (main menu, new game) are captured once.
bool SceneSweep_HasVariants()
{
	if(ssKind == "menu" || ssKind == "newgame") return false;
	return SceneSweepVariants() > 0;
}

void SceneSweep_Frame()
{
	aref fader;
	ssFrames++;
	if(ssFrozen) ssTime += SS_FROZEN_FRAME_MS;
	else ssTime += GetDeltaTime();
	switch(ssState)
	{
	case SS_BOOT:
		if(CurrentInterface == INTERFACE_MAINMENU)
		{
			if(sti(InterfaceStates.Launched)) SceneSweep_Begin("menu:main");
		}
		break;
	case SS_LOAD:
		if(SceneSweep_Loaded())
		{
			SceneSweepEvent("loaded " + ssScene);
			SceneSweep_SetState(SS_SETTLE);
			ssVariant = 0;
			if(SceneSweep_HasVariants()) SceneSweepVariant(0);
			ssSettleTime = 0;
			ssSteady = false;
			break;
		}
		if(ssTime > SS_LOAD_TIMEOUT_MS) SceneSweep_Fail("not loaded after " + ssTime + " ms, step " + ssStep);
		break;
	case SS_SETTLE:
		// The loading picture fades out first; the capture must show the scene itself.
		ssSettleTime += GetDeltaTime();
		if(GetEntity(fader, "fader"))
		{
			if(ssSettleTime > SS_FADE_TIMEOUT_MS)
			{
				SceneSweep_Fail("the loading fader is still on screen after " + ssSettleTime + " ms");
				break;
			}
			ssFrames = 0;
			ssTime = 0;
			break;
		}
		if(!ssSteady)
		{
			if(SceneSweep_HasVariants() && !SceneSweepVariantReady())
			{
				// The previous settings draw while the renderer prepares the variant's programs.
				if(ssTime > SS_VARIANT_READY_TIMEOUT_MS) SceneSweep_Fail("graphics variant " + ssVariant + " not active after " + ssTime + " ms");
				ssFrames = 0;
				break;
			}
			if(SceneSweep_HasVariants() && ssFrames < SS_VARIANT_WARM) break; // target rebuilds
			// Every variant of a location shows the same picture (SceneSweep_Freeze).
			if(SceneSweep_HasVariants())
			{
				if(ssKind == "loc") SceneSweep_Freeze(true);
				if(ssKind == "night") SceneSweep_Freeze(true);
			}
			// Restarts the runner's frame and renderer counters: the capture reports the scene alone.
			SceneSweepEvent("steady " + ssScene);
			ssSteady = true;
			ssFrames = 0;
			ssTime = 0;
			break;
		}
		if(ssFrames < SS_SETTLE_FRAMES) break;
		if(SceneSweep_HasVariants() && ssFrames < SS_VARIANT_FRAMES) break;
		if(ssTime < SceneSweepSettleMs()) break;
		SceneSweepCapture();
		if(SceneSweep_HasVariants() && ssVariant + 1 < SceneSweepVariants())
		{
			// The same scene again with the next graphics settings (they apply from the next frame).
			ssVariant++;
			SceneSweepVariant(ssVariant);
			ssSteady = false;
			ssFrames = 0;
			ssTime = 0;
			break;
		}
		SceneSweepEvent("end " + ssScene);
		SceneSweep_Freeze(false);
		SceneSweep_Finish();
		break;
	case SS_NEWGAME:
		// The character selection screen with its defaults, as a player who just presses OK.
		if(CurrentInterface == INTERFACE_CHARACTER_SELECT)
		{
			if(sti(InterfaceStates.Launched))
			{
				Event("exitOk");
				SceneSweep_Begin("newgame:intro");
			}
		}
		break;
	}
}

// What follows a captured scene.
void SceneSweep_Finish()
{
	switch(ssKind)
	{
	case "menu":
		Event("backgroundcommand", "s", "New");
		SceneSweep_SetState(SS_NEWGAME);
		return;
		break;
	case "newgame":
		SceneSweep_PreparePlayer();
		break;
	}
	SceneSweep_Next();
}

// The opening dialogue is over for the sweep: free the hero and give him a ship,
// as the game does for heroes that start with one.
void SceneSweep_PreparePlayer()
{
	DialogExit();
	// A new game leaves its opening weather ("11 Hour", seadogs.c startGameWeather) pending until the next
	// CreateWeatherEnvironment, which applies it over the location's QuestlockWeather and then clears it. The
	// intro did not use it up, so the first scene of the sweep got it (UnderWater with day lighting instead of
	// its "Underwater" weather). Every scene sets its own time and weather.
	startGameWeather = false;
	ssHome = pchar.location;
	ssHomeGroup = pchar.location.group;
	ssHomeLocator = pchar.location.locator;
	chrDisableReloadToLocation = false;
	LAi_SetPlayerType(pchar);
	LAi_SetImmortal(pchar, true);
	pchar.Ship.Type = GenerateShipExt(SHIP_CORVETTE, 0, pchar);
	SetBaseShipData(pchar);
	pchar.Ship.Name = "Scene Sweep";
	SetCrewQuantityFull(pchar);
	// run.py compares these with the tables it derived from the scripts.
	int i;
	int islandCount = 0;
	for(i = 0; i < MAX_ISLANDS; i++)
	{
		if(Islands[i].id != "") islandCount++;
	}
	int locationCount = 0;
	for(i = 0; i < nLocationsNum; i++)
	{
		if(!CheckAttribute(&Locations[i], "id")) continue;
		if(Locations[i].id == "" || Locations[i].id == "NONE") continue;
		if(FindLocation(Locations[i].id) == i) locationCount++;	// ids can repeat; the first one wins
	}
	SceneSweepEvent("tables locations=" + locationCount + " islands=" + islandCount);
}

bool SceneSweep_OnMap()
{
	if(!IsEntity(&worldMap)) return false;
	return worldMap.isLoaded == "true";
}

bool SceneSweep_AtSea()
{
	if(!bSeaActive) return false;
	if(bAbordageStarted) return false;
	return bSeaLoaded;
}

bool SceneSweep_InLocation(string id)
{
	int loaded = FindLoadedLocation();
	if(loaded < 0) return false;
	return Locations[loaded].id == id;
}

// Sea scenes are entered from land, as a player leaves a port. run.py puts them
// between land scenes; anywhere else the hero first returns to where the game began.
bool SceneSweep_NeedsLand()
{
	if(ssKind == "loc" || ssKind == "night" || ssKind == "combat" || ssKind == "surface") return false;
	if(ssKind == "map" || ssKind == "storm")
	{
		if(SceneSweep_OnMap()) return false;
		if(SceneSweep_AtSea()) return false;
	}
	if(SceneSweep_OnMap()) return true;
	return bSeaActive;
}

// Starts the current step of the current scene.
void SceneSweep_Start()
{
	if(ssStep == 0)
	{
		if(SceneSweep_NeedsLand())
		{
			ssViaHome = true;
			chrDisableReloadToLocation = false;
			sQuestNameAfterReload = "_";
			if(!DoQuestReloadToLocation(ssHome, ssHomeGroup, ssHomeLocator, "")) SceneSweep_Fail("the game refused the reload to land");
			return;
		}
	}
	switch(ssKind)
	{
	case "loc":		SceneSweep_StartLocation(12);	break;
	case "combat":	SceneSweep_StartLocation(12);	break;
	case "night":	SceneSweep_StartLocation(0);	break;
	case "surface":	SceneSweep_StartLocation(12);	break;
	case "sea":		SceneSweep_StartSea(false);		break;
	case "dusk":	SceneSweep_StartSea(false);		break;
	case "dawn":	SceneSweep_StartSea(false);		break;
	case "ship":	SceneSweep_StartSea(false);		break;
	case "battle":	SceneSweep_StartSea(true);		break;
	case "boarding":
		if(ssStep == 0) SceneSweep_StartSea(true);
		else Sea_AbordageStartNow(SHIP_ABORDAGE, GetCharacterIndex(SS_ENEMY_CAPTAIN), true, true);
		break;
	case "map":
		if(ssStep == 0)
		{
			if(!SceneSweep_OnMap()) SceneSweep_StartSea(false);
		}
		else Sea_MapLoad();
		break;
	case "storm":
		if(ssStep == 0)
		{
			if(SceneSweep_OnMap()) ssStep = 2;
			else SceneSweep_StartSea(false);
		}
		if(ssStep == 1) Sea_MapLoad();
		if(ssStep == 2)
		{
			// What the world map does when the player's ship sails into a storm.
			worldMap.playerInStorm = "1";
			wdmReloadToSea();
		}
		break;
	}
	if(ssKind != "loc" && ssKind != "combat" && ssKind != "night" && ssKind != "surface" && ssKind != "sea" && ssKind != "dusk" && ssKind != "dawn" && ssKind != "ship" &&
		ssKind != "battle" && ssKind != "boarding" && ssKind != "map" && ssKind != "storm")
	{
		SceneSweep_Fail("unknown scene kind");
	}
}

// True when the current scene is on screen; advances multi-step scenes.
bool SceneSweep_Loaded()
{
	aref fader;
	if(ssViaHome)
	{
		if(!SceneSweep_InLocation(ssHome)) return false;
		if(GetEntity(fader, "fader")) return false;
		ssViaHome = false;
		SceneSweep_Start();
		return false;
	}
	switch(ssKind)
	{
	case "menu":	return true;	break;
	case "newgame":
		if(FindLoadedLocation() < 0) return false;
		return DialogRun;
		break;
	case "loc":		return SceneSweep_InLocationAtEntry();	break;
	case "night":	return SceneSweep_InLocationAtEntry();	break;
	case "surface":
		if(!SceneSweep_InLocationAtEntry()) return false;
		if(ssStep == 0)
		{
			ssStep = 1;
			float x, y, z;
			GetCharacterPos(pchar, &x, &y, &z);
			// From the hero's head up through the water at the surface (the hero stands on the sea bed).
			locCameraFromToPos(x, y + 1.7, z, true, x + 12.0, y + 40.0, z + 12.0);
		}
		return true;
		break;
	case "combat":
		if(!SceneSweep_InLocationAtEntry()) return false;
		if(ssStep == 0)
		{
			ssStep = 1;
			SceneSweep_StartCombat();
		}
		return true;
		break;
	case "sea":		return SceneSweep_AtSea();	break;
	case "dusk":	return SceneSweep_AtSea();	break;
	case "dawn":	return SceneSweep_AtSea();	break;
	case "ship":
		if(!SceneSweep_AtSea()) return false;
		if(ssStep == 0)
		{
			ssStep = 1;
			// Close behind the ship, about 50 degrees down: the camera's pitch is held between these limits.
			SeaShipCamera.MinDistance = 12.0;
			SeaShipCamera.Distance = 0.0;
			SeaShipCamera.MinAngleX = -0.95;
			SeaShipCamera.MaxAngleX = -0.9;
		}
		return true;
		break;
	case "battle":
		if(!SceneSweep_AtSea()) return false;
		// The enemy attacks the hero: its cannons must fire (sea AI relations, cannons, balls).
		if(ssEnemyBalls > 0) return true;
		ssAtSeaTime += GetDeltaTime();
		if(ssAtSeaTime > SS_ENEMY_FIRE_TIMEOUT_MS) SceneSweep_Fail("the enemy ship did not fire within " + ssAtSeaTime + " ms at sea");
		return false;
		break;
	case "boarding":
		if(ssStep == 0)
		{
			if(SceneSweep_AtSea()) SceneSweep_NextStep();
			return false;
		}
		if(!bAbordageStarted) return false;
		return FindLoadedLocation() >= 0;
		break;
	case "map":
		if(SceneSweep_OnMap()) return true;
		if(ssStep == 0)
		{
			if(SceneSweep_AtSea()) SceneSweep_NextStep();
		}
		return false;
		break;
	case "storm":
		if(ssStep == 0)
		{
			if(SceneSweep_AtSea()) SceneSweep_NextStep();
			return false;
		}
		if(ssStep == 1)
		{
			if(SceneSweep_OnMap()) SceneSweep_NextStep();
			return false;
		}
		if(!SceneSweep_AtSea()) return false;
		return bStorm;
		break;
	}
	return false;
}

void SceneSweep_NextStep()
{
	ssStep++;
	SceneSweepEvent("step " + ssStep);
	SceneSweep_SetState(SS_LOAD);
	SceneSweep_Start();
}

// The location is loaded; the hero must stand at the entry locator. Locators come from
// the location's model, so a missing one shows only now (the game merely traces it).
bool SceneSweep_InLocationAtEntry()
{
	if(!SceneSweep_InLocation(ssId)) return false;
	int loaded = FindLoadedLocation();
	if(!CheckAttribute(&Locations[loaded], "locators." + ssGroup + "." + ssLocator))
	{
		SceneSweep_Fail("teleport failed: the location has no locator " + ssGroup + "::" + ssLocator);
		return false;
	}
	return true;
}

void SceneSweep_StartLocation(int hour)
{
	int n = FindLocation(ssId);
	if(n < 0)
	{
		SceneSweep_Fail("no such location");
		return;
	}
	// Enter where the game itself puts the hero: boarding locations (decks, cabins) at
	// their boarding.Loc.Hero locator in the "rld" group, as Cabin_LoadLocation and the
	// teleport menu do; other locations through their first door, or the first walk point.
	string group = "goto";
	string locator = "goto1";
	aref reloads;
	if(CheckAttribute(&Locations[n], "boarding.Loc.Hero"))
	{
		group = "rld";
		locator = Locations[n].boarding.Loc.Hero;
	}
	else
	{
		// A deck one only reaches from the deck before it in a boarding (the gun deck): it has
		// no hero locator of its own and no doors; the fighters' places ("rld") are its walk points.
		if(CheckAttribute(&Locations[n], "boarding") && !CheckAttribute(&Locations[n], "reload"))
		{
			group = "rld";
			locator = "loc0";
		}
		if(CheckAttribute(&Locations[n], "reload"))
		{
			makearef(reloads, Locations[n].reload);
			if(GetAttributesNum(reloads) > 0)
			{
				aref first = GetAttributeN(reloads, 0);
				if(CheckAttribute(first, "name"))
				{
					group = "reload";
					locator = first.name;
				}
			}
		}
	}
	ssGroup = group;
	ssLocator = locator;
	// A day passes between locations, as it does for a player travelling between them:
	// phantom NPCs (citizens, soldiers) are freed by EmptyAllFantomCharacter on the next
	// reload only once their LifeDay is over. Hundreds of locations within one game day
	// would fill the 1200 character slots (NPC_GeneratePhantomCharacter returns -1).
	AddDataToCurrent(0, 0, 1);
	SetCharacterGoods(pchar, GOOD_FOOD, 500);	// and the hero restocks, or the crew starves on the way
	SetCurrentTime(hour, 0);
	if(hour == 0) SetNextWeather("0 Hours");
	else SetNextWeather("12 Hour");
	chrDisableReloadToLocation = false;
	sQuestNameAfterReload = "_";
	if(!DoQuestReloadToLocation(ssId, group, locator, "")) SceneSweep_Fail("the game refused the reload to " + group + ":" + locator);
}

// The sea login angle ay with which the camera behind the ship looks towards the sun at `hour`, `offset` radians
// to the side (so that the ship does not hide it). WEATHER moves the sun from azimuth 0 at 5:00 to PI at 23:00
// (WEATHER::SunAt, the game keeps the defaults) and puts it at (cos az, -sin az) in x, z; the camera of a ship
// logged in at ay looks along (sin(ay - PI), cos(ay - PI)) (measured: its yaw is ay - PI).
float SceneSweep_SunHeading(float hour, float offset)
{
	float az = (hour - 5.0) / 18.0 * PI;
	float x = cos(az);
	float z = -sin(az);
	if(abs(z) < 0.001) z = 0.001;
	float ay = atan(x / z);
	if(z < 0.0) ay = ay + PI;
	return ay + PI + offset;
}

void SceneSweep_StartSea(bool withEnemy)
{
	if(SceneSweep_AtSea())
	{
		if(!withEnemy)
		{
			if(ssKind == "map" || ssKind == "storm") return;	// already where the step leads
		}
	}
	// The game's sunset sky is the 19:00 weather, while WEATHER::SunAt still has the sun about 39 degrees up (above
	// the view); at 7:00 the sun stands about 15 degrees above the sea, in view, with its glow and flare.
	if(ssKind == "dusk")
	{
		SetCurrentTime(19, 0);
		SetNextWeather("19 Hour");
	}
	if(ssKind == "dawn")
	{
		SetCurrentTime(7, 0);
		SetNextWeather("7 Hour");
	}
	if(ssKind == "ship")
	{
		SetCurrentTime(0, 0);
		SetNextWeather("0 Hours");
	}
	if(ssKind != "dusk" && ssKind != "dawn" && ssKind != "ship")
	{
		SetCurrentTime(12, 0);
		SetNextWeather("12 Hour");
	}
	string island = ssId;
	if(ssKind == "map" || ssKind == "storm" || ssKind == "dusk" || ssKind == "dawn" || ssKind == "ship") island = "";
	DeleteAttribute(&questToSeaLoginer, "");
	QuestToSeaLogin_Prepare(0.0, 0.0, island);
	// The open sea, the ship (and its camera) heading into the sun: sun glare, shafts, the sun road.
	if(ssKind == "dusk") questToSeaLoginer.PlayerGroup.ay = SceneSweep_SunHeading(19.0, 0.3);	// the sunset sky ahead
	if(ssKind == "dawn") questToSeaLoginer.PlayerGroup.ay = SceneSweep_SunHeading(7.0, 0.45);
	if(island != "")
	{
		if(FindIsland(island) < 0)
		{
			SceneSweep_Fail("no such island");
			return;
		}
		QuestToSeaLogin_PrepareLoc(island, "reload", "l1", false);
		// At sunset the ship (and its camera) heads into the sun: sun glare, shafts, the sun road.
	}
	if(withEnemy)
	{
		ref captain = GetCharacter(NPC_GenerateCharacter(SS_ENEMY_CAPTAIN, "mercen_1", "man", "man", 10, PIRATE, 1, true, "quest"));
		FantomMakeCoolSailor(captain, SHIP_BRIG, "", CANNON_TYPE_CANNON_LBS12, 50, 50, 50);
		captain.AlwaysEnemy = true;	// hostile whatever the nations' relations, as the game's quest ambushes
		Group_DeleteGroup(SS_ENEMY_GROUP);
		Group_FindOrCreateGroup(SS_ENEMY_GROUP);
		Group_AddCharacter(SS_ENEMY_GROUP, SS_ENEMY_CAPTAIN);
		Group_SetGroupCommander(SS_ENEMY_GROUP, SS_ENEMY_CAPTAIN);
		Group_SetTaskAttack(SS_ENEMY_GROUP, PLAYER_GROUP);
		Group_LockTask(SS_ENEMY_GROUP);
		// Within cannon range, ahead of where the hero enters the sea (the game's
		// Group_SetPursuitGroup puts it 500-1200 m away at a random angle, often out of range).
		ref enemyGroup = Group_FindOrCreateGroup(SS_ENEMY_GROUP);
		ref seaIsland = GetIslandByID(island);
		float heading = stf(seaIsland.reload.l1.ay);
		enemyGroup.Pos.x = stf(seaIsland.reload.l1.x) + SS_ENEMY_DISTANCE * sin(heading);
		enemyGroup.Pos.z = stf(seaIsland.reload.l1.z) + SS_ENEMY_DISTANCE * cos(heading);
		enemyGroup.Pos.ay = heading;
		QuestToSeaLogin_Add(SS_ENEMY_GROUP);
		DelEventHandler(CANNON_FIRE, "SceneSweep_CannonFire");
		SetEventHandler(CANNON_FIRE, "SceneSweep_CannonFire", 0);
	}
	QuestToSeaLogin_Launch();
}

// Every cannon shot at sea (the game's Cannon_FireCannon adds the ball).
void SceneSweep_CannonFire()
{
	aref shooter = GetEventData();
	if(shooter.id == SS_ENEMY_CAPTAIN) ssEnemyBalls++;
}

// A quest-style ambush: one armed enemy next to the hero, both sides drawn.
void SceneSweep_StartCombat()
{
	ref enemy = GetCharacter(NPC_GenerateCharacter("SceneSweepEnemy", "mercen_1", "man", "man", 10, PIRATE, 0, true, "quest"));
	FantomMakeCoolFighter(enemy, 10, 30, 30, "blade_10", "pistol1", "bullet", 50);
	ChangeCharacterAddressGroup(enemy, ssId, pchar.location.group, pchar.location.locator);
	LAi_SetWarriorType(enemy);
	LAi_group_MoveCharacter(enemy, "EnemyFight");
	LAi_group_SetRelation("EnemyFight", LAI_GROUP_PLAYER, LAI_GROUP_ENEMY);
	LAi_group_FightGroups("EnemyFight", LAI_GROUP_PLAYER, true);
	LAi_SetFightMode(pchar, true);
}
