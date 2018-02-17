/**
* This file is part of Hercules.
* http://herc.ws - http://github.com/HerculesWS/Hercules
*
* Copyright (C) 2013-2015  Hercules Dev Team
*
* Hercules is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * Mapcache Plugin
 * This Plugin is made to handle the creation and update the new format of mapcache
 * it also handles the convertion from the old to the new mapcache format
 **/

#include "common/hercules.h" /* Should always be the first Hercules file included! (if you don't make it first, you won't be able to use interfaces) */

#include "common/memmgr.h"
#include "common/md5calc.h"
#include "common/nullpo.h"
#include "common/grfio.h"
#include "common/utils.h"
#include "map/map.h"

#include "common/HPMDataCheck.h" /* should always be the last Hercules file included! (if you don't make it last, it'll intentionally break compile time) */

#include <stdio.h>
#include <string.h>

HPExport struct hplugin_info pinfo = {
	"Mapcache",      ///< Plugin name
	SERVER_TYPE_MAP, ///< Which server types this plugin works with?
	"1.0.0",         ///< Plugin version
	HPM_VERSION,     ///< HPM Version (don't change, macro is automatically updated)
};

/**
 * Yes.. old mapcache was never packed, and we loaded and wrote a compiler paded structs
 * DON'T BLAME IF SOMETHING EXPLODED [hemagx]
 **/
// This is the main header found at the very beginning of the map cache
struct old_mapcache_main_header {
	uint32 file_size;
	uint16 map_count;
};

// This is the header appended before every compressed map cells info in the map cache
struct old_mapcache_map_info {
	char name[MAP_NAME_LENGTH];
	int16 xs;
	int16 ys;
	int32 len;
};

/**
 *
 */

#define NO_WATER 1000000

VECTOR_DECL(char *) maplist;
bool needs_grfio;


/**
 * code from utlis.c until it's interfaced
 **/

#ifdef WIN32
#	ifndef F_OK
#		define F_OK   0x0
#	endif  /* F_OK */
#else
#	include <unistd.h>
#endif


// Reads an uint32 in little-endian from the buffer
uint32 GetULong(const unsigned char* buf)
{
	return (((uint32)(buf[0])))
		| (((uint32)(buf[1])) << 0x08)
		| (((uint32)(buf[2])) << 0x10)
		| (((uint32)(buf[3])) << 0x18);
}

// Reads a float (32 bits) from the buffer
float GetFloat(const unsigned char* buf)
{
	uint32 val = GetULong(buf);
	return *((float*)(void*)&val);
}

bool write_mapcache(const uint8 *buf, int32 buf_len, bool is_compressed, const char *mapname, int16 xs, int16 ys)
{
	struct map_cache_header header = { 0 };
	char file_path[255];
	int mapname_len;
	unsigned long compressed_buf_len;
	uint8 *compressed_buf = NULL;
	FILE *new_mapcache_fp;

	nullpo_retr(false, buf);
	nullpo_retr(false, mapname);

	mapname_len = (int)strlen(mapname);

	if (mapname_len > MAP_NAME_LENGTH || mapname_len < 1) {
		ShowError("write_mapcache: A map with invalid name length has beed passed '%s' size (%d)\n", mapname, mapname_len);
		return false;
	}

	if ((xs < 0 || ys < 0)) {
		ShowError("write_mapcache: '%s' given with invalid coords xs = %d, ys = %d\n", mapname, xs, ys);
		return false;
	}

	if (((int)xs * ys) > MAX_MAP_SIZE) {
		ShowError("write_mapcache: map '%s' exceeded MAX_MAP_SIZE of %d\n", mapname, MAX_MAP_SIZE);
		return false;
	}



	snprintf(file_path, sizeof(file_path), "%s%s%s.%s", "maps/", DBPATH, mapname, "mcache");
	new_mapcache_fp = fopen(file_path, "wb");

	if (new_mapcache_fp == NULL) {
		ShowWarning("Could not open file '%s', map cache creating failed.\n", file_path);
		return false;
	}

	header.version = 0x1;
	header.xs = xs;
	header.ys = ys;

	if (is_compressed == false) {
		compressed_buf_len = buf_len * 2; //Creating big enough buffer to ensure ability to hold compressed data
		CREATE(compressed_buf, uint8, compressed_buf_len);
		grfio->encode_zip(compressed_buf, &compressed_buf_len, buf, buf_len);

		header.len = (int)compressed_buf_len;
		md5->binary(compressed_buf, (int)compressed_buf_len, header.md5_checksum);
	} else {
		header.len = buf_len;
		md5->binary(buf, buf_len, header.md5_checksum);
	}


	fwrite(&header, sizeof(header), 1, new_mapcache_fp);
	if (is_compressed == false)
		fwrite(compressed_buf, compressed_buf_len, 1, new_mapcache_fp);
	else
		fwrite(buf, buf_len, 1, new_mapcache_fp);

	fclose(new_mapcache_fp);
	if (compressed_buf != NULL)
		aFree(compressed_buf);

	return true;
}

bool convert_old_mapcache(void)
{
	const char *path = "db/"DBPATH"map_cache.dat";
	FILE *mapcache_fp = fopen(path, "rb");
	struct old_mapcache_main_header header = { 0 };
	uint8 *p, *cursor;
	uint32 file_size;
	int i;

	if (mapcache_fp == NULL) {
		ShowError("Could not open mapcache file in the following path '%s' \n", path);
		return false;
	}

	if (fread(&header, sizeof(header), 1, mapcache_fp) != 1) {
		ShowError("Failed to read mapcache header \n");
		fclose(mapcache_fp);
		return false;
	}

	fseek(mapcache_fp, 0, SEEK_END);
	file_size = (int)ftell(mapcache_fp);
	fseek(mapcache_fp, 0, SEEK_SET);

	if (file_size != header.file_size) {
		ShowError("File size in mapcache header doesn't match actual mapcache file size \n");
		fclose(mapcache_fp);
		return false;
	}

	CREATE(p, uint8, header.file_size);
	cursor = p + sizeof(header);

	if (fread(p, header.file_size, 1, mapcache_fp) != 1) {
		ShowError("Could not load mapcache file into memory, fread failed.\n");
		aFree(p);
		fclose(mapcache_fp);
		return false;
	}

	for (i = 0; i < header.map_count; ++i) {
		struct old_mapcache_map_info *info = (struct old_mapcache_map_info *)cursor;

		ShowStatus("Creating mapcache: %s"CL_CLL"\n", info->name);

		if (write_mapcache((uint8 *)info + sizeof(*info), info->len, true, info->name, info->xs, info->ys) == false) {
			ShowError("failed To convert map '%s'\n", info->name);
		}

		cursor += sizeof(*info) + info->len;
	}

	aFree(p);
	fclose(mapcache_fp);
	return true;
}

bool mapcache_read_maplist(const char *filepath)
{
	char line[4096] = { 0 };
	FILE *fp;

	nullpo_retr(false, filepath);

	fp = fopen(filepath, "r");

	if (fp == NULL)
		return false;

	while (fgets(line, sizeof(line), fp)) {
		char map_name[MAP_NAME_LENGTH];
		if (line[0] == '/' && line[1] == '/')
			continue;

		if (sscanf(line, "%11s", map_name) == 1) {
			VECTOR_ENSURE(maplist, 1, 1);
			VECTOR_PUSH(maplist, aStrdup(map_name));
		}
	}

	ShowStatus("%d map loaded from map_index.txt\n", VECTOR_LENGTH(maplist));
	fclose(fp);
	return true;
}

bool mapcache_cache_map(const char *mapname)
{
	char filepath[255] = { 0 };
	uint8 *gat, *rsw, *gat_cursor;
	uint8 *cells;
	int water_height, map_size, xy;
	int16 xs, ys;

	nullpo_retr(false, mapname);

	snprintf(filepath, sizeof(filepath), "data\\%s.gat", mapname);
	gat = grfio_read(filepath);

	if (gat == NULL) {
		ShowError("mapcache_cache_map: Could not read %s, aborting caching map %s\n", filepath, mapname);
		return false;
	}

	snprintf(filepath, sizeof(filepath), "data\\%s.rsw", mapname);

	rsw = grfio_read(filepath);

	if (rsw == NULL) {
		water_height = NO_WATER;
	} else {
		water_height = (int)GetFloat(rsw + 166);
		aFree(rsw);
	}

	xs = (int16)GetULong(gat + 6);
	ys = (int16)GetULong(gat + 10);

	if (xs <= 0 || ys <= 0) {
		ShowError("mapcache_cache_map: map '%s' doesn't have valid size xs = %d, ys = %d\n", mapname, xs, ys);
		aFree(gat);
		return false;
	}

	map_size = xs * ys;
	CREATE(cells, uint8, map_size);

	gat_cursor = gat;
	for (xy = 0; xy < map_size; ++xy) {
		float height = GetFloat(gat_cursor + 14);
		uint32 type = GetULong(gat_cursor + 30);
		gat_cursor += 20;

		if (type == 0 && water_height != NO_WATER && height > water_height)
			type = 3;

		cells[xy] = (uint8)type;
	}

	write_mapcache(cells, map_size, false, mapname, xs, ys);

	aFree(gat);
	aFree(cells);
	return true;
}

bool mapcache_rebuild(void)
{
	int i;
	char file_path[255];

	if (mapcache_read_maplist("db/map_index.txt") == false) {
		ShowError("mapcache_rebuild: Could not read maplist, aborting\n");
		return false;
	}

	for (i = 0; i < VECTOR_LENGTH(maplist); ++i) {
		snprintf(file_path, sizeof(file_path), "%s%s%s.%s", "maps/", DBPATH, VECTOR_INDEX(maplist, i), "mcache");
		if (access(file_path, F_OK) == 0 && remove(file_path) != 0) {
			ShowWarning("mapcache_rebuild: Could not remove file '%s' \n", file_path);
		}
	}

	for (i = 0; i < VECTOR_LENGTH(maplist); ++i) {
		ShowStatus("Creating mapcache: %s"CL_CLL"\r", VECTOR_INDEX(maplist, i));
		mapcache_cache_map(VECTOR_INDEX(maplist, i));
	}

	return true;
}

CMDLINEARG(convertmapcache)
{
	map->minimal = true;
	return convert_old_mapcache();
}

CMDLINEARG(rebuild)
{
	needs_grfio = true;
	grfio->init("conf/grf-files.txt");
	map->minimal = true;
	return mapcache_rebuild();
}

CMDLINEARG(cachemap)
{
	needs_grfio = true;
	grfio->init("conf/grf-files.txt");
	map->minimal = true;
	return mapcache_cache_map(params);
}

HPExport void server_preinit(void)
{
	addArg("--convert-old-mapcache", false, convertmapcache,
			"Converts an old db/"DBPATH"map_cache.dat file to the new format.");
	addArg("--rebuild-mapcache", false, rebuild,
			"Rebuilds the entire mapcache folder (maps/"DBPATH"), using db/map_index.txt as index.");
	addArg("--map", true, cachemap,
			"Rebuilds an individual map's cache into maps/"DBPATH" (usage: --map <map_name_without_extension>).");

	needs_grfio = false;
	VECTOR_INIT(maplist);
}

HPExport void plugin_final(void)
{
	VECTOR_CLEAR(maplist);
	if (needs_grfio)
		grfio->final();
}