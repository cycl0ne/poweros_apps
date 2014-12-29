/**
* File: copy.c
* User: Srinivas Nayak
* Date: 2014-12-07
* Time: 04:22 PM
* PowerOS, Copyright (C) 2014.  All rights reserved.
**/

#include "types.h"

#include "dos.h"
#include "dos_packets.h"
#include "dos_errors.h"
#include "dos_io.h"
#include "dos_asl.h"
#include "exec_interface.h"
#include "dos_interface.h"
#include "utility_interface.h"

#define	VSTRING	"resident 0.1 (07.12.2014)\n\r"
#define CMDREV  "\0$VER: " VSTRING

#define TEMPLATE    "FROM/M,TO,PATTERN/K,BUFFER/K/N,ALL/S,DIRECT/S,CLONE/S,DATES/S,NOPRO/S,COMMENT/S,QUIET/S,NOREQ/S,ERRWARN/S,MAKEDIR/S,MOVE/S,DELETE/S,HARDLINK/S,SOFTLINK/S,FORCELINK/S,FORCEDELETE/S,FORCEOVERWRITE/S,DONTOVERWRITE/S,FORCE/S,NEWER/S" CMDREV

#define CTRL_C          (SetSignal(0L,0L) & SIGBREAKF_CTRL_C)
#define FILEPATH_SIZE           2048    /* maximum size of filepaths     */

#define COPYMODE_COPY           0
#define COPYMODE_MOVE           1
#define COPYMODE_DELETE         2
#define COPYMODE_MAKEDIR        3
#define COPYMODE_LINK           4


#define COPYFLAG_ALL            (1<<0)
#define COPYFLAG_DATES          (1<<1)
#define COPYFLAG_NOPRO          (1<<2)
#define COPYFLAG_COMMENT        (1<<3)
#define COPYFLAG_FORCELINK      (1<<4)
#define COPYFLAG_FORCEDELETE    (1<<5)
#define COPYFLAG_FORCEOVERWRITE (1<<6)
#define COPYFLAG_DONTOVERWRITE  (1<<7)
#define COPYFLAG_QUIET          (1<<8)
#define COPYFLAG_ERRWARN        (1<<10)
#define COPYFLAG_NEWER          (1<<11)
#define COPYFLAG_SOFTLINK       (1<<20) /* produce softlinks */
#define COPYFLAG_DEST_FILE      (1<<21) /* one file mode */
#define COPYFLAG_DONE           (1<<22) /* did something in DoWork */
#define COPYFLAG_ENTERSECOND    (1<<23) /* entered directory second time */
#define COPYFLAG_SRCNOFILESYS   (1<<24) /* source is no filesystem */
#define COPYFLAG_DESNOFILESYS   (1<<25) /* destination is no filesystem */

#define TEXT_READ               texts[0]
#define TEXT_COPIED             texts[1]
#define TEXT_MOVED              texts[2]
#define TEXT_DELETED            texts[3]
#define TEXT_LINKED             texts[4]
#define TEXT_RENAMED            texts[5]
#define TEXT_CREATED            texts[6]
#define TEXT_ENTERED            texts[7]
#define TEXT_OPENED_FOR_OUTPUT  texts[8]
#define TEXT_DIRECTORY          texts[15]
#define TEXT_NOT_DONE           texts[16]
#define TEXT_NOTHING_DONE       texts[17]
#define TEXT_ERR_FORCELINK      texts[18]
#define TEXT_ERR_DELETE_DEVICE  texts[19]
#define TEXT_ERR_DEST_DIR       texts[20]
#define TEXT_ERR_INFINITE_LOOP  texts[21]
#define TEXT_ERR_WILDCARD_DEST  texts[22]

const STRPTR texts[] =
{
	"read.",
	"copied",
	"moved",
	"deleted",
	"linked",
	"renamed",
	"   [created]",
	"entered",
	"opened for output",
	"COPY mode\n",
	"MOVE mode\n",
	"DELETE mode\n",
	"MAKEDIR mode\n",
	"HARDLINK mode\n",
	"SOFTLINK mode\n",
	"%s (Dir)",         /* output of directories */
	" not %s",
	"No file was processed.\n",
	"FORCELINK keyword required.\n",
	"A device cannot be deleted.",
	"Destination must be a directory.\n",
	"Infinite loop not allowed.\n",
	"Wildcard destination invalid.\n",
};

#define TESTDEST_DIR_OK         2       /* directory exists, go in */
#define TESTDEST_DELETED        1       /* file or empty directory deleted */
#define TESTDEST_NONE           0       /* nothing existed */
#define TESTDEST_ERROR          -1      /* an error occured */
#define TESTDEST_CANTDELETE     -2      /* deletion not allowed (DONTOV) */

#define CHECKVER_DESTOLDER -1
#define CHECKVER_DESTNEWER  1
#define CHECKVER_EQUAL    0

struct IptrArgs
{
    INT32  from;
    INT32  to;
    INT32  pattern;
    INT32  buffer;
    INT32  all;
    INT32  direct;
    INT32  clone;
    INT32  dates;
    INT32  nopro;
    INT32  comment;
    INT32  quiet;
    INT32  noreq;
    INT32  errwarn;
    INT32  makedir;
    INT32  move_mode;
    INT32  delete_mode;
    INT32  hardlink;
    INT32  softlink;
    INT32  forcelink;
    INT32  forcedelete;
    INT32  forceoverwrite;
    INT32  dontoverwrite;
    INT32  force;
    INT32  newer;
};


struct Args
{
    STRPTR *from;
    STRPTR  to;
    STRPTR  pattern;
    INT32   *buffer;
    INT32    all;
    INT32    direct;
    INT32    clone;
    INT32    dates;
    INT32    nopro;
    INT32    comment;
    INT32    quiet;
    INT32    noreq;
    INT32    errwarn;
    INT32    makedir;
    INT32    move_mode;
    INT32    delete_mode;
    INT32    hardlink;
    INT32    softlink;
    INT32    forcelink;
    INT32    forcedelete;
    INT32    forceoverwrite;
    INT32    dontoverwrite;
    INT32    force;
    INT32    newer;
};

struct CopyData
{
    pSysBase SysBase;
    pDOSBase DOSBase;
    pUtilBase UtilBase;

    UINT32       Flags;
    UINT32       BufferSize;
    STRPTR      Pattern;
    STRPTR        Destination;
    STRPTR        CurDest;  /* Current Destination */
    UINT32       DestPathSize;
    struct FileInfoBlock Fib;
    UINT8       Mode;
    UINT8       RetVal;         /* when set, error output is already done */
    UINT8       RetVal2;        /* when set, error output must be done */
    INT32        IoErr;
    UINT8       Deep;
    UINT8       FileName[FILEPATH_SIZE];
    UINT8       DestName[FILEPATH_SIZE];

    STRPTR      CopyBuf;
    UINT32       CopyBufLen;
};

#define VDNAMESIZE 96

struct VersionData {
    UINT8  vd_Name[VDNAMESIZE];
    INT32   vd_Version;
    INT32   vd_Revision;

    INT32   vd_Day;
    INT32   vd_Month;
    INT32   vd_Year;
};

INT32  CopyFile(APTR, APTR, UINT32, struct CopyData *);
void  DoWork(STRPTR, struct CopyData *);
INT32  IsMatchPattern(STRPTR name, struct CopyData *cd);
INT32  IsPattern(STRPTR, struct CopyData *); /* return 0 -> NOPATTERN, return -1 --> ERROR */
INT32  KillFile(STRPTR, UINT32, struct CopyData *);
INT32  KillFileKeepErr(STRPTR name, UINT32 doit, struct CopyData *);
INT32  LinkFile(APTR, STRPTR, UINT32, struct CopyData *);
APTR OpenDestDir(STRPTR, struct CopyData *);
void  PatCopy(STRPTR, struct CopyData *);
void  PrintName(const STRPTR, UINT32, UINT32, UINT32, struct CopyData *);
void  PrintNotDone(const STRPTR, const STRPTR, UINT32, UINT32, struct CopyData *);
UINT32 TestFileSys(STRPTR, struct CopyData *); /* returns value, when is a filesystem */
void  SetData(STRPTR, struct CopyData *);
INT32  TestDest(STRPTR, UINT32, struct CopyData *);
UINT32 TestLoop(APTR, APTR, struct CopyData *);
static INT32 CheckVersion( struct CopyData *cd );
static void makeversionfromstring( STRPTR buffer, struct VersionData *vd, struct CopyData *cd);
static STRPTR skipspaces( STRPTR buffer);
static STRPTR skipnonspaces( STRPTR buffer);
static BOOL VersionFind( const STRPTR path, struct VersionData *vds, struct CopyData *cd);
INT32 StrToLong(CONST_STRPTR string,INT32 *value);

DOSCALL main(APTR SysBase)
{
	pDOSBase	DOSBase;
	pUtilBase	UtilBase;

	struct RDargs *rdargs;
	INT32	rc 	= RETURN_ERROR;

	struct CopyData *cd;

	STRPTR a[2] = { "", 0 };
	struct IptrArgs iArgs;
	struct Args args = {};

	DOSBase = OpenLibrary("dos.library", 0);
	if (DOSBase)
	{

		UtilBase = OpenLibrary("utility.library",0);
		if (UtilBase)
		{
			MemSet((char *)&iArgs, 0, sizeof(struct IptrArgs));
			rdargs = ReadArgs(TEMPLATE, (UINT32*)&iArgs);

			if (rdargs == NULL) PrintFault(IoErr(), NULL);
			else
			{
				cd = AllocVec(sizeof(*cd), MEMF_PUBLIC | MEMF_CLEAR);
				if (cd == NULL)	PrintFault(rc, "No Momory");

				rc = RETURN_OK;
/*
 * Your console code goes here!
 */

				cd->SysBase = SysBase;
				cd->DOSBase = DOSBase;
				cd->UtilBase = UtilBase;

#define SysBase cd->SysBase
#define DOSBase cd->DOSBase
#define UtilBase cd->UtilBase

				cd->BufferSize = 512*1024;
				cd->Mode = COPYMODE_COPY;
				cd->RetVal2 = RETURN_FAIL;
				cd->Deep = 1;


                UINT32 patbufsize = 0;
                INT32 i = 0;

                args.from = (STRPTR *)iArgs.from;
                args.to   = (STRPTR)iArgs.to;
                args.pattern = (STRPTR)iArgs.pattern;
                args.buffer = (INT32 *)iArgs.buffer;
                args.all = (INT32)iArgs.all;
                args.direct = (INT32)iArgs.direct;
                args.clone = (INT32)iArgs.clone;
                args.dates = (INT32)iArgs.dates;
                args.nopro = (INT32)iArgs.nopro;
                args.comment = (INT32)iArgs.comment;
                args.quiet = (INT32)iArgs.quiet;
                args.noreq = (INT32)iArgs.noreq;
                args.errwarn = (INT32)iArgs.errwarn;
                args.makedir = (INT32)iArgs.makedir;
                args.move_mode = (INT32)iArgs.move_mode;
                args.delete_mode = (INT32)iArgs.delete_mode;
                args.hardlink = (INT32)iArgs.hardlink;
                args.softlink = (INT32)iArgs.softlink;
                args.forcelink = (INT32)iArgs.forcelink;
                args.forcedelete = (INT32)iArgs.forcedelete;
                args.forceoverwrite = (INT32)iArgs.forceoverwrite;
                args.dontoverwrite = (INT32)iArgs.dontoverwrite;
                args.force = (INT32)iArgs.force;
                args.newer = (INT32)iArgs.newer;

                if (args.buffer && *args.buffer > 0) /* minimum buffer size */
                {
                    cd->BufferSize = *args.buffer * 512;
                }

                if (args.quiet)
                {
                    cd->Flags |= COPYFLAG_QUIET;
                }

                if (args.all)
                {
                    cd->Flags |= COPYFLAG_ALL;
                }

                if (args.clone)
                {
                    cd->Flags |= COPYFLAG_DATES | COPYFLAG_COMMENT;
                }

                if (args.dates)
                {
                    cd->Flags |= COPYFLAG_DATES;
                }

                if (args.comment)
                {
                    cd->Flags |= COPYFLAG_COMMENT;
                }

                if (args.nopro)
                {
                    cd->Flags |= COPYFLAG_NOPRO;
                }

                if (args.forcelink)
                {
                    cd->Flags |= COPYFLAG_FORCELINK;
                }

                if (args.forcedelete)
                {
                    cd->Flags |= COPYFLAG_FORCEDELETE;
                }

                if (args.forceoverwrite)
                {
                    cd->Flags |= COPYFLAG_FORCEOVERWRITE;
                }

                if (args.dontoverwrite)
                {
                    cd->Flags |= COPYFLAG_DONTOVERWRITE;
                }

                if (args.newer)
                {
                    cd->Flags |= COPYFLAG_NEWER|COPYFLAG_DONTOVERWRITE;
                }

                if (args.errwarn)
                {
                    cd->Flags |= COPYFLAG_ERRWARN;
                }

                if (args.force) /* support OS Delete and MakeLink command
                                   options */
                {
                    if (args.delete_mode)
                    {
                        cd->Flags |= COPYFLAG_FORCEDELETE;
                    }

                    if (args.hardlink || args.softlink)
                    {
                        cd->Flags |= COPYFLAG_FORCELINK;
                    }
                }

                if (!args.from)  /* no args.from means currentdir */
                {
                    args.from = a;
                }

                if (args.delete_mode)
                {
                    ++i;
                    cd->Mode = COPYMODE_DELETE;
                }

                if (args.move_mode)
                {
                    ++i;
                    cd->Mode = COPYMODE_MOVE;
                }

                if (args.makedir)
                {
                    ++i;
                    cd->Mode = COPYMODE_MAKEDIR;
                }

                if (args.hardlink)
                {
                    ++i;
                    cd->Mode = COPYMODE_LINK;
                }

                if (args.softlink)
                {
                    ++i;
                    cd->Mode = COPYMODE_LINK;
                    cd->Flags |= COPYFLAG_SOFTLINK | COPYFLAG_FORCELINK;
                }

                if (cd->Mode != COPYMODE_DELETE &&
                    cd->Mode != COPYMODE_MAKEDIR && !args.to)
                {
                    if (*(args.from + 1)) /* when no TO is specified, the arg
                                             is last */
                    {                     /* one of from. Copy this argument into */
                        STRPTR *a;        /* args.to */

                        a = args.from;

                        while(*(++a))
                            ;

                        args.to = *(--a);
                        *a = 0;
                    }
                }

                /* test if more than one of the above four or any other wrong
                   arguments */

                if (i > 1 ||
                        (args.from == a && cd->Mode == COPYMODE_MAKEDIR) ||
                        (args.direct && (args.from == a || !*args.from ||
                                         args.pattern ||
                                         (cd->Flags & ~(COPYFLAG_QUIET | COPYFLAG_ERRWARN)) ||
                                         (cd->Mode != COPYMODE_DELETE && (cd->Mode != COPYMODE_COPY ||
                                                                         !args.to || args.from[1])))) ||
                        (args.dontoverwrite && args.forceoverwrite) ||
                        /* (args.nopro && args.clone) ||*/ /* Ignore, like original - Piru */
                        (args.softlink && args.all) ||
                        (!args.to && cd->Mode != COPYMODE_DELETE && cd->Mode != COPYMODE_MAKEDIR))
                {
                    cd->IoErr = ERROR_TOO_MANY_ARGS;
                }
                else if (cd->Mode == COPYMODE_MAKEDIR)
                {
                    INT32 i;
                    APTR dir;
                    cd->RetVal2 = RETURN_OK;

                    while (!cd->RetVal && !cd->RetVal2 && *args.from)
                    {
                        if ((i = IsPattern(*args.from, cd)))
                        {
                            if (i != -1)
                            {
                                cd->RetVal = RETURN_ERROR;

                                if (!args.quiet)
                                {
                                    PutStr(TEXT_ERR_WILDCARD_DEST);
                                }
                            }
                            else
                            {
                                cd->RetVal2 = RETURN_FAIL;
                            }
                        }

                        if ((dir = OpenDestDir(*args.from, cd)))
                        {
                            UnLock(dir);
                            cd->Flags |= COPYFLAG_DONE;
                        }

                        ++args.from;
                    }
                } /* cd->Mode == COPYMODE_MAKEDIR */
                else if (args.direct)
                {
                    if (cd->Mode == COPYMODE_COPY)
                    {
                        APTR in, out;

                        if ((in = Open(*args.from, MODE_OLDFILE)))
                        {
                            if ((out = Open(args.to, MODE_NEWFILE)))
                            {
                                cd->RetVal2 = CopyFile(in, out, cd->BufferSize, cd);
                                if (cd->RetVal2 != 0)
                                    cd->IoErr = IoErr();
                                Close(out);
                            }
                            else
                                cd->IoErr = IoErr();

                            Close(in);
                        }
                        else
                            cd->IoErr = IoErr();
                    }
                    else /* COPYMODE_DELETE */
                    {
                        while (*args.from)
                        {
                            KillFile(*(args.from++), cd->Flags & COPYFLAG_FORCEDELETE, cd);
                        }

                        cd->RetVal2 = RETURN_OK;
                    }
                }
                else
                {
                    if (args.pattern && *args.pattern)
                    {
                        patbufsize = (Strlen(args.pattern) << 1) + 3;

                        if ((cd->Pattern = (STRPTR)AllocVec(patbufsize,
                                                            MEMF_FAST)))
                        {
                            if (ParsePatternNoCase(args.pattern, cd->Pattern,
                                                   patbufsize) < 0)
                            {
                                FreeVec(cd->Pattern);
                                cd->Pattern = 0;
                            }
                        }
                    }

                    if (1)
                    {
                        if (args.pattern && !cd->Pattern)
                        {
                            if (!*args.pattern)
                            {
                                cd->IoErr = ERROR_BAD_TEMPLATE;
                            }
                        }
                        else if (cd->Mode == COPYMODE_DELETE)
                        {
                            cd->RetVal2 = RETURN_OK;

                            while (cd->RetVal <= (args.errwarn ? RETURN_OK : RETURN_WARN)
                                    && *args.from)
                            {
                                PatCopy(*(args.from++), cd);
                            }
                        }
                        else if ((i = IsPattern(args.to, cd)))
                        {
                            if (i != -1)
                            {
                                cd->RetVal = RETURN_ERROR;

                                if (!args.quiet)
                                {
                                    PutStr(TEXT_ERR_WILDCARD_DEST);
                                }
                            }
                        }
                        else
                        {
                            STRPTR path;

                            if (*(path = PathPart(args.to)) == '/')
                            {
                                ++path; /* is destination a path description ? */
                            }

                            if (*path && !*(args.from+1) &&
                                !(i = IsMatchPattern(*args.from, cd)))
                            {
                                APTR lock;

                                /* is destination an existing directory */
                                if ((lock = Lock(args.to, SHARED_LOCK)))
                                {
                                    if (Examine(lock, &cd->Fib))
                                    {
                                        if (cd->Fib.fib_DirEntryType > 0)
                                        {
                                            cd->RetVal2 = RETURN_OK;
                                        }

                                        /* indicate dir-mode for next if */
                                    }
                                    else
                                    {
                                        i = 1;
                                    }

                                    UnLock(lock);
                                }

/* Some magic to handle tick quoted pattern object names. Quite crude way to
 * handle it, but I couldn't think of anything better. - Piru
 */
#if 1
                                if (!i && cd->RetVal2 && !IsMatchPattern(*args.from, cd))
                                {
                                    UINT32 len;
                                    STRPTR pat;

                                    //Printf("pattern check <%s>\n", *args.from);

                                    len = (Strlen(*args.from) << 1) + 3;

                                    if ((pat = (STRPTR)AllocVec(len,
                                                                MEMF_FAST)))
                                    {
                                        if (ParsePattern(*args.from, pat, len) > -1 &&
                                            Strlen(pat) <= Strlen(*args.from))
                                        {
                                            lock = Lock(pat, SHARED_LOCK);
                                            if (lock)
                                            {
                                                UnLock(lock);

                                                Strcpy(*args.from, pat);
                                            }
                                        }

                                        FreeVec(pat);
                                    }
                                }
#endif

                                /* is source a directory */
                                if (!i && cd->RetVal2 &&
                                    (lock = Lock(*args.from, SHARED_LOCK)))
                                {
                                    if (Examine(lock, &cd->Fib))
                                    {
                                        cd->RetVal2 = RETURN_OK;
                                        if (cd->Mode != COPYMODE_COPY ||
                                            cd->Fib.fib_DirEntryType < 0)
                                        {
                                            UINT8 sep;

                                            cd->Flags |= COPYFLAG_DEST_FILE;

                                            /* produce missing destination directories */
                                            sep = *path;
                                            *path = 0;

                                            if ((cd->CurDest = OpenDestDir(args.to, cd)))
                                            {
                                                *path = sep;

                                                /* do the job */
                                                UnLock(lock);
                                                lock = 0;
                                                CopyMem(*args.from, cd->FileName,
                                                        1 + Strlen(*args.from));
                                                DoWork(FilePart(args.to), cd); /* on file call */
                                                UnLock(cd->CurDest);
                                            }
                                        }
                                    }

                                    if (lock)
                                    {
                                        UnLock(lock);
                                    }
                                }

                                if (lock == 0 && cd->Mode == COPYMODE_COPY && !TestFileSys(*args.from, cd))
                                {
                                    UINT8 sep;
                                    cd->Flags |= COPYFLAG_DEST_FILE | COPYFLAG_SRCNOFILESYS;
                                    cd->RetVal2 = RETURN_OK;

                                    /* produce missing destination directories */
                                    sep = *path;
                                    *path = 0;

                                    if ((cd->CurDest = OpenDestDir(args.to, cd)))
                                    {
                                        *path = sep;

                                        /* do the job */
                                        CopyMem(*args.from, cd->FileName, 1 + Strlen(*args.from));
                                        DoWork(FilePart(args.to), cd); /* on file call */
                                        UnLock(cd->CurDest);
                                    }
                                }
                            }
                            else if (i != -1)
                            {
                                cd->RetVal2 = RETURN_OK;
                            }

                            if (!cd->RetVal && !cd->RetVal2 && !(cd->Flags & COPYFLAG_DEST_FILE) &&
                                (cd->Destination = OpenDestDir(args.to, cd)))
                            {
                                while (cd->RetVal <= (args.errwarn ? RETURN_OK : RETURN_WARN)
                                        && *args.from && !CTRL_C)
                                {
                                    PatCopy(*(args.from++), cd);
                                }

                                UnLock(cd->Destination);
                            }
                        } /* else */

                        if (!(cd->Flags & COPYFLAG_DONE) &&
                            !cd->RetVal && !cd->RetVal2)
                        {
                            PutStr(TEXT_NOTHING_DONE);
                        }

                    } /* if (1) */

                    if (cd->Pattern)
                    {
                        FreeVec(cd->Pattern);
                    }
                }

                if (!cd->RetVal2 && CTRL_C)
				{
					SetIoErr(ERROR_BREAK);
					cd->RetVal2 = RETURN_WARN;
				}

				if (cd->RetVal)
				{
					cd->RetVal2 = cd->RetVal;
				}

				if (args.errwarn && cd->RetVal2 == RETURN_WARN)
				{
					cd->RetVal2 = RETURN_ERROR;
				}

				if (cd->CopyBuf)
				{
					FreeVec(cd->CopyBuf);
				}


				//if (cd)
				//{
					rc = cd->RetVal2;
					SetIoErr(cd->IoErr);
					FreeVec(cd);
				//}

#undef SysBase
#undef DOSBase
#undef UtilBase

/*
 * Your console code goes here!
 */
				FreeArgs(rdargs);
			}
			CloseLibrary(UtilBase);
		}
		CloseLibrary(DOSBase);
	}

	return rc;
}


#define SysBase cd->SysBase
#define DOSBase cd->DOSBase
#define UtilBase cd->UtilBase


void PatCopy(STRPTR name, struct CopyData *cd)
{
    struct AnchorPath *APath;
    UINT32 retval, doit = 0, deep = 0, failval = RETURN_WARN, first = 0;

    if ((cd->Mode == COPYMODE_COPY || (cd->Flags & COPYFLAG_ALL)) && !IsMatchPattern(name, cd))
    {
        first = 1; /* enter first directory (support of old copy style) */
    }

    if (cd->Flags & COPYFLAG_ERRWARN)
    {
        failval = RETURN_OK;
    }

    cd->CurDest = cd->Destination;
    cd->DestPathSize = 0;

    if (cd->Mode == COPYMODE_COPY && !TestFileSys(name, cd))
    {
        cd->Flags |= COPYFLAG_SRCNOFILESYS;
        CopyMem(name, cd->FileName, 1 + Strlen(name));
        DoWork(FilePart(name), cd);
        cd->Flags &= ~COPYFLAG_SRCNOFILESYS;

        return;
    }

    if ((APath = (struct AnchorPath *)AllocVec(sizeof(struct AnchorPath) + FILEPATH_SIZE,
                                               MEMF_PUBLIC | MEMF_CLEAR)))
    {
        int parentdirerr = 0;

        APath->ap_BreakBits = SIGBREAKF_CTRL_C;
        APath->ap_Strlen    = FILEPATH_SIZE;

        for (retval = MatchFirst(name, APath);
             !retval && cd->RetVal <= failval && !cd->RetVal2;
             retval = MatchNext(APath)
            )
        {
            if (parentdirerr)
            {
                retval = IoErr();
                if (!retval)
                    cd->IoErr = retval = ERROR_INVALID_LOCK;
                break;
            }

            if (doit)
            {
                DoWork(cd->Fib.fib_FileName, cd);
                doit = 0;
            }

            if (deep)         /* used for Deep checking */
            {
                ++cd->Deep;
                deep = 0;
            }

            cd->Flags &= ~COPYFLAG_ENTERSECOND;

            CopyMem(APath->ap_Buf, cd->FileName, FILEPATH_SIZE);
            CopyMem(&APath->ap_Info, &cd->Fib, sizeof(struct FileInfoBlock));

            if (first && APath->ap_Info.fib_DirEntryType > 0)
            {
                APath->ap_Flags |= APF_DODIR;
            }
            else if (APath->ap_Flags & APF_DIDDIR)
            {
                APTR i;

                cd->Flags |= COPYFLAG_ENTERSECOND;
                APath->ap_Flags &= ~APF_DIDDIR;
                --cd->Deep;

                if (cd->Mode == COPYMODE_DELETE || cd->Mode == COPYMODE_MOVE)
                {
                    doit = 1;
                }

                if ((i = cd->CurDest))
                {
                    cd->CurDest = ParentDir(i);
                    cd->DestPathSize = 0;

                    if (i != cd->Destination)
                    {
                        UnLock(i);
                    }

                    if (!cd->CurDest)
                    {
                        parentdirerr = 1;
                        continue;
                    }
                }
            }
            else if (APath->ap_Info.fib_DirEntryType > 0)
            {
                doit = 1;

                if (cd->Flags & COPYFLAG_ALL)
                {
                    BOOL enter;
                    enter = TRUE;

                    if (enter)
                    {
                        APath->ap_Flags |= APF_DODIR;
                        deep = 1;
                    }
                }
            }
            else if (!cd->Pattern || MatchPatternNoCase(cd->Pattern, APath->ap_Info.fib_FileName))
            {
                doit = 1;
            }

            first = 0;
        }

        MatchEnd(APath);

        if (retval && retval != ERROR_NO_MORE_ENTRIES)
        {
            INT32 ioerr = IoErr();
            Printf("%s - ", APath->ap_Info.fib_FileName);
            PrintFault(ioerr, NULL);
            cd->IoErr = ioerr;

            cd->RetVal2 = RETURN_FAIL;
        }

        if (doit)
        {
            DoWork(cd->Fib.fib_FileName, cd);
        }

        /* No need to clear the flags here, as they are cleared on next PatJoin
           call (DoWork is not called first round, as lock is zero!). */

        FreeVec(APath);
    }
    else
    {
        cd->RetVal = RETURN_FAIL;

        if (!(cd->Flags & COPYFLAG_QUIET))
        {
            PrintFault(ERROR_NO_FREE_STORE, NULL);
        }
    }

    if (cd->CurDest && cd->CurDest != cd->Destination)
    {
        UnLock(cd->CurDest);
    }
}

INT32 IsPattern(STRPTR name, struct CopyData *cd)
{
    INT32 a, ret = -1;
    STRPTR buffer;

    a = (Strlen(name) << 1) + 3;

    if ((buffer = (STRPTR)AllocVec(a, MEMF_ANY)))
    {
        ret = ParsePattern(name, buffer, a);
        FreeVec(buffer);
    }

    if (ret == -1)
    {
        cd->IoErr = ERROR_NO_FREE_STORE;
    }

    return ret;
}


INT32 IsMatchPattern(STRPTR name, struct CopyData *cd)
{
    struct AnchorPath ap;
    INT32 ret = -1;

    ap.ap_BreakBits = 0;
    ap.ap_Flags     = APF_DOWILD;
    ap.ap_Strlen    = 0;

    if (MatchFirst(name, &ap) == 0)
    {
        ret = (ap.ap_Flags & APF_ITSWILD) ? TRUE : FALSE;

        MatchEnd(&ap);
    }

    return ret;
}


INT32 KillFile(STRPTR name, UINT32 doit, struct CopyData *cd)
{
    if (doit)
    {
        SetProtection(name, 0);
    }

    return DeleteFile(name);
}


APTR OpenDestDir(STRPTR name, struct CopyData *cd)
{
    INT32 a, err = 0, cr = 0;
    APTR dir;
    STRPTR ptr = name;
    UINT8 as;

    if ((cd->Mode == COPYMODE_COPY || cd->Mode == COPYMODE_MOVE) && !TestFileSys(name, cd))
    {
        cd->Flags |= COPYFLAG_DESNOFILESYS;
        CopyMem(name, cd->DestName, 1 + Strlen(name));

        return Lock("", SHARED_LOCK);
    }

    while (!err && *ptr != 0)
    {
        while (*ptr && *ptr != '/')
        {
            ++ptr;
        }

        as = *ptr;
        *ptr = 0;

        if ((a = TestDest(name, 1, cd)) == TESTDEST_CANTDELETE)
        {
            if (!(cd->Flags & COPYFLAG_QUIET))
            {
                PutStr(TEXT_ERR_DEST_DIR);
            }

            err = 2;
        }
        else if (a < 0)
        {
            err = 1;
        }
        else if (a != TESTDEST_DIR_OK)
        {
            if ((dir = CreateDir(name)))
            {
                ++cr;

                if ((cd->Flags))
                {
                    PrintName(name, 1, 1, 1, cd);
                    Printf("%s\n", TEXT_CREATED);
                }

                UnLock(dir);
            }
            else
            {
                cd->IoErr = IoErr();
                if (!(cd->Flags & COPYFLAG_QUIET))
                {
                    PrintNotDone(name, TEXT_CREATED, 1, 1, cd);
                }

                err = 2;
            }
        }

        *ptr = as;

        /* 26-Oct-2003 bugfix: Don't scan past end of the string.
         * as is the old char, if '\0' we've reached the end of the
         * string. - Piru
         */
        if (as)
        {
            ptr++;
        }
    }

    if (err)
    {
        cd->RetVal = RETURN_ERROR;

        if (!(cd->Flags & COPYFLAG_QUIET) && err == 1)
        {
            PrintNotDone(name, TEXT_OPENED_FOR_OUTPUT, 1, 1, cd);
        }

        return 0;
    }

    if (cd->Mode == COPYMODE_MAKEDIR && !cr && !(cd->Flags & COPYFLAG_QUIET))
    {
        cd->IoErr = ERROR_OBJECT_EXISTS;
        PrintNotDone(name, TEXT_CREATED, 1, 1, cd);
    }

    return Lock(name, SHARED_LOCK);
}

void PrintName(const STRPTR name, UINT32 deep, UINT32 dir, UINT32 txt, struct CopyData *cd)
{
    PutStr("   ");
    if (deep)
    {
        while (--deep)
        {
            PutStr("        ");
        }
    }

    if (dir)
    {
        PutStr("     ");
    }

    Printf((dir ? TEXT_DIRECTORY : (STRPTR) "%s"), name);

    if (!dir && txt)
    {
        PutStr("..");
    }

    Flush(Output());
}


void PrintNotDone(const STRPTR name, const STRPTR txt, UINT32 deep, UINT32 dir, struct CopyData *cd)
{
    Printf(TEXT_NOT_DONE, txt);
    if (cd->IoErr != 0)
    {
        PutStr(" - ");
        PrintFault(cd->IoErr, NULL);
    }
    else
        PutStr("\n");
}


/* returns value, when it seems to be a filesystem */
UINT32 TestFileSys(STRPTR name, struct CopyData *cd)
{
    STRPTR n = name;
    UINT32 ret = 1;

    while (*n && *n != ':')
    {
        ++n;
    }

    if (*(n++) == ':')
    {
        UINT8 a;

        a = *n;
        *n = 0;
        ret = IsFileSystem(name);
        ret = 0;
        *n = a;
    }

    return ret;
}


void DoWork(STRPTR name, struct CopyData *cd)
{
    APTR pdir, lock = 0;
    STRPTR printerr = NULL, printok = "";

    if (cd->RetVal > (cd->Flags & COPYFLAG_ERRWARN ? RETURN_OK : RETURN_WARN) || cd->RetVal2)
    {
        return;
    }

    if (cd->Mode != COPYMODE_DELETE && !(cd->Flags & COPYFLAG_DESNOFILESYS))
    {
        if (!cd->DestPathSize)
        {
            if (!NameFromLock(cd->CurDest, cd->DestName, FILEPATH_SIZE))
            {
                cd->RetVal2 = RETURN_FAIL;
                return;
            }

            cd->DestPathSize = Strlen(cd->DestName);
        }

        cd->DestName[cd->DestPathSize] = 0;
        AddPart(cd->DestName, name, FILEPATH_SIZE);
    }

    if (cd->Flags & (COPYFLAG_SRCNOFILESYS|COPYFLAG_DESNOFILESYS))
    {
        UINT32 res = 0;
        APTR in, out;
        STRPTR txt = TEXT_OPENED_FOR_OUTPUT;

        if ((in = Open(cd->FileName, MODE_OLDFILE)))
        {
            txt = cd->Mode == COPYMODE_MOVE ? TEXT_MOVED : TEXT_COPIED;

            if ((out = Open(cd->DestName, MODE_NEWFILE)))
            {
                UINT32 h;

                h = CopyFile(in, out, cd->BufferSize, cd);
                if (h != 0)
                    cd->IoErr = IoErr();
                Close(out);
                Close(in);
                in = NULL;

                if (!h)
                {
                    if (cd->Mode == COPYMODE_MOVE)
                    {
                        if (KillFile(cd->FileName, cd->Flags & COPYFLAG_FORCEDELETE, cd))
                        {
                            res = 1;
                        }
                    }
                    else
                    {
                        res = 1;
                    }
                } else {
                    KillFile(cd->DestName, 0, cd);
                }
            }
            else
                cd->IoErr = IoErr();

            if (in != NULL)
                Close(in);
        }
        else
            cd->IoErr = IoErr();

        if (!res && !(cd->Flags & COPYFLAG_QUIET))
        {
            PrintNotDone(name, txt, cd->Deep, cd->Fib.fib_DirEntryType > 0, cd);
        }
        else
        {
            cd->Flags |= COPYFLAG_DONE;

            if ((cd->Flags))
            {
                Printf("%s\n", txt);
            }
        }
        return;
    }

    if (!(lock = Lock(cd->FileName, SHARED_LOCK)))
    {
        cd->RetVal = RETURN_WARN;
        cd->IoErr = IoErr();

        if (!(cd->Flags & COPYFLAG_QUIET))
        {
            PrintNotDone(cd->Fib.fib_FileName, TEXT_READ, cd->Deep,
                         cd->Fib.fib_DirEntryType > 0, cd);
        }
        return;
    }

    if (!(pdir = ParentDir(lock)))
    {
        cd->RetVal = RETURN_ERROR;

        if (cd->Mode == COPYMODE_DELETE)
        {
            if (!(cd->Flags & COPYFLAG_QUIET))
            {
                Printf(" %s ", cd->FileName);
                Printf(TEXT_NOT_DONE, TEXT_DELETED);
                Printf("%s\n", TEXT_ERR_DELETE_DEVICE);
            }
        }

        UnLock(lock);
        return;
    }

    UnLock(pdir);

    if (!(cd->Flags & COPYFLAG_QUIET))
    {
        if ((cd->Flags))
        {
            PrintName(name, cd->Deep, cd->Fib.fib_DirEntryType > 0, cd->Fib.fib_DirEntryType < 0 ||
                      (cd->Flags & COPYFLAG_ALL ? cd->Mode != COPYMODE_DELETE : cd->Mode != COPYMODE_COPY) ||
                      cd->Flags & COPYFLAG_ENTERSECOND, cd);
        }
    }

    if ((cd->Flags & COPYFLAG_ENTERSECOND) || (cd->Mode == COPYMODE_DELETE &&
        (!(cd->Flags & COPYFLAG_ALL) || cd->Fib.fib_DirEntryType < 0)))
    {
        UnLock(lock);
        lock = 0;

        if (KillFile(cd->FileName, cd->Flags & COPYFLAG_FORCEDELETE, cd))
        {
            printok = TEXT_DELETED;
        }
        else
        {
            cd->RetVal = RETURN_WARN;
            printerr = TEXT_DELETED;
        }
    }
    else if (cd->Mode == COPYMODE_DELETE)
    {
        ;
    }
    else if (cd->Fib.fib_DirEntryType > 0)
    {
                INT32 a;

        if ((cd->Flags & COPYFLAG_ALL || cd->Mode == COPYMODE_LINK ||
             cd->Mode == COPYMODE_MOVE) && TestLoop(lock, cd->CurDest, cd))
        {
            printok = 0;
            cd->RetVal = RETURN_ERROR;

            if (!(cd->Flags & COPYFLAG_QUIET))
            {
                if (!(cd->Flags))
                {
                    PrintName(name, cd->Deep, 1, 1, cd);
                }

                Printf(TEXT_NOT_DONE, TEXT_ENTERED);
                PutStr(TEXT_ERR_INFINITE_LOOP);
            }
        }
        else if ((a = TestDest(cd->DestName, 1, cd)) < 0)
        {
            printerr = TEXT_CREATED;
            cd->RetVal = RETURN_ERROR;
        }
        else if (cd->Flags & COPYFLAG_ALL)
        {
            APTR i;

            i = cd->CurDest;
            cd->DestPathSize = 0;

            if (a == TESTDEST_DIR_OK)
            {
                if (!(cd->CurDest = Lock(cd->DestName, SHARED_LOCK)))
                {
                    cd->IoErr = IoErr();
                    printerr = TEXT_ENTERED;
                    cd->RetVal = RETURN_ERROR;
                }
                else
                {
                    printok  = "";
                }
            }
            else if ((cd->CurDest = CreateDir(cd->DestName)))
            {
                UnLock(cd->CurDest);

                if ((cd->CurDest = Lock(cd->DestName, SHARED_LOCK)))
                {
                    printok = TEXT_CREATED;
                }
                else
                {
                    cd->IoErr = IoErr();
                    printerr = TEXT_ENTERED;
                    cd->RetVal = RETURN_ERROR;
                }
            }
            else
            {
                cd->IoErr = IoErr();
                printerr = TEXT_CREATED;
                cd->RetVal = RETURN_ERROR;
            }

            if (!cd->CurDest)
            {
                cd->CurDest = i;
            }
            else if (i != cd->Destination)
            {
                UnLock(i);
            }
        }
        else if (cd->Mode == COPYMODE_MOVE)
        {
            if (Rename(cd->FileName, cd->DestName))
            {
                printok = TEXT_RENAMED;
            }
            else
            {
                cd->IoErr = IoErr();
                printerr = TEXT_RENAMED;
                cd->RetVal = RETURN_WARN;
            }
        }
        else if (cd->Mode == COPYMODE_LINK)
        {
            if (!(cd->Flags & COPYFLAG_FORCELINK))
            {
                printok = 0;
                cd->RetVal = RETURN_WARN;

                if (!(cd->Flags & COPYFLAG_QUIET))
                {
                    if (!(cd->Flags))
                    {
                        PrintName(name, cd->Deep, 1, 1, cd);
                    }

                    Printf(TEXT_NOT_DONE, TEXT_LINKED);
                    PutStr(TEXT_ERR_FORCELINK);
                }
            }
            else if (LinkFile(lock, cd->DestName, cd->Flags & COPYFLAG_SOFTLINK, cd))
            {
                printok = TEXT_LINKED;
            }
            else
            {
                printerr = TEXT_LINKED;
                cd->RetVal = RETURN_WARN;
            }
        }
        else /* COPY mode only displays directories, when not ALL */
        {
            printok = 0;

            if (!(cd->Flags & COPYFLAG_QUIET))
            {
                if (cd->Flags)
                {
                    PutStr("\n");
                }
            }
        }
    }
    else
    {
        /* test for existing destination file */
        if (TestDest(cd->DestName, 0, cd) < 0)
        {
            printerr = TEXT_OPENED_FOR_OUTPUT;
        }
        else if (cd->Mode == COPYMODE_MOVE && Rename(cd->FileName, cd->DestName))
        {
            printok = TEXT_RENAMED;
        }
        else if (cd->Mode == COPYMODE_LINK)
        {
            if (!(cd->Flags & COPYFLAG_SOFTLINK) && LinkFile(lock, cd->DestName, 0, cd))
            {
                printok = TEXT_LINKED;
            }
            else
            {
                printerr = TEXT_LINKED;
                cd->RetVal = RETURN_WARN;

                if (cd->Flags & COPYFLAG_SOFTLINK)
                {
                    cd->IoErr = ERROR_OBJECT_WRONG_TYPE;
                }
            }
        }
        else
        {
            UINT32 res = 0, h;
            APTR in, out;
            STRPTR txt = TEXT_OPENED_FOR_OUTPUT;

            if ((out = Open(cd->DestName, MODE_NEWFILE)))
            {
                UINT32 kill = 1;

                txt = cd->Mode == COPYMODE_MOVE ? TEXT_MOVED : TEXT_COPIED;
                UnLock(lock);
                lock = 0;

                if ((in = Open(cd->FileName, MODE_OLDFILE)))
                {
                    h = CopyFile(in, out, cd->BufferSize, cd);
                    if (h != 0)
                        cd->IoErr = IoErr();
                    Close(out);
                    out = NULL;
                    Close(in);

                    if (!h)
                    {
                        kill = 0;

                        if (cd->Mode == COPYMODE_MOVE)
                        {
                            if (KillFile(cd->FileName, cd->Flags & COPYFLAG_FORCEDELETE, cd))
                            {
                                res = 1;
                            }
                        }
                        else
                        {
                            res = 1;
                        }
                    }
                }
                else cd->IoErr = IoErr();

                if (out)
                {
                    Close(out);
                }

                if (kill)
                {
                    KillFile(cd->DestName, 0, cd);
                }
            }
            else cd->IoErr = IoErr();

            if (!res)
            {
                printerr = txt;
                cd->RetVal = RETURN_WARN;
            }
            else
            {
                printok = txt;
            }
        }
    }

    if (printerr && !(cd->Flags & COPYFLAG_QUIET))
    {
        PrintNotDone(name, printerr, cd->Deep, cd->Fib.fib_DirEntryType > 0, cd);
    }
    else if (printok)
    {
        cd->Flags |= COPYFLAG_DONE;

        if (!(cd->Flags & COPYFLAG_QUIET))
        {
            if ((cd->Flags))
            {
                Printf("%s\n", printok);
            }
        }

        SetData(cd->DestName, cd);
    }

    if (lock)
    {
        UnLock(lock);
    }
}

INT32 CopyFile(APTR from, APTR to, UINT32 bufsize, struct CopyData *cd)
{
    STRPTR buffer;
    INT32 s, err = 0;

    if (cd->CopyBuf)
    {
        buffer  = cd->CopyBuf;
        bufsize = cd->CopyBufLen;
    }
    else
    {
        do
        {
            buffer = (STRPTR)AllocVec(bufsize, MEMF_PUBLIC);
            if (buffer)
            {
                cd->CopyBuf    = buffer;
                cd->CopyBufLen = bufsize;
                break;
            }

            bufsize >>= 1;

        } while (bufsize >= 512);
    }

    if (buffer)
    {
        {
            /* Stream or so, copy until EOF or error */
            do
            {
                UINT32 brk = CTRL_C;
                /* AROS: This flush appears to be required if reading from '*'
                 * Maybe a bug in Read(), or AROS buffering?
                 */
                Flush(from);
                if (brk || (s = Read(from, buffer, bufsize)) == -1 || Write(to, buffer, s) != s)
                {
                    if (brk)
                    {
                        cd->IoErr = ERROR_BREAK;
                    }
                    err = RETURN_FAIL;
                    break;
                }
            } while (s > 0);
        }

        /* Freed at exit to avoid fragmentation */
        /*FreeMem(buffer, bufsize);*/
    }
    else
    {
        err = RETURN_FAIL;
    }

    return err;
}


/* Softlink's path starts always with device name! f.e. "Ram Disk:T/..." */
INT32 LinkFile(APTR from, STRPTR to, UINT32 soft, struct CopyData *cd)
{
    if (soft)
    {
        INT32 ret = FALSE;
        UINT8 *name;

        name = AllocVec(FILEPATH_SIZE, MEMF_ANY);
        if (name)
        {
            if (NameFromLock(from, name, FILEPATH_SIZE))
            {
                ret = MakeLink(to, (UINT16*)name, LINK_SOFT);
            }

            FreeVec(name);
        }

        return ret;
    }
    else
    {
        return MakeLink(to, (UINT16*)from, LINK_HARD);
    }
}


/* return 0 means no loop, return != 0 means loop found */
UINT32 TestLoop(APTR srcdir, APTR destdir, struct CopyData *cd)
{
    UINT32 loop = 0;
    APTR par, lock;

    lock = destdir;

    if (SameDevice(srcdir, destdir))
    {
        do
        {
            if (!SameLock(srcdir, lock))
            {
                loop = 1;
            }
            else
            {
                par = ParentDir(lock);

                if (lock != destdir)
                {
                    UnLock(lock);
                }

                lock = par;
            }
        }
        while(!loop && lock);
    }

    if (lock != destdir)
    {
        UnLock(lock);
    }

    return loop;
}


void SetData(STRPTR name, struct CopyData *cd)
{
    if (cd->Flags & COPYFLAG_NOPRO)
    {
        /* Is already set! - Piru */
        //SetProtection(name, 0);
    }
    else
    {
        SetProtection(name, cd->Fib.fib_Protection & (UINT32) ~FIBF_ARCHIVE);
    }

    if (cd->Flags & COPYFLAG_DATES)
    {
        SetFileDate(name, &cd->Fib.fib_Date);
    }

    if (cd->Flags & COPYFLAG_COMMENT)
    {
        SetComment(name, cd->Fib.fib_Comment);
    }
}

INT32 TestDest(STRPTR name, UINT32 type, struct CopyData *cd)
{
    INT32 ret = TESTDEST_ERROR;
    APTR lock;

    if ((lock = Lock(name, SHARED_LOCK)))
    {
        struct FileInfoBlock *fib;

        if (fib = (struct FileInfoBlock *) AllocVec(sizeof(struct FileInfoBlock), MEMF_CLEAR))
        {
            if (Examine(lock, fib))
            {
                UnLock(lock);
                lock = 0;

                if (type)
                {
                    if (fib->fib_DirEntryType > 0)
                    {
                        ret = TESTDEST_DIR_OK;
                    }
                    else if (!(cd->Flags & COPYFLAG_DONTOVERWRITE))
                    {
                        if (KillFile(name, cd->Flags & COPYFLAG_FORCEOVERWRITE, cd))
                        {
                            ret = TESTDEST_DELETED;
                        }
                    }
                    else
                    {
                        ret = TESTDEST_CANTDELETE;
                    }
                }
                else if (cd->Flags & COPYFLAG_DONTOVERWRITE)
                {
                    if (cd->Flags & COPYFLAG_NEWER)
                    {
                        if (CheckVersion( cd ) == CHECKVER_DESTOLDER )
                        {
                            if (KillFile(name, cd->Flags & COPYFLAG_FORCEOVERWRITE, cd))
                            {
                                ret = TESTDEST_DELETED;
                            }
                        }
                        else
                        {
                            ret = TESTDEST_CANTDELETE;
                        }
                    }
                    else
                    {
                        ret = TESTDEST_CANTDELETE;
                    }
                }
                else if (KillFile(name, cd->Flags & COPYFLAG_FORCEOVERWRITE, cd))
                {
                    ret = TESTDEST_DELETED;
                }
            }

            FreeVec(fib);
        }

        if (lock)
        {
            UnLock(lock);
        }
    }
    else
    {
        ret = TESTDEST_NONE;
    }

    if (ret == TESTDEST_CANTDELETE)
    {
        cd->IoErr = ERROR_OBJECT_EXISTS;
    }
    else if (ret == TESTDEST_ERROR)
    {
        cd->IoErr = IoErr();
    }

    return ret;
}

/*
** We compare current file versions and return the result
** see CHECKVER_#? values
*/

static INT32 CheckVersion( struct CopyData *cd )
{
struct VersionData vds;
struct VersionData vdd;
INT32 resversion = CHECKVER_EQUAL;
INT32 resdate = CHECKVER_EQUAL;

    if( VersionFind( cd->FileName, &vds, cd ) )
    {
        if( VersionFind( cd->DestName, &vdd, cd ) )
        {
/* version and revision must be available to ensure a proper operation */
            if( ((vdd.vd_Version != -1) && (vds.vd_Version != -1) && (vdd.vd_Revision != -1) && (vds.vd_Revision != -1)) )
            {
/* first we make the stuff comparable. If one component is missing we reset both */
                if( vdd.vd_Year == -1 || vds.vd_Year == -1 )
                {
                    vdd.vd_Year = 0;
                    vds.vd_Year = 0;
                }
                if( vdd.vd_Month == -1 || vds.vd_Month == -1 )
                {
                    vdd.vd_Month = 0;
                    vds.vd_Month = 0;
                }
                if( vdd.vd_Day == -1 || vds.vd_Day == -1 )
                {
                    vdd.vd_Day = 0;
                    vds.vd_Day = 0;
                }

/* check version */
                resversion = CHECKVER_DESTOLDER;
                if( ((vdd.vd_Version == vds.vd_Version) && vdd.vd_Revision == vds.vd_Revision ) )
                {
                    resversion = CHECKVER_EQUAL;
                }
                else if( (vdd.vd_Version  > vds.vd_Version) ||
                        ((vdd.vd_Version == vds.vd_Version) && vdd.vd_Revision > vds.vd_Revision ) )
                {
                    resversion = CHECKVER_DESTNEWER;
                }
/* check date */

                resdate = CHECKVER_DESTOLDER;
                if( ((vdd.vd_Year == vds.vd_Year) && (vdd.vd_Month == vds.vd_Month) && (vdd.vd_Day == vds.vd_Day) ) )
                {
                    resdate = CHECKVER_EQUAL;
                }
                else
                {
                    if( ( (vdd.vd_Year  > vds.vd_Year ) ||
                        ( (vdd.vd_Year == vds.vd_Year) && (vdd.vd_Month  > vds.vd_Month ) ) ||
                        ( (vdd.vd_Year == vds.vd_Year) && (vdd.vd_Month == vds.vd_Month ) && (vdd.vd_Day  > vds.vd_Day ) ) ))
                    {
                        resdate = CHECKVER_DESTNEWER;
                    }
                }

/* plausible check */
                if( ((resversion == CHECKVER_DESTNEWER) && (resdate == CHECKVER_DESTOLDER)) || /* newer version with older date */
                    ((resversion == CHECKVER_DESTOLDER) && (resdate == CHECKVER_DESTNEWER)) )  /* older version with newer date */
                {
                    /* we maybe should inform the user about this */
                    return( CHECKVER_EQUAL );
                }
            }
        }
    }
/* compose result */

    if( (resversion == resdate) || (resversion == CHECKVER_EQUAL) )
    {
        return( resdate );
    }
    else
    {
        return( resversion );
    }
}



/*
** Searches the given file for a version string and fills version data struct with the result.
** Returns false if no version was found. Returns true if the version got parsed and version data
** is valid.
*/

#define VERSBUFFERSIZE 4096 /* must be as big as the biggest version string we want to handle. */

static BOOL VersionFind( const STRPTR path, struct VersionData *vds, struct CopyData *cd)
{
    APTR handle;
    STRPTR buf;
    UINT32 i, rc;

    rc = FALSE;

    if ( (buf = AllocVec(VERSBUFFERSIZE, MEMF_PUBLIC | MEMF_CLEAR)) )
    {
        if ( (handle = Open(path, MODE_OLDFILE)) )
        {
            long index = 0;

            while( ( (index += Read(handle, &buf[index], VERSBUFFERSIZE-index)) > 5) && !rc )
            {
                for (i = 0; i < index-5; i++) {
                    if( buf[i] == '$' && buf[i+1] == 'V' && buf[i+2] == 'E' && buf[i+3] == 'R' && buf[i+4] == ':' ) {
                        CopyMem( &buf[i], buf, index-i );
                        index -= i;
                        (index += Read(handle, &buf[index], VERSBUFFERSIZE-index));
                        /* now the version string is aligned and complete in buffer */
                        makeversionfromstring( buf, vds, cd );
                        rc = TRUE;
                        break;
                    }
                }
                CopyMem( &buf[index-5], &buf[0], 5 );
                index = 5;
            }
            Close(handle);
        }
        FreeVec(buf);
    }
    return (rc);
}


/*
** This function extracts the version information from a given version string.
** the result will be store in the given version data structure.
**
** NOTE: There is no need to preset the version data struct. All fields will
** be reset to defaults, so in case of a faulty version string the result data
** will be checkable.
*/

static
void makeversionfromstring( STRPTR buffer, struct VersionData *vd, struct CopyData *cd)
{
    INT32  pos;
    UINT32 tmp;
    STRPTR name;

/* reset data field */

    vd->vd_Name[0]  = '\0';
    vd->vd_Version  = -1;
    vd->vd_Revision = -1;
    vd->vd_Day      = -1;
    vd->vd_Month    = -1;
    vd->vd_Year     = -1;

    buffer = skipspaces( buffer ); /* skip before $VER: */
    buffer = skipnonspaces( buffer ); /* skip $VER: */
    buffer = skipspaces( buffer ); /* skip spaces before tool name */
    name = buffer;
    buffer = skipnonspaces( buffer ); /* skip name of tool */

    if( (tmp = ((long) buffer - (long) name) ) && *buffer )
    {
        CopyMem( name, vd->vd_Name, MIN( tmp, VDNAMESIZE-1) );
        vd->vd_Name[MIN( tmp, VDNAMESIZE-1)] = '\0'; /* terminate name string inside target buffer */

        buffer = skipspaces( buffer ); /* skip spaces before version */
        if( *buffer ) {

        /* Do version */

            if( (StrToLong((STRPTR) buffer, &tmp)) != -1 )
            {
                vd->vd_Version = tmp;

                /* Do revision */

                buffer += pos;
                buffer = skipspaces(buffer);
                if (*buffer++ == '.')
                {
                    if( (StrToLong((STRPTR) buffer, &tmp)) != -1 )
                    {
                        vd->vd_Revision = tmp;
                        buffer += pos;
                        buffer = skipspaces(buffer);
                        if (*buffer++ == '(')
                        {
                            if( (StrToLong((STRPTR) buffer, &tmp)) != -1 )
                            {
                                vd->vd_Day = tmp;
                                buffer += pos;
                                if (*buffer++ == '.')
                                {
                                    if( (StrToLong((STRPTR) buffer, &tmp)) != -1 )
                                    {
                                        vd->vd_Month = tmp;
                                        buffer += pos;
                                        if (*buffer++ == '.')
                                        {
                                            if( (StrToLong((STRPTR) buffer, &tmp)) != -1 )
                                            {
                                                if( (tmp >= 70) && (tmp <= 99) )
                                                {
                                                    tmp += 1900;
                                                }
                                                if( (tmp < 70) )
                                                {
                                                    tmp += 2000;
                                                }
                                                vd->vd_Year = tmp;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

/* Return a pointer to a string, stripped by all leading space characters
 * (SPACE).
 */
static
STRPTR skipspaces( STRPTR buffer)
{
    for (;; buffer++)
    {
        if (buffer[0] == '\0' || buffer[0] != ' ')
        {
            return( buffer );
        }
    }
}

/* Return a pointer to a string, stripped by all non space characters
 * (SPACE).
 */
static
STRPTR skipnonspaces( STRPTR buffer)
{
    for (;; buffer++)
    {
        if (buffer[0] == '\0' || buffer[0] == ' ')
        {
            return( buffer );
        }
    }
}

INT32 StrToLong(CONST_STRPTR string,INT32 *value)
{
    INT32 sign=0, v=0;
    CONST_STRPTR s=string;

    /* Skip leading whitespace characters */
    while(*s==' '||*s=='\t')
        s++;

    /* Swallow sign */
    if(*s=='+'||*s=='-')
        sign=*s++;

    /* If there is no number return an error. */
    if(*s<'0'||*s>'9')
    {
        *value=0;
        return -1;
    }

    /* Calculate result */
    do
        v=v*10+*s++-'0';
    while(*s>='0'&&*s<='9');

    /* Negative? */
    if(sign=='-')
        v=-v;

    /* All done. */
    *value=v;
    return s-string;
}
