// General rules for the campaign
//
// * Enable unit design and minimap only when an HQ exists
receiveAllEvents(true); //Needed to allow enemy research to apply to them

var mainReticule = true;
var allowDesign = false;

function setMainReticule()
{
	var offHQ = enumStructOffWorld(selectedPlayer, "A0CommandCentre");
	var offRes = enumStructOffWorld(selectedPlayer, "A0ResearchFacility");
	var fCount = 0;
	var facs = ["A0LightFactory", "A0CyborgFactory", "A0VTolFactory1"];

	for (var i = 0, l = facs.length; i < l; ++i)
	{
		var f = facs[i];
		var offWorldCount = enumStructOffWorld(selectedPlayer, f);
		fCount = fCount + countStruct(f) + (offWorldCount != null ? offWorldCount.length : 0);
	}

	setReticuleButton(0, _("Close"), "image_cancel_up.png", "image_cancel_down.png");
	if (fCount > 0 && levelType != LDS_EXPAND_LIMBO)
	{
		setReticuleButton(1, _("Manufacture (F1)"), "image_manufacture_up.png", "image_manufacture_down.png");
	}
	else
	{
		setReticuleButton(1, _("Manufacture - build factory first"), "", "");
	}
	if (countStruct("A0ResearchFacility") + (offRes != null ? offRes.length : 0) > 0 && levelType != LDS_EXPAND_LIMBO)
	{
		setReticuleButton(2, _("Research (F2)"), "image_research_up.png", "image_research_down.png");
	}
	else
	{
		setReticuleButton(2, _("Research - build research facility first"), "", "");
	}
	if (countDroid(DROID_CONSTRUCT, selectedPlayer) > 0)
	{
		setReticuleButton(3, _("Build (F3)"), "image_build_up.png", "image_build_down.png");
	}
	else
	{
		setReticuleButton(3, _("Build - manufacture constructor droids first"), "", "");
	}
	if (allowDesign == true || (offHQ != null ? offHQ.length > 0 : false))
	{
		setReticuleButton(4, _("Design (F4)"), "image_design_up.png", "image_design_down.png");
	}
	else
	{
		setReticuleButton(4, _("Design - construct HQ first"), "", "");
	}
	setReticuleButton(5, _("Intelligence Display (F5)"), "image_intelmap_up.png", "image_intelmap_down.png");
	if (countDroid(DROID_COMMAND, selectedPlayer) > 0)
	{
		setReticuleButton(6, _("Commanders (F6)"), "image_commanddroid_up.png", "image_commanddroid_down.png");
	}
	else
	{
		setReticuleButton(6, _("Commanders - manufacture commanders first"), "", "");
	}
	mainReticule = true; // main reticule window is open
}

function setupGame()
{
	if (tilesetType == "ARIZONA")
	{
		setCampaignNumber(1);
	}
	else if (tilesetType == "URBAN")
	{
		setCampaignNumber(2);
		replaceTexture("page-7-barbarians-arizona.png", "page-7-barbarians-urban.png");
		replaceTexture("page-8-player-buildings-bases.png", "page-8-player-buildings-bases-urban.png");
		replaceTexture("page-9-player-buildings-bases.png", "page-9-player-buildings-bases-urban.png");
	}
	else if (tilesetType == "ROCKIES")
	{
		setCampaignNumber(3);
		replaceTexture("page-8-player-buildings-bases.png", "page-8-player-buildings-bases-rockies.png");
		replaceTexture("page-9-player-buildings-bases.png", "page-9-player-buildings-bases-rockies.png");
		replaceTexture("page-7-barbarians-arizona.png", "page-7-barbarians-kevlar.png");
	}

	if (tilesetType != "ARIZONA")
	{
		setSky("texpages/page-25-sky-urban.png", 0.5, 10000.0);
	}

	setReticuleButton(0, _("Close"), "image_cancel_up.png", "image_cancel_down.png");
	setReticuleButton(1, _("Manufacture (F1)"), "image_manufacture_up.png", "image_manufacture_down.png");
	setReticuleButton(2, _("Research (F2)"), "image_research_up.png", "image_research_down.png");
	setReticuleButton(3, _("Build (F3)"), "image_build_up.png", "image_build_down.png");
	setReticuleButton(4, _("Design (F4)"), "image_design_up.png", "image_design_down.png");
	setReticuleButton(5, _("Intelligence Display (F5)"), "image_intelmap_up.png", "image_intelmap_down.png");
	setReticuleButton(6, _("Commanders (F6)"), "image_commanddroid_up.png", "image_commanddroid_down.png");
	setMainReticule();
	showInterface();
	hackPlayIngameAudio();
}

function eventGameLoaded()
{
	setupGame();
	queue("resetPower", 1000);
}

function eventGameInit()
{
	setupGame();
	queue("resetPower", 1000)
}

function resetPower() {
	var powerLimit = 999999;
	var powerProductionRate = 100;

	// set income modifier/power storage for player 0 (human)
	if (difficulty == EASY)
	{
		powerProductionRate = 115;
	}
	else if (difficulty == HARD)
	{
		powerProductionRate = 85;
		powerLimit = 20000; //base value for Alpha

		if (tilesetType == "URBAN")
		{
			powerLimit = powerLimit + 5000;
		}
		else if (tilesetType == "ROCKIES")
		{
			powerLimit = powerLimit + 10000;
		}
	}
	else if (difficulty == INSANE)
	{
		powerProductionRate = 70;
		powerLimit = 12000; //base value for Alpha

		if (tilesetType == "URBAN")
		{
			powerLimit = powerLimit + 2000;
		}
		else if (tilesetType == "ROCKIES")
		{
			powerLimit = powerLimit + 4000;
		}
	}

	setPowerModifier(powerProductionRate);
	setPowerStorageMaximum(powerLimit);
	if (playerPower(selectedPlayer) >= powerLimit)
	{
		setPower(powerLimit - 1, selectedPlayer);
	}
}

function eventStartLevel()
{
	// Disable by default
	setMiniMap(false);
	setDesign(false);
	setDroidLimit(selectedPlayer, 100, DROID_ANY);
	setDroidLimit(selectedPlayer, 10, DROID_COMMAND);
	setDroidLimit(selectedPlayer, 15, DROID_CONSTRUCT);

	setStructureLimits("A0PowerGenerator", 5, selectedPlayer);
	setStructureLimits("A0ResourceExtractor", 200, selectedPlayer);
	setStructureLimits("A0ResearchFacility", 5, selectedPlayer);
	setStructureLimits("A0LightFactory", 5, selectedPlayer);
	setStructureLimits("A0CommandCentre", 1, selectedPlayer);
	setStructureLimits("A0ComDroidControl", 1, selectedPlayer);
	setStructureLimits("A0CyborgFactory", 5, selectedPlayer);
	setStructureLimits("A0VTolFactory1", 5, selectedPlayer);

	var structlist = enumStruct(selectedPlayer, HQ);
	for (var i = 0; i < structlist.length; i++)
	{
		// Simulate build events to enable minimap/unit design when an HQ exists
		eventStructureBuilt(structlist[i]);
	}
	structlist = enumStructOffWorld(selectedPlayer, HQ);
	for (var i = 0; i < structlist.length; i++)
	{
		eventStructureBuilt(structlist[i]);
	}

	resetPower();
}

function eventDroidBuilt(droid, structure)
{
	var update_reticule = false;

	if (droid.player == selectedPlayer && droid.type == DROID
	    && (droid.droidType == DROID_CONSTRUCT || droid.droidType == DROID_COMMAND))
	{
		update_reticule = true;
	}

	if (mainReticule && update_reticule)
	{
		setMainReticule();
	}
}

function eventStructureBuilt(struct)
{
	var update_reticule = false;

	if (struct.player == selectedPlayer && struct.type == STRUCTURE && struct.stattype == HQ)
	{
		setMiniMap(true); // show minimap
		setDesign(true); // permit designs
		allowDesign = true;
		update_reticule = true;
	}

	if (struct.player == selectedPlayer && struct.type == STRUCTURE
	    && (struct.stattype == RESEARCH_LAB || struct.stattype == CYBORG_FACTORY
	        || struct.stattype == VTOL_FACTORY || struct.stattype == FACTORY))
	{
		update_reticule = true;
	}

	if (mainReticule && update_reticule)
	{
		setMainReticule();
	}
}

function eventDestroyed(victim)
{
	var update_reticule = false;

	if (victim.player == selectedPlayer && victim.type == STRUCTURE && victim.stattype == HQ && !enumStruct(selectedPlayer, HQ).length)
	{
		setMiniMap(false); // hide minimap if HQ is destroyed and no other HQs are present
		setDesign(false); // and disallow design
		allowDesign = false;
		update_reticule = true;
	}

	if (victim.player == selectedPlayer && victim.type == STRUCTURE
	    && (victim.stattype == RESEARCH_LAB || victim.stattype == CYBORG_FACTORY
	        || victim.stattype == VTOL_FACTORY || victim.stattype == FACTORY))
	{
		update_reticule = true;
	}

	if (victim.player == selectedPlayer && victim.type == DROID
	    && (victim.droidType == DROID_CONSTRUCT || victim.droidType == DROID_COMMAND))
	{
		update_reticule = true;
	}

	if (mainReticule && update_reticule)
	{
		setMainReticule();
	}
}

//The mission may not start with any constructors until they come from a transporter.
function eventTransporterLanded(transport)
{
	if (transport.player == 0)
	{
		queue("setMainReticule", 1000);
	}
}

function eventResearched(research, structure, player)
{
	//debug("RESEARCH : " + research.fullname + "(" + research.name + ") for " + player);
	// iterate over all results
	for (var i = 0; i < research.results.length; i++)
	{
		var v = research.results[i];
		//debug("    RESULT : class=" + v['class'] + " parameter=" + v['parameter'] + " value=" + v['value'] + " filter=" + v['filterParameter'] + " filterparam=" + v['filterParameter']);
		for (var cname in Upgrades[player][v['class']]) // iterate over all components of this type
		{
			var parameter = v['parameter'];
			var ctype = v['class'];
			var filterparam = v['filterParameter'];
			if ('filterParameter' in v && Stats[ctype][cname][filterparam] != v['filterValue']) // more specific filter
			{
				continue;
			}
			if (Stats[ctype][cname][parameter] > 0) // only applies if stat has above zero value already
			{
				Upgrades[player][ctype][cname][parameter] += Math.ceil(Stats[ctype][cname][parameter] * v['value'] / 100);
				//debug("      upgraded " + cname + " to " + Upgrades[player][ctype][cname][parameter] + " by " + Math.ceil(Stats[ctype][cname][parameter] * v['value'] / 100));
			}
		}
	}
}

var lastHitTime = 0;
function eventAttacked(victim, attacker) {
	if ((victim.player == selectedPlayer) && gameTime > lastHitTime + 5000)
	{
		lastHitTime = gameTime;
		if (victim.type === STRUCTURE)
			playSound("pcv337.ogg", victim.x, victim.y, victim.z);
		else
			playSound("pcv399.ogg", victim.x, victim.y, victim.z);
	}
}
