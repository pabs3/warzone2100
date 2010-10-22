/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2010  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/
/**
 * @file map.c
 *
 * Utility functions for the map data structure.
 *
 */
#include <time.h>

#include "lib/framework/frame.h"
#include "lib/framework/endian_hack.h"
#include "lib/framework/file.h"
#include "lib/framework/physfs_ext.h"
#include "lib/framework/tagfile.h"
#include "lib/ivis_common/tex.h"
#include "lib/netplay/netplay.h"  // For syncDebug

#include "map.h"
#include "hci.h"
#include "projectile.h"
#include "display3d.h"
#include "lighting.h"
#include "game.h"
#include "texture.h"
#include "environ.h"
#include "advvis.h"
#include "research.h"
#include "mission.h"
#include "gateway.h"
#include "wrappers.h"
#include "mapgrid.h"
#include "astar.h"
#include "fpath.h"
#include "levels.h"
#include "scriptfuncs.h"

struct ffnode
{
	struct ffnode *next;
	int x, y;
};

//scroll min and max values
SDWORD		scrollMinX, scrollMaxX, scrollMinY, scrollMaxY;

/* Structure definitions for loading and saving map data */
typedef struct _map_save_header
{
	char		aFileType[4];
	UDWORD		version;
	UDWORD		width;
	UDWORD		height;
} MAP_SAVEHEADER;

typedef struct _map_save_tilev2
{
	UWORD		texture;
	UBYTE		height;
} MAP_SAVETILE;

typedef struct _gateway_save_header
{
	UDWORD		version;
	UDWORD		numGateways;
} GATEWAY_SAVEHEADER;

typedef struct _gateway_save
{
	UBYTE	x0,y0,x1,y1;
} GATEWAY_SAVE;

typedef struct _zonemap_save_header {
	UWORD version;
	UWORD numZones;
	UWORD numEquivZones;
	UWORD pad;
} ZONEMAP_SAVEHEADER;

/* Sanity check definitions for the save struct file sizes */
#define SAVE_HEADER_SIZE	16
#define SAVE_TILE_SIZE		3
#define SAVE_TILE_SIZEV1	6
#define SAVE_TILE_SIZEV2	3

// Maximum expected return value from get height
#define	MAX_HEIGHT			(256 * ELEVATION_SCALE)

/* Number of entries in the sqrt(1/(1+x*x)) table for aaLine */
#define	ROOT_TABLE_SIZE		1024

/* The size and contents of the map */
SDWORD	mapWidth = 0, mapHeight = 0;
MAPTILE	*psMapTiles = NULL;

#define WATER_DEPTH	180

static void SetGroundForTile(const char *filename, const char *nametype);
static int getTextureType(const char *textureType);
static BOOL hasDecals(int i, int j);
static void SetDecals(const char *filename, const char *decal_type);
static void init_tileNames(int type);

/// The different ground types
GROUND_TYPE *psGroundTypes;
int numGroundTypes;
int waterGroundType;
int cliffGroundType;
char *tileset = NULL;
static int numTile_names;
static char *Tile_names = NULL;
#define ARIZONA 1
#define URBAN 2
#define ROCKIE 3

static int *map;			// 3D array pointer that holds the texturetype
static int *mapDecals;		// array that tells us what tile is a decal
#define MAX_TERRAIN_TILES 100		// max that we support (for now)

/* Look up table that returns the terrain type of a given tile texture */
UBYTE terrainTypes[MAX_TILE_TEXTURES];

/* Create a new map of a specified size */
BOOL mapNew(UDWORD width, UDWORD height)
{
	MAPTILE *psTile;
	UDWORD	i;

	/* See if a map has already been allocated */
	if (psMapTiles != NULL)
	{
		/* Clear all the objects off the map and free up the map memory */
		gwShutDown();
		releaseAllProxDisp();
		freeAllDroids();
		freeAllStructs();
		freeAllFeatures();
		freeAllFlagPositions();
		proj_FreeAllProjectiles();
		free(psMapTiles);
		psMapTiles = NULL;
		initStructLimits();
		
		free(psGroundTypes);
	}

	if (width*height > MAP_MAXAREA)
	{
		debug(LOG_ERROR, "Map too large : %u %u", width, height);
		return false;
	}

	if (width <=1 || height <=1)
	{
		debug(LOG_ERROR, "Map is too small : %u, %u", width, height);
		return false;
	}

	psMapTiles = calloc(width * height, sizeof(MAPTILE));
	if (psMapTiles == NULL)
	{
		debug(LOG_FATAL, "Out of memory");
		abort();
		return false;
	}

	psTile = psMapTiles;
	for (i = 0; i < width * height; i++)
	{
		psTile->height = MAX_HEIGHT / 4;
		psTile->illumination = 255;
		psTile->level = psTile->illumination;
		memset(psTile->watchers, 0, sizeof(psTile->watchers));
		psTile->colour= WZCOL_WHITE;
		psTile->tileExploredBits = 0;
		psTile->sensorBits = 0;
		psTile++;
	}

	mapWidth = width;
	mapHeight = height;

	intSetMapPos(mapWidth * TILE_UNITS/2, mapHeight * TILE_UNITS/2);

	environReset();

	/*set up the scroll mins and maxs - set values to valid ones for a new map*/
	scrollMinX = scrollMinY = 0;
	scrollMaxX = mapWidth;
	scrollMaxY = mapHeight;

	gridReset();

	return true;
}

static void init_tileNames(int type)
{
	char	*pFileData = NULL;
	char	name[100] = {'\0'};
	int		numlines = 0, i = 0, cnt = 0;
	uint32_t	fileSize = 0;

	pFileData = fileLoadBuffer;

	switch (type)
	{
		case ARIZONA:
		{
			if (!loadFileToBuffer("tileset/arizona_enum.txt", pFileData, FILE_LOAD_BUFFER_SIZE, &fileSize))
			{
				debug(LOG_FATAL, "tileset/arizona_enum.txt not found.  Aborting.");
				abort();
			}

			sscanf(pFileData, "%[^','],%d%n", name, &numlines, &cnt);
			pFileData += cnt;

			if (strcmp("arizona_enum", name))
			{
				debug(LOG_FATAL, "%s found, but was expecting arizona_enum, aborting.", name);
				abort();
			}
			break;
		}
		case URBAN:
		{
			if (!loadFileToBuffer("tileset/urban_enum.txt", pFileData, FILE_LOAD_BUFFER_SIZE, &fileSize))
			{
				debug(LOG_FATAL, "tileset/urban_enum.txt not found.  Aborting.");
				abort();
			}

			sscanf(pFileData, "%[^','],%d%n", name, &numlines, &cnt);
			pFileData += cnt;

			if (strcmp("urban_enum", name))
			{
				debug(LOG_FATAL, "%s found, but was expecting urban_enum, aborting.", name);
				abort();
			}
			break;
		}
		case ROCKIE:
		{
			if (!loadFileToBuffer("tileset/rockie_enum.txt", pFileData, FILE_LOAD_BUFFER_SIZE, &fileSize))
			{
				debug(LOG_FATAL, "tileset/rockie_enum.txt not found.  Aborting.");
				abort();
			}

			sscanf(pFileData, "%[^','],%d%n", name, &numlines, &cnt);
			pFileData += cnt;

			if (strcmp("rockie_enum", name))
			{
				debug(LOG_FATAL, "%s found, but was expecting rockie_enum, aborting.", name);
				abort();
			}
			break;
		}
		default:
		debug(LOG_FATAL, "Unknown type (%d) given.  Aborting.", type);
		abort();
	}

	debug(LOG_TERRAIN, "name: %s, with %d entries", name, numlines);
	if (numlines == 0 || numlines > MAX_TERRAIN_TILES)
	{
		debug(LOG_FATAL, "Rockie_enum paramater is out of range (%d). Aborting.", numlines);
		abort();
	}

	numTile_names = numlines;
	//increment the pointer to the start of the next record
	pFileData = strchr(pFileData,'\n') + 1;

	Tile_names = malloc(numlines * sizeof(char[40]) );
	memset(Tile_names, 0x0, (numlines * sizeof(char[40])));

	for (i=0; i < numlines; i++)
	{
		sscanf(pFileData, "%s%n", &Tile_names[i*40], &cnt);
		pFileData += cnt;
		//increment the pointer to the start of the next record
		pFileData = strchr(pFileData,'\n') + 1;
	}
}

// This is the main loading routine to get all the map's parameters set.
// Once it figures out what tileset we need, we then parse the files for that tileset.
// Currently, we only support 3 tilesets.  Arizona, Urban, and Rockie
static BOOL mapLoadGroundTypes(void)
{
	char	*pFileData = NULL;
	char	tilename[255] = {'\0'};
	char	textureName[255] = {'\0'};
	char	textureType[255] = {'\0'};
	double	textureSize = 0.f;
	int		numlines = 0;
	int		cnt = 0, i = 0;
	uint32_t	fileSize = 0;

	pFileData = fileLoadBuffer;

	debug(LOG_TERRAIN, "tileset: %s", tileset);
	// For Arizona
	if (strcmp(tileset, "texpages/tertilesc1hw") == 0)
	{
fallback:
		init_tileNames(ARIZONA);
		if (!loadFileToBuffer("tileset/tertilesc1hwGtype.txt", pFileData, FILE_LOAD_BUFFER_SIZE, &fileSize))
		{
			debug(LOG_FATAL, "tileset/tertilesc1hwGtype.txt not found, aborting.");
			abort();
		}

		sscanf(pFileData, "%[^','],%d%n", tilename, &numlines, &cnt);
		pFileData += cnt;

		if (strcmp(tilename, "tertilesc1hw"))
		{
			debug(LOG_FATAL, "%s found, but was expecting tertilesc1hw!  Aborting.", tilename);
			abort();
		}

		debug(LOG_TERRAIN, "tilename: %s, with %d entries", tilename, numlines);
		//increment the pointer to the start of the next record
		pFileData = strchr(pFileData,'\n') + 1;
		numGroundTypes = numlines;
		psGroundTypes = malloc(sizeof(GROUND_TYPE)*numlines);

		for (i=0; i < numlines; i++)
		{
			sscanf(pFileData, "%[^','],%[^','],%lf%n", textureType, textureName, &textureSize, &cnt);
			pFileData += cnt;
			//increment the pointer to the start of the next record
			pFileData = strchr(pFileData,'\n') + 1;

			psGroundTypes[getTextureType(textureType)].textureName = strdup(textureName);
			psGroundTypes[getTextureType(textureType)].textureSize = textureSize ;
		}

		waterGroundType = getTextureType("a_water");
		cliffGroundType = getTextureType("a_cliff");

		SetGroundForTile("tileset/arizonaground.txt", "arizona_ground");
		SetDecals("tileset/arizonadecals.txt", "arizona_decals");
	}
	// for Urban
	else if (strcmp(tileset, "texpages/tertilesc2hw") == 0)
	{
		init_tileNames(URBAN);
		if (!loadFileToBuffer("tileset/tertilesc2hwGtype.txt", pFileData, FILE_LOAD_BUFFER_SIZE, &fileSize))
		{
			debug(LOG_POPUP, "tileset/tertilesc2hwGtype.txt not found, using default terrain ground types.");
			goto fallback;
		}

		sscanf(pFileData, "%[^','],%d%n", tilename, &numlines, &cnt);
		pFileData += cnt;

		if (strcmp(tilename, "tertilesc2hw"))
		{
			debug(LOG_POPUP, "%s found, but was expecting tertilesc2hw!", tilename);
			goto fallback;
		}

		debug(LOG_TERRAIN, "tilename: %s, with %d entries", tilename, numlines);
		//increment the pointer to the start of the next record
		pFileData = strchr(pFileData,'\n') + 1;
		numGroundTypes = numlines;
		psGroundTypes = malloc(sizeof(GROUND_TYPE)*numlines);

		for (i=0; i < numlines; i++)
		{
			sscanf(pFileData, "%[^','],%[^','],%lf%n", textureType, textureName, &textureSize, &cnt);
			pFileData += cnt;
			//increment the pointer to the start of the next record
			pFileData = strchr(pFileData,'\n') + 1;

			psGroundTypes[getTextureType(textureType)].textureName = strdup(textureName);
			psGroundTypes[getTextureType(textureType)].textureSize = textureSize;
		}

		waterGroundType = getTextureType("u_water");
		cliffGroundType = getTextureType("u_cliff");

		SetGroundForTile("tileset/urbanground.txt", "urban_ground");
		SetDecals("tileset/urbandecals.txt", "urban_decals");
	}
	// for Rockie
	else if (strcmp(tileset, "texpages/tertilesc3hw") == 0)
	{
		init_tileNames(ROCKIE);
		if (!loadFileToBuffer("tileset/tertilesc3hwGtype.txt", pFileData, FILE_LOAD_BUFFER_SIZE, &fileSize))
		{
			debug(LOG_POPUP, "tileset/tertilesc3hwGtype.txt not found, using default terrain ground types.");
			goto fallback;
		}

		sscanf(pFileData, "%[^','],%d%n", tilename, &numlines, &cnt);
		pFileData += cnt;

		if (strcmp(tilename, "tertilesc3hw"))
		{
			debug(LOG_POPUP, "%s found, but was expecting tertilesc3hw!", tilename);
			goto fallback;
		}

		debug(LOG_TERRAIN, "tilename: %s, with %d entries", tilename, numlines);
		//increment the pointer to the start of the next record
		pFileData = strchr(pFileData,'\n') + 1;
		numGroundTypes = numlines;
		psGroundTypes = malloc(sizeof(GROUND_TYPE)*numlines);

		for (i=0; i < numlines; i++)
		{
			sscanf(pFileData, "%[^','],%[^','],%lf%n", textureType, textureName, &textureSize, &cnt);
			pFileData += cnt;
			//increment the pointer to the start of the next record
			pFileData = strchr(pFileData,'\n') + 1;

			psGroundTypes[getTextureType(textureType)].textureName = strdup(textureName);
			psGroundTypes[getTextureType(textureType)].textureSize = textureSize;
		}

		waterGroundType = getTextureType("r_water");
		cliffGroundType = getTextureType("r_cliff");

		SetGroundForTile("tileset/rockieground.txt", "rockie_ground");
		SetDecals("tileset/rockiedecals.txt", "rockie_decals");
	}
	// When a map uses something other than the above, we fallback to Arizona
	else
	{
		debug(LOG_ERROR, "unsupported tileset: %s", tileset);
		debug(LOG_POPUP, "This is a UNSUPPORTED map with a custom tileset.\nDefaulting to tertilesc1hw -- map may look strange!");
		// HACK: / FIXME: For now, we just pretend this is a tertilesc1hw map.
		goto fallback;
	}
	return true;
}

// Parse the file to set up the ground type
static void SetGroundForTile(const char *filename, const char *nametype)
{
	char	*pFileData = NULL;
	char	tilename[255] = {'\0'};
	char	val1[25], val2[25], val3[25], val4[25];
	int		numlines = 0;
	int		cnt = 0, i = 0;
	uint32_t	fileSize = 0;

	pFileData = fileLoadBuffer;
	if (!loadFileToBuffer(filename, pFileData, FILE_LOAD_BUFFER_SIZE, &fileSize))
	{
		debug(LOG_FATAL, "%s not found, aborting.", filename);
		abort();
	}

	sscanf(pFileData, "%[^','],%d%n", tilename, &numlines, &cnt);
	pFileData += cnt;

	if (strcmp(tilename, nametype))
	{
		debug(LOG_FATAL, "%s found, but was expecting %s, aborting.", tilename, nametype);
		abort();
	}

	debug(LOG_TERRAIN, "tilename: %s, with %d entries", tilename, numlines);
	//increment the pointer to the start of the next record
	pFileData = strchr(pFileData,'\n') + 1;

	map = malloc(sizeof(int) * numlines * 2 * 2 );	// this is a 3D array map[numlines][2][2]

	for (i=0; i < numlines; i++)
	{
		sscanf(pFileData, "%[^','],%[^','],%[^','],%s%n", val1, val2, val3, val4, &cnt);
		pFileData += cnt;
		//increment the pointer to the start of the next record
		pFileData = strchr(pFileData,'\n') + 1;

		// inline int iA(int i, int j, int k){ return i*N2*N3 + j*N3 + k; }
		// in case it isn't obvious, this is a 3D array, and using pointer math to access each element.
		// so map[10][0][1] would be map[10*2*2 + 0 + 1] == map[41]
		// map[10][1][0] == map[10*2*2 + 2 + 0] == map[42]
		map[i*2*2+0*2+0] = getTextureType(val1);
		map[i*2*2+0*2+1] = getTextureType(val2);
		map[i*2*2+1*2+0] = getTextureType(val3);
		map[i*2*2+1*2+1] = getTextureType(val4);
	}
}

// getTextureType() -- just returns the value for that texture type.
static int getTextureType(const char *textureType)
{
	int i = 0;
	for (i=0; i < numTile_names; i++)
	{
		if (!strcmp(textureType, &Tile_names[i*40]))
		{
			return i;
		}
	}
	debug(LOG_FATAL, "unknown type [%s] found, aborting!", textureType);
	abort();
}

// groundFromMapTile() just a simple lookup table, using pointers to access the 3D map array
//	(quasi) pointer math is: map[num elements][2][2]
//	so map[10][0][1] would be map[10*2*2 + 0*2 + 1] == map[41]
static int groundFromMapTile(int tile, int j, int k)
{
	return map[TileNumber_tile(tile)* 2 * 2 + j * 2 + k];
}

static void rotFlip(int tile, int *i, int *j)
{
	int texture = TileNumber_texture(tile);
	int rot;
	int map[2][2], invmap[4][2];

	if (texture & TILE_XFLIP)
	{
		*i = 1 - *i;
	}
	if (texture & TILE_YFLIP)
	{
		*j = 1 - *j;
	}

	map[0][0] = 0; invmap[0][0] = 0; invmap[0][1] = 0;
	map[1][0] = 1; invmap[1][0] = 1; invmap[1][1] = 0;
	map[1][1] = 2; invmap[2][0] = 1; invmap[2][1] = 1;
	map[0][1] = 3; invmap[3][0] = 0; invmap[3][1] = 1;
	rot = map[*i][*j];
	rot -= (texture & TILE_ROTMASK) >> TILE_ROTSHIFT;
	while(rot < 0) rot += 4;
	*i = invmap[rot][0];
	*j = invmap[rot][1];
}

/// Tries to figure out what ground type a grid point is from the surrounding tiles
static int determineGroundType(int x, int y, const char *tileset)
{
	int ground[2][2];
	int votes[2][2];
	int i,j, tile;
	int a,b, best;
	BOOL arizona, rockies, urban;
	arizona = rockies = urban = false;
	if (strcmp(tileset, "texpages/tertilesc1hw") == 0)
	{
		arizona = true;
	} else if (strcmp(tileset, "texpages/tertilesc2hw") == 0)
	{
		urban = true;
	} else if (strcmp(tileset, "texpages/tertilesc3hw") == 0)
	{
		rockies = true;
	} else
	{
		debug(LOG_ERROR, "unknown tileset");
		return 0;
	}

	if (x < 0 || y < 0 || x >= mapWidth || y >= mapHeight)
	{
		return 0; // just return the first ground type
	}
	
	// check what tiles surround this grid point
	for(i=0;i<2;i++)
	{
		for(j=0;j<2;j++)
		{
			if (x+i-1 < 0 || y+j-1 < 0 || x+i-1 >= mapWidth || y+j-1 >= mapHeight)
			{
				tile = 0;
			}
			else
			{
				tile = mapTile(x+i-1, y+j-1)->texture;
			}
			a = i;
			b = j;
			rotFlip(tile, &a, &b);
			ground[i][j] = groundFromMapTile(tile, a, b);
			
			votes[i][j] = 0;

			// cliffs are so small they won't show up otherwise
			if (urban)
			{
				if (ground[i][j] == getTextureType("u_cliff"))
					return ground[i][j];
			}
			else if (arizona)
			{
				if (ground[i][j] == getTextureType("a_cliff"))
					return ground[i][j];
			}
			else if (rockies)
			{
				if (ground[i][j] == getTextureType("r_cliff"))
					return ground[i][j];
			}
		}
	}

	// now vote, because some maps have seams
	for(i=0;i<2;i++)
	{
		for(j=0;j<2;j++)
		{
			for(a=0;a<2;a++)
			{
				for(b=0;b<2;b++)
				{
					if (ground[i][j] == ground[a][b])
					{
						votes[i][j]++;
					}
				}
			}
		}
	}
	// and determine the winner
	best = -1;
	for(i=0;i<2;i++)
	{
		for(j=0;j<2;j++)
		{
			if (votes[i][j] > best)
			{
				best = votes[i][j];
				a = i;
				b = j;
			}
		}
	}
	return ground[a][b];
}

// SetDecals()
// reads in the decal array for the requested tileset.
static void SetDecals(const char *filename, const char *decal_type)
{
	char decalname[50], *pFileData;
	int numlines, cnt, i, tiledecal;
	uint32_t fileSize;

	pFileData = fileLoadBuffer;

	if (!loadFileToBuffer(filename, pFileData, FILE_LOAD_BUFFER_SIZE, &fileSize))
	{
		debug(LOG_POPUP, "%s not found, aborting.", filename);
		abort();
	}

	sscanf(pFileData, "%[^','],%d%n", decalname, &numlines, &cnt);
	pFileData += cnt;

	if (strcmp(decalname, decal_type))
	{
		debug(LOG_POPUP, "%s found, but was expecting %s, aborting.", decalname, decal_type);
		abort();
	}

	debug(LOG_TERRAIN, "reading: %s, with %d entries", filename, numlines);
	//increment the pointer to the start of the next record
	pFileData = strchr(pFileData,'\n') + 1;
	if (numlines > MAX_TERRAIN_TILES)
	{
		debug(LOG_FATAL, "Too many tiles, we only support %d max at this time", MAX_TERRAIN_TILES);
		abort();
	}
	mapDecals = malloc(sizeof(int)*MAX_TERRAIN_TILES);		// max of 80 tiles that we support
	memset(mapDecals, 0x0, sizeof(int)*MAX_TERRAIN_TILES);	// set everything to false;

	for (i=0; i < numlines; i++)
	{
		sscanf(pFileData, "%d%n", &tiledecal, &cnt);
		pFileData += cnt;
		//increment the pointer to the start of the next record
		pFileData = strchr(pFileData,'\n') + 1;
		mapDecals[tiledecal] = 1;
	}
}
// hasDecals()
// Checks to see if the requested tile has a decal on it or not.
static BOOL hasDecals(int i, int j)
{
	int index = 0;
	index = TileNumber_tile(mapTile(i, j)->texture);
	if (index > MAX_TERRAIN_TILES)
	{
		debug(LOG_FATAL, "Tile index is out of range!  Was %d, our max is %d", index, MAX_TERRAIN_TILES);
		abort();
	}
	return mapDecals[index];
}
// mapSetGroundTypes()
// Sets the ground type to be a decal or not
static BOOL mapSetGroundTypes(void)
{
	int i,j;

	for (i=0;i<mapWidth;i++)
	{
		for (j=0;j<mapHeight;j++)
		{
			MAPTILE *psTile = mapTile(i, j);

			psTile->ground = determineGroundType(i,j,tileset);

			if (hasDecals(i,j))
			{
				SET_TILE_DECAL(psTile);
			}
			else
			{
				CLEAR_TILE_DECAL(psTile);
			}
		}
	}
	return true;
}

/* Initialise the map structure */
BOOL mapLoad(char *filename)
{
	UDWORD		numGw, width, height;
	char		aFileType[4];
	UDWORD		version;
	UDWORD		i, j;
	PHYSFS_file	*fp = PHYSFS_openRead(filename);

	if (!fp)
	{
		debug(LOG_ERROR, "%s not found", filename);
		return false;
	}
	else if (PHYSFS_read(fp, aFileType, 4, 1) != 1
	    || !PHYSFS_readULE32(fp, &version)
	    || !PHYSFS_readULE32(fp, &width)
	    || !PHYSFS_readULE32(fp, &height)
	    || aFileType[0] != 'm'
	    || aFileType[1] != 'a'
	    || aFileType[2] != 'p')
	{
		debug(LOG_ERROR, "Bad header in %s", filename);
		goto failure;
	}
	else if (version <= VERSION_9)
	{
		debug(LOG_ERROR, "%s: Unsupported save format version %u", filename, version);
		goto failure;
	}
	else if (version > CURRENT_VERSION_NUM)
	{
		debug(LOG_ERROR, "%s: Undefined save format version %u", filename, version);
		goto failure;
	}
	else if (width * height > MAP_MAXAREA)
	{
		debug(LOG_ERROR, "Map %s too large : %d %d", filename, width, height);
		goto failure;
	}

	if (width <=1 || height <=1)
	{
		debug(LOG_ERROR, "Map is too small : %u, %u", width, height);
		goto failure;
	}

	/* See if this is the first time a map has been loaded */
	ASSERT(psMapTiles == NULL, "Map has not been cleared before calling mapLoad()!");

	/* Allocate the memory for the map */
	psMapTiles = calloc(width * height, sizeof(MAPTILE));
	ASSERT(psMapTiles != NULL, "Out of memory" );

	mapWidth = width;
	mapHeight = height;
	
	// FIXME: the map preview code loads the map without setting the tileset
	if (!tileset)
	{
		debug(LOG_WARNING, "tileset not loaded, using arizona (map preview?)");
		tileset = strdup("texpages/tertilesc1hw");
	}
	
	// load the ground types
	if (!mapLoadGroundTypes())
	{
		goto failure;
	}
	
	//load in the map data itself
	
	/* Load in the map data */
	for (i = 0; i < mapWidth * mapHeight; i++)
	{
		UWORD	texture;
		UBYTE	height;

		if (!PHYSFS_readULE16(fp, &texture) || !PHYSFS_readULE8(fp, &height))
		{
			debug(LOG_ERROR, "%s: Error during savegame load", filename);
			goto failure;
		}

		psMapTiles[i].texture = texture;
		psMapTiles[i].height = height;

		// Visibility stuff
		memset(psMapTiles[i].watchers, 0, sizeof(psMapTiles[i].watchers));
		psMapTiles[i].sensorBits = 0;
		psMapTiles[i].tileExploredBits = 0;
	}

	if (!PHYSFS_readULE32(fp, &version) || !PHYSFS_readULE32(fp, &numGw) || version != 1)
	{
		debug(LOG_ERROR, "Bad gateway in %s", filename);
		goto failure;
	}

	for (i = 0; i < numGw; i++)
	{
		UBYTE	x0, y0, x1, y1;

		if (!PHYSFS_readULE8(fp, &x0) || !PHYSFS_readULE8(fp, &y0) || !PHYSFS_readULE8(fp, &x1) || !PHYSFS_readULE8(fp, &y1))
		{
			debug(LOG_ERROR, "%s: Failed to read gateway info", filename);
			goto failure;
		}
		if (!gwNewGateway(x0, y0, x1, y1))
		{
			debug(LOG_ERROR, "%s: Unable to add gateway", filename);
			goto failure;
		}
	}
	
	if (!mapSetGroundTypes())
	{
		goto failure;
	}

	// reset the random water bottom heights
	environReset();

	// set the river bed
	for (i = 0; i < mapWidth; i++)
	{
		for (j = 0; j < mapHeight; j++)
		{
			// FIXME: magic number
			mapTile(i, j)->waterLevel = mapTile(i, j)->height - world_coord(1) / 3.0f / (float)ELEVATION_SCALE;
			// lower riverbed
			if (mapTile(i, j)->ground == waterGroundType)
			{
				mapTile(i, j)->height -= (WATER_DEPTH - 2.0f * environGetData(i, j)) / (float)ELEVATION_SCALE;
			}
		}
	}

	/* set up the scroll mins and maxs - set values to valid ones for any new map */
	scrollMinX = scrollMinY = 0;
	scrollMaxX = mapWidth;
	scrollMaxY = mapHeight;

	/* Set continents. This should ideally be done in advance by the map editor. */
	mapFloodFillContinents();

	PHYSFS_close(fp);
	return true;
	
failure:
	PHYSFS_close(fp);
	return false;
}

/* Save the map data */
BOOL mapSave(char **ppFileData, UDWORD *pFileSize)
{
	UDWORD	i;
	MAP_SAVEHEADER	*psHeader = NULL;
	MAP_SAVETILE	*psTileData = NULL;
	MAPTILE	*psTile = NULL;
	GATEWAY *psCurrGate = NULL;
	GATEWAY_SAVEHEADER *psGateHeader = NULL;
	GATEWAY_SAVE *psGate = NULL;
	SDWORD	numGateways = 0;
	ZONEMAP_SAVEHEADER *psZoneHeader = NULL;

	// find the number of non water gateways
	for(psCurrGate = gwGetGateways(); psCurrGate; psCurrGate = psCurrGate->psNext)
	{
		numGateways += 1;
	}

	/* Allocate the data buffer */
	*pFileSize = SAVE_HEADER_SIZE + mapWidth*mapHeight * SAVE_TILE_SIZE;
	// Add on the size of the gateway data.
	*pFileSize += sizeof(GATEWAY_SAVEHEADER) + sizeof(GATEWAY_SAVE)*numGateways;
	// Add on the size of the zone data header. For backwards compatibility.
	*pFileSize += sizeof(ZONEMAP_SAVEHEADER);

	*ppFileData = (char*)malloc(*pFileSize);
	if (*ppFileData == NULL)
	{
		debug( LOG_FATAL, "Out of memory" );
		abort();
		return false;
	}

	/* Put the file header on the file */
	psHeader = (MAP_SAVEHEADER *)*ppFileData;
	psHeader->aFileType[0] = 'm';
	psHeader->aFileType[1] = 'a';
	psHeader->aFileType[2] = 'p';
	psHeader->aFileType[3] = ' ';
	psHeader->version = CURRENT_VERSION_NUM;
	psHeader->width = mapWidth;
	psHeader->height = mapHeight;

	/* MAP_SAVEHEADER */
	endian_udword(&psHeader->version);
	endian_udword(&psHeader->width);
	endian_udword(&psHeader->height);

	/* Put the map data into the buffer */
	psTileData = (MAP_SAVETILE *)(*ppFileData + SAVE_HEADER_SIZE);
	psTile = psMapTiles;
	for(i=0; i<mapWidth*mapHeight; i++)
	{
		psTileData->texture = psTile->texture;
		if (psTile->ground == waterGroundType)
		{
			psTileData->height = MIN(255.0f, psTile->height + (WATER_DEPTH - 2.0f * environGetData(i % mapWidth, i / mapWidth)) / (float)ELEVATION_SCALE);
		}
		else
		{
			psTileData->height = psTile->height;
		}

		/* MAP_SAVETILE */
		endian_uword(&psTileData->texture);

		psTileData = (MAP_SAVETILE *)((UBYTE *)psTileData + SAVE_TILE_SIZE);
		psTile ++;
	}

	// Put the gateway header.
	psGateHeader = (GATEWAY_SAVEHEADER*)psTileData;
	psGateHeader->version = 1;
	psGateHeader->numGateways = numGateways;

	/* GATEWAY_SAVEHEADER */
	endian_udword(&psGateHeader->version);
	endian_udword(&psGateHeader->numGateways);

	psGate = (GATEWAY_SAVE*)(psGateHeader+1);

	i=0;
	// Put the gateway data.
	for(psCurrGate = gwGetGateways(); psCurrGate; psCurrGate = psCurrGate->psNext)
	{
		psGate->x0 = psCurrGate->x1;
		psGate->y0 = psCurrGate->y1;
		psGate->x1 = psCurrGate->x2;
		psGate->y1 = psCurrGate->y2;
		psGate++;
		i++;
	}

	// Put the zone header.
	psZoneHeader = (ZONEMAP_SAVEHEADER*)psGate;
	psZoneHeader->version = 3;
	psZoneHeader->numZones = 0;
	psZoneHeader->numEquivZones = 0;

	/* ZONEMAP_SAVEHEADER */
	endian_uword(&psZoneHeader->version);
	endian_uword(&psZoneHeader->numZones);
	endian_uword(&psZoneHeader->numEquivZones);
	endian_uword(&psZoneHeader->pad);

	return true;
}

/* Shutdown the map module */
BOOL mapShutdown(void)
{
	if (psMapTiles)
	{
		free(psMapTiles);
	}
	if (mapDecals)
	{
		free(mapDecals);
	}
	if (psGroundTypes)
	{
		free(psGroundTypes);
	}
	if (map)
	{
		free(map);
	}
	if (Tile_names)
	{
		free(Tile_names);
	}

	map = NULL;
	psGroundTypes = NULL;
	mapDecals = NULL;
	psMapTiles = NULL;
	mapWidth = mapHeight = 0;
	numTile_names = 0;
	Tile_names = NULL;
	return true;
}

/// The max height of the terrain and water at the specified world coordinates
extern int32_t map_Height(int x, int y)
{
	int tileX, tileY;
	int i, j;
	float height[2][2], center;
	float onTileX, onTileY;
	float left, right, middle;
	float onBottom, result;
	float towardsCenter, towardsRight;

	// Clamp x and y values to actual ones
	// Give one tile worth of leeway before asserting, for units/transporters coming in from off-map.
	ASSERT(x >= -TILE_UNITS, "map_Height: x value is too small (%d,%d) in %dx%d",map_coord(x),map_coord(y),mapWidth,mapHeight);
	ASSERT(y >= -TILE_UNITS, "map_Height: y value is too small (%d,%d) in %dx%d",map_coord(x),map_coord(y),mapWidth,mapHeight);
	x = (x < 0 ? 0 : x);
	y = (y < 0 ? 0 : y);
	ASSERT(x < world_coord(mapWidth)+TILE_UNITS, "map_Height: x value is too big (%d,%d) in %dx%d",map_coord(x),map_coord(y),mapWidth,mapHeight);
	ASSERT(y < world_coord(mapHeight)+TILE_UNITS, "map_Height: y value is too big (%d,%d) in %dx%d",map_coord(x),map_coord(y),mapWidth,mapHeight);
	x = (x >= world_coord(mapWidth) ? world_coord(mapWidth) - 1 : x);
	y = (y >= world_coord(mapHeight) ? world_coord(mapHeight) - 1 : y);

	// on which tile are these coords?
	tileX = map_coord(x);
	tileY = map_coord(y);

	// where on the tile? (scale to (0,1))
	onTileX = (x - world_coord(tileX))/(float)world_coord(1);
	onTileY = (y - world_coord(tileY))/(float)world_coord(1);

	// get the height for the corners and center
	center = 0;
	for (i = 0; i < 2; i++)
	{
		for (j = 0; j < 2; j++)
		{
			height[i][j] = map_TileHeightSurface(tileX+i, tileY+j);
			center += height[i][j];
		}
	}
	center /= 4;

	// we have:
	//   y ->
	// x 0,0  A  0,1
	// |
	// V D  center B
	//
	//   1,0  C  1,1

	// get heights for left and right corners and the distances
	if (onTileY > onTileX)
	{
		if (onTileY < 1 - onTileX)
		{
			// A
			right = height[0][0];
			left  = height[0][1];
			towardsCenter = onTileX;
			towardsRight  = 1 - onTileY;
		}
		else
		{
			// B
			right = height[0][1];
			left  = height[1][1];
			towardsCenter = 1 - onTileY;
			towardsRight  = 1 - onTileX;
		}
	}
	else
	{
		if (onTileX > 1 - onTileY)
		{
			// C
			right = height[1][1];
			left  = height[1][0];
			towardsCenter = 1 - onTileX;
			towardsRight  = onTileY;
		}
		else
		{
			// D
			right = height[1][0];
			left  = height[0][0];
			towardsCenter = onTileY;
			towardsRight  = onTileX;
		}
	}
	ASSERT(towardsCenter <= 0.5, "towardsCenter is too high");

	// now we have:
	//         center
	//    left   m    right

	middle = (left + right)/2;
	onBottom = left * (1 - towardsRight) + right * towardsRight;
	result = onBottom + (center - middle) * towardsCenter * 2;

	return (SDWORD)(result+0.5f);
}

/* returns true if object is above ground */
extern BOOL mapObjIsAboveGround( BASE_OBJECT *psObj )
{
	// min is used to make sure we don't go over array bounds!
	// TODO Using the corner of the map instead doesn't make sense. Fix this...
	SDWORD	iZ,
			tileX = map_coord(psObj->pos.x),
			tileY = map_coord(psObj->pos.y),
			tileYOffset1 = (tileY * mapWidth),
			tileYOffset2 = ((tileY+1) * mapWidth),
			h1 = psMapTiles[MIN(mapWidth * mapHeight - 1, tileYOffset1 + tileX)    ].height,
			h2 = psMapTiles[MIN(mapWidth * mapHeight - 1, tileYOffset1 + tileX + 1)].height,
			h3 = psMapTiles[MIN(mapWidth * mapHeight - 1, tileYOffset2 + tileX)    ].height,
			h4 = psMapTiles[MIN(mapWidth * mapHeight - 1, tileYOffset2 + tileX + 1)].height;

	/* trivial test above */
	if ( (psObj->pos.z > h1) && (psObj->pos.z > h2) &&
		 (psObj->pos.z > h3) && (psObj->pos.z > h4)    )
	{
		return true;
	}

	/* trivial test below */
	if ( (psObj->pos.z <= h1) && (psObj->pos.z <= h2) &&
		 (psObj->pos.z <= h3) && (psObj->pos.z <= h4)    )
	{
		return false;
	}

	/* exhaustive test */
	iZ = map_Height( psObj->pos.x, psObj->pos.y );
	if ( psObj->pos.z > iZ )
	{
		return true;
	}
	else
	{
		return false;
	}
}

/* returns the max and min height of a tile by looking at the four corners
   in tile coords */
void getTileMaxMin(UDWORD x, UDWORD y, UDWORD *pMax, UDWORD *pMin)
{
	UDWORD	height, i, j;

	*pMin = TILE_MAX_HEIGHT;
	*pMax = TILE_MIN_HEIGHT;

	for (j=0; j < 2; j++)
	{
		for (i=0; i < 2; i++)
		{
			height = map_TileHeight(x+i, y+j);
			if (*pMin > height)
			{
				*pMin = height;
			}
			if (*pMax < height)
			{
				*pMax = height;
			}
		}
	}
}

UDWORD GetWidthOfMap(void)
{
	return mapWidth;
}



UDWORD GetHeightOfMap(void)
{
	return mapHeight;
}


// -----------------------------------------------------------------------------------
/* This will save out the visibility data */
bool writeVisibilityData(const char* fileName)
{
	unsigned int i;
	VIS_SAVEHEADER fileHeader;

	PHYSFS_file* fileHandle = openSaveFile(fileName);
	if (!fileHandle)
	{
		return false;
	}

	fileHeader.aFileType[0] = 'v';
	fileHeader.aFileType[1] = 'i';
	fileHeader.aFileType[2] = 's';
	fileHeader.aFileType[3] = 'd';

	fileHeader.version = CURRENT_VERSION_NUM;

	// Write out the current file header
	if (PHYSFS_write(fileHandle, fileHeader.aFileType, sizeof(fileHeader.aFileType), 1) != 1
	 || !PHYSFS_writeUBE32(fileHandle, fileHeader.version))
	{
		debug(LOG_ERROR, "writeVisibilityData: could not write header to %s; PHYSFS error: %s", fileName, PHYSFS_getLastError());
		PHYSFS_close(fileHandle);
		return false;
	}

	for (i = 0; i < mapWidth * mapHeight; ++i)
	{
		if (!PHYSFS_writeUBE8(fileHandle, psMapTiles[i].tileExploredBits))
		{
			debug(LOG_ERROR, "writeVisibilityData: could not write to %s; PHYSFS error: %s", fileName, PHYSFS_getLastError());
			PHYSFS_close(fileHandle);
			return false;
		}
	}

	// Everything is just fine!
	PHYSFS_close(fileHandle);
	return true;
}

// -----------------------------------------------------------------------------------
/* This will read in the visibility data */
bool readVisibilityData(const char* fileName)
{
	VIS_SAVEHEADER fileHeader;
	unsigned int expectedFileSize, fileSize;
	unsigned int i;

	PHYSFS_file* fileHandle = openLoadFile(fileName, false);
	if (!fileHandle)
	{
		// Failure to open the file is no failure to read it
		return true;
	}

	// Read the header from the file
	if (PHYSFS_read(fileHandle, fileHeader.aFileType, sizeof(fileHeader.aFileType), 1) != 1
	 || !PHYSFS_readUBE32(fileHandle, &fileHeader.version))
	{
		debug(LOG_ERROR, "readVisibilityData: error while reading header from file: %s", PHYSFS_getLastError());
		PHYSFS_close(fileHandle);
		return false;
	}

	// Check the header to see if we've been given a file of the right type
	if (fileHeader.aFileType[0] != 'v'
	 || fileHeader.aFileType[1] != 'i'
	 || fileHeader.aFileType[2] != 's'
	 || fileHeader.aFileType[3] != 'd')
	{
		debug(LOG_ERROR, "readVisibilityData: Weird file type found? Has header letters - '%c' '%c' '%c' '%c' (should be 'v' 'i' 's' 'd')",
		      fileHeader.aFileType[0],
		      fileHeader.aFileType[1],
		      fileHeader.aFileType[2],
		      fileHeader.aFileType[3]);

		PHYSFS_close(fileHandle);
		return false;
	}

	// Validate the filesize
	expectedFileSize = sizeof(fileHeader.aFileType) + sizeof(fileHeader.version) + mapWidth * mapHeight * sizeof(uint8_t);
	fileSize = PHYSFS_fileLength(fileHandle);
	if (fileSize != expectedFileSize)
	{
		PHYSFS_close(fileHandle);
		ASSERT(!"readVisibilityData: unexpected filesize", "readVisibilityData: unexpected filesize; should be %u, but is %u", expectedFileSize, fileSize);

		return false;
	}

	// For every tile...
	for(i=0; i<mapWidth*mapHeight; i++)
	{
		/* Get the visibility data */
		if (!PHYSFS_readUBE8(fileHandle, &psMapTiles[i].tileExploredBits))
		{
			debug(LOG_ERROR, "readVisibilityData: could not read from %s; PHYSFS error: %s", fileName, PHYSFS_getLastError());
			PHYSFS_close(fileHandle);
			return false;
		}
	}

	// Close the file
	PHYSFS_close(fileHandle);

	/* Hopefully everything's just fine by now */
	return true;
}

static void astarTest(const char *name, int x1, int y1, int x2, int y2)
{
	int		asret, i;
	MOVE_CONTROL	route;
	int		x = world_coord(x1);
	int		y = world_coord(y1);
	int		endx = world_coord(x2);
	int		endy = world_coord(y2);
	clock_t		stop;
	clock_t		start = clock();
	bool		retval;

	scriptInit();
	retval = levLoadData(name, NULL, 0);
	ASSERT(retval, "Could not load %s", name);
	route.asPath = NULL;
	for (i = 0; i < 100; i++)
	{
		PATHJOB job;

		route.numPoints = 0;
		job.origX = x;
		job.origY = y;
		job.destX = endx;
		job.destY = endy;
		job.propulsion = PROPULSION_TYPE_WHEELED;
		job.droidID = 1;
		job.owner = 0;
		asret = fpathAStarRoute(&route, &job);
		free(route.asPath);
		route.asPath = NULL;
	}
	stop = clock();
	fprintf(stdout, "\t\tA* timing %s: %.02f (%d nodes)\n", name,
	        (double)(stop - start) / (double)CLOCKS_PER_SEC, route.numPoints);
	start = clock();
	fpathTest(x, y, endx, endy);
	stop = clock();
	fprintf(stdout, "\t\tfPath timing %s: %.02f (%d nodes)\n", name,
	        (double)(stop - start) / (double)CLOCKS_PER_SEC, route.numPoints);
	retval = levReleaseAll();
	assert(retval);
}

void mapTest()
{
	fprintf(stdout, "\tMap self-test...\n");

	astarTest("Sk-BeggarsKanyon-T1", 16, 5, 119, 182);
	astarTest("Sk-MizaMaze-T3", 5, 5, 108, 112);

	fprintf(stdout, "\tMap self-test: PASSED\n");
}

// Convert a direction into an offset.
// dir 0 => x = 0, y = -1
#define NUM_DIR		8
static const Vector2i aDirOffset[NUM_DIR] =
{
        { 0, 1},
        {-1, 1},
        {-1, 0},
        {-1,-1},
        { 0,-1},
        { 1,-1},
        { 1, 0},
        { 1, 1},
};

// Flood fill a "continent". Note that we reuse x, y inside this function.
static void mapFloodFill(int x, int y, int continent, PROPULSION_TYPE propulsion)
{
	struct ffnode *open = NULL;
	int i;

	do
	{
		MAPTILE *currTile = mapTile(x, y);

		// Add accessible neighbouring tiles to the open list
		for (i = 0; i < NUM_DIR; i++)
		{
			// rely on the fact that all border tiles are inaccessible to avoid checking explicitly
			Vector2i npos = { x + aDirOffset[i].x, y + aDirOffset[i].y };
			MAPTILE *psTile;
			bool limitedTile = (propulsion == PROPULSION_TYPE_PROPELLOR || propulsion == PROPULSION_TYPE_WHEELED);

			if (!tileOnMap(npos.x, npos.y) || (npos.x == x && npos.y == y))
			{
				continue;
			}
			psTile = mapTile(npos.x, npos.y);

			if (!fpathBlockingTile(npos.x, npos.y, propulsion) && ((limitedTile && psTile->limitedContinent == 0) || (!limitedTile && psTile->hoverContinent == 0)))
			{
				struct ffnode *node = malloc(sizeof(*node));

				node->next = open;	// add to beginning of open list
				node->x = npos.x;
				node->y = npos.y;
				open = node;
			}
		}

		// Set continent value
		if (propulsion == PROPULSION_TYPE_PROPELLOR || propulsion == PROPULSION_TYPE_WHEELED)
		{
			currTile->limitedContinent = continent;
		}
		else
		{
			currTile->hoverContinent = continent;
		}

		// Pop the first open node off the list for the next iteration
		if (open)
		{
			struct ffnode *tmp = open;

			x = open->x;
			y = open->y;
			open = open->next;
			free(tmp);
		}
	} while (open);
}

void mapFloodFillContinents()
{
	int x, y, limitedContinents = 0, hoverContinents = 0;

	/* Clear continents */
	for (x = 0; x < mapWidth; x++)
	{
		for (y = 0; y < mapHeight; y++)
		{
			MAPTILE *psTile = mapTile(x, y);

			psTile->limitedContinent = 0;
			psTile->hoverContinent = 0;
		}
	}

	/* Iterate over the whole map, looking for unset continents */
	for (x = 0; x < mapWidth; x++)
	{
		for (y = 0; y < mapHeight; y++)
		{
			MAPTILE *psTile = mapTile(x, y);

			if (psTile->limitedContinent == 0 && !fpathBlockingTile(x, y, PROPULSION_TYPE_WHEELED))
			{
				mapFloodFill(x, y, 1 + limitedContinents++, PROPULSION_TYPE_WHEELED);
			}
			else if (psTile->limitedContinent == 0 && !fpathBlockingTile(x, y, PROPULSION_TYPE_PROPELLOR))
			{
				mapFloodFill(x, y, 1 + limitedContinents++, PROPULSION_TYPE_PROPELLOR);
			}
		}
	}
	debug(LOG_MAP, "Found %d limited continents", (int)limitedContinents);
	for (x = 0; x < mapWidth; x++)
	{
		for (y = 0; y < mapHeight; y++)
		{
			MAPTILE *psTile = mapTile(x, y);

			if (psTile->hoverContinent == 0 && !fpathBlockingTile(x, y, PROPULSION_TYPE_HOVER))
			{
				mapFloodFill(x, y, 1 + hoverContinents++, PROPULSION_TYPE_HOVER);
			}
		}
	}
	debug(LOG_MAP, "Found %d hover continents", (int)hoverContinents);
}

void tileSetFire(int32_t x, int32_t y, uint32_t duration)
{
	const int posX = map_coord(x);
	const int posY = map_coord(y);
	MAPTILE *const tile = mapTile(posX, posY);

	uint16_t currentTime =  gameTime             / GAME_TICKS_PER_UPDATE;
	uint16_t fireEndTime = (gameTime + duration) / GAME_TICKS_PER_UPDATE;
	if (currentTime == fireEndTime)
	{
		return;  // Fire already ended.
	}
	if ((tile->tileInfoBits & BITS_ON_FIRE) != 0 && (uint16_t)(fireEndTime - currentTime) < (uint16_t)(tile->fireEndTime - currentTime))
	{
		return;  // Tile already on fire, and that fire lasts longer.
	}

	// Burn, tile, burn!
	tile->tileInfoBits |= BITS_ON_FIRE;
	tile->fireEndTime = fireEndTime;

	syncDebug("Fire tile{%d, %d} dur%u end%d", posX, posY, duration, fireEndTime);
}

/** Check if tile contained within the given world coordinates is burning. */
bool fireOnLocation(unsigned int x, unsigned int y)
{
	const int posX = map_coord(x);
	const int posY = map_coord(y);
	const MAPTILE *psTile = mapTile(posX, posY);

	ASSERT(psTile, "Checking fire on tile outside the map (%d, %d)", posX, posY);
	return psTile != NULL && TileIsBurning(psTile);
}

static void dangerFloodFill(int player)
{
	struct ffnode *open = NULL;
	int i;
	Vector2i pos = getPlayerStartPosition(player);

	pos.x = map_coord(pos.x);
	pos.y = map_coord(pos.y);

	do
	{
		MAPTILE *currTile = mapTile(pos.x, pos.y);

		// Add accessible neighbouring tiles to the open list
		for (i = 0; i < NUM_DIR; i++)
		{
			Vector2i npos = { pos.x + aDirOffset[i].x, pos.y + aDirOffset[i].y };
			MAPTILE *psTile;

			if (!tileOnMap(npos.x, npos.y) || (npos.x == pos.x && npos.y == pos.y))
			{
				continue;
			}
			psTile = mapTile(npos.x, npos.y);

			if (!(psTile->threatBits & (1 << player)) && (psTile->dangerBits & (1 <<player)) && !fpathBlockingTile(npos.x, npos.y, PROPULSION_TYPE_WHEELED))
			{
				struct ffnode *node = malloc(sizeof(*node));

				node->next = open;	// add to beginning of open list
				node->x = npos.x;
				node->y = npos.y;
				open = node;
			}
		}

		// Clear danger
		currTile->dangerBits &= ~(1 << player);

		// Pop the first open node off the list for the next iteration
		if (open)
		{
			struct ffnode *tmp = open;

			pos.x = open->x;
			pos.y = open->y;
			open = open->next;
			free(tmp);
		}
	} while (open);
}

static inline void threatUpdateTarget(int player, BASE_OBJECT *psObj, bool ground, bool air)
{
	int i;

	if (psObj->visible[player] || psObj->born == 2)
	{
		for (i = 0; i < psObj->numWatchedTiles; i++)
		{
			const TILEPOS pos = psObj->watchedTiles[i];
			MAPTILE *psTile = mapTile(pos.x, pos.y);
			if (ground)
			{
				psTile->threatBits |= (1 << player);			// set ground threat for this tile
			}
			if (air)
			{
				psTile->aaThreatBits |= (1 << player);			// set air threat for this tile
			}
		}
	}
}

static void threatUpdate(int player)
{
	MAPTILE *psTile = psMapTiles;
	int i, weapon;

	// Step 1: Clear our threat bits
	for (i = 0; i < mapWidth * mapHeight; i++, psTile++)
	{
		psTile->threatBits &= ~(1 << player);
	}

	// Step 2: Set threat bits
	for (i = 0; i < MAX_PLAYERS; i++)
	{
		DROID *psDroid;
		STRUCTURE *psStruct;

		if (aiCheckAlliances(player, i))
		{
			// No need to iterate friendly objects
			continue;
		}

		for (psDroid = apsDroidLists[i]; psDroid; psDroid = psDroid->psNext)
		{
			UBYTE mode = 0;

			if (psDroid->droidType == DROID_CONSTRUCT || psDroid->droidType == DROID_CYBORG_CONSTRUCT
			    || psDroid->droidType == DROID_REPAIR || psDroid->droidType == DROID_CYBORG_REPAIR)
			{
				continue;	// hack that really should not be needed, but is -- trucks can SHOOT_ON_GROUND...!
			}
			for (weapon = 0; weapon < psDroid->numWeaps; weapon++)
			{
				mode |= asWeaponStats[psDroid->asWeaps[weapon].nStat].surfaceToAir;
			}
			if (psDroid->droidType == DROID_SENSOR)	// special treatment for sensor turrets, no multiweapon support
			{
				mode |= SHOOT_ON_GROUND;		// assume it only shoots at ground targets for now
			}
			if (mode > 0)
			{
				threatUpdateTarget(player, (BASE_OBJECT *)psDroid, mode & SHOOT_ON_GROUND, mode & SHOOT_IN_AIR);
			}
		}

		for (psStruct = apsStructLists[i]; psStruct; psStruct = psStruct->psNext)
		{
			UBYTE mode = 0;

			for (weapon = 0; weapon < psStruct->numWeaps; weapon++)
			{
				mode |= asWeaponStats[psStruct->asWeaps[weapon].nStat].surfaceToAir;
			}
			if (psStruct->pStructureType->pSensor && psStruct->pStructureType->pSensor->location == LOC_TURRET)	// special treatment for sensor turrets
			{
				mode |= SHOOT_ON_GROUND;		// assume it only shoots at ground targets for now
			}
			if (mode > 0)
			{
				threatUpdateTarget(player, (BASE_OBJECT *)psStruct, mode & SHOOT_ON_GROUND, mode & SHOOT_IN_AIR);
			}
		}
	}
}

static void dangerUpdate(int player)
{
	MAPTILE *psTile = psMapTiles;
	int i;

	// Set our danger bits
	for (i = 0; i < mapWidth * mapHeight; i++, psTile++)
	{
		psTile->dangerBits |= (1 << player);
	}
	if (game.type == SKIRMISH)
	{
		dangerFloodFill(player);
	}
	// else everything is equally dangerous
}

void mapInit()
{
	int player;

	for (player = 0; player < MAX_PLAYERS; player++)
	{
		threatUpdate(player);
		dangerUpdate(player);
	}
}

void mapUpdate()
{
	uint16_t currentTime = gameTime / GAME_TICKS_PER_UPDATE;
	int updatedBase = currentTime % (game.maxPlayers * 2);
	int updatedPlayer = updatedBase / 2;
	bool updateThreat = updatedBase % 2;
	int posX, posY;

	for (posY = 0; posY < mapHeight; ++posY)
		for (posX = 0; posX < mapWidth; ++posX)
	{
		MAPTILE *const tile = mapTile(posX, posY);

		if ((tile->tileInfoBits & BITS_ON_FIRE) != 0 && tile->fireEndTime == currentTime)
		{
			// Extinguish, tile, extinguish!
			tile->tileInfoBits &= ~BITS_ON_FIRE;

			syncDebug("Extinguished tile{%d, %d}", posX, posY);
		}
	}

	// We do only a bit of danger map update each logical frame to avoid overheating the CPU
	if (updateThreat)
	{
		threatUpdate(updatedPlayer);
	}
	else
	{
		dangerUpdate(updatedPlayer);
	}
}
