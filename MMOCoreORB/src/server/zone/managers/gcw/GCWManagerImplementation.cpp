/*
 * GCWManagerImplementation.cpp
 *
 *  Created on: Oct 22, 2012
 *      Author: root
 */
#include "server/zone/managers/gcw/GCWManager.h"
#include "server/zone/Zone.h"
#include "server/zone/ZoneServer.h"
#include "server/zone/objects/building/BuildingObject.h"
#include "server/zone/objects/player/PlayerObject.h"
#include "server/zone/objects/creature/ai/AiAgent.h"
#include "server/zone/objects/installation/InstallationObject.h"
#include "server/zone/objects/tangible/deed/Deed.h"
#include "server/zone/objects/tangible/deed/structure/StructureDeed.h"

#include "server/zone/objects/building/components/DestructibleBuildingDataComponent.h"
#include "server/zone/objects/tangible/terminal/components/TurretControlTerminalDataComponent.h"
#include "server/zone/objects/installation/components/MinefieldDataComponent.h"
#include "server/zone/objects/installation/components/TurretDataComponent.h"

#include "server/zone/managers/gcw/tasks/StartVulnerabilityTask.h"
#include "server/zone/managers/gcw/tasks/EndVulnerabilityTask.h"
#include "server/zone/managers/gcw/tasks/BaseDestructionTask.h"
#include "server/zone/managers/gcw/tasks/CheckGCWTask.h"
#include "server/zone/managers/gcw/tasks/SecurityRepairTask.h"

#include "server/zone/objects/player/sui/messagebox/SuiMessageBox.h"
#include "server/zone/objects/player/sui/transferbox/SuiTransferBox.h"
#include "server/zone/objects/player/sui/inputbox/SuiInputBox.h"

#include "server/zone/objects/player/sui/callbacks/HQDefenseStatusSuiCallback.h"
#include "server/zone/objects/player/sui/callbacks/JamUplinkSuiCallback.h"
#include "server/zone/objects/player/sui/callbacks/OverrideTerminalSuiCallback.h"
#include "server/zone/objects/player/sui/callbacks/PowerRegulatorSuiCallback.h"
#include "server/zone/objects/player/sui/callbacks/RemoveDefenseSuiCallback.h"
#include "server/zone/objects/player/sui/callbacks/DonateDefenseSuiCallback.h"
#include "server/zone/objects/player/sui/callbacks/SelectTurretDonationSuiCallback.h"
#include "server/zone/objects/player/sui/callbacks/TurretControlSuiCallback.h"

#include "server/zone/managers/structure/StructureManager.h"
#include "server/zone/packets/scene/PlayClientEffectLocMessage.h"

//#define GCW_DEBUG

void GCWManagerImplementation::initialize() {
	// TODO: initialize things
}

void GCWManagerImplementation::start() {
	loadLuaConfig();

	// randomize a bit so every zone doesn't run it's check at the same time
	uint64 timer = (uint64)(System::random(gcwCheckTimer / 10) + gcwCheckTimer) * 1000;

	CheckGCWTask* task = new CheckGCWTask(_this.getReferenceUnsafeStaticCast());
	task->schedule(timer);

	initialize();
}

void GCWManagerImplementation::loadLuaConfig() {
	Locker locker(&baseMutex);

	info("Loading gcw configuration file.");

	Lua* lua = new Lua();
	lua->init();
	lua->runFile("scripts/managers/gcw_manager.lua");

	gcwCheckTimer = lua->getGlobalInt("gcwCheckTimer");
	vulnerabilityDuration = lua->getGlobalInt("vulnerabilityDuration");
	vulnerabilityFrequency = lua->getGlobalInt("vulnerabilityFrequency");
	resetTimer = lua->getGlobalInt("resetTimer");
	sliceCooldown = lua->getGlobalInt("sliceCooldown");
	totalDNASamples = lua->getGlobalInt("totalDNASamples");
	dnaStrandLength = lua->getGlobalInt("dnaStrandLength");
	powerSwitchCount = lua->getGlobalInt("powerSwitchCount");
	destructionTimer = lua->getGlobalInt("destructionTimer");
	maxBases = lua->getGlobalInt("maxBases");
	overtCooldown = lua->getGlobalInt("overtCooldown");
	reactivationTimer = lua->getGlobalInt("reactivationTimer");
	turretAutoFireTimeout = lua->getGlobalInt("turretAutoFireTimeout");
	maxBasesPerPlayer = lua->getGlobalInt("maxBasesPerPlayer");
	bonusXP = lua->getGlobalInt("bonusXP");
	winnerBonus = lua->getGlobalInt("winnerBonus");
	loserBonus = lua->getGlobalInt("loserBonus");
	racialPenaltyEnabled = lua->getGlobalInt("racialPenaltyEnabled");
	initialVulnerabilityDelay = lua->getGlobalInt("initialVulnerabilityDelay");
	spawnDefenses = lua->getGlobalInt("spawnDefenses");

	LuaObject nucleotides = lua->getGlobalObject("dnaNucleotides");
	if (nucleotides.isValidTable()) {
		for(int i = 1; i <= nucleotides.getTableSize(); ++i) {
			dnaNucleotides.add(nucleotides.getStringAt(i));
		}
	}
	nucleotides.pop();

	LuaObject pairs = lua->getGlobalObject("dnaPairs");
	if (pairs.isValidTable()) {
		for(int i = 1; i <= pairs.getTableSize(); ++i) {
			dnaPairs.add(pairs.getStringAt(i));
		}
	}
	pairs.pop();

	LuaObject pointsObject = lua->getGlobalObject("HQValues");

	if (pointsObject.isValidTable()) {

		for(int i = 1; i <= pointsObject.getTableSize(); ++i) {
			LuaObject baseObject = pointsObject.getObjectAt(i);
			if (baseObject.isValidTable()) {
				String templateString = baseObject.getStringAt(1);
				int pointsValue = baseObject.getIntAt(2);
				addPointValue(templateString, pointsValue);
			}
			baseObject.pop();

		}
	}

	pointsObject.pop();

	info("Loaded " + String::valueOf(baseValue.size()) + " GCW base scoring values.");

	LuaObject penaltyObject = lua->getGlobalObject("imperial_racial_penalty");
	if (penaltyObject.isValidTable()) {
		for(int i = 1; i <= penaltyObject.getTableSize(); ++i) {
			LuaObject raceObject = penaltyObject.getObjectAt(i);
			if (raceObject.isValidTable()) {
				int race = raceObject.getIntAt(1);
				float penalty = raceObject.getFloatAt(2);
				addRacialPenalty(race, penalty);
			}
			raceObject.pop();
		}
	}

	penaltyObject.pop();

	info("Loaded " + String::valueOf(racialPenaltyMap.size()) + " racial penalties.");

	LuaObject strongholdsObject = lua->getGlobalObject("strongholdCities");
	if (strongholdsObject.isValidTable()) {
		LuaObject imperialObject = strongholdsObject.getObjectField("imperial");
		if (imperialObject.isValidTable()) {
			for(int i = 1; i <= imperialObject.getTableSize(); ++i) {
				imperialStrongholds.add(imperialObject.getStringAt(i));
			}
		}
		imperialObject.pop();

		LuaObject rebelObject = strongholdsObject.getObjectField("rebel");
		if (rebelObject.isValidTable()) {
			for(int i = 1; i <= rebelObject.getTableSize(); ++i) {
				rebelStrongholds.add(rebelObject.getStringAt(i));
			}
		}
		rebelObject.pop();
	}

	strongholdsObject.pop();

	info("Loaded " + String::valueOf(imperialStrongholds.size()) + " imperial strongholds and " + String::valueOf(rebelStrongholds.size()) + " rebel strongholds.");
}

// PRE: Nothing needs to be locked
// should only be called by the startvulnerabilityTask or when loading from the db in the middle of vuln
void GCWManagerImplementation::startVulnerability(BuildingObject* building) {
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

#ifdef GCW_DEBUG
	info("BASE " + String::valueOf(building->getObjectID()) + " IS NOW VULNERABLE " + Time().getFormattedTime(),true);
#endif

	renewUplinkBand(building);

	if (baseData == NULL) {
		error("ERROR:  could not get base data for base");
		return;
	}

	Locker block(building);
	baseData->setLastVulnerableTime(baseData->getNextVulnerableTime());
	block.release();

	if (!dropStartTask(building->getObjectID())) {
#ifdef GCW_DEBUG
		error("No starttask found to drop while starting vulnerability");
#endif
	}

	Locker block2(building);

	if (building->getZone() == NULL)
		return;

	verifyTurrets(building);
	scheduleVulnerabilityEnd(building);
	building->broadcastCellPermissions();
}

void GCWManagerImplementation::initializeNewVulnerability(BuildingObject* building) {
	Locker _lock(building);
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);
	if (baseData == NULL)
		return;

	initializeNewVulnerability(baseData);
	_lock.release();
}

// PRE:  building / objectdatacomponent are locked
void GCWManagerImplementation::initializeNewVulnerability(DestructibleBuildingDataComponent* baseData) {
	baseData->setTerminalDamaged(false);
	baseData->setState(DestructibleBuildingDataComponent::VULNERABLE);
	baseData->setRebootFinishTime(Time(0));
}

// PRE: nothing needs to be locked!
void GCWManagerImplementation::scheduleVulnerabilityStart(BuildingObject* building) {
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (!hasBase(building))
		return;


	if (baseData == NULL)
		return;

#ifdef GCW_DEBUG
	info("Base " + String::valueOf(building->getObjectID()) + " scheduling next vulnerable time for " + baseData->getNextVulnerableTime().getFormattedTime(),true);
#endif

	Time vulnTime = baseData->getNextVulnerableTime();
	int64 vulnDif = vulnTime.miliDifference();
	if (vulnDif >= 0) {
#ifdef GCW_DEBUG
		info("Base: " + String::valueOf(building->getObjectID()) + " Cannot schedule start time.  IT has already passed",true);
#endif
		return;
	}

	Reference<Task*> newTask = new StartVulnerabilityTask(_this.getReferenceUnsafeStaticCast(), building);
	newTask->schedule(llabs(vulnDif));
	addStartTask(building->getObjectID(),newTask);
}

// changes timers and schedules nextVulnerabilityStart task
void GCWManagerImplementation::endVulnerability(BuildingObject* building) {
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL)
		return;

#ifdef GCW_DEBUG
	info("BASE " + String::valueOf(building->getObjectID()) + " IS NO LONGER VULNERABLE " + Time().getFormattedTime(),true);
#endif

	Locker block(building);

	baseData->setLastVulnerableTime(baseData->getNextVulnerableTime());

	Time nextTime;

	if (baseData->getLastVulnerableTime().getTime() == 0)
		nextTime = baseData->getNextVulnerableTime();
	else
		nextTime = baseData->getLastVulnerableTime();

	int64 intPeriodsPast = (llabs(nextTime.miliDifference())) / (vulnerabilityFrequency*1000);

	// TODO: use periodspast to get the amount of time to add and avoid the loop
	while (nextTime.isPast()) {
		nextTime.addMiliTime(vulnerabilityFrequency*1000);
	}

	baseData->setNextVulnerableTime(nextTime);
	nextTime.addMiliTime(vulnerabilityDuration*1000);
	baseData->setVulnerabilityEndTime(nextTime);
	baseData->setState(DestructibleBuildingDataComponent::INVULNERABLE);

	block.release();

	// TODO: check the destruction task list and remove the destruction task
	if (!dropEndTask(building->getObjectID()))
		info("No endtask found to remove while scheduling new startvulnerability task",true);

	// schedule
	scheduleVulnerabilityStart(building);
	verifyTurrets(building);
	building->broadcastCellPermissions();

}

// only call if the last expired time has already past and we need the timers
// back up to date.  usually after a long server down or something
void GCWManagerImplementation::refreshExpiredVulnerability(BuildingObject* building) {
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);
	if (baseData == NULL) {
		error("ERROR:  could not get base data for base");
		return;
	}

	Time thisStartTime(baseData->getLastVulnerableTime());

#ifdef GCW_DEBUG
	info("BEfore Refreshed Current time is  " + Time().getFormattedTime(),true);
	info("before Refreshed NextStart is     " + thisStartTime.getFormattedTime(),true);
	info("before Refreshed Next end time is " + baseData->getVulnerabilityEndTime().getFormattedTime(),true);
#endif

	while ((thisStartTime.getTime() + vulnerabilityFrequency) <= Time().getTime()) {
		int amountToAdd = vulnerabilityFrequency*1000;
		thisStartTime.addMiliTime(amountToAdd);
	}

#ifdef GCW_DEBUG
	info("Looped starttime to get " + thisStartTime.getFormattedTime(),true);
#endif

	// test time is the vulnerability end time for this current period.  it can be past or presetnt.
	Time testTime(thisStartTime);
	testTime.addMiliTime(vulnerabilityDuration*1000);

	Locker block(building);

	if (!testTime.isPast()) {
		// if we're still in a vuln period

		info("Loaded while vulnerable in refresh",true);
		baseData->setLastVulnerableTime(thisStartTime);

		// testTime should the same thing as vEnd
		Time vEnd(thisStartTime);
		vEnd.addMiliTime((vulnerabilityDuration*1000));
		baseData->setVulnerabilityEndTime(vEnd);

		Time nStartTime(thisStartTime);
		nStartTime.addMiliTime(vulnerabilityFrequency*1000);
		baseData->setNextVulnerableTime(nStartTime);

		initializeNewVulnerability(baseData);
		bool wasDropped = gcwStartTasks.drop(building->getObjectID());

		block.release();
		scheduleVulnerabilityEnd(building);
	} else {

#ifdef GCW_DEBUG
		info("Loaded " + String::valueOf(building->getObjectID()) + " while invulnerable between vuln and the next start",true);
#endif

		baseData->setLastVulnerableTime(thisStartTime);
		Time nStartTime(thisStartTime);
		nStartTime.addMiliTime(vulnerabilityFrequency*1000);
		baseData->setNextVulnerableTime(nStartTime);

		Time vEnd(nStartTime);
		vEnd.addMiliTime(vulnerabilityDuration*1000);
		baseData->setVulnerabilityEndTime(vEnd);

		baseData->setState(DestructibleBuildingDataComponent::INVULNERABLE);
		block.release();
		scheduleVulnerabilityStart(building);
	}

	renewUplinkBand(building);
}

// PRE:  nothing needs to be locked... building NOT locked
void GCWManagerImplementation::scheduleVulnerabilityEnd(BuildingObject* building) {
	if (!hasBase(building)) {

#ifdef GCW_DEBUG
		info("Not scheduling end task.  Building is not in the base list");
#endif
		return;
	}

	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL)
		return;

	Time endTime = baseData->getVulnerabilityEndTime();
	int64 endDif = endTime.miliDifference();

	if (endDif >= 0) {
#ifdef GCW_DEBUG
		info("error scheduling end time.  it has already passed");
#endif
		return;
	}

#ifdef GCW_DEBUG
	info("Scheduling end  vulnerability for " + String::valueOf(endDif));
#endif
	Reference<Task*> newTask = new EndVulnerabilityTask(_this.getReferenceUnsafeStaticCast(), building);

	newTask->schedule(llabs(endDif));

	addEndTask(building->getObjectID(),newTask);
}


void GCWManagerImplementation::scheduleBaseDestruction(BuildingObject* building, CreatureObject* creature) {
	if (isBaseVulnerable(building) && !hasDestroyTask(building->getObjectID()) ) {
		DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

		if (baseData == NULL) {
			error("ERROR:  could not get base data for base");
			return;
		}

		if (!baseData->getRebootFinishTime().isPast()) {
			if (creature != NULL)
				creature->sendSystemMessage("You must wait for the facility to finish rebooting before activating the overload again");

			return;
		}

		Locker block(building);

		StringIdChatParameter destroyMessage("@faction/faction_hq/faction_hq_response:terminal_response40"); // COUNTDOWN INITIATED: estimated time to detonation: %DI minutes.
		int minutesRemaining = (int) ceil((double)destructionTimer / (double)60);
		destroyMessage.setDI(minutesRemaining);
		broadcastBuilding(building, destroyMessage);
		baseData->setState(DestructibleBuildingDataComponent::SHUTDOWNSEQUENCE);
		block.release();

		Reference<Task*> newTask = new BaseDestructionTask(_this.getReferenceUnsafeStaticCast(), building);
		newTask->schedule(60000);
		addDestroyTask(building->getObjectID(),newTask);
	}
}

void GCWManagerImplementation::abortShutdownSequence(BuildingObject* building, CreatureObject* creature) {
	if (creature != NULL && !creature->checkCooldownRecovery("declare_overt_cooldown")) {
		StringIdChatParameter params("@faction/faction_hq/faction_hq_response:terminal_response42"); // Before issuing the shutdown, you must have been in special forces for at least %TO
		int timer = overtCooldown / 60;
		params.setTO(String::valueOf(timer) + " minutes.");
		creature->sendSystemMessage(params); // Before issuing the shutdown, you must hve beenin Special forces for at least %TO
		return;
	}

	if (isBaseVulnerable(building) && hasDestroyTask(building->getObjectID())) {
		Reference<Task*> oldDestroyTask = getDestroyTask(building->getObjectID());
		if (oldDestroyTask != NULL) {
			oldDestroyTask->cancel();
			dropDestroyTask(building->getObjectID());
		}

		DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

		if (baseData == NULL) {
			error("ERROR:  could not get base data for base");
			return;
		}

		Locker block(building);

		baseData->setState(DestructibleBuildingDataComponent::OVERLOADED);
		Time finishTime = Time();
		finishTime.addMiliTime(reactivationTimer * 1000);
		baseData->setRebootFinishTime(finishTime);

		StringIdChatParameter reloadMessage;
		reloadMessage.setStringId("@faction/faction_hq/faction_hq_response:terminal_response07"); // COUNTDOWN ABORTED: FACILITY SHUTTIGN DOWN
		broadcastBuilding(building, reloadMessage);
	}
}

void GCWManagerImplementation::doBaseDestruction(StructureObject* structure) {
	if (structure == NULL)
		return;

	BuildingObject* building = cast<BuildingObject*>(structure);

	if (building != NULL)
		doBaseDestruction(building);
}

void GCWManagerImplementation::doBaseDestruction(BuildingObject* building) {
	if (building == NULL)
		return;

	Reference<Task*> oldEndTask = getDestroyTask(building->getObjectID());

	if (oldEndTask != NULL) {
		BaseDestructionTask* dTask = cast<BaseDestructionTask*>(oldEndTask.get());
		if (dTask != NULL && dTask->getCountdown() > 0) {
			oldEndTask->reschedule(60000);
			StringIdChatParameter msg("@faction/faction_hq/faction_hq_response:terminal_response39"); // Countdown: Estimated time to detonation: %DI minutes
			int minutesRemaining = dTask->getCountdown();
			msg.setDI(minutesRemaining);
			broadcastBuilding(building, msg);
			return;
		}

	}
#ifdef GCW_DEBUG
	info("Destroying Base " + String::valueOf(building->getObjectID()),true);
#endif
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL) {
		error("ERROR:  could not get base data for base");
		return;
	}

	// need to lock both.  building must be locked for destroyStructure() and then _this is locked when it calls unregister.
	Locker locker(_this.getReferenceUnsafeStaticCast());
	Locker block(building,_this.getReferenceUnsafeStaticCast());

	int baseType = building->getFactionBaseType();

	if (baseType == PLAYERFACTIONBASE) {
		unregisterGCWBase(building);

		StructureManager::instance()->destroyStructure(building);
	} else if (baseType == STATICFACTIONBASE) {
		building->notifyObservers(ObserverEventType::FACTIONBASEFLIPPED);
	}

}

void GCWManagerImplementation::unregisterGCWBase(BuildingObject* building) {

	if (hasBase(building)) {
		dropBase(building);

		if (building->getFaction() == IMPERIALHASH)
			imperialBases--;

		else if (building->getFaction() == REBELHASH)
			rebelBases--;

		String templateString = building->getObjectTemplate()->getFullTemplateString();

		int pointsValue = getPointValue(templateString);

		if (pointsValue > -1) {
			if (building->getFaction() == REBELHASH)
				setRebelScore(getRebelScore() - pointsValue);
			else if (building->getFaction() == IMPERIALHASH)
				setImperialScore(getImperialScore() - pointsValue);

		} else
			info("ERROR looking up value for GCW Base: " + templateString, true);

	}
	Reference<Task*> oldStartTask = getStartTask(building->getObjectID());

	if (oldStartTask != NULL) {
		oldStartTask->cancel();
		dropStartTask(building->getObjectID());
#ifdef GCW_DEBUG
		info("deleting start task for building " + String::valueOf(building->getObjectID()),true);
#endif
	}

	Reference<Task*> oldEndTask = getEndTask(building->getObjectID());
	if (oldEndTask != NULL) {
#ifdef GCW_DEBUG
		info("deleting the end task for building " + String::valueOf(building->getObjectID()), true);
#endif
		oldEndTask->cancel();
		dropEndTask(building->getObjectID());
	}

	Reference<Task*> oldDestroyTask = getDestroyTask(building->getObjectID());
	if (oldDestroyTask != NULL) {
#ifdef GCW_DEBUG
		info("deleting destroy task for building " + String::valueOf(building->getObjectID()),true);
#endif
		oldDestroyTask->cancel();
		dropDestroyTask(building->getObjectID());
	}

#ifdef GCW_DEBUG
	info("Base " + String::valueOf(building->getObjectID()) + " has been removed",true);
#endif
}

void GCWManagerImplementation::performGCWTasks() {
	Locker locker(_this.getReferenceUnsafeStaticCast());

	if (gcwBaseList.size() == 0) {
		setRebelBaseCount(0);
		setImperialBaseCount(0);
		return;
	}

#ifdef GCW_DEBUG
	info("Performing gcw maintenance");
#endif
	int totalBase = gcwBaseList.size();
	int startCount = gcwStartTasks.size();
	int endCount = gcwEndTasks.size();
	int destroyCount = gcwDestroyTasks.size();

	info("Checking " + String::valueOf(totalBase) + " bases", true);
	//info("Size of start list is " + String::valueOf(startCount), true);
	//info("Size of end list is   " + String::valueOf(endCount),true);
	//info("Size of destroy list is   " + String::valueOf(destroyCount),true);

	uint64  thisOid;

	int rebelCheck = 0;
	int imperialCheck = 0;

	for(int i = 0; i< gcwBaseList.size();i++) {
		thisOid = getBase(i)->getObjectID();

		Reference<BuildingObject*> building = zone->getZoneServer()->getObject(thisOid).castTo<BuildingObject*>();

		if (building == NULL)
			continue;

		if (building->getFaction() == REBELHASH)
			rebelCheck++;
		else if (building->getFaction() == IMPERIALHASH)
			imperialCheck++;

		verifyTurrets(building);

#ifdef GCW_DEBUG
		info("Base " + String::valueOf(i) + " id: " + String::valueOf(thisOid) + " - " +   " Start: " + String::valueOf( hasStartTask(thisOid) )
		+ " End: " + String::valueOf(hasEndTask(thisOid)) + " DESTROY: " + String::valueOf(hasDestroyTask(thisOid))
		+ " FACTION:  " + String::valueOf(building->getFaction()),true );
#endif
	}

	setRebelBaseCount(rebelCheck);
	setImperialBaseCount(imperialCheck);

	CheckGCWTask* task = new CheckGCWTask(_this.getReferenceUnsafeStaticCast());
	task->schedule(gcwCheckTimer * 1000);
}

void GCWManagerImplementation::registerGCWBase(BuildingObject* building, bool initializeBase) {
	if ( !hasBase(building)) {

		if (building->getFaction() == IMPERIALHASH)
			imperialBases++;
		else if (building->getFaction() == REBELHASH)
			rebelBases++;

		if (initializeBase) {
			DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

			if (baseData == NULL)
				return;

			ManagedReference<CreatureObject*> ownerCreature = building->getOwnerCreatureObject();

			if (ownerCreature == NULL) {
				error("No owner when initializing a gcw base");
				return;
			}

			int delay = getInitialVulnerabilityDelay();

			Locker bLock(building, ownerCreature);

			initializeBaseTimers(building);

			if (delay == 0)
				initializeNewVulnerability(baseData);

			bLock.release();

			if ( delay == 0) {
				Locker gLock(_this.getReferenceUnsafeStaticCast(), ownerCreature);
				addBase(building);
				startVulnerability(building);
			} else {
				Locker cLock(_this.getReferenceUnsafeStaticCast(), ownerCreature);
				Reference<Task*> newTask = new StartVulnerabilityTask(_this.getReferenceUnsafeStaticCast(), building);
				newTask->schedule(delay * 1000);
				addStartTask(building->getObjectID(),newTask);
			}
		} else {
			addBase(building);
			checkVulnerabilityData(building);
		}
		//info("contains " + String::valueOf(baseValue.contains(templateString)),true);

		String templateString = building->getObjectTemplate()->getFullTemplateString();

		int pointsValue = getPointValue(templateString);

		if (pointsValue > -1) {
			if (building->getFaction() == REBELHASH)
				setRebelScore(getRebelScore() + pointsValue);
			else if (building->getFaction() == IMPERIALHASH)
				setImperialScore(getImperialScore() + pointsValue);
		} else {
			info("ERROR looking up value for GCW Base: " + templateString, true);
		}
	} else {
		error("Building already in gcwBaseList");
	}
}

// PRE: nothing locked
void GCWManagerImplementation::checkVulnerabilityData(BuildingObject* building) {
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL) {
		error("ERROR:  could not get base data for base");
		return;
	}

	Time currentTime;
	Time vulnTime = baseData->getNextVulnerableTime();
	Time nextEnd = baseData->getVulnerabilityEndTime();

	int64 vulnDif = vulnTime.miliDifference();
	int64 endDif = nextEnd.miliDifference();

	if (!vulnTime.isPast()) {

#ifdef GCW_DEBUG
		info("scheduling building " + String::valueOf(building->getObjectID()) + "vulnerability start " + String::valueOf(llabs(endDif)));
#endif
		scheduleVulnerabilityStart(building);
	} else if (vulnTime.isPast() && !nextEnd.isPast()) {

#ifdef GCW_DEBUG
		info("loading vulnerable base " + String::valueOf(building->getObjectID()) + " with vulnerability in progress");
#endif
		startVulnerability(building);
	} else if (nextEnd.isPast()) {

#ifdef GCW_DEBUG
		info("base " + String::valueOf(building->getObjectID()) + " vuln end time has already passed... need to refresh next vuln times " + String::valueOf(vulnDif));
#endif
		refreshExpiredVulnerability(building);
	}

	if (baseData->getState() == DestructibleBuildingDataComponent::SHUTDOWNSEQUENCE) {
		scheduleBaseDestruction(building, NULL);
	}
}

// PRE: no locks or only lock on building
// sets the bandwidth to guess during jamming of the uplink
void GCWManagerImplementation::renewUplinkBand(BuildingObject* building) {
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL) {
		error("ERROR:  could not get base data for base");
		return;
	}

	// 10 possible codes bands to guess
	int secretCode = System::random(0x9);
	//info("New uplink band is " + String::valueOf(secretCode), true);

	Locker block(building);
	baseData->setUplinkBand(secretCode);
}

// pre: building is locked
// initializes times when a base is placed for the first time
void GCWManagerImplementation::initializeBaseTimers(BuildingObject* building) {

	// THESE WORK IF YOU DONT WANT A BASE VULN ON PLANT
	// IT DOES THE NEXT ONE
	/*
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL) {
		error("ERROR:  could not get base data for base");
		return;
	}

	baseData->setPlacementTime(Time());
	baseData->setLastVulnerableTime(Time());

	Time nextTime;

	nextTime.addMiliTime(GCWManager::VULNERABILITYFREQUENCY*1000);
	baseData->setNextVulnerableTime(nextTime);

	nextTime.addMiliTime(GCWManager::VULNERABILITYDURATION*1000);
	baseData->setVulnerabilityEndTime(nextTime);

	baseData->setTerminalDamaged(false);
	baseData->setLastResetTime(Time(0)); // set it to a long, long time ago
	 */

	// try to do initial vuln on plant
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL) {
		error("ERROR:  could not get base data for base");
		return;
	}

	baseData->setPlacementTime(Time());
	baseData->setLastVulnerableTime(Time());

	Time endTime(baseData->getPlacmenetTime());
	endTime.addMiliTime(vulnerabilityDuration*1000 + getInitialVulnerabilityDelay()*1000);
	baseData->setVulnerabilityEndTime(endTime);

	if ( getInitialVulnerabilityDelay() == 0) {
		Time nextVuln(baseData->getPlacmenetTime());
		nextVuln.addMiliTime(vulnerabilityFrequency*1000);
		baseData->setNextVulnerableTime(nextVuln);
	} else {
		Time nextVuln(baseData->getPlacmenetTime());
		nextVuln.addMiliTime(getInitialVulnerabilityDelay()*1000);
		baseData->setNextVulnerableTime(nextVuln);
	}

	baseData->setTerminalDamaged(false);
	baseData->setLastResetTime(Time(0)); // set it to a long, long time ago
}

DestructibleBuildingDataComponent* GCWManagerImplementation::getDestructibleBuildingData(BuildingObject* building) {
	DestructibleBuildingDataComponent* baseData = NULL;

	if (building != NULL && building->isGCWBase()) {
		DataObjectComponentReference* data = building->getDataObjectComponent();

		if (data != NULL)
			baseData = cast<DestructibleBuildingDataComponent*>(data->get());
	}

	return baseData;
}

// PRE: nothing is locked
// time of the day
void GCWManagerImplementation::resetVulnerability(CreatureObject* creature, BuildingObject* building) {
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL)
		return;

	if (isBaseVulnerable(building))
		return;

	if (!hasResetTimerPast(building))
		return;

	Locker clock(building,creature);

	//info("Resetting vulnerability timer",true);
	baseData->setLastResetTime(Time());

	Time nextTime = Time();

	clock.release();

	Locker glock(_this.getReferenceUnsafeStaticCast(),creature);
	baseData->setLastVulnerableTime(nextTime);

	nextTime.addMiliTime(vulnerabilityFrequency*1000);
	baseData->setNextVulnerableTime(nextTime.getTime()); // working()

	nextTime.addMiliTime(vulnerabilityDuration*1000);
	baseData->setVulnerabilityEndTime(nextTime.getTime()); // (working)


	Reference<Task*> task = getStartTask(building->getObjectID());
	if (task != NULL ) {
		task->cancel();
		dropStartTask(building->getObjectID());
	}

	scheduleVulnerabilityStart(building);

	if (creature != NULL)
		creature->sendSystemMessage("@hq:vulnerability_reset"); // The vulnerability for this structure has been reset.
}

bool GCWManagerImplementation::hasResetTimerPast(BuildingObject* building) {
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL)
		return false;

	Time ttime = baseData->getLastResetTime();

	ttime.addMiliTime(resetTimer*1000);

	return ttime.isPast();
}

void GCWManagerImplementation::sendTurretAttackListTo(CreatureObject* creature, SceneObject* turretControlTerminal) {
	if (turretControlTerminal == NULL || creature == NULL || creature->isInCombat() )
		return;

	PlayerObject* ghost = creature->getPlayerObject();

	if (ghost == NULL)
		return;

	if (ghost->hasSuiBoxWindowType(SuiWindowType::HQ_TURRET_TERMINAL))
		ghost->closeSuiWindowType(SuiWindowType::HQ_TURRET_TERMINAL);

	ManagedReference<BuildingObject*> building = cast<BuildingObject*>(turretControlTerminal->getParentRecursively(SceneObjectType::FACTIONBUILDING).get().get());

	if (building == NULL)
		return;

	// get the base data component
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL)
		return;

	TurretControlTerminalDataComponent* controlData = getTurretControlDataComponent(turretControlTerminal);

	if (controlData == NULL)
		return;

	uint64 tindex = baseData->getTurretID(controlData->getTurrteIndex());

	if (tindex == 0 || controlData->getTurrteIndex() < 0) {
		creature->sendSystemMessage("@hq:none_active");  //  There are no available turrets to control using this terminal.
		return;
	}

	ZoneServer* server = zone->getZoneServer();

	if (server == NULL)
		return;

	Reference<SceneObject*> turret = server->getObject(tindex);

	if (turret == NULL || !turret->isTurret()) {
		creature->sendSystemMessage("@hq:none_active"); // There are no available turrets to control using this terminal.
		return;
	}

	TangibleObject* turretObject = cast<TangibleObject*>(turret.get());

	if (turretObject == NULL)
		return;

	TurretDataComponent* turretData = getTurretDataComponent(turret);

	if (turretData == NULL)
		return;

	if (!canUseTurret(turretData, controlData, creature)) {
		creature->sendSystemMessage("@hq:in_use");  //  This turret control terminal is already in use."
		return;
	}

	generateTurretControlBoxTo(creature, turretObject, turretControlTerminal);

}

TurretDataComponent* GCWManagerImplementation::getTurretDataComponent(SceneObject* turret) {
	DataObjectComponentReference* turretComponent = turret->getDataObjectComponent();

	if (turretComponent == NULL)
		return NULL;

	return cast<TurretDataComponent*>(turretComponent->get());
}

TurretControlTerminalDataComponent* GCWManagerImplementation::getTurretControlDataComponent(SceneObject* terminal) {
	DataObjectComponentReference* terminalData  = terminal->getDataObjectComponent();

	if (terminalData == NULL)
		return NULL;

	return cast<TurretControlTerminalDataComponent*>(terminalData->get());
}

void GCWManagerImplementation::generateTurretControlBoxTo(CreatureObject* creature, TangibleObject* turret, SceneObject* terminal) {
	TurretControlTerminalDataComponent* controlData = getTurretControlDataComponent(terminal);

	if (controlData == NULL)
		return;

	TurretDataComponent* turretData = getTurretDataComponent(turret);

	if (turretData == NULL)
		return;

	PlayerObject* ghost = creature->getPlayerObject();

	if (ghost == NULL)
		return;

	ManagedReference<SuiListBox*> status = new SuiListBox(creature, SuiWindowType::HQ_TURRET_TERMINAL);
	status->setPromptTitle("@hq:control_title"); //"Turret Control Consule"
	status->setCancelButton(true, "@cancel");
	status->setCallback(new TurretControlSuiCallback(zone->getZoneServer(), turret,terminal));
	status->setOtherButton(true,"@ui:refresh"); // refresh
	status->setOkButton(true,"@hq:btn_attack"); // Attack
	status->setUsingObject(terminal);
	status->setForceCloseDistance(5);
	StringIdChatParameter params;
	params.setStringId(("@hq:attack_targets")); // Turret is now attacking %TO.");
	StringBuffer msg;
	msg << "Turret is now targeting: ";

	if (turretData->getManualTarget() != NULL)
		msg << turretData->getManualTarget()->getFirstName();

	status->setPromptText(msg.toString());

	CloseObjectsVector* vec = (CloseObjectsVector*)turret->getCloseObjects();

	SortedVector<QuadTreeEntry*> closeObjects;

	vec->safeCopyTo(closeObjects);
	Reference<WeaponObject*> weapon = turret->getSlottedObject("hold_r").castTo<WeaponObject*>();

	if (weapon == NULL)
		return;

	int targetTotal = 0;

	for(int i = 0; i < closeObjects.size(); ++i) {
		CreatureObject* creo = cast<CreatureObject*>(closeObjects.get(i));

		if (creo != NULL && creo->isAttackableBy(turret)) {
			if (!CollisionManager::checkLineOfSight(creo, turret)) {
				continue;
			}

			if (turret->getDistanceTo(creo) <= weapon->getMaxRange()) {

				if (creo->isPlayerCreature())
					status->addMenuItem(creo->getFirstName() + " - " + String::valueOf((int)turret->getDistanceTo(creo)) + "m",creo->getObjectID());
				else
					status->addMenuItem(creo->getObjectNameStringIdName() + " - " + String::valueOf((int)turret->getDistanceTo(creo)) + "m",creo->getObjectID());

				targetTotal++;
			}
		}

		if (targetTotal > 20)
			break;
	}

	if ( status->getMenuSize() > 0 ) {
		ghost->addSuiBox(status);
		creature->sendMessage(status->generateMessage());

		Locker _lock(terminal, creature);
		controlData->setSuiBoxID(status->getBoxID());
		_lock.release();

		Locker tlock(turret, creature);
		turretData->setController(creature);
		//turretData->updateAutoCooldown(turretAutoFireTimeout);

	} else
		creature->sendSystemMessage("@hq:no_targets"); // This turret has no valid targets.
}


bool GCWManagerImplementation::canUseTurret(TurretDataComponent* turretData, TurretControlTerminalDataComponent* controlData, CreatureObject* creature) {

	if (turretData->getController() != NULL && turretData->getController() != creature) {

		CreatureObject* controllerCreature = turretData->getController();
		PlayerObject* controllerGhost = controllerCreature->getPlayerObject();

		// if there is no manual target, give it to the new guy, close it from the old guy
		if (turretData->getManualTarget() == NULL) {
			// try to close it from the old controller if it's still up
			controllerGhost->closeSuiWindowType(SuiWindowType::HQ_TURRET_TERMINAL);
		} else if (controllerGhost != NULL) {

			// if the controller creatures has the same window up
			if (turretData->getManualTarget() != NULL) {
				int controllingSuiBoxID = controlData->getSuiBoxID();

				if (controllingSuiBoxID >= 0) {
					// get the sui from the controllerGhost to see if it's still up
					if (controllerGhost->hasSuiBox(controllingSuiBoxID)) {
						return false;
					}
				}
			}
		}
	}

	return true;
}

String GCWManagerImplementation::getVulnerableStatus(BuildingObject* building, CreatureObject* creature) {
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (creature == NULL || baseData == NULL)
		return "";

	double dif = 0;

	if (isBaseVulnerable(building)) {
		return "@player_structure:next_vulnerability_prompt Now";
	} else {
		dif = baseData->getNextVulnerableTime().getTime() - time(0);
	}

	int days = (int) floor(dif / 86400.f);
	dif = dif - (days*86400);
	int hours = (int) floor(dif / 3600.f);
	dif = dif - (hours * 3600);
	int minutes = (int) ceil(dif / 60.f);

	return "@player_structure:next_vulnerability_prompt "+ String::valueOf(days) + " days, " + String::valueOf(hours) + " hours, " + String::valueOf(minutes) + " minutes";
}

void GCWManagerImplementation::sendBaseDefenseStatus(CreatureObject* creature, BuildingObject* building) {
	ManagedReference<PlayerObject* > ghost = creature->getPlayerObject();
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);
	if (ghost == NULL || baseData == NULL)
		return;

	if (ghost->hasSuiBoxWindowType(SuiWindowType::HQ_TERMINAL))
		ghost->closeSuiWindowType(SuiWindowType::HQ_TERMINAL);

	ManagedReference<SuiListBox*> status = new SuiListBox(creature, SuiWindowType::HQ_TERMINAL);
	status->setPromptTitle("@hq:mnu_defense_status"); //Defense status
	status->setPromptText("@faction/faction_hq/faction_hq_response:terminal_response21"); // If you want to remove a defense select it and press remove
	status->setUsingObject(building);
	status->setCancelButton(true, "@cancel");
	if (creature == building->getOwnerCreatureObject()) {
		status->setOtherButton(true,"@ui:permission_remove");
	}
	status->setOkButton(true, "@ok");
	status->setCallback(new HQDefenseStatusSuiCallback(zone->getZoneServer()));

	ZoneServer* zoneServer = zone->getZoneServer();
	if (zoneServer != NULL) {
		for(int i =0; i < baseData->getTotalTurretCount();i++) {
			ManagedReference<SceneObject*> sceno = zoneServer->getObject(baseData->getTurretID(i));
			if (sceno != NULL && sceno->isTurret()) {

				status->addMenuItem(sceno->getDisplayedName(),sceno->getObjectID());
			} else {
				//status->addMenuItem("Turret " + String::valueOf(i+1) + ": EMPTY");
			}
		}
	}
	ghost->addSuiBox(status);
	creature->sendMessage(status->generateMessage());
}

void GCWManagerImplementation::sendJamUplinkMenu(CreatureObject* creature, BuildingObject* building, TangibleObject* uplinkTerminal) {
	ManagedReference<PlayerObject* > ghost = creature->getPlayerObject();
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (ghost == NULL || baseData == NULL || uplinkTerminal == NULL)
		return;

	if (!isBaseVulnerable(building))
		return;

	if (ghost->hasSuiBoxWindowType(SuiWindowType::HQ_TERMINAL))
		ghost->closeSuiWindowType(SuiWindowType::HQ_TERMINAL);

	ManagedReference<SuiListBox*> status = new SuiListBox(creature, SuiWindowType::HQ_TERMINAL);

	status->setPromptTitle("JAMMING...");
	status->setUsingObject(uplinkTerminal);
	status->setOkButton(true, "@ok");
	status->setCancelButton(true, "@cancel");
	status->setCallback( new JamUplinkSuiCallback(zone->getZoneServer()) );

	if (!isBandIdentified(building)) {
		status->setPromptText(" \0Select the BAND that you wish to search.");

		for(int i =0;i<10;i++)
			status->addMenuItem("Band #" + String::valueOf(i+1),9);
	} else {
		status->setPromptText(" \0Select the CHANNEL that you wish to search.");

		for(int i =0;i<10;i++)
			status->addMenuItem("Channel #" + String::valueOf(i+1),9);
	}


	ghost->addSuiBox(status);
	creature->sendSystemMessage("You begin scanning for baseline carrier signals...");
	creature->sendMessage(status->generateMessage());
}

void GCWManagerImplementation::verifyUplinkBand(CreatureObject* creature, BuildingObject* building, int band) {
	ManagedReference<PlayerObject* > ghost = creature->getPlayerObject();
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (ghost == NULL || baseData == NULL)
		return;

	if (band == baseData->getUplinkBand()) {
		Locker block(building,creature);

		if (isBandIdentified(building)) {
			baseData->setState(DestructibleBuildingDataComponent::JAMMED);
			creature->sendSystemMessage("You isolate the carrier signal to Channel #" + String::valueOf(band + 1) + ".");
			creature->sendSystemMessage("Jamming complete! You disable the uplink...");
			awardSlicingXP(creature, "bountyhunter", 1000);
		} else {
			baseData->setState(DestructibleBuildingDataComponent::BAND);
			creature->sendSystemMessage("You narrow the carrier signal down to Band #" + String::valueOf(band + 1) + ".");
		}
		renewUplinkBand(building);
		block.release();
	} else {
		int rand = System::random(300);

		if (rand >= 290) {
			creature->sendSystemMessage("You lose concentration and become lost in a sea of white noise...");
		} else if (band < baseData->getUplinkBand()) {
			creature->sendSystemMessage("You feel like you need to search higher...");
		} else {
			creature->sendSystemMessage("You feel like you need to search lower...");
		}
	}
}

bool GCWManagerImplementation::isBaseVulnerable(BuildingObject* building) {
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL) {
		error("ERROR:  could not get base data for base");
		return false;
	}

	return (baseData->getState() > DestructibleBuildingDataComponent::INVULNERABLE || !(building->getPvpStatusBitmask() & CreatureFlag::OVERT));
}

bool GCWManagerImplementation::isBandIdentified(BuildingObject* building) {
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL) {
		error("ERROR:  could not get base data for base");
		return false;
	}

	return (baseData->getState() >= DestructibleBuildingDataComponent::BAND);
}
bool GCWManagerImplementation::isUplinkJammed(BuildingObject* building) {
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL) {
		error("ERROR:  could not get base data for base");
		return false;
	}

	return (baseData->getState() >= DestructibleBuildingDataComponent::JAMMED);
}

bool GCWManagerImplementation::isSecurityTermSliced(BuildingObject* building) {

	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL) {
		error("ERROR:  could not get base data for base");
		return false;
	}

	return (baseData->getState() >= DestructibleBuildingDataComponent::SLICED);
}

bool GCWManagerImplementation::isDNASampled(BuildingObject* building) {
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL) {
		error("ERROR:  could not get base data for base");
		return false;
	}

	return (baseData->getState() >= DestructibleBuildingDataComponent::DNA);

}

bool GCWManagerImplementation::isPowerOverloaded(BuildingObject* building) {
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL) {
		error("ERROR:  could not get base data for base");
		return false;
	}

	return(baseData->getState() >= DestructibleBuildingDataComponent::OVERLOADED);
}

bool GCWManagerImplementation::isShutdownSequenceStarted(BuildingObject* building) {
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL) {
		info("ERROR:  could not get base data for base",true);
		return false;
	}
	//info("State of the base is " + String::valueOf(baseData->getState()),true);

	return(baseData->getState() == DestructibleBuildingDataComponent::SHUTDOWNSEQUENCE);
}

bool GCWManagerImplementation::canStartSlice(CreatureObject* creature, TangibleObject* tano) {
	Locker _lock(creature);
	Locker clocker(tano, creature);

	ManagedReference<BuildingObject*> building = cast<BuildingObject*>(tano->getParentRecursively(SceneObjectType::FACTIONBUILDING).get().get());

	if (!isBaseVulnerable(building))
		return false;

	if (!areOpposingFactions(creature->getFaction(), building->getFaction())) {
		creature->sendSystemMessage("@faction/faction_hq/faction_hq_response:no_tamper"); // You are not an enemy of this structure. Why would you want to tamper?
		return false;
	} else if (isSecurityTermSliced(building)) {
		creature->sendSystemMessage("The security terminal has already been sliced!");
		return false;
	} else if (!isUplinkJammed(building))	{
		creature->sendSystemMessage("@faction/faction_hq/faction_hq_response:other_objectives"); // Other objectives must be disabled prior to gaining access to this one.
		return false;
	} else if (creature->isInCombat()) {
		creature->sendSystemMessage("You cannot slice the terminal while you are in combat!");
		return false;
	} else if (tano->getParentID() != creature->getParentID()) {
		creature->sendSystemMessage("You cannot slice the terminal if you are not even in the same room!");
		return false;
	} else if (tano->getDistanceTo(creature) > 15) {
		creature->sendSystemMessage("You are too far away from the terminal to continue slicing!");
		return false;
	} else if (!creature->hasSkill("combat_smuggler_slicing_01")) {
		creature->sendSystemMessage("Only a smuggler with terminal slicing knowledge could expect to disable this security terminal!");
		return false;
	}

	return true;
}

// @pre: player is locked since called from Slicing session
// @post: player is locked
void GCWManagerImplementation::completeSecuritySlice(CreatureObject* creature, TangibleObject* securityTerminal) {
	ManagedReference<BuildingObject*> building = cast<BuildingObject*>(securityTerminal->getParentRecursively(SceneObjectType::FACTIONBUILDING).get().get());

	if (building == NULL)
		return;

	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL) {
		error("ERROR:  could not get base data for base");
		return;
	}

	creature->sendSystemMessage("@slicing/slicing:hq_security_success"); // You have managed to slice into the terminal. The security protocol for the override terminal has been significantly relaxed.
	Locker block(building);
	baseData->setState(DestructibleBuildingDataComponent::SLICED);
}

bool GCWManagerImplementation::isTerminalDamaged(TangibleObject* securityTerminal) {
	ManagedReference<BuildingObject*> building = cast<BuildingObject*>(securityTerminal->getParentRecursively(SceneObjectType::FACTIONBUILDING).get().get());

	if (building == NULL)
		return true;

	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL)
		return true;

	return baseData->isTerminalDamaged();
}
void GCWManagerImplementation::repairTerminal(CreatureObject* creature, TangibleObject* securityTerminal) {
	if (securityTerminal == NULL)
		return;

	ManagedReference<BuildingObject*> building = cast<BuildingObject*>(securityTerminal->getParentRecursively(SceneObjectType::FACTIONBUILDING).get().get());

	if (building == NULL)
		return;


	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL) {
		error("ERROR:  could not get base data for base");
		return;
	}

	if (!isBaseVulnerable(building) || !isTerminalDamaged(building))
		return;

	if (baseData->isTerminalBeingRepaired()) {
		creature->sendSystemMessage("Terminal is already in the process of being repaired.");
	} else {
		SecurityRepairTask* repairTask = new SecurityRepairTask(_this.getReferenceUnsafeStaticCast(), securityTerminal, creature, 10);
		repairTask->schedule(5000);

		Locker locker(building);

		baseData->setTerminalBeingRepaired(true);
	}
}
void GCWManagerImplementation::failSecuritySlice(TangibleObject* securityTerminal) {
	if (securityTerminal == NULL)
		return;

	ManagedReference<BuildingObject*> building = cast<BuildingObject*>(securityTerminal->getParentRecursively(SceneObjectType::FACTIONBUILDING).get().get());

	if (building == NULL)
		return;

	if (!isBaseVulnerable(building))
		return;

	Locker block(building);

	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL) {
		error("ERROR:  could not get base data for base");
		return;
	}
	//info("Failing slice",true);
	baseData->setTerminalBeingRepaired(false);
	baseData->setTerminalDamaged(true);
}

void GCWManagerImplementation::sendDNASampleMenu(CreatureObject* creature, BuildingObject* building, TangibleObject* overrideTerminal) {
	ManagedReference<PlayerObject* > ghost = creature->getPlayerObject();
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (ghost == NULL || baseData == NULL || overrideTerminal == NULL)
		return;

	if (!isBaseVulnerable(building))
		return;

	if (ghost->hasSuiBoxWindowType(SuiWindowType::HQ_TERMINAL))
		ghost->closeSuiWindowType(SuiWindowType::HQ_TERMINAL);

	Vector<String> dnaStrand = baseData->getDnaStrand();

	if (dnaStrand.size() == 0) {
		constructDNAStrand(building);
		dnaStrand = baseData->getDnaStrand();
	}

	Vector<int> dnaLocks = baseData->getDnaLocks();

	ManagedReference<SuiListBox*> status = new SuiListBox(creature, SuiWindowType::HQ_TERMINAL);
	status->setPromptTitle("DNA SEQUENCING");
	status->setUsingObject(overrideTerminal);
	status->setOkButton(true, "@ok");
	status->setCancelButton(true, "@cancel");

	int numLocks = 0;
	Vector<String> dnaEntries;

	for (int i = 0; i < dnaStrand.size(); i++) {
		String dna = dnaStrand.get(i);

		if (dnaLocks.get(i) == 0) {
			dnaEntries.add(dna);
		} else {
			numLocks++;
			for (int j = 0; j < dnaPairs.size(); j++) {
				String pair = dnaPairs.get(j);

				if (pair.beginsWith(dna)) {
					dnaEntries.add("\\#00FF00" + pair + " \\#.");
					break;
				}
			}
		}
	}

	String chain = baseData->getCurrentDnaChain();

	if (chain == "") {
		int length = 3;

		if (creature->hasSkill("outdoors_bio_engineer_master"))
			length = 8;
		else if (creature->hasSkill("outdoors_bio_engineer_dna_harvesting_04"))
			length = 7;
		else if (creature->hasSkill("outdoors_bio_engineer_dna_harvesting_03"))
			length = 6;
		else if (creature->hasSkill("outdoors_bio_engineer_dna_harvesting_02"))
			length = 5;
		else if (creature->hasSkill("outdoors_bio_engineer_dna_harvesting_01"))
			length = 4;

		for (int i = 0; i < length; i++) {
			chain += dnaNucleotides.get(System::random(dnaNucleotides.size() - 1));
		}
	}

	baseData->setCurrentDnaChain(chain);

	String prompt = "DNA Sequence Processing...\nComplete the missing pairs: AT,TA,GC,CG\nMatched Pairs: " + String::valueOf(numLocks) + "\nSampled Chain: " + chain + "\n\nSelect the DNA index to match the chain to...";
	status->setPromptText(prompt);

	for (int i = 0; i < dnaEntries.size(); i++)
		status->addMenuItem(dnaEntries.get(i),i);

	ghost->addSuiBox(status);
	status->setCallback( new OverrideTerminalSuiCallback(zone->getZoneServer()) );
	creature->sendMessage(status->generateMessage());
}

void GCWManagerImplementation::processDNASample(CreatureObject* creature, TangibleObject* overrideTerminal, const int index) {
	ManagedReference<BuildingObject*> building = cast<BuildingObject*>(overrideTerminal->getParentRecursively(SceneObjectType::FACTIONBUILDING).get().get());

	if (building == NULL || creature == NULL)
		return;

	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL)
		return;

	if (!isBaseVulnerable(building))
		return;

	if (isDNASampled(building)) {
		creature->sendSystemMessage("You stop sequencing as the fail-safe sequence has already been overridden.");
		return;
	}

	Locker clocker(building, creature);

	Vector<String> dnaStrand = baseData->getDnaStrand();
	Vector<int> dnaLocks = baseData->getDnaLocks();
	int newLocks = 0;

	if (index > -1) {
		String chain = baseData->getCurrentDnaChain();

		for (int i = 0; i < chain.length(); i++) {
			int idx = index + i;

			if (idx < dnaStrand.size()) {
				String nucleotide = chain.subString(i, i + 1);
				String pair = dnaStrand.get(idx) + nucleotide;

				if (dnaLocks.get(idx) == 0 && dnaPairs.contains(pair)) {
					dnaLocks.set(idx, 1);
					newLocks++;
				}
			}
		}

		baseData->setDnaLocks(dnaLocks);
	}

	int totalLocks = 0;

	for (int i = 0; i < dnaLocks.size(); i++) {
		if (dnaLocks.get(i) == 1)
			totalLocks++;
	}

	if (newLocks == 1) {
		creature->sendSystemMessage("You match 1 new set of nucleotides.");
	} else if (newLocks > 1) {
		creature->sendSystemMessage("You match " + String::valueOf(newLocks) + " new sets of nucleotides.");
	} else {
		creature->sendSystemMessage("You fail to match any new set of nucleotides.");
	}

	if (totalLocks == dnaLocks.size()) {
		creature->sendSystemMessage("Sequencing complete! You disable the security override for the facility...");
		baseData->setState(DestructibleBuildingDataComponent::DNA);
		awardSlicingXP(creature, "bio_engineer_dna_harvesting", 1000);
		constructDNAStrand(building);
		return;
	}

	baseData->setCurrentDnaChain("");
	creature->sendSystemMessage("\"Retrieving new DNA sample...\"");
	sendDNASampleMenu(creature, building, overrideTerminal);
}

void GCWManagerImplementation::sendPowerRegulatorControls(CreatureObject* creature, BuildingObject* building, TangibleObject* powerRegulator) {
	ManagedReference<PlayerObject* > ghost = creature->getPlayerObject();
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (ghost == NULL || baseData == NULL)
		return;

	if (!isBaseVulnerable(building))
		return;

	if (ghost->hasSuiBoxWindowType(SuiWindowType::HQ_TERMINAL))
		ghost->closeSuiWindowType(SuiWindowType::HQ_TERMINAL);

	Vector<bool> switchStates = baseData->getPowerSwitchStates();

	if (switchStates.size() == 0)
		randomizePowerRegulatorSwitches(building);

	ManagedReference<SuiListBox*> status = new SuiListBox(creature, SuiWindowType::HQ_TERMINAL);
	status->setPromptTitle("@hq:mnu_set_overload"); //Set to Overload
	status->setUsingObject(powerRegulator);
	status->setOkButton(true, "@ok");
	status->setCancelButton(true, "@cancel");

	String prompt = "To successfully align the power flow to overload, you must activate all the flow regulators to ON.\n\n Select the switch to toggle...";

	status->setPromptText(prompt);
	status->setCallback( new PowerRegulatorSuiCallback(zone->getZoneServer()) );

	for(int i = 0; i < powerSwitchCount; i++) {
		if (baseData->getPowerPosition(i))
			status->addMenuItem("Switch #" + String::valueOf(i+1) + ": ON",i);
		else
			status->addMenuItem("Switch #" + String::valueOf(i+1) + ": OFF",i);
	}

	ghost->addSuiBox(status);
	creature->sendMessage(status->generateMessage());

}

void GCWManagerImplementation::handlePowerRegulatorSwitch(CreatureObject* creature, TangibleObject* powerRegulator, int index) {
	ManagedReference<BuildingObject*> building = cast<BuildingObject*>(powerRegulator->getParentRecursively(SceneObjectType::FACTIONBUILDING).get().get());

	if (building == NULL)
		return;

	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL)
		return;

	if (!isBaseVulnerable(building))
		return;

	Locker block(building,creature);

	if (index < 0)
		return;

	Vector<bool> switchStates = baseData->getPowerSwitchStates();

	flipPowerSwitch(building, switchStates, index);

	baseData->setPowerSwitchStates(switchStates);

	bool checkStatus = true;

	for (int i = 0; i < powerSwitchCount; i++)
		checkStatus &= switchStates.get(i);

	if (checkStatus) {
		creature->sendSystemMessage("@faction/faction_hq/faction_hq_response:alignment_complete"); // Alignment complete! The facility may now be set to overload from the primary terminal!
		baseData->setState(DestructibleBuildingDataComponent::OVERLOADED);
		awardSlicingXP(creature, "combat_rangedspecialize_heavy", 1000);
		randomizePowerRegulatorSwitches(building);
	} else {
		sendPowerRegulatorControls(creature, building, powerRegulator);
	}

}

void GCWManagerImplementation::notifyInstallationDestruction(InstallationObject* installation) {
	if (installation == NULL)
		return;

	PlayClientEffectLoc* explodeLoc = new PlayClientEffectLoc("clienteffect/lair_damage_heavy.cef", zone->getZoneName(), installation->getPositionX(), installation->getPositionZ(), installation->getPositionY());
	installation->broadcastMessage(explodeLoc, false);

	uint64 ownerid = installation->getOwnerObjectID();
	BuildingObject* building = NULL;

	ZoneServer* server = zone->getZoneServer();

	if (server == NULL)
		return;

	Reference<SceneObject*> ownerObject = server->getObject(ownerid);

	if (ownerObject == NULL) {

#ifdef GCW_DEBUG
		info("owner object for the turret is null",true);
#endif
		PlayClientEffectLoc* explodeLoc = new PlayClientEffectLoc("clienteffect/lair_damage_heavy.cef", zone->getZoneName(), installation->getPositionX(), installation->getPositionZ(), installation->getPositionY());
		installation->broadcastMessage(explodeLoc, false);

		Locker _lock(installation);
		installation->destroyObjectFromWorld(true);
		installation->destroyObjectFromDatabase(true);

		return;
	}

	if (ownerObject->isGCWBase()) {
		building = cast<BuildingObject*>(ownerObject.get());

		Locker _lock(installation);
		Locker clock(building, installation);

		if (building->containsChildObject(installation)) {
			//info("removed child",true);
			building->getChildObjects()->removeElement(installation);
		}

		DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

		if (baseData != NULL && baseData->hasTurret(installation->getObjectID())) {
			if (installation->isTurret())
				notifyTurretDestruction(building, installation);
		} else if (baseData != NULL && baseData->hasMinefield(installation->getObjectID())) {
			if (installation->isMinefield())
				notifyMinefieldDestruction(building, installation);
		} else {
			clock.release();
			Locker tlock(ownerObject, installation);
			StructureManager::instance()->destroyStructure(installation);
			tlock.release();
		}
	} else if (ownerObject->isCreatureObject()) {

#ifdef GCW_DEBUG
		info("Destroying faction installation not part of a base",true);
#endif

		Locker plock(ownerObject);
		Locker tlock(installation, ownerObject);
		StructureManager::instance()->destroyStructure(installation);
		tlock.release();
		plock.release();
	}

}

void GCWManagerImplementation::notifyTurretDestruction(BuildingObject* building, InstallationObject* turret) {
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL)
		return;

	int indx = baseData->getIndexOfTurret(turret->getObjectID());

	if (indx < 0)
		return;

	baseData->setTurretID(indx,0);

	turret->destroyObjectFromWorld(true);
	turret->destroyObjectFromDatabase(true);

	verifyTurrets(building);
}

void GCWManagerImplementation::notifyMinefieldDestruction(BuildingObject* building, InstallationObject* minefield) {
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);
	if (baseData == NULL)
		return;

	int indx = baseData->getIndexOfMinefield(minefield->getObjectID());

	if (indx < 0 )
		return;

	baseData->setMinefieldID(indx,0);

	// see if all the turrets are destroyed
	int defensecount = 0;

	for(int i = 0; i < baseData->getTotalMinefieldCount();i++) {
		if (baseData->getMinefieldOID(i))
			defensecount++;
	}

#ifdef GCW_DEBUG
	info("Base " + String::valueOf(building->getObjectID()) + " minefield destroyed.  Remaining minefields: " + String::valueOf(defensecount),true);
#endif

	if (!defensecount) {
		//baseData->setDefense(false);
		//building->broadcastCellPermissions();
	}

	minefield->destroyObjectFromWorld(true);
	minefield->destroyObjectFromDatabase(true);
}

void GCWManagerImplementation::sendSelectDeedToDonate(BuildingObject* building, CreatureObject* creature, int turretIndex) {
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (creature == NULL || baseData == NULL)
		return;

	ManagedReference<PlayerObject*> ghost = creature->getPlayerObject();

	if (ghost == NULL)
		return;

	if (isBaseVulnerable(building) && !ghost->isPrivileged() ) {
		// TODO: Figure out what timer is put into %TO
		creature->sendSystemMessage("@hq:under_attack"); // You cannot add defenses while this HQ is under attack. This function will be restored in %TO.
		return;
	}

	if (ghost->hasSuiBoxWindowType(SuiWindowType::HQ_TERMINAL))
		ghost->closeSuiWindowType(SuiWindowType::HQ_TERMINAL);

	ManagedReference<SceneObject*> inv = creature->getSlottedObject("inventory");

	if (inv == NULL)
		return;

	ManagedReference<SuiListBox*> donate = new SuiListBox(creature, SuiWindowType::HQ_TERMINAL);

	donate->setPromptTitle("@faction/faction_hq/faction_hq_response:terminal_response26"); // Donate Deed
	donate->setPromptText("@faction/faction_hq/faction_hq_response:terminal_response23"); // Which deed would you like to donate?
	donate->setUsingObject(building);
	donate->setOkButton(true, "@ok");
	donate->setCancelButton(true, "@cancel");
	donate->setCallback( new DonateDefenseSuiCallback(zone->getZoneServer(), turretIndex) );

	for(int i =0;i < inv->getContainerObjectsSize(); ++i) {
		ManagedReference<SceneObject*> inventoryObject = inv->getContainerObject(i);

		if (inventoryObject->isDeedObject() ) {
			ManagedReference<Deed*> deed = dynamic_cast<Deed*>(inventoryObject.get());
			if (deed != NULL) {

				Reference<SharedObjectTemplate* > generatedTemplate = TemplateManager::instance()->getTemplate(deed->getGeneratedObjectTemplate().hashCode());
				if (generatedTemplate != NULL &&
						(generatedTemplate->getGameObjectType() == SceneObjectType::MINEFIELD ||
								generatedTemplate->getGameObjectType() == SceneObjectType::TURRET) ) {

					donate->addMenuItem(inventoryObject->getDisplayedName(),inventoryObject->getObjectID());
				}
			}
		}
		else if ( inventoryObject->getGameObjectType() == SceneObjectType::MINE) {
			donate->addMenuItem(inventoryObject->getDisplayedName(),inventoryObject->getObjectID());
		}

	}

	ghost->addSuiBox(donate);
	creature->sendMessage(donate->generateMessage());

}

void GCWManagerImplementation::sendRemoveDefenseConfirmation(BuildingObject* building, CreatureObject* creature, uint64 deedOID) {
	ZoneServer* zoneServer = zone->getZoneServer();
	if (zoneServer == NULL)
		return;

	ManagedReference<PlayerObject* > ghost = creature->getPlayerObject();
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (ghost == NULL || baseData == NULL)
		return;

	if (ghost->hasSuiBoxWindowType(SuiWindowType::HQ_TERMINAL))
		ghost->closeSuiWindowType(SuiWindowType::HQ_TERMINAL);

	ManagedReference<SuiListBox*> removeDefense = new SuiListBox(creature, SuiWindowType::HQ_TERMINAL);
	removeDefense->setPromptTitle("@faction/faction_hq/faction_hq_response:terminal_response24"); // Confirm Defense Removal?
	removeDefense->setPromptText("@faction/faction_hq/faction_hq_response:terminal_response25"); // Are you sure you want to remove the selected defense?
	removeDefense->setUsingObject(building);
	removeDefense->setOkButton(true, "@ok");
	removeDefense->setCancelButton(true, "@cancel");
	removeDefense->setCallback( new RemoveDefenseSuiCallback(zone->getZoneServer(),deedOID));

	ghost->addSuiBox(removeDefense);
	creature->sendMessage(removeDefense->generateMessage());
}

void GCWManagerImplementation::removeDefense(BuildingObject* building, CreatureObject* creature, uint64 deedOID) {
	//info("remove defense " + String::valueOf(deedOID),true);

	ZoneServer* zoneServer = zone->getZoneServer();

	if (zoneServer == NULL)
		return;

	ManagedReference<SceneObject*> defense = zoneServer->getObject(deedOID);

	if (defense == NULL || !defense->isTurret())
		return;


	InstallationObject* turret = cast<InstallationObject*>(defense.get());

	notifyInstallationDestruction(turret);
	//Locker clock(defense,creature);
	//TangibleObject* tano = cast<TangibleObject*>(defense.get());
	//tano->inflictDamage(creature,0,999999,true,true);

}

void GCWManagerImplementation::performDefenseDonation(BuildingObject* building, CreatureObject* creature, uint64 deedOID, int turretIndex) {
	//info("deed oid is " + String::valueOf(deedOID),true);

	ZoneServer* zoneServer = zone->getZoneServer();
	if (zoneServer == NULL)
		return;

	ManagedReference<SceneObject*> defenseObj = zoneServer->getObject(deedOID);

	if (defenseObj == NULL)
		return;

	if (defenseObj->getGameObjectType() == SceneObjectType::MINE) {

		performDonateMine(building, creature, defenseObj);
		return;
	}

	if (defenseObj->isDeedObject()) {


		ManagedReference<Deed*> deed = dynamic_cast<Deed*>(defenseObj.get());
		if (deed != NULL) {

			Reference<SharedObjectTemplate* > generatedTemplate = TemplateManager::instance()->getTemplate(deed->getGeneratedObjectTemplate().hashCode());
			if (generatedTemplate == NULL) {
				return;
			}
			if (generatedTemplate->getGameObjectType() == SceneObjectType::MINEFIELD) {
				performDonateMinefield(building,creature,deed);
				return;
			} else if (generatedTemplate->getGameObjectType() == SceneObjectType::TURRET) {
				performDonateTurret(building,creature,deed);
				return;
			}
		}
	}

	StringIdChatParameter param("@faction/faction_hq/faction_hq_response:terminal_response43"); // This facility does not accept deeds of type '%TO'. Cancelling donation..."
	param.setTO(defenseObj->getObjectName());
	creature->sendSystemMessage(param);
	return;


}

void GCWManagerImplementation::performDonateMine(BuildingObject* building, CreatureObject* creature, SceneObject* mine) {
	// search the building for a minefield that isn't full

	Locker _lock(building, creature);

	for(int i =0; i < building->getChildObjects()->size(); i++) {
		ManagedReference<SceneObject*> obj = building->getChildObjects()->get(i);

		int precount = obj->getContainerObjectsSize();


		if (obj->isMinefield() && precount < 20) {
			_lock.release();
			Locker clock(obj,creature);

			obj->transferObject(mine,-1,true);


			if (precount < obj->getContainerObjectsSize()) {
				StringIdChatParameter param("@faction/faction_hq/faction_hq_response:terminal_response46"); // YOu sucessfully donate a %TO
				param.setTO(mine->getObjectNameStringIdFile(),mine->getObjectNameStringIdName());

				creature->sendSystemMessage(param);
				// broadcast the fact that the minefield is no longer attackable since it just donated
				TangibleObject* tano = cast<TangibleObject*>(obj.get());
				if (tano == NULL)
					return;

				int newbitmask = tano->getPvpStatusBitmask() & (255 - CreatureFlag::ATTACKABLE);
				tano->setPvpStatusBitmask(newbitmask);

				return;
			}
		}
	}

	creature->sendSystemMessage("Unable to donate mines at this time.  Full or no minefields");
}

void GCWManagerImplementation::performDonateMinefield(BuildingObject* building, CreatureObject* creature,  Deed* deed) {
	String serverTemplatePath = deed->getGeneratedObjectTemplate();
	TemplateManager* templateManager = TemplateManager::instance();
	Reference<SharedObjectTemplate*> baseServerTemplate = building->getObjectTemplate();
	Reference<SharedObjectTemplate*> minefieldTemplate = NULL;
	ChildObject* child = NULL;

	int currentMinefieldIndex = 0;

	Locker block(building,creature);

	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL)
		return;

	// go through it and inf the first available mine
	int minefieldIndex = 0;
	for(minefieldIndex = 0; minefieldIndex < baseData->getTotalMinefieldCount(); minefieldIndex++) {
		if (baseData->getMinefieldOID(minefieldIndex) == 0 )
			break;
	}

	//creature->sendSystemMessage("Minefield open at postiion " + String::valueOf(minefieldIndex));

	// this is turret donation
	int nextAvailableMinefield = 0;
	for(nextAvailableMinefield = 0; nextAvailableMinefield < baseData->getTotalTurretCount(); nextAvailableMinefield++) {
		if (baseData->getMinefieldOID(nextAvailableMinefield) == 0)
			break;
	}

	if ( nextAvailableMinefield >= baseData->getTotalMinefieldCount() ) {
		return;
	}

	// now find the coords of the nth turret

	for (int i = 0; i < baseServerTemplate->getChildObjectsSize(); ++i) {
		child = baseServerTemplate->getChildObject(i);
		minefieldTemplate = NULL;
		if (child != NULL) {

			minefieldTemplate = TemplateManager::instance()->getTemplate(child->getTemplateFile().hashCode());
			if (minefieldTemplate->getGameObjectType() == SceneObjectType::MINEFIELD) {
				if (currentMinefieldIndex == nextAvailableMinefield) {
					break;
				} else{
					currentMinefieldIndex++;
				}
			}
		}
	}

	if (child == NULL || minefieldTemplate == NULL || minefieldTemplate->getGameObjectType() != SceneObjectType::MINEFIELD)
		return;

	uint64 minefieldID = addChildInstallationFromDeed(building, child, creature, deed);
	if (minefieldID > 0) {
		baseData->setMinefieldID(currentMinefieldIndex,minefieldID);

		StringIdChatParameter params;
		params.setStringId("@faction/faction_hq/faction_hq_response:terminal_response45");  //"You successfully donate a %TO deed to the current facility."
		params.setTO(deed->getObjectNameStringIdFile(),deed->getObjectNameStringIdName());
		creature->sendSystemMessage(params);
		// TODO: Implement .. verify minefields

		block.release();

		Locker clock(deed, creature);
		deed->destroyObjectFromWorld(true);
	}
}

void GCWManagerImplementation::performDonateTurret(BuildingObject* building, CreatureObject* creature,  Deed* turretDeed) {
	String serverTemplatePath = turretDeed->getGeneratedObjectTemplate();
	TemplateManager* templateManager = TemplateManager::instance();
	Reference<SharedObjectTemplate*> baseServerTemplate = building->getObjectTemplate();

	Reference<SharedObjectTemplate*> turretTemplate = NULL;
	ChildObject* child = NULL;
	int currentTurretIndex = 0;

	// search through the baseData to find the first empty turret index

	Locker block(building,creature);

	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL)
		return;


	// this is turret donation
	int nextAvailableTurret = 0;
	for(nextAvailableTurret = 0; nextAvailableTurret < baseData->getTotalTurretCount(); nextAvailableTurret++) {
		if (baseData->getTurretID(nextAvailableTurret) == 0)
			break;
	}

	if ( nextAvailableTurret >= baseData->getTotalTurretCount() ) {
		return;
	}

	// now find the coords of the nth turret

	for (int i = 0; i < baseServerTemplate->getChildObjectsSize(); ++i) {
		child = baseServerTemplate->getChildObject(i);
		turretTemplate = NULL;
		if (child != NULL) {

			turretTemplate = TemplateManager::instance()->getTemplate(child->getTemplateFile().hashCode());
			if (turretTemplate->getGameObjectType() == SceneObjectType::TURRET) {
				if (currentTurretIndex == nextAvailableTurret) {
					break;
				} else{
					currentTurretIndex++;
				}
			}
		}
	}

	if (child == NULL || turretTemplate == NULL || turretTemplate->getGameObjectType() != SceneObjectType::TURRET)
		return;

	uint64 turretID = addChildInstallationFromDeed(building, child, creature, turretDeed);

	if (turretID > 0) {
		baseData->setTurretID(currentTurretIndex, turretID);

		StringIdChatParameter params;
		params.setStringId("@faction/faction_hq/faction_hq_response:terminal_response45");  // "You successfully donate a %TO deed to the current facility."
		params.setTO(turretDeed->getObjectNameStringIdFile(),turretDeed->getObjectNameStringIdName());
		creature->sendSystemMessage(params);

		verifyTurrets(building);
		block.release();

		Locker clock(turretDeed, creature);
		turretDeed->destroyObjectFromWorld(true);
	}
}

uint64 GCWManagerImplementation::addChildInstallationFromDeed(BuildingObject* building, ChildObject* child, CreatureObject* creature, Deed* deed) {
	Vector3 position = building->getPosition();

	Quaternion* direction = building->getDirection();
	Vector3 childPosition = child->getPosition();
	float angle = direction->getRadians();

	float x = (Math::cos(angle) * childPosition.getX()) + (childPosition.getY() * Math::sin(angle));
	float y = (Math::cos(angle) * childPosition.getY()) - (childPosition.getX() * Math::sin(angle));

	x += position.getX();
	y += position.getY();

	float z = position.getZ() + childPosition.getZ();

	float degrees = direction->getDegrees();
	Quaternion dir = child->getDirection();

	ManagedReference<SceneObject*> obj = zone->getZoneServer()->createObject(deed->getGeneratedObjectTemplate().hashCode(), 1);

	if (obj == NULL) {
		return 0;
	}

	Locker locker(obj);

	obj->initializePosition(x, z, y);
	obj->setDirection(dir.rotate(Vector3(0, 1, 0), degrees));

	if (!obj->isTangibleObject()) {
		obj->destroyObjectFromDatabase(true);
		return 0;
	}

	TangibleObject* tano = cast<TangibleObject*>(obj.get());

	tano->setFaction(building->getFaction());

	tano->setPvpStatusBitmask(building->getPvpStatusBitmask() | tano->getPvpStatusBitmask());

	if (tano->isTurret())
		tano->setDetailedDescription("Donated Turret");

	if (tano->isInstallationObject()) {
		InstallationObject* turret = cast<InstallationObject*>(tano);
		if (turret != NULL) {
			turret->setOwner(building->getObjectID());
			turret->createChildObjects();
			turret->setDeedObjectID(deed->getObjectID());
		}
	}

	zone->transferObject(obj, -1, false);
	building->getChildObjects()->put(obj);
	return obj->getObjectID();
}

void GCWManagerImplementation::addMinefield(BuildingObject* building, SceneObject* minefield) {
	if (building == NULL)
		return;
	//info("adding minefield",true);

	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL)
		return;
	Locker _lock(building);
	if (minefield != NULL)
		baseData->addMinefield(baseData->getTotalMinefieldCount(), minefield->getObjectID());
	else
		baseData->addMinefield(baseData->getTotalMinefieldCount(), 0);


}

void GCWManagerImplementation::addScanner(BuildingObject* building, SceneObject* scanner) {
	if (building == NULL)
		return;

	//info("adding scanner",true);
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL)
		return;

	Locker _lock(building);

	if (scanner != NULL)
		baseData->addScanner(baseData->getTotalScannerCount(), scanner->getObjectID());
	else
		baseData->addScanner(baseData->getTotalScannerCount(), 0);

}

void GCWManagerImplementation::addTurret(BuildingObject* building, SceneObject* turret) {
	if (building == NULL)
		return;

	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL)
		return;
	Locker _lock(building);

	if (turret != NULL)
		baseData->addTurret(baseData->getTotalTurretCount(), turret->getObjectID());
	else {
		// create empty turret slot
		baseData->addTurret(baseData->getTotalTurretCount(),0);
	}

	verifyTurrets(building);
}

void GCWManagerImplementation::verifyTurrets(BuildingObject* building) {
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL)
		return;

	ZoneServer* zoneServer = zone->getZoneServer();

	if (zoneServer == NULL)
		return;

	int turretCount = 0;

	Locker blocker(building);

	for(int i = 0; i < baseData->getTotalTurretCount(); ++i) {
		uint64 turretID = baseData->getTurretID(i);
		ManagedReference<SceneObject*> turret = zoneServer->getObject(baseData->getTurretID(i));

		if (turret != NULL)
			turretCount++;
	}

	baseData->setDefense(turretCount != 0);
}

bool GCWManagerImplementation::canPlaceMoreBases(CreatureObject* creature) {
	if (creature == NULL || !creature->isPlayerCreature())
		return false;

	PlayerObject* ghost = creature->getPlayerObject();
	if (ghost == NULL)
		return false;

	if (zone == NULL)
		return false;

	ZoneServer* server = zone->getZoneServer();
	if (server == NULL)
		return false;

	int baseCount = 0;
	for (int i =0; i < ghost->getTotalOwnedStructureCount(); ++i) {
		ManagedReference<SceneObject*> structure = server->getObject(ghost->getOwnedStructure(i));

		if (structure != NULL && structure->isGCWBase())
			baseCount++;
	}

	if (baseCount >= maxBasesPerPlayer) {
		creature->sendSystemMessage("You own " + String::valueOf(baseCount) + " bases.  The maximum amount is " + String::valueOf(maxBasesPerPlayer));
		return false;
	}

	return true;

}

bool GCWManagerImplementation::hasTooManyBasesNearby(int x, int y) {
	if (zone == NULL)
		return true;

	SortedVector<QuadTreeEntry*> inRangeObjects;
	zone->getInRangeObjects(x, y, 600, &inRangeObjects, true);
	int count = 0;

	for (int i = 0; i < inRangeObjects.size(); ++i) {
		SceneObject* scene = cast<SceneObject*>(inRangeObjects.get(i));

		if (scene == NULL)
			continue;

		if (scene->isGCWBase())
			count++;
	}

	if (count >= 3)
		return true;

	return false;
}

bool GCWManagerImplementation::canUseTerminals(CreatureObject* creature, BuildingObject* building, SceneObject* terminal){
	ManagedReference<PlayerObject*> ghost = creature->getPlayerObject();

	if(ghost == NULL)
		return false;

	if (creature->isDead() || creature->isIncapacitated())
		return false;

	// Make sure the player is in the same cell
	ValidatedPosition* validPosition = ghost->getLastValidatedPosition();
	uint64 parentid = validPosition->getParent();

	if (parentid != terminal->getParentID()) {
		creature->sendSystemMessage("@pvp_rating:ch_terminal_too_far");  // you are too far away from the terminal to use it
		return false;
	}

	// check for PvP base
	if (building->getPvpStatusBitmask() & CreatureFlag::OVERT ){
		if( ghost->getFactionStatus() != FactionStatus::OVERT){
			creature->sendSystemMessage("@faction/faction_hq/faction_hq_response:declared_personnel_only"); // Only Special Forces personnel may access this terminal
			return false;
		}
	}
	// check for PvE base
	else{
		if(ghost->getFactionStatus() < FactionStatus::COVERT) {
			creature->sendSystemMessage("You must be at least combatant");
			return false;
		}
	}
	return true;
}

void GCWManagerImplementation::broadcastBuilding(BuildingObject* building, StringIdChatParameter& params) {
	//Default range of broadcast
	float range = 64;

	if (zone == NULL)
		return;

	SortedVector<QuadTreeEntry*> closeObjects;
	if (building->getCloseObjects() == NULL) {
		building->info("Null closeobjects vector in GCWManagerImplementation::broadcastBuilding", true);
		zone->getInRangeObjects(building->getPositionX(), building->getPositionY(), range, &closeObjects, true);
	} else {
		CloseObjectsVector* closeVector = (CloseObjectsVector*) building->getCloseObjects();
		closeVector->safeCopyTo(closeObjects);
	}

	// send message to all the players in range
	for (int i = 0; i < closeObjects.size(); i++) {
		SceneObject* targetObject = cast<SceneObject*>(closeObjects.get(i));

		if (targetObject->isPlayerCreature() && building->isInRange(targetObject, range)) {
			CreatureObject* targetPlayer = cast<CreatureObject*>(targetObject);

			if (targetPlayer != NULL)
				targetPlayer->sendSystemMessage(params);
		}
	}
}

void GCWManagerImplementation::awardSlicingXP(CreatureObject* creature,  const String& xpType, int val) {
	if (creature->getZoneServer() == NULL)
		return;

	PlayerManager* playerManager = creature->getZoneServer()->getPlayerManager();

	if (playerManager == NULL)
		return;

	playerManager->awardExperience(creature, xpType, val, true);
}


// returns a cost multiplier for faction items
// includes racial penalty and Bonus&Penality for Loser and Winner side
float GCWManagerImplementation::getGCWDiscount(CreatureObject* creature) {

	float discount = 1.0f;

	if (getWinningFaction() != 1 && creature->getFaction() != 0) {
		if (getWinningFaction() == creature->getFaction())
			discount -= winnerBonus /100.f;
		else
			discount -= loserBonus /100.f;
	}

	if (creature->getFaction() == IMPERIALHASH && racialPenaltyEnabled && getRacialPenalty(creature->getSpecies()) > 0)
		discount *= getRacialPenalty(creature->getSpecies());

	return discount;
}

int GCWManagerImplementation::isStrongholdCity(String& city) {
	for (int i = 0; i < imperialStrongholds.size(); i++) {
		if (city.contains(imperialStrongholds.get(i))) {
			return IMPERIALHASH;
		}
	}

	for (int i = 0; i < rebelStrongholds.size(); i++) {
		if (city.contains(rebelStrongholds.get(i))) {
			return REBELHASH;
		}
	}

	return 0;
}

bool GCWManagerImplementation::areOpposingFactions(int faction1, int faction2) {
	if (faction1 == 0 || faction2 == 0)
		return false;

	return faction1 != faction2;
}

void GCWManagerImplementation::constructDNAStrand(BuildingObject* building) {
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL)
		return;

	Vector<String> dnaStrand;

	for (int i = 0; i < dnaStrandLength; i++) {
		int randNucleotide = System::random(dnaNucleotides.size() - 1);
		dnaStrand.add(dnaNucleotides.get(randNucleotide));
	}

	baseData->setDnaStrand(dnaStrand);

	Vector<int> dnaLocks;

	for (int i = 0; i < dnaStrandLength; i++)
		dnaLocks.add(0);

	baseData->setDnaLocks(dnaLocks);
}

void GCWManagerImplementation::createPowerRegulatorRules(BuildingObject* building) {
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL)
		return;

	Vector<int> rules;

	for (int i = 0; i < powerSwitchCount; i++) {
		rules.add(System::random(powerSwitchCount - 1));
	}

	baseData->setPowerSwitchRules(rules);
}

void GCWManagerImplementation::randomizePowerRegulatorSwitches(BuildingObject* building) {
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL)
		return;

	createPowerRegulatorRules(building);

	Vector<bool> switchStates;

	for (int i = 0; i < powerSwitchCount; i++)
		switchStates.add(true);

	int numCycles = ((System::random(2) + 2) * 2) + 1; // 5 to 9 cycles

	for (int i = 0; i < numCycles; i++)
		flipPowerSwitch(building, switchStates, System::random(powerSwitchCount - 1));

	// Make sure the switches arent all set on
	bool doubleCheck = false;

	for (int i = 0; i < powerSwitchCount; i++)
		doubleCheck &= switchStates.get(i);

	if (doubleCheck)
		flipPowerSwitch(building, switchStates, System::random(powerSwitchCount - 1));

	baseData->setPowerSwitchStates(switchStates);
}

void GCWManagerImplementation::flipPowerSwitch(BuildingObject* building, Vector<bool>& switchStates, int flipSwitch) {
	DestructibleBuildingDataComponent* baseData = getDestructibleBuildingData(building);

	if (baseData == NULL)
		return;

	Vector<int> rules = baseData->getPowerSwitchRules();

	switchStates.get(flipSwitch) = !switchStates.get(flipSwitch);

	int affectedSwitch = rules.get(flipSwitch);

	switchStates.get(affectedSwitch) = !switchStates.get(affectedSwitch);
}
