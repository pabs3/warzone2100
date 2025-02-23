include("script/campaign/libcampaign.js");
include("script/campaign/templates.js");

const COLLECTIVE_RES = [
	"R-Defense-WallUpgrade03", "R-Struc-Materials04", "R-Struc-Factory-Upgrade04",
	"R-Struc-Factory-Cyborg-Upgrade04", "R-Struc-VTOLFactory-Upgrade01",
	"R-Struc-VTOLPad-Upgrade01", "R-Vehicle-Engine04", "R-Vehicle-Metals04",
	"R-Cyborg-Metals04", "R-Wpn-Cannon-Accuracy02", "R-Wpn-Cannon-Damage04", "R-Wpn-Cannon-ROF02",
	"R-Wpn-Flamer-Damage05", "R-Wpn-Flamer-ROF02", "R-Wpn-MG-Damage05",
	"R-Wpn-MG-ROF03", "R-Wpn-Mortar-Acc02", "R-Wpn-Mortar-Damage04",
	"R-Wpn-Mortar-ROF02", "R-Wpn-Rocket-Accuracy02", "R-Wpn-Rocket-Damage05",
	"R-Wpn-Rocket-ROF03", "R-Wpn-RocketSlow-Accuracy03",
	"R-Wpn-RocketSlow-Damage03", "R-Sys-Sensor-Upgrade01",
];

camAreaEvent("vtolRemoveZone", function(droid)
{
	if ((droid.player === THE_COLLECTIVE) && isVTOL(droid))
	{
		camSafeRemoveObject(droid, false);
	}

	resetLabel("vtolRemoveZone");
});

camAreaEvent("factoryTrigger", function(droid)
{
	camEnableFactory("COHeavyFacL-b1");
	camEnableFactory("COCybFacL-b2");
	camEnableFactory("COHeavyFacR-b1");
	camEnableFactory("COCybFacR-b2");
});

function camEnemyBaseDetected_COMiddleBase()
{
	hackRemoveMessage("C2B_OBJ1", PROX_MSG, CAM_HUMAN_PLAYER);
}

function activateBase1Defenders()
{
	camManageGroup(camMakeGroup("NBaseGroup"), CAM_ORDER_PATROL, {
		pos: [
			camMakePos("leftSideAmbushPos1"),
			camMakePos("leftSideAmbushPos2"),
			camMakePos("leftSideAmbushPos3"),
		],
		interval: 60000,
		regroup: false,
	});
}

function activateBase1Defenders2()
{
	camManageGroup(camMakeGroup("NBaseGroup-below"), CAM_ORDER_PATROL, {
		pos: [
			camMakePos("grp2Pos1"),
			camMakePos("grp2Pos2"),
			camMakePos("grp2Pos3"),
			camMakePos("grp2Pos4"),
			camMakePos("grp2Pos5"),
		],
		interval: 60000,
		regroup: false,
	});
}

function ambushPlayer()
{
	camManageGroup(camMakeGroup("centralBaseGroup"), CAM_ORDER_ATTACK, {
		fallback: camMakePos("COCybFacR-b2Assembly"),
		morale: 20,
		regroup: false,
	});
}

function vtolAttack()
{
	var list; with (camTemplates) list = [colcbv, colatv];
	const POSITIONS = [
		"vtolAppearPosW",
		"vtolAppearPosN",
	];
	camSetVtolData(THE_COLLECTIVE, POSITIONS, "vtolRemove", list, camChangeOnDiff(300000), "COCommandCenter"); //5 min
}

function truckDefense()
{
	if (enumDroid(THE_COLLECTIVE, DROID_CONSTRUCT).length > 0)
		queue("truckDefense", 160000);

	const list = ["CO-Tower-MG3", "CO-Tower-LtATRkt", "CO-Tower-MdCan", "CO-Tower-LtATRkt"];
	camQueueBuilding(THE_COLLECTIVE, list[camRand(list.length)]);
}

function transferPower()
{
	//increase player power level and play sound
     setPower(playerPower(me) + 4000);
     playSound("power-transferred.ogg");
}

function eventStartLevel()
{
	camSetStandardWinLossConditions(CAM_VICTORY_STANDARD, "SUB_2_2S");

	var startpos = getObject("startPosition");
	var lz = getObject("landingZone"); //player lz
	centreView(startpos.x, startpos.y);
	setNoGoArea(lz.x, lz.y, lz.x2, lz.y2, CAM_HUMAN_PLAYER);

	setPower(AI_POWER, THE_COLLECTIVE);
	setMissionTime(camChangeOnDiff(7200)); // 2 hr.
	camPlayVideos(["MB2_B_MSG", "MB2_B_MSG2"]);

	camSetArtifacts({
		"COResearchLab": { tech: "R-Wpn-Flame2" },
		"COHeavyFac-b4": { tech: "R-Wpn-RocketSlow-ROF01" },
		"COHeavyFacL-b1": { tech: "R-Wpn-MG-ROF03" },
		"COCommandCenter": { tech: "R-Vehicle-Body06" }, //Panther
	});

	setPower(camChangeOnDiff(20000, true), THE_COLLECTIVE);
	camCompleteRequiredResearch(COLLECTIVE_RES, THE_COLLECTIVE);

	camSetEnemyBases({
		"CONorthBase": {
			cleanup: "base1Cleanup",
			detectMsg: "C2B_BASE1",
			detectSnd: "pcv379.ogg",
			eliminateSnd: "pcv394.ogg",
		},
		"COCentralBase": {
			cleanup: "base2Cleanup",
			detectMsg: "C2B_BASE2",
			detectSnd: "pcv379.ogg",
			eliminateSnd: "pcv394.ogg",
		},
		"COMiddleBase": {
			cleanup: "base4Cleanup",
			detectMsg: "C2B_BASE4",
			detectSnd: "pcv379.ogg",
			eliminateSnd: "pcv394.ogg",
		},
	});

	with (camTemplates) camSetFactories({
		"COHeavyFacL-b1": {
			assembly: "COHeavyFacL-b1Assembly",
			order: CAM_ORDER_ATTACK,
			groupSize: 5,
			throttle: camChangeOnDiff(70000),
			data: {
				regroup: false,
				repair: 30,
				count: -1,
			},
			templates: [comatt, cohct, comct]
		},
		"COHeavyFacR-b1": {
			assembly: "COHeavyFacR-b1Assembly",
			order: CAM_ORDER_ATTACK,
			groupSize: 5,
			throttle: camChangeOnDiff(60000),
			data: {
				regroup: false,
				repair: 30,
				count: -1,
			},
			templates: [comatt, cohct, comct]
		},
		"COCybFacL-b2": {
			assembly: "COCybFacL-b2Assembly",
			order: CAM_ORDER_ATTACK,
			groupSize: 4,
			throttle: camChangeOnDiff(30000),
			data: {
				regroup: false,
				repair: 40,
				count: -1,
			},
			templates: [npcybc, npcybr]
		},
		"COCybFacR-b2": {
			assembly: "COCybFacR-b2Assembly",
			order: CAM_ORDER_ATTACK,
			groupSize: 4,
			throttle: camChangeOnDiff(40000),
			data: {
				regroup: false,
				repair: 40,
				count: -1,
			},
			templates: [npcybc, npcybr, npcybf, npcybm]
		},
		"COHeavyFac-b4": {
			assembly: "COHeavyFac-b4Assembly",
			order: CAM_ORDER_ATTACK,
			groupSize: 4,
			throttle: camChangeOnDiff(50000),
			data: {
				regroup: false,
				repair: 30,
				count: -1,
			},
			templates: [comatt, comit]
		},
		"COCybFac-b4": {
			assembly: "COCybFac-b4Assembly",
			order: CAM_ORDER_ATTACK,
			groupSize: 4,
			throttle: camChangeOnDiff(40000),
			data: {
				regroup: false,
				repair: 40,
				count: -1,
			},
			templates: [npcybc, npcybr, npcybf]
		},
	});

	camManageTrucks(THE_COLLECTIVE);
	truckDefense();
	hackAddMessage("C2B_OBJ1", PROX_MSG, CAM_HUMAN_PLAYER, true);

	camEnableFactory("COHeavyFac-b4");
	camEnableFactory("COCybFac-b4");

	queue("transferPower", 2000);
	queue("ambushPlayer", 10000); // 10 sec
	queue("vtolAttack", camChangeOnDiff(240000)); //4 min
	queue("activateBase1Defenders2", camChangeOnDiff(1200000)); //20 min.
	queue("activateBase1Defenders", camChangeOnDiff(1800000)); //30 min.
}
