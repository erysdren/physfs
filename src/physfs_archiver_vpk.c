/*
 * VPK support routines for PhysicsFS.
 *
 *  This archiver handles the archive format utilized by the Source Engine.
 *
 *  ========================================================================
 *
 *  This format info (in more detail) comes from:
 *     https://developer.valvesoftware.com/wiki/VPK_(file_format)
 *
 *  Source Engine VPK Format
 *
 *  Header
 *   (4 bytes)  signature = 0x55AA1234
 *   (4 bytes)  version (1 or 2)
 *   (4 bytes)  directory size (in bytes)
 *
 *  Directory
 *   (56 bytes) file name
 *   (4 bytes)  file position
 *   (4 bytes)  file length
 *
 *  ========================================================================
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by erysdren (it/she/they).
 */

#define __PHYSICSFS_INTERNAL__
#include "physfs_internal.h"

#if PHYSFS_SUPPORTS_VPK

#define VPK_SIG 0x55AA1234

static int vpkReadString(PHYSFS_Io *io, char *dest, size_t maxlen)
{
	size_t pos = 0;

	/* read null terminated string */
	while (pos < maxlen)
	{
		PHYSFS_uint8 c;
		BAIL_IF_ERRPASS(!__PHYSFS_readAll(io, &c, 1), 0);
		dest[pos++] = c;
		if (c == 0)
			return 1;
	}

	/* buffer wasn't big enough for read */
	return 0;
}

static int vpkLoadEntries(PHYSFS_Io *io, const PHYSFS_uint32 count, void *arc)
{
	char ext[256];
	char dir[256];
	char name[256];
	char path[1024];

	struct vpk_entry {
		PHYSFS_uint32 crc;
		PHYSFS_uint16 preload;
		PHYSFS_sint16 archive;
		PHYSFS_uint32 offset;
		PHYSFS_uint32 size;
		PHYSFS_uint16 terminator;
	} entry;

	while (1)
	{
		/* read ext */
		BAIL_IF_ERRPASS(!vpkReadString(io, ext, sizeof(ext)), 0);
		if (ext[0] == '\0')
			break;

		while (1)
		{
			/* read dir */
			BAIL_IF_ERRPASS(!vpkReadString(io, dir, sizeof(dir)), 0);
			if (dir[0] == '\0')
				break;

			while (1)
			{
				/* read name */
				BAIL_IF_ERRPASS(!vpkReadString(io, name, sizeof(name)), 0);
				if (name[0] == '\0')
					break;

				/* assemble full name */
				/* seems like vpk always uses forward slash, so lets use that */
				snprintf(path, sizeof(path), "%s%c%s%c%s", dir, '/', name, '.', ext);

				/* read file info */
				BAIL_IF_ERRPASS(!__PHYSFS_readAll(io, &entry.crc, 4), 0);
				BAIL_IF_ERRPASS(!__PHYSFS_readAll(io, &entry.preload, 2), 0);
				BAIL_IF_ERRPASS(!__PHYSFS_readAll(io, &entry.archive, 2), 0);
				BAIL_IF_ERRPASS(!__PHYSFS_readAll(io, &entry.offset, 4), 0);
				BAIL_IF_ERRPASS(!__PHYSFS_readAll(io, &entry.size, 4), 0);
				BAIL_IF_ERRPASS(!__PHYSFS_readAll(io, &entry.terminator, 2), 0);
				BAIL_IF(entry.terminator != 0xFFFF, PHYSFS_ERR_CORRUPT, 0);
			}
		}
	}

	return 1;
}

static int vpkLoadEntries(PHYSFS_Io *io, const PHYSFS_uint32 count, void *arc)
{
    PHYSFS_uint32 i;
    for (i = 0; i < count; i++)
    {
        PHYSFS_uint32 size;
        PHYSFS_uint32 pos;
        char name[56];
        BAIL_IF_ERRPASS(!__PHYSFS_readAll(io, name, 56), 0);
        BAIL_IF_ERRPASS(!__PHYSFS_readAll(io, &pos, 4), 0);
        BAIL_IF_ERRPASS(!__PHYSFS_readAll(io, &size, 4), 0);
        size = PHYSFS_swapULE32(size);
        pos = PHYSFS_swapULE32(pos);
        BAIL_IF_ERRPASS(!UNPK_addEntry(arc, name, 0, -1, -1, pos, size), 0);
    } /* for */

    return 1;
} /* vpkLoadEntries */


static void *VPK_openArchive(PHYSFS_Io *io, const char *name,
                              int forWriting, int *claimed)
{
    PHYSFS_uint32 magic = 0;
    PHYSFS_uint32 version = 0;
    void *unpkarc;

    assert(io != NULL);  /* shouldn't ever happen. */

    BAIL_IF(forWriting, PHYSFS_ERR_READ_ONLY, NULL);

    BAIL_IF_ERRPASS(!__PHYSFS_readAll(io, &magic, 4), NULL);
    magic = PHYSFS_swapULE32(magic);
    if (magic != VPK_SIG)
        BAIL(PHYSFS_ERR_UNSUPPORTED, NULL);

    BAIL_IF_ERRPASS(!__PHYSFS_readAll(io, &version, 4), NULL);
    version = PHYSFS_swapULE32(version);
    if (version != 1 && version != 2)
        BAIL(PHYSFS_ERR_UNSUPPORTED, NULL);

    *claimed = 1;

    BAIL_IF_ERRPASS(!__PHYSFS_readAll(io, &val, 4), NULL);
    pos = PHYSFS_swapULE32(val);  /* directory table offset. */

    BAIL_IF_ERRPASS(!__PHYSFS_readAll(io, &val, 4), NULL);
    count = PHYSFS_swapULE32(val);

    /* corrupted archive? */
    BAIL_IF((count % 64) != 0, PHYSFS_ERR_CORRUPT, NULL);
    count /= 64;

    BAIL_IF_ERRPASS(!io->seek(io, pos), NULL);

    /* !!! FIXME: check case_sensitive and only_usascii params for this archive. */
    unpkarc = UNPK_openArchive(io, 1, 0);
    BAIL_IF_ERRPASS(!unpkarc, NULL);

    if (!vpkLoadEntries(io, count, unpkarc))
    {
        UNPK_abandonArchive(unpkarc);
        return NULL;
    } /* if */

    return unpkarc;
} /* VPK_openArchive */


const PHYSFS_Archiver __PHYSFS_Archiver_VPK =
{
    CURRENT_PHYSFS_ARCHIVER_API_VERSION,
    {
        "VPK",
        "VPK format",
        "erysdren <contact@erysdren.me>",
        "https://erysdren.me/",
        0,  /* supportsSymlinks */
    },
    VPK_openArchive,
    UNPK_enumerate,
    UNPK_openRead,
    UNPK_openWrite,
    UNPK_openAppend,
    UNPK_remove,
    UNPK_mkdir,
    UNPK_stat,
    UNPK_closeArchive
};

#endif  /* defined PHYSFS_SUPPORTS_VPK */

/* end of physfs_archiver_vpk.c ... */

