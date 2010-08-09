/*
 *
 * Some simple physfs extensions
 *
 */


#if !defined(macintosh) && !defined(_MSC_VER)
#include <sys/param.h>
#endif
#if defined(__MACH__) && defined(__APPLE__)
#include <sys/mount.h>
#include <unistd.h>	// for chdir hack
#include <HIServices/Processes.h>
#endif

#include "physfsx.h"

// Initialise PhysicsFS, set up basic search paths and add arguments from .ini file.
// The .ini file can be in either the user directory or the same directory as the program.
// The user directory is searched first.
void PHYSFSX_init(int argc, char *argv[])
{
#if defined(__unix__)
	char *path = NULL;
	char fullPath[PATH_MAX + 5];
#endif
#ifdef macintosh	// Mac OS 9
	char base_dir[PATH_MAX];
	int bundle = 0;
#else
#define base_dir PHYSFS_getBaseDir()
#endif
	
	PHYSFS_init(argv[0]);
	PHYSFS_permitSymbolicLinks(1);
	
#ifdef macintosh
	strcpy(base_dir, PHYSFS_getBaseDir());
	if (strstr(base_dir, ".app:Contents:MacOSClassic"))	// the Mac OS 9 program is still in the .app bundle
	{
		char *p;
		
		bundle = 1;
		if (base_dir[strlen(base_dir) - 1] == ':')
			base_dir[strlen(base_dir) - 1] = '\0';
		p = strrchr(base_dir, ':'); *p = '\0';	// path to 'Contents'
		p = strrchr(base_dir, ':'); *p = '\0';	// path to bundle
		p = strrchr(base_dir, ':'); *p = '\0';	// path to directory containing bundle
	}
#endif
	
#if (defined(__APPLE__) && defined(__MACH__))	// others?
	chdir(base_dir);	// make sure relative hogdir paths work
#endif
	
#if defined(__unix__)
# if !(defined(__APPLE__) && defined(__MACH__))
	path = "~/.d1x-rebirth/";
# else
	path = "~/Library/Preferences/D1X Rebirth/";
# endif
	
	if (path[0] == '~') // yes, this tilde can be put before non-unix paths.
	{
		const char *home = PHYSFS_getUserDir();
		
		strcpy(fullPath, home); // prepend home to the path
		path++;
		if (*path == *PHYSFS_getDirSeparator())
			path++;
		strncat(fullPath, path, PATH_MAX + 5 - strlen(home));
	}
	else
		strncpy(fullPath, path, PATH_MAX + 5);
	
	PHYSFS_setWriteDir(fullPath);
	if (!PHYSFS_getWriteDir())
	{                                               // need to make it
		char *p;
		char ancestor[PATH_MAX + 5];    // the directory which actually exists
		char child[PATH_MAX + 5];               // the directory relative to the above we're trying to make
		
		strcpy(ancestor, fullPath);
		while (!PHYSFS_getWriteDir() && ((p = strrchr(ancestor, *PHYSFS_getDirSeparator()))))
		{
			if (p[1] == 0)
			{                                       // separator at the end (intended here, for safety)
				*p = 0;                 // kill this separator
				if (!((p = strrchr(ancestor, *PHYSFS_getDirSeparator()))))
					break;          // give up, this is (usually) the root directory
			}
			
			p[1] = 0;                       // go to parent
			PHYSFS_setWriteDir(ancestor);
		}
		
		strcpy(child, fullPath + strlen(ancestor));
		for (p = child; (p = strchr(p, *PHYSFS_getDirSeparator())); p++)
			*p = '/';
		PHYSFS_mkdir(child);
		PHYSFS_setWriteDir(fullPath);
	}
	
	PHYSFS_addToSearchPath(PHYSFS_getWriteDir(), 1);
#endif
	
	PHYSFS_addToSearchPath(base_dir, 1);
	InitArgs( argc,argv );
	PHYSFS_removeFromSearchPath(base_dir);
	
	if (!PHYSFS_getWriteDir())
	{
		PHYSFS_setWriteDir(base_dir);
		if (!PHYSFS_getWriteDir())
			Error("can't set write dir: %s\n", PHYSFS_getLastError());
		else
			PHYSFS_addToSearchPath(PHYSFS_getWriteDir(), 0);
	}
	
	//tell cfile where hogdir is
	if (GameArg.SysHogDir)
		PHYSFS_addToSearchPath(GameArg.SysHogDir,1);
#if defined(__unix__)
	else if (!GameArg.SysNoHogDir)
		PHYSFS_addToSearchPath(SHAREPATH, 1);
#endif
	
	// For Macintosh, add the 'Resources' directory in the .app bundle to the searchpaths
#if defined(__APPLE__) && defined(__MACH__)
	{
		ProcessSerialNumber psn = { 0, kCurrentProcess };
		FSRef fsref;
		OSStatus err;
		
		err = GetProcessBundleLocation(&psn, &fsref);
		if (err == noErr)
			err = FSRefMakePath(&fsref, (ubyte *)fullPath, PATH_MAX);
		
		if (err == noErr)
		{
			strncat(fullPath, "/Contents/Resources/", PATH_MAX + 4 - strlen(fullPath));
			fullPath[PATH_MAX + 4] = '\0';
			PHYSFS_addToSearchPath(fullPath, 1);
		}
	}
#elif defined(macintosh)
	if (bundle)
	{
		base_dir[strlen(base_dir)] = ':';	// go back in the bundle
		base_dir[strlen(base_dir)] = ':';	// go back in 'Contents'
		strncat(base_dir, ":Resources:", PATH_MAX - 1 - strlen(base_dir));
		base_dir[PATH_MAX - 1] = '\0';
		PHYSFS_addToSearchPath(base_dir, 1);
	}
#endif
}

int PHYSFSX_getRealPath(const char *stdPath, char *realPath)
{
	const char *realDir = PHYSFS_getRealDir(stdPath);
	const char *sep = PHYSFS_getDirSeparator();
	char *p;
	
	if (!realDir)
	{
		realDir = PHYSFS_getWriteDir();
		if (!realDir)
			return 0;
	}
	
	strncpy(realPath, realDir, PATH_MAX - 1);
	if (strlen(realPath) >= strlen(sep))
	{
		p = realPath + strlen(realPath) - strlen(sep);
		if (strcmp(p, sep)) // no sep at end of realPath
			strncat(realPath, sep, PATH_MAX - 1 - strlen(realPath));
	}
	
	if (strlen(stdPath) >= 1)
		if (*stdPath == '/')
			stdPath++;
	
	while (*stdPath)
	{
		if (*stdPath == '/')
			strncat(realPath, sep, PATH_MAX - 1 - strlen(realPath));
		else
		{
			if (strlen(realPath) < PATH_MAX - 2)
			{
				p = realPath + strlen(realPath);
				p[0] = *stdPath;
				p[1] = '\0';
			}
		}
		stdPath++;
	}
	
	return 1;
}

int PHYSFSX_rename(char *oldpath, char *newpath)
{
	char old[PATH_MAX], new[PATH_MAX];
	
	PHYSFSX_getRealPath(oldpath, old);
	PHYSFSX_getRealPath(newpath, new);
	return (rename(old, new) == 0);
}

// Find files at path that have an extension listed in exts
// The extension list exts must be NULL-terminated, with each ext beginning with a '.'
char **PHYSFSX_findFiles(char *path, char **exts)
{
	char **list = PHYSFS_enumerateFiles(path);
	char **i, **j = list, **k;
	char *ext;
	
	if (list == NULL)
		return NULL;	// out of memory: not so good
	
	for (i = list; *i; i++)
	{
		ext = strrchr(*i, '.');
		if (ext)
			for (k = exts; *k != NULL && stricmp(ext, *k); k++) {}	// see if the file is of a type we want
		
		if (ext && *k)
			*j++ = *i;
		else
			free(*i);
	}
	
	*j = NULL;
	list = realloc(list, (j - list + 1)*sizeof(char *));	// save a bit of memory (or a lot?)
	return list;
}

// Same function as above but takes a real directory as second argument, only adding files originating from this directory.
// This can be used to further seperate files in search path but it must be made sure realpath is properly formatted.
char **PHYSFSX_findabsoluteFiles(char *path, char *realpath, char **exts)
{
	char **list = PHYSFS_enumerateFiles(path);
	char **i, **j = list, **k;
	char *ext;
	
	if (list == NULL)
		return NULL;	// out of memory: not so good
	
	for (i = list; *i; i++)
	{
		ext = strrchr(*i, '.');
		if (ext)
			for (k = exts; *k != NULL && stricmp(ext, *k); k++) {}	// see if the file is of a type we want
		
		if (ext && *k && (!strcmp(PHYSFS_getRealDir(*i),realpath)))
			*j++ = *i;
		else
			free(*i);
	}
	
	*j = NULL;
	list = realloc(list, (j - list + 1)*sizeof(char *));	// save a bit of memory (or a lot?)
	return list;
}

#if 0
// returns -1 if error
// Gets bytes free in current write dir
PHYSFS_sint64 PHYSFSX_getFreeDiskSpace()
{
#if defined(__LINUX__) || (defined(__MACH__) && defined(__APPLE__))
	struct statfs sfs;
	
	if (!statfs(PHYSFS_getWriteDir(), &sfs))
		return (PHYSFS_sint64)(sfs.f_bavail * sfs.f_bsize);
	
	return -1;
#else
	return 0x7FFFFFFF;
#endif
}
#endif

//Open a file for reading, set up a buffer
PHYSFS_file *PHYSFSX_openReadBuffered(char *filename)
{
	PHYSFS_file *fp;
	PHYSFS_uint64 bufSize;
	char filename2[PATH_MAX];
	
	if (filename[0] == '\x01')
	{
		//FIXME: don't look in dir, only in hogfile
		filename++;
	}
	
	snprintf(filename2, strlen(filename)+1, filename);
	PHYSFSEXT_locateCorrectCase(filename2);
	
	fp = PHYSFS_openRead(filename2);
	if (!fp)
		return NULL;
	
	bufSize = PHYSFS_fileLength(fp);
	while (!PHYSFS_setBuffer(fp, bufSize) && bufSize)
		bufSize /= 2;	// even if the error isn't memory full, for a 20MB file it'll only do this 8 times
	
	return fp;
}

//Open a file for writing, set up a buffer
PHYSFS_file *PHYSFSX_openWriteBuffered(char *filename)
{
	PHYSFS_file *fp;
	PHYSFS_uint64 bufSize = 1024*1024;	// hmm, seems like an OK size.
	
	fp = PHYSFS_openWrite(filename);
	if (!fp)
		return NULL;
	
	while (!PHYSFS_setBuffer(fp, bufSize) && bufSize)
		bufSize /= 2;
	
	return fp;
}

//Open a 'data' file for reading (a file that would go in the 'Data' folder for the original Mac Descent II)
//Allows backwards compatibility with old Mac directories while retaining PhysicsFS flexibility
PHYSFS_file *PHYSFSX_openDataFile(char *filename)
{
	PHYSFS_file *fp = PHYSFSX_openReadBuffered(filename);
	
	if (!fp)
	{
		char name[255];
		
		sprintf(name, "Data/%s", filename);
		fp = PHYSFSX_openReadBuffered(name);
	}
	
	return fp;
}

