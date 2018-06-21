#define	LOG_TAG	"STT:Clientservice"

#define	VERSION	"2.9"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <getopt.h>

#include "common.h"
#include "client.h"
#include "headers/process.h"
#include "headers/conf.h"

#define	CONF_VERSION		"3"
#define	USE_LOCAL_SOCKET	(0)

static const char *LOGGER_CONFIG_FILENAME = GHOST_DIR "SystemLoggers.conf";

static int run_service (int port, const char *command, char *response, int length)
{
	char buffer [MAX_NAME_LEN + MAX_ARGUMENT_LEN];

	int sockfd = -1;
	int err = -1;

#if USE_LOCAL_SOCKET
	sockfd = local_init_client (port);
#else
	sockfd = init_client (LOCAL_IP, port);
#endif

	if (sockfd < 0)
	{
		DM ("cannot connect to service!\n");
		goto end;
	}

	/* run service */
	strncpy (buffer, command, sizeof (buffer) - 1);
	buffer [sizeof (buffer) - 1] = 0;

	DM ("send [%s] to port [%d] ...\n", buffer, port);

	if (write (sockfd, buffer, strlen (buffer)) != (ssize_t) strlen (buffer))
	{
		DM ("send command [%s] to service failed!\n", buffer);
		goto end;
	}

	/* get response */
	memset (response, 0, length);

	if (read (sockfd, response, length - 1) < 0)
	{
		DM ("get resposne failed!\n");
		goto end;
	}

	err = 0;
end:;
	if (sockfd >= 0)
	{
		close (sockfd);
	#if USE_LOCAL_SOCKET
		local_destroy_client (port);
	#endif
	}

	return err;
}

static void usage (const char *prgpath)
{
	const char *prg = strrchr (prgpath, '/');

	prg = prg ? prg + 1 : prgpath;

	DM ("\n");
	DM ("htcservice v" VERSION "\n");
	DM ("\n");
	DM ("usage:\n");
	DM ("\n");
	//DM ("  %s data <rotate_size_mb rotate_count size_limitation_mb size_reserved_mb log_path auto_start_bool auto_clear_bool auto_compress_bool loggers...>\n", prg);
	DM ("  %s conf name=value [name=value]...\n", prg);
	DM ("  %s status htclog\n", prg);
	DM ("  %s logdata <file." LOG_DATA_EXT ">\n", prg);
	DM ("    or\n");
	DM ("  %s [-s service_name | -p service_port] command\n", prg);
	DM ("\n");
	DM ("  if no given service_name or service_port, the command will be sent to service server.\n");
	DM ("\n");
	DM ("  the command option can be a service command or an alias command listed below:\n");
	DM ("\n");
	DM ("    list    : the same as " CMD_LISTSERVICES "\n");
	DM ("    ver     : the same as " CMD_GETVER "\n");
	DM ("    end     : the same as " CMD_ENDSERVER "\n");
	DM ("\n");
	DM ("example:\n");
	DM ("\n");
	//DM ("  %s data 5 40 1000 100 auto true true false logdevice logradio logevents logkmsg dumplastkmsg\n", prg);
	//DM ("  %s data version\n", prg);
	DM ("  %s conf Version=3 AutoStart=true LogPath=auto RotateSizeMB=auto LimitedTotalLogSizeMB=auto ReservedStorageSizeMB=auto Compress=false\n", prg);
	DM ("  %s status htclog\n", prg);
	DM ("  %s logdata /data/htclog/.session." LOG_DATA_EXT "\n", prg);
	DM ("  %s -s logctl :stop:\n", prg);
	DM ("  %s -s logctl :syncconf:\n", prg);
	DM ("  %s -s logctl :run:\n", prg);
	DM ("\n");
}

static int create_data_file (int argc, char **argv)
{
	FILE *fp;
	char *log_path = NULL, *auto_start = NULL, *auto_clear = NULL, *auto_compress = NULL;
	int file_size = -1, file_rotate = -1, size_limit_mb = -1, size_reserved_mb = -1, logger_count;
	int reserved_in_percentage = 0;
	int i = 1, loggeri;

	if ((++ i) >= argc) goto err_arg;
	file_size = atoi (argv [i]);
	if (file_size <= 0)
	{
		DM ("wrong file size (%d)!\n", file_size);
		return -1;
	}

	if ((++ i) >= argc) goto err_arg;
	file_rotate = atoi (argv [i]);
	if (file_rotate < 0)
	{
		DM ("wrong rotation count (%d)!\n", file_rotate);
		return -1;
	}

	if ((++ i) >= argc) goto err_arg;
	size_limit_mb = atoi (argv [i]);
	if (size_limit_mb <= 0)
	{
		DM ("wrong log size limitation value (%d)!\n", size_limit_mb);
		return -1;
	}

	if ((++ i) >= argc) goto err_arg;
	if (argv [i][0] == 'p')
	{
		reserved_in_percentage = 1;

		size_reserved_mb = atoi (& argv [i][1]);

		if ((size_reserved_mb < 0) || (size_reserved_mb > 100))
		{
			DM ("wrong reserved storage size percentage value (%d)!\n", size_reserved_mb);
			return -1;
		}
	}
	else
	{
		size_reserved_mb = atoi (argv [i]);

		if (size_reserved_mb <= 0)
		{
			DM ("wrong reserved storage size value (%d)!\n", size_reserved_mb);
			return -1;
		}
	}

	if ((++ i) >= argc) goto err_arg;
	if ((! strcmp (argv [i], "auto")) || (! strcmp (argv [i], "internal")) || (! strcmp (argv [i], "external")) || (! strcmp (argv [i], "phone")) || (argv [i][0] == '/'))
	{
		log_path = argv [i];
	}
	else
	{
		DM ("wrong log path value (%s)!\n", argv [i]);
		return -1;
	}

	if ((++ i) >= argc) goto err_arg;
	if ((! strcmp (argv [i], "true")) || (! strcmp (argv [i], "false")))
	{
		auto_start = argv [i];
	}
	else
	{
		DM ("wrong auto start value (%s)!\n", argv [i]);
		return -1;
	}

	if ((++ i) >= argc) goto err_arg;
	if ((! strcmp (argv [i], "true")) || (! strcmp (argv [i], "false")))
	{
		auto_clear = argv [i];
	}
	else
	{
		DM ("wrong auto clear value (%s)!\n", argv [i]);
		return -1;
	}

	if ((++ i) >= argc) goto err_arg;
	if ((! strcmp (argv [i], "true")) || (! strcmp (argv [i], "false")))
	{
		auto_compress = argv [i];
	}
	else
	{
		DM ("wrong auto compress value (%s)!\n", argv [i]);
		return -1;
	}

	if ((++ i) >= argc) goto err_arg;

	fp = fopen (LOGGER_CONFIG_FILENAME, "wb");

	if (! fp)
	{
		DM ("%s: %s\n", LOGGER_CONFIG_FILENAME, strerror (errno));
		return -1;
	}

	fprintf (fp,
		"Patched = 1\n"
		"Version = 2\n"
		"AutoStart = %s\n"
		"RotateSizeMB = %d\n"
		"RotateCount = %d\n"
		"LimitedTotalLogSizeMB = %d\n"
		"ReservedStorageSizeMB = %s%d\n"
		"LogPath = %s\n"
		"CheckBoxAutoStart = %s\n"
		"AutoClear = %s\n"
		"Compress = %s\n"
		"SelectedLoggers = "
		, auto_start, file_size, file_rotate, size_limit_mb, reserved_in_percentage ? "p" : "", size_reserved_mb, log_path, auto_start, auto_clear, auto_compress);

	loggeri = i;

	for (logger_count = 0; i < argc; i ++)
	{
		fprintf (fp, "[%s]", argv [i]);

		if (strcmp (argv [i], "dumplastkmsg") != 0) logger_count ++;
	}

	if (logger_count == 0)
		logger_count = 1;

	i = size_limit_mb / (file_size * (file_rotate + 1) * logger_count);

	if (i <= 3)
	{
		i = 3;
	}

	fprintf (fp, "\nSessionCount = %d\n", i);

	for (i = loggeri; i < argc; i ++)
	{
		fprintf (fp, "Logger.%s = true\n", argv [i]);
	}

	fclose (fp);

	return 0;

err_arg:;
	DM ("not enough arguments!\n");
	return -1;
}

static int create_data_conf (int argc, char **argv)
{
	char *pn, *pv;
	int i;

	CONF *conf = NULL;

	if (argc < 3)
	{
		DM ("no given settings!\n");
		return -1;
	}

	if ((access (LOGGER_CONFIG_FILENAME, R_OK) != 0) || ((conf = conf_load_from_file (LOGGER_CONFIG_FILENAME)) == NULL))
	{
		conf = conf_new (LOGGER_CONFIG_FILENAME);

		if (! conf)
		{
			DM ("failed to create a new config!\n");
			return -1;
		}
	}

	for (i = 2; i < argc; i ++)
	{
		pn = strdup (argv [i]);

		if (! pn)
		{
			DM ("overwrite configs: no memory!\n");
			continue;
		}

		if ((pv = strchr (pn, '=')) == NULL)
		{
			DM ("overwrite configs: [%s] is not a name=value pair!\n", pn);
			free (pn);
			continue;
		}

		*pv ++ = 0;

		DM ("overwrite configs: [%s]=[%s]\n", pn, pv);

		conf_set (conf, pn, pv);

		free (pn);
	}

	conf_dump (conf);
	conf_save_to_file (conf);
	conf_destroy (conf);
	return 0;
}

static int dump_status (int argc, char **argv)
{
	if (argc != 3)
	{
		DM ("invalid arguments!\n");
		return -1;
	}

	if (strcmp (argv [2], "htclog") == 0)
	{
		int enabled = 0;

		GLIST_NEW (head);
		GLIST_NEW (ptr);

		PID_INFO *pi;

		pid_t server_pid = 0;

		head = find_all_pids ();

		for (ptr = head; ptr; ptr = GLIST_NEXT (ptr))
		{
			pi = GLIST_DATA (ptr);

			if (strcmp (pi->bin, "htcserviced") == 0)
			{
				server_pid = pi->pid;
				break;
			}
		}

		for (ptr = head; ptr; ptr = GLIST_NEXT (ptr))
		{
			pi = GLIST_DATA (ptr);

			if ((pi->ppid == server_pid) && (strcmp (pi->bin, "logcat") == 0))
			{
				enabled = 1;
				break;
			}
		}

		DM ("%s\n", enabled ? "1" : "0");

		glist_clear (& head, free);
		return 0;
	}

	DM ("unknown target [%s]!\n", argv [2]);
	return -1;
}

static void show_info (char *buffer, int buflen, int argc, char **argv)
{
	char info [512];
	size_t len;
	pid_t ppid, pppid;
	int i;

	bzero (buffer, buflen);

	snprintf (info, sizeof (info), "version=%s", VERSION);
	info [sizeof (info) - 1] = 0;
	len = strlen (info);

	/* pids */
	ppid = getppid ();
	pppid = getpppid ();
	get_pid_name (ppid, & buffer [0], buflen >> 1);
	get_pid_name (pppid, & buffer [buflen >> 1], buflen >> 1);
	buffer [buflen - 1] = 0;
	buffer [(buflen >> 1) - 1] = 0;
	snprintf (& info [len], sizeof (info) - len - 1, ", pid=%d, ppid=%d (%s), pppid=%d (%s),", getpid (), ppid, buffer, pppid, & buffer [buflen >> 1]);
	info [sizeof (info) - 1] = 0;
	len = strlen (info);

	/* args */
	for (i = 0; i < argc; i ++)
	{
		size_t l = strlen (argv [i]);

		if ((l > 0) && ((len + l + 1) < sizeof (info)))
		{
			snprintf (& info [len], sizeof (info) - len - 1, " %s", argv [i]);
			len += l + 1;
		}
	}
	info [sizeof (info) - 1] = 0;

	//LOGD ("%s", info);

	file_log (LOG_TAG ": %s\n", info);
}

#include "logdata.c"

int main (int argc, char **argv)
{
	char buffer [2048];
	char *command = NULL;
	char *service = NULL;
	int port = -1;
	int i;

	show_info (buffer, sizeof (buffer), argc, argv);

	if ((argc > 1) && (strcasecmp (argv [1], "data") == 0))
	{
		if (argc > 2)
		{
			if (strcasecmp (argv [2], "version") == 0)
			{
				DM (CONF_VERSION "\n");
				return 0;
			}
		}
		return create_data_file (argc, argv);
	}

	if ((argc > 1) && (strcasecmp (argv [1], "conf") == 0))
	{
		return create_data_conf (argc, argv);
	}

	if ((argc > 1) && (strcasecmp (argv [1], "status") == 0))
	{
		return dump_status (argc, argv);
	}

	if ((argc > 1) && (strcasecmp (argv [1], "logdata") == 0))
	{
		return dump_logdata (argc, argv);
	}

	if ((argc > 1) && (strcasecmp (argv [1], "ats") == 0))
	{
		int ats_argc = 15;
		char *ats_argv [] = {"client", "data", "10", "250", "10000", "20", "auto", "true", "true", "false", "logdevice", "logradio", "logevents", "logkmsg", "dumplastkmsg"};
		long long uptime;
		int fd;
		int ret;

		DM ("wait for uptime 180 ...\n");
		ret = -1;
		uptime = 0;
		while (1)
		{
			if ((fd = open ("/proc/uptime", O_RDONLY)) < 0)
			{
				DM ("/proc/uptime: %s\n", strerror (errno));
				return ret;
			}
			if (read (fd, buffer, sizeof (buffer)) < 0)
			{
				DM ("/proc/uptime: %s\n", strerror (errno));
				return ret;
			}
			close (fd);
			command = strchr (buffer, '.');
			if (command)
			{
				*command = 0;
			}
			uptime = atoll (buffer);
			DM ("  uptime %lld\n", uptime);
			if ((uptime <= 0) || (uptime > 180))
				break;
			sleep (1);
		}

		DM ("kill processes ...\n");
		strncpy (buffer, argv [0], sizeof (buffer));
		buffer [sizeof (buffer) - 1] = 0;
		command = strrchr (buffer, '/');
		if (command)
		{
			strcpy (command + 1, "htccleaner process");
			if ((ret = system (buffer)) < 0)
				return ret;
		}

		DM ("generate config file ...\n");
		if ((ret = create_data_file (ats_argc, ats_argv)) < 0)
			return ret;

		DM ("try to unlock keyguard ...\n");
		system ("am start -n com.htc.android.ssdtest/.PowerTest");

		DM ("launch tool main activity ...\n");
		system ("am start -n com.htc.android.ssdtest/.MainActivity");

		return ret;
	}

	for (i = 1; i < argc; i ++)
	{
		if (! strcmp (argv [i], "-h"))
		{
			usage (argv [0]);
			return 1;
		}
		else if (! strcmp (argv [i], "-p"))
		{
			if (++ i == argc)
				break;

			port = atoi (argv [i]);

			if (port <= 0)
			{
				DM ("invalid port (%s)!\n", argv [i]);
			}
		}
		else if (! strcmp (argv [i], "-s"))
		{
			if (++ i == argc)
				break;

			service = argv [i];
		}
		else
		{
			command = argv [i];
		}
	}

	if (! command)
	{
		DM ("no command was specified!\n");
		usage (argv [0]);
		return -1;
	}

	if (! IS_CMD (command))
	{
		if (! strcmp (command, "list"))
		{
			command = CMD_LISTSERVICES;
		}
		else if (! strcmp (command, "ver"))
		{
			command = CMD_GETVER;
		}
		else if (! strcmp (command, "end"))
		{
			command = CMD_ENDSERVER;
		}
		else
		{
			DM ("warning: not a valid service command [%s]!\n", command);
		}
	}

	if (service)
	{
		if (run_service (SOCKET_PORT_SERVICE, service, buffer, sizeof (buffer)) < 0)
		{
			DM ("try to get the port of service [%s] failed!\n", service);
			return -1;
		}

		port = atoi (buffer);

		if (port <= 0)
		{
			DM ("get the port of service [%s] failed!\n", service);
			return -1;
		}
	}
	else if (port == -1)
	{
		port = SOCKET_PORT_SERVICE;
	}

	if (run_service (port, command, buffer, sizeof (buffer)) < 0)
	{
		DM ("run command failed!\n");
		return -1;
	}

	DM ("response:\n%s\n", buffer);
	DM ("done.\n");
	return 0;
}
