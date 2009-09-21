#ifdef __cplusplus
extern "C" {
#endif

#include <sgl.h>
#include <sl_def.h>
#include <sega_cdc.h>
#include <sega_gfs.h>
#include <sega_mem.h>

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

#include "gfs_wrap.h"

#include "saturn_print.h"

static char current_path[15][16];
static Uint32 current_cached = 0;
static Uint8 cache[210000];

// Used for initialization and such
GfsDirTbl gfsDirTbl;
GfsDirName gfsDirName[DIR_MAX];
Uint32 gfsLibWork[GFS_WORK_SIZE(OPEN_MAX)/sizeof(Uint32)];     
Sint32 gfsDirN;

Uint8 dir_depth = 0;

Uint32 hashString(const char *string) {
        char c, *f ;
        Uint32 n ;

        if (!string)
                return 0 ;

        n = 0 ;
        f = (char *)string ;

        while ((c = *f) != 0) {
                n = (n << 7) + (n << 1) + n + c ;
                f++ ;
        }

        return n ;
}

void back_to_root() {
	while(dir_depth) {
		GFS_LoadDir(1, &gfsDirTbl); // to the parent
		GFS_SetDir(&gfsDirTbl);

		memset(current_path[dir_depth], 0, 16);
		dir_depth--;
	}
}

Sint32 crawl_dir(char *token) {
	Sint32 ret;
	Sint32 id;

	if(strcmp("..", token) == 0) { // parent
		GFS_LoadDir(1, &gfsDirTbl);
		ret = GFS_SetDir(&gfsDirTbl);
		if(dir_depth > 0) { 
			memset(current_path[dir_depth], 0, 16);
			dir_depth--;
		}
	} else if (strcmp(".", token) == 0) {
		ret = 0; // just do nothing in this case
	} else { // normal entry
		id = GFS_NameToId((Sint8*)token);
		if(id < 0) return -1;

		GFS_LoadDir(id, &gfsDirTbl);
		ret = GFS_SetDir(&gfsDirTbl);
		if (ret < 0) return ret;

		dir_depth++;
		strcpy(current_path[dir_depth], token);
	}

	return ret;
}

void init_GFS() { //Initialize GFS system
    GFS_DIRTBL_TYPE(&gfsDirTbl) = GFS_DIR_NAME;
    GFS_DIRTBL_DIRNAME(&gfsDirTbl) = gfsDirName;
    GFS_DIRTBL_NDIR(&gfsDirTbl) = DIR_MAX;
    gfsDirN = GFS_Init(OPEN_MAX, gfsLibWork, &gfsDirTbl);

	Uint8 idx;
	for(idx = 0; idx < 15; idx++) {
		memset(current_path[idx], 0, 16);
	}
	current_path[idx][0] = '/';
	dir_depth = 0;

	//memset(cache, 0, 250000);
}

GFS_FILE *sat_fopen(const char *path) {
	if (path == NULL) return NULL; // nothing to do...
	Uint16 idx;
	GFS_FILE *fp = NULL;

	idx = 0;
	if (path[idx] == '\0') return NULL; // Malformed path

	Uint16 path_len = strlen(path);
	char *satpath = (char*)MEM_Malloc(path_len + 3);

	Uint16 tokens = 0; // How many "entries" we have in this path?

	strncpy(satpath, path, path_len + 1); // Prepare the string to work on

	if ((char*)strtok(satpath, "/") != NULL) tokens++; // it's not an empty path...
	else return NULL; // empty path... nonsense.
	while((char*)strtok(NULL, "/") != NULL) tokens++; // count remaining

	strncpy(satpath, path, path_len + 1);

	char *path_token = (char*)strtok(satpath, "/");
	Uint8 sameDir = 1;
	if((tokens - 1) == dir_depth)
		for (idx = 0; idx < (tokens - 1) && idx < 14; idx++) {
			if (strncasecmp(path_token, current_path[idx + 1], 15) != 0) {
				sameDir = 0;
				break;
			}
			path_token = (char*)strtok(NULL, "/"); // next entry
		}
	else
		sameDir = 0;

	if(!sameDir) {
		back_to_root();
	}

	strncpy(satpath, path, path_len + 3);
	for (idx = 0; idx < strlen(satpath); idx++)
		satpath[idx] = toupper(satpath[idx]);

	if(!strstr(satpath, "."))
		strcat(satpath, ".");

	/* We now have the number of entries: N-1 are dirs, the last is the file */

	// Now, crawl through the dir path
	Sint32 ret = 0;
	path_token = (char*)strtok(satpath, "/");
	for (idx = 0; idx < (tokens - 1); idx++) {
		if(!sameDir)
			ret = crawl_dir(path_token);

		if (ret < 0) break; // FIXME: (not sure checking for < 1 is ok) Argh, something is wrong in the path! 

		path_token = (char*)strtok(NULL, "/"); // next entry
	}

	GfsHn fid = NULL;
	// OPEN FILE
	if(ret >= 0) // Open only if we are sure we traversed the path correctly
		fid = GFS_Open(GFS_NameToId((Sint8*)path_token));

	if(fid != NULL) { // Opened!
		Sint32 fsize;
		
		GFS_SetTmode(fid, GFS_TMODE_SCU); // DMA transfer by SCU
		
		// Encapsulate the file data
		fp = (GFS_FILE*)MEM_Malloc(sizeof(GFS_FILE));
		if (fp == NULL) {MEM_Free(satpath); return NULL;}
		fp->fid = fid;
		fp->f_seek_pos = 0;
		GFS_GetFileInfo(fid, NULL, NULL, &fsize, NULL);
		fp->f_size = fsize;
		fp->file_hash = hashString(path);

		if(current_cached != fp->file_hash) {
			current_cached = fp->file_hash;
			
			Sint32 tot_sectors;
			GFS_Seek(fp->fid, 0, GFS_SEEK_SET);
			tot_sectors = GFS_ByteToSct(fp->fid, fp->f_size);
			memset((Uint8*)cache, 0, fp->f_size);

			// FIXME: Maybe i should find a better way than copy it two times...
			GFS_Fread(fp->fid, tot_sectors, (Uint8*)cache, fp->f_size);
		}
	}

	// Now... get back to the roots!
	//back_to_root();

	MEM_Free(satpath);
	return fp;
}

#define MATCH_SIZE 32
char *sat_match(const char *path) {
	if (path == NULL) return NULL; // nothing to do...
	Uint16 idx = 0;
	Uint8 found = 0;
	char *filename = (char*)MEM_Malloc(MATCH_SIZE);

	if (path[0] == '\0') return NULL; // Malformed path

	Uint16 path_len = strlen(path);
	char *satpath = (char*)MEM_Malloc(path_len + 1);

	strncpy(satpath, path, path_len + 1); // Prepare the string to work on

	Uint16 tokens = 0; // How many "entries" we have in this path?
	if ((char*)strtok(satpath, "/") != NULL) tokens++; // it's not an empty path...
	else return NULL; // empty path... nonsense.
	while((char*)strtok(NULL, "/") != NULL) tokens++; // count remaining

	strncpy(satpath, path, path_len + 1);
	char *path_token = (char*)strtok(satpath, "/");
	Uint8 sameDir = 1;
	if ((tokens - 1) == dir_depth)
		for (idx = 0; idx < (tokens - 1) && idx < 14; idx++) {
			if (strncasecmp(path_token, current_path[idx + 1], 15) != 0) {
				sameDir = 0;
				break;
			}
			path_token = (char*)strtok(NULL, "/"); // next entry
		}
	else
		sameDir = 0;

	if(!sameDir) {
		back_to_root();
	}

	strncpy(satpath, path, path_len + 1);
	for (idx = 0; idx < strlen(satpath); idx++)
		satpath[idx] = toupper(satpath[idx]);

	/* We now have the number of entries: N-1 are dirs, the last is the file */

	// Now, crawl through the dir path
	Sint32 ret = 0;
	path_token = (char*)strtok(satpath, "/");
	for (idx = 0; idx < (tokens - 1); idx++) {
		if(!sameDir)
			ret = crawl_dir(path_token);

		if (ret < 0) break; // FIXME: (not sure checking for < 1 is ok) Argh, something is wrong in the path! 

		path_token = (char*)strtok(NULL, "/"); // next entry
	}

	// ** SEARCH FOR THE FILE !!! **
	if(ret >= 0) { // Match only if we are sure we traversed the path correctly
		Uint32 dir_entry;
		char *fname = NULL;
	
		if(path_token[0] == '*') path_token++;

		for (dir_entry = 2; dir_entry < gfsDirTbl.ndir; dir_entry++) {
			fname = (char*)GFS_IdToName(dir_entry);
			if (fname == NULL) break;
			if(strstr(fname, path_token) != 0) {
				strncpy(filename, fname, MATCH_SIZE);
				found = 1;
				break;
			}
		}
	}

	// Now... get back to the roots!
	//back_to_root();

	MEM_Free(satpath);

	if (found) {
		return filename;
	} else {
		MEM_Free(filename);
		return NULL;
	}
}

int sat_fclose(GFS_FILE* fp) {
	GFS_Close(fp->fid);
	MEM_Free(fp);

	return 0; // always ok :-)
}

int sat_fseek(GFS_FILE *stream, long offset, int whence) {
	if(stream == NULL) return -1;

	switch(whence) {
		case SEEK_SET:
			if(offset < 0 || offset >= stream->f_size) return -1;
			stream->f_seek_pos = offset;
			
			break;
		case SEEK_CUR:
			if((offset + stream->f_seek_pos) >= stream->f_size) return -1;
			stream->f_seek_pos += offset;
			
			break;
		case SEEK_END:
			if(stream->f_size + offset < 0) return -1;
			stream->f_seek_pos = (stream->f_size + offset);

			break;
	}

	return 0;
}

long sat_ftell(GFS_FILE *stream) {
	if (stream == NULL) return -1;

	return stream->f_seek_pos;
}

size_t sat_fread(void *ptr, size_t size, size_t nmemb, GFS_FILE *stream) {
	if (ptr == NULL || stream == NULL) return 0; // nothing to do then
	if (size == 0 || nmemb == 0) return 0;

	Uint8 *read_buffer = NULL;
	Sint32 tot_bytes; // Total bytes to read
	Sint32 tot_sectors;
	Uint32 readBytes;

	// Get to the sector which contains our data beginning
	Uint32 start_sector = (stream->f_seek_pos)/SECTOR_SIZE;
	Uint32 skip_bytes = (stream->f_seek_pos)%SECTOR_SIZE; // Bytes to skip at the beginning of sector
	if(GFS_Seek(stream->fid, start_sector, GFS_SEEK_SET) < 0) return 0;

	tot_bytes = (nmemb * size) + skip_bytes;

	tot_sectors = GFS_ByteToSct(stream->fid, tot_bytes);
	if(tot_sectors < 0) return 0;

	Uint32 remaining_data, request_block;
	
	if(stream->file_hash == current_cached) {
		remaining_data = stream->f_size - stream->f_seek_pos;
		request_block = nmemb * size;

		memcpy(ptr, cache + stream->f_seek_pos, MIN(request_block, remaining_data));	
		stream->f_seek_pos += MIN(request_block, remaining_data);
		
		return MIN(request_block, remaining_data);
	}

	if(skip_bytes) {
		read_buffer = (Uint8*)MEM_Malloc(tot_bytes);
		
		readBytes = GFS_Fread(stream->fid, tot_sectors, read_buffer, tot_bytes);
		memcpy(ptr, read_buffer + skip_bytes, readBytes - skip_bytes);

		MEM_Free(read_buffer);
	} else {
		readBytes = GFS_Fread(stream->fid, tot_sectors, ptr, tot_bytes);
	}

	stream->f_seek_pos += (readBytes - skip_bytes); // Update the seek cursor 

	return (readBytes - skip_bytes);
}

char *sat_fgets(char *s, int size, GFS_FILE *stream) {
	if(s == NULL || stream == NULL) return NULL;

	int idx;
	char *buffer = NULL;
		
	buffer = (char*)MEM_Malloc(size);
	size_t read_data;

	if (buffer == NULL) return NULL;

	read_data = sat_fread(buffer, size - 1, 1, stream); // We have filled the buffer, but we are not sure that everything will be needed

	if(read_data == 0) {
		MEM_Free(buffer);
		return NULL;
	}
	
	for(idx = 0; idx < read_data; idx++)
		if (buffer[idx] == '\n') break;

	memcpy(s, buffer, idx);

	if (idx < read_data)
		stream->f_seek_pos -= (read_data - idx - 1);

	MEM_Free(buffer);

	return s;
}

int sat_feof(GFS_FILE *stream) {
	if((stream->f_size - 1) <= stream->f_seek_pos) return 1;
	else return 0;
}

// We won't ever write on CD... this is a dummy placeholder. 
size_t sat_fwrite(const void *ptr, size_t size, size_t nmemb, GFS_FILE *stream) {
	return 0;
}

int sat_ferror(GFS_FILE *stream) {
	return 0;
}

#ifdef __cplusplus
}
#endif
