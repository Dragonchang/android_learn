
#define	LOG_TAG		"klogcat"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <libgen.h>

#include <dirent.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/klog.h>
#include <sys/prctl.h>
#include <sys/time.h>
#include <private/android_filesystem_config.h>

#include <cutils/sockets.h>
#include "headers/common.h"
#include "headers/sem.h"
#include "headers/process.h"

#define	VERSION	"1.4"
/*
 * 1.4	: Check log file size and rotate log file if over file limitation.
 * 1.3	: Change log file permission to 0600 by request.
 * 1.2	: Append log file on next klogcat session.
 * 1.1	: Rotate log file on next klogcat session to avoid large size of log file.
 * 1.0	: Initial version.
 */

static sem_t timed_lock;

int  m_nHelp = 0;
char m_szPath [PATH_MAX];
int  m_nFileToSave = 0;
int  m_nKBytes = 0;
int  m_nCount = 4;
int  m_nDumpAndExit = 0;
int  m_nDumpType = 0;  					// 0: kmsg, 1: lastkmsg
int  m_nEnableDebug = 0;				// 0: disable, 1: enable

static int usage (char *prg)
{
	printf ("\n"
		"%s version " VERSION "\n"
		"\n"
		"usage:\n"
		"\t%s [ -f <filename> | -r <kbytes> | -n <count> | -d | -b <type> ]\n"
		"\n"
		"descriptions:\n"
		"\t --help        Show help information.\n"
		"\t -f <filename> Log to the filename.  Default to stdout.\n"
		"\t -r <kbytes>   Rotate log every kbytes.  Requires -f.\n"
		"\t -n <count>    Set maximum number of rotated logs to <count>.  Default count is 4.\n"
		"\t -d            Dump the log and then exit (don't block).\n"
		"\t -b <type>     Request alternative log type, could be 'kmsg' or 'lastkmsg'.\n"
		"\t -e            Eanble debug log for debugging klogcat.\n"
		"\n"
		"example:\n"
		"\t%s \n"
		"\t%s --help\n"
		"\t%s -b lastkmsg\n"
		"\t%s -f /data/htclog/KernelLog.txt -r 2048 -n 8\n"
		"\t%s -f /data/htclog/KernelLog.txt -d\n"
		"\t%s -f /data/htclog/KernelLog.txt -b lastkmsg\n"
		"\t%s -e\n"
		"\n"
		, prg, prg, prg, prg, prg, prg, prg, prg, prg);
	return 1;
}

static void *thread_monitor_main (void *UNUSED_VAR (null))
{
	file_log (LOG_TAG ": m_nFileToSave[%d].\n", m_nFileToSave);

	do
	{
		if (m_nDumpType == 0)
		{
			logkmsg_start ("logkmsg");
		}
		else
		{
			logkmsg_start ("loglastkmsg");
		}

		timed_wait (& timed_lock, 3000);

	}while(is_thread_alive (working) && ! m_nDone);

end:;

	m_nDone = 1;

	file_log (LOG_TAG ": stop thread_monitor_main thread ...\n");

	return NULL;
}

static void process_klogcat ()
{
	pthread_t thread_monitor = (pthread_t) -1;

	for (; ! m_nDone;)
	{
		if (thread_monitor == (pthread_t) -1)
		{
			if (pthread_create (& thread_monitor, NULL, thread_monitor_main, NULL) < 0)
			{
				thread_monitor = (pthread_t) -1;
			}
		}

		timed_wait (& timed_lock, 3000);
	}

end:;
	thread_monitor = (pthread_t) -1;

	return;
}

static void parse_args(int argc, char **argv)
{
	char command;

	struct option long_options[] =
	{
		{"help",		no_argument,		0, 'h'},
		{"filename",	required_argument,	0, 'f'},
		{"kbytes",  	required_argument,	0, 'r'},
		{"count",		required_argument,	0, 'n'},
		{"dump",		no_argument, 		0, 'd'},
		{"type",		required_argument,	0, 'b'},
		{"debug",		no_argument, 		0, 'e'},
	};

	m_nDone = 0;

	while ((command = getopt_long(argc, argv, "f:r:n:db:e", long_options, NULL)) != -1)
	{
		switch (command)
		{
			case 'h':
			{
				m_nHelp = 1;
			}
			break;

			case 'f':
			{
				snprintf (m_szPath, PATH_MAX, "%s", optarg);
				m_nFileToSave = 1;
			}
			break;

			case 'r':
			{
				m_nKBytes = atoi (optarg);
			}
			break;

			case 'n':
			{
				m_nCount = atoi (optarg);
			}
			break;

			case 'd':
			{
				m_nDumpAndExit = 1;
			}
			break;

			case 'b':
			{
				if ( strcmp (optarg, "lastkmsg") >= 0 )
				{
					m_nDumpType = 1;
				}
			}
			break;

			case 'e':
			{
				m_nEnableDebug = 1;
			}
			break;

			default:
			{
				file_log (LOG_TAG ": klogcat version(%s). \n", VERSION);
				file_log (LOG_TAG ": option -e(%d). \n", m_nEnableDebug);

				file_log (LOG_TAG ": option -f(%s). \n", m_szPath);
				file_log (LOG_TAG ": option -r(%d). \n", m_nKBytes);
				file_log (LOG_TAG ": option -n(%d). \n", m_nCount);
				file_log (LOG_TAG ": option -d(%d). \n", m_nDumpAndExit);
				file_log (LOG_TAG ": option -b(%d)[0: kmsg, 1: lastkmsg]. \n", m_nDumpType);

				if (m_nHelp == 1)
				{
					usage (argv [0]);
				}
				else
				{
					process_klogcat ();
				}

				if (m_nDumpType == 0)
				{
					logkmsg_stop ("logkmsg");
				}
				else
				{
					logkmsg_stop ("loglastkmsg");
				}

				file_log (LOG_TAG ": END. \n");

				exit(-1);
			}
			break;
		};
	}// End while()
}

int main (int argc, char **argv)
{
	char *value = NULL;
	int ret = 0;

	uid_t myuid = getuid();

	if (myuid != AID_ROOT && myuid != AID_SYSTEM)
	{
		DM ("main(), klogcat: uid %d not allowed.\n", myuid);
		return 1;
	}

	parse_args(argc, argv);

	return ret;
}

