#define LOG_TAG "STT:dumpstate"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/prctl.h>
#include <linux/input.h>

#include "common.h"
#include "server.h"
#include "headers/sem.h"
#include "headers/poll.h"
#include "headers/input.h"
#include "headers/process.h"

#define VERSION "3.8"
/*
 * 3.8  : It doesn't launch UTF to dump DQ logs for MIA_C on pressing the vol-up key
 * 3.7  : Delay trigger mini-bugreport timing from 4s to 6s.
 * 3.6  : Don't "power off device when long press power key on MFG ROM".
 * 3.5  : By SSD Storage Ocean's request, change default path from SD Card to eMMC for saving bugreport.
 * 3.4  : Remove trigger dump dq log feature to UserTrialFeedback.
 * 3.3  : Remove check diag_rb flag.
 * 3.2  : support find new diag_rb node in htc_diag.
 * 3.1  : support send intent to trigger dumping dynamic qxdm log while .dqqxdm2sd.stat exists.
 * 3.0  : support dumping QXDM buffer to a file when long pressing vol-up key over 10 seconds.
 * 2.9  : support mini-bugreport.
 * 2.8  : correct the right keycode sequence in do_bugreport
 * 2.7  : remove do_dumpsystrace () because it is done by HtcEventReceiver
 * 2.6  : add do_dumpsystrace() function
 * 2.5  : do not power off device if not yet boot completed when long press power key on MFG ROM.
 * 2.4  : set timeout to hot key debug functions.
 * 2.3  : power off device when long press power key on MFG ROM.
 * 2.2  : accept filename prefix in do_emergency_dump().
 * 2.1  : add do_emergency_dump() function.
 * 2.0  : support specific external storage.
 * 1.9  : save bugreport to htclog/ folder.
 * 1.8  : also vibrate when debug key triggerred.
 * 1.7  : add another debug key.
 * 1.6  : ignore headset and BT event nodes.
 * 1.5  : change key combination.
 * 1.4  : monitor all input devices for key events.
 */

/* custom commands */
#define LOG_GETPATH     ":getpath:"
#define LOG_EMERGENCY   ":emergency:"

#define HOTKEY_DEBUG_TIMEOUT_MS (1000)

static POLL pl;
static int commfd = -1;
static int done = 0;
static char *log_filename = NULL;

static char path [PATH_MAX] = LOG_DIR;

static pthread_mutex_t data_lock = PTHREAD_MUTEX_INITIALIZER;
static sem_t power_lock;
static sem_t volup_lock;

static int is_mfg = 0;

/* runs the vibrator using the given pattern */
static int start_pattern [] = { 150, 0 };
static int end_pattern [] = { 75, 50, 75, 50, 75, 0 };

/* QXDM ram buffer */
static char qxdm_flag_path[256]    = {0};
static char* diag_rb_nodes[] = {
    "/sys/class/diag/diag/diag_rb",
    "/sys/class/htc_diag/htc_diag/diag_rb",
    ""};
static const char *qxdm_ram_buffer_path = "/sys/class/diag/diag/diag_rb_dump";
static const char *qxdm_stat_path = "/data/data/com.htc.android.qxdm2sd/data/.dqqxdm2sd.stat";
static const char *mtk_dq_filename = "/data/data/com.htc.android.ssdtest/mtk_dq/.mtk_dq.stat";

static char* find_diag_rb_node ()
{
    int i;
    /*
     * check modems
     */

    for (i = 0; strlen(diag_rb_nodes[i]) > 0; i ++)
    {
        if (access (diag_rb_nodes[i], R_OK) == 0)
        {
            DM ("find_diag_rb_node: successfully access diag_rb node (%s)!\n", diag_rb_nodes[i]);
            break;
        }
        else
            DM ("find_diag_rb_node: ignore: cannot access diag_rb node (%s)!\n", diag_rb_nodes[i]);
    }

    DM ("find_diag_rb_node: use diag_rb node is %s!\n", diag_rb_nodes[i]);

    return diag_rb_nodes[i];
}

static void vibrate_pattern (int fd, int *pattern)
{
    struct timespec tm;
    char buffer [10];
    int on_time, delay;

    if (fd < 0)
        return;

    while (*pattern)
    {
        /* read vibrate on time */
        on_time = *pattern ++;
        snprintf (buffer, sizeof (buffer), "%d", on_time);
        write (fd, buffer, strlen (buffer));

        /* read vibrate off time */
        delay = *pattern ++;

        if (! delay)
            break;

        delay += on_time;
        tm.tv_sec = delay / 1000;
        tm.tv_nsec = (delay % 1000) * 1000000;
        nanosleep (& tm, NULL);
    }
}

static void auto_select_path (const char *default_path)
{
    const char *ext;

    if (default_path && (dir_write_test (default_path) == 0))
    {
        strncpy (path, default_path, sizeof (path) - 1);
        path [sizeof (path) - 1] = 0;
        return;
    }
    /*
     * By SSD Storage Ocean's request, change default path from SD Card to eMMC for saving bugreport.
     */
    /*
    if ((ext = dir_get_external_storage ()) == NULL)
        return;

    path [0] = 0;

    if (dir_storage_state (ext) == 1)  // mounted
    {
        strcpy (path, ext);
        strcat (path, "/" LOG_FOLDER_NAME);

        // check if the external device is writable
        if (dir_write_test (path) < 0)
            path [0] = 0;
    }
    */
    if (path [0] == 0) strcpy (path, LOG_DIR);
}

static int open_vibrator (void)
{
    int vibrate_fd = open ("/sys/class/timed_output/vibrator/enable", O_WRONLY);

    if (vibrate_fd >= 0)
        fcntl (vibrate_fd, F_SETFD, FD_CLOEXEC);

    return vibrate_fd;
}

static void close_vibrator (int fd)
{
    if (fd >= 0) close (fd);
}

static void update_mfg_flag (void)
{
#if 1
    char buf [PROPERTY_VALUE_MAX];

    memset (buf, 0, sizeof (buf));

    property_get ("ro.bootmode", buf, "unknown");

    is_mfg = (strstr (buf, "factory") != NULL);
#else
    /* for debug */
    is_mfg = 1;
#endif
}

int do_bugreport (char *filename, int buflen)
{
#define    BUGREPORT    "/system/bin/bugreport"
#define    DUMPSTATE    "/system/bin/dumpstate"

    struct stat st;
    char buf [PATH_MAX];
    int vibrate_fd, ret;

    pthread_mutex_lock (& data_lock);

    if (! filename)
    {
        auto_select_path (NULL);
    }
    else
    {
        strncpy (buf, filename, sizeof (buf) - 2);
        buf [sizeof (buf) - 2] = 0;

        if (buf [strlen (buf) - 1] != '/')
            strcat (buf, "/");

        auto_select_path (buf);
    }

    if (log_filename)
        free (log_filename);

    sprintf (buf, "%sdumpstate_" TAG_DATETIME ".txt", path);
    //sprintf (buf, "%sdumpstate_" TAG_DATETIME, path);
    str_replace_tags (buf);

    log_filename = strdup (buf);

    if (stat (BUGREPORT, & st) == 0)
    {
        /*
         * For Cupcake/Donut (bugreport) or Eclair (dumpstate)
         *
         * It seems there is a bug checking file mode for symbolic link, so we check size instead.
         */
    #if 0
        snprintf (buf, sizeof (buf) - 1, "%s -o %s", (S_ISLNK (st.st_mode) ? BUGREPORT : DUMPSTATE), log_filename);
    #else
        struct stat st2;

        if (stat (DUMPSTATE, & st2) < 0)
        {
            DM ("stat " DUMPSTATE ": %s\n", strerror (errno));
            st2.st_size = st.st_size;
        }

        snprintf (buf, sizeof (buf) - 1, "%s > %s 2>&1 ; chmod 666 %s",
                ((st.st_size == st2.st_size) ? BUGREPORT : DUMPSTATE),
                log_filename,
                log_filename);
    #endif
    }
    else
    {
        DM ("stat " BUGREPORT ": %s\n", strerror (errno));

        /* for Platform 1.0 */
        snprintf (buf, sizeof (buf) - 1, DUMPSTATE " %s", log_filename);
    }
    buf [sizeof (buf) - 1] = 0;

    DM ("run [%s]\n", buf);

    vibrate_fd = open_vibrator ();
    vibrate_pattern (vibrate_fd, start_pattern);
    ret = (system (buf) == 0) ? 0 : -1;
    vibrate_pattern (vibrate_fd, end_pattern);

    if (filename && (buflen > 0))
    {
        strncpy (filename, log_filename, buflen - 1);
        filename [buflen - 1] = 0;

        if (access (filename, F_OK) != 0)
        {
            snprintf (filename, buflen - 1, "%s.txt", log_filename);
            filename [buflen - 1] = 0;
        }
    }

    close_vibrator (vibrate_fd);

    DM ("end of [%s]\n", buf);

    pthread_mutex_unlock (& data_lock);
    return ret;
}

int do_emergency_dump (const char *prefix_name, char *iobuf, int iobuflen)
{
    char cmd [PATH_MAX];
    size_t i;

    DM ("do_emergency_dump: begin");

    /* file path */
    snprintf (iobuf, iobuflen, TMP_DIR "%s", prefix_name ? prefix_name : "");
    iobuf [iobuflen - 1] = 0;

    for (i = 0; i < strlen (iobuf); i ++)
    {
        /* to prevent the prefix string contains hacker stuff */
        if ((iobuf [i] == ';') || (iobuf [i] == '|'))
            iobuf [i] = '_';
    }

    snprintf (cmd, sizeof (cmd), "/system/bin/dmesg > %skernel.txt", iobuf);
    cmd [sizeof (cmd) - 1] = 0;

    DM ("do_emergency_dump: %s", cmd);
    system (cmd);

    snprintf (cmd, sizeof (cmd), "%sdevice.txt", iobuf);
    cmd [sizeof (cmd) - 1] = 0;

    unlink (cmd);

    snprintf (cmd, sizeof (cmd), "/system/bin/logcat -d -v time -b main -f %sdevice.txt", iobuf);
    cmd [sizeof (cmd) - 1] = 0;

    DM ("do_emergency_dump: %s", cmd);
    system (cmd);

    DM ("do_emergency_dump: end");
    return 0;
}

static void do_hotkey_debug (void)
{
    int vibrate_fd;
    char cmd [PATH_MAX];
    sprintf (cmd,  "touch_debug -ds");
    cmd [sizeof (cmd) - 1] = 0;
    DM ("do launch ssdptouch ap");
    system (cmd);

    vibrate_fd = open_vibrator ();
    vibrate_pattern (vibrate_fd, start_pattern);
    close_vibrator (vibrate_fd);
}

static unsigned long long get_cur_ns (void)
{
    struct timespec ts;
    clock_gettime (CLOCK_REALTIME, & ts);
#if 1
    unsigned long long ret = ((unsigned long long) ts.tv_sec * 1000000000) + ts.tv_nsec;
    DM ("get_cur_ns=%llu\n", ret);
    return ret;
#else
    return (((unsigned long long) ts.tv_sec * 1000000000) + ts.tv_nsec);
#endif
}

#define QXDM_SUPPORT_CAT        (0)
#define QXDM_TRANS_BUFFER_SIZE  (4096)

static void qxdm_dump_to_file ()
{
    char log_path [PATH_MAX];
    char *buffer = NULL;
    int fdr = -1;
    int fdw = -1;
    int count = 0;

    if (dir_create_recursive (LOG_DIR) < 0)
        return;

    snprintf (log_path, sizeof (log_path), "%s%s", LOG_DIR, "qxdm_ram_" TAG_DATETIME ".dm");
    log_path [sizeof (log_path) - 1] = 0;

    str_replace_tags (log_path);

    if ((fdw = open (log_path, O_CREAT | O_RDWR | O_APPEND, 0666)) < 0)
    {
        DM ("open [%s]: %s\n", log_path, strerror (errno));
        goto end;
    }

#if QXDM_SUPPORT_CAT
    if ((fdr = open (qxdm_ram_buffer_path, O_RDONLY)) < 0)
    {
        DM ("open [%s]: %s\n", qxdm_ram_buffer_path, strerror (errno));
        goto end;
    }
#endif

    if ((buffer = malloc (QXDM_TRANS_BUFFER_SIZE)) == NULL)
    {
        DM ("alloc: %s\n", strerror (errno));
        goto end;
    }

    DM ("dump qxdm to [%s] ...\n", log_path);

    for (;;)
    {
#if ! QXDM_SUPPORT_CAT
        if ((fdr = open (qxdm_ram_buffer_path, O_RDONLY)) < 0)
        {
            DM ("open [%s]: %s\n", qxdm_ram_buffer_path, strerror (errno));
            break;
        }
#endif

        count = read (fdr, buffer, QXDM_TRANS_BUFFER_SIZE);

        if (count < 0)
        {
            DM ("read [%s]: %s\n", qxdm_ram_buffer_path, strerror (errno));
            break;
        }

        if (count == 0)
        {
            DM ("read 0, no more data.");
            break;
        }

        if (write (fdw, buffer, count) < 0)
        {
            DM ("write [%s]: %s\n", log_path, strerror (errno));
            break;
        }

#if ! QXDM_SUPPORT_CAT
        close (fdr);
        fdr = -1;
#endif
    }

end:;
    if (buffer)
    {
        free (buffer);
        buffer = NULL;
    }

    if (fdr >= 0)
    {
        close (fdr);
    }

    if (fdw >= 0)
    {
        close (fdw);
    }
}

#if 0
static int qxdm_read_flag ()
{
    char buffer [PATH_MAX];
    FILE *fp;

    if ((fp = fopen (qxdm_flag_path, "rb")) == NULL)
    {
        DM ("open [%s]: %s\n", qxdm_flag_path, strerror (errno));
        return 0;
    }

    fread (buffer, sizeof (char), sizeof (buffer), fp);
    fclose (fp);

    int ret = 0;
    if (strncasecmp (buffer, "diag_rb_enable=1", 16) == 0)
        ret = 1;
    else if (strncasecmp (buffer, "diag_rb_enable=2", 16) == 0)
        ret = 2;
    else if (strncasecmp (buffer, "diag_rb_enable=3", 16) == 0)
        ret = 3;
    else
        ret = 0;

    return ret;
}

static int qxdm_set_flag (int val)
{
    char buffer [8];
    int fd = -1;
    int err = 0;

    snprintf (buffer, sizeof (buffer), "%d", val);
    buffer [sizeof (buffer) - 1] = 0;

    if ((fd = open (qxdm_flag_path, O_RDWR , 0666)) < 0)
    {
        err = errno;
        DM ("open [%s]: %s\n", qxdm_flag_path, strerror (errno));
        goto end;
    }

    if (write (fd, buffer, sizeof (buffer)) < 0)
    {
        err = errno;
        DM ("write [%s]: %s\n", qxdm_flag_path, strerror (errno));
        goto end;
    }

end:;
    if (fd >= 0)
    {
        close (fd);
    }

    return err;
}
#endif

static int check_rom_version(char* prop_key)
{
    int n_val = 0, ret_val = 0;
    char value[PROPERTY_VALUE_MAX] = {0};

    property_get(prop_key, value, "");

    if(1 == sscanf(value, "%*d.%*d.%d.%*d", &n_val))
    {
        if ((n_val == 999) || ((n_val >= 90000) && (n_val < 92000)))
        {
            ret_val = 1;
        }
    }

    DM ("[check_rom_version]check key:%s=%s, ret=%d", prop_key, value, ret_val);
    return ret_val;
}

static int is_user_trial_rom()
{
    return check_rom_version("ro.aa.romver");
}

static int is_debug()
{
    return check_rom_version("persist.aa.romver");;
}

static int is_user_trial_rom_or_debug()
{
    return (is_user_trial_rom() + is_debug());
}

static void *thread_volup (void *UNUSED_VAR (null))
{
    int vibrate_fd, enable = 0;
    char log_path [PATH_MAX];
    char value[PROPERTY_VALUE_MAX] = {0};
    DM ("trigger volume-up key begin");

    pthread_detach (pthread_self ());

    timed_wait (& volup_lock, 5000 /* 5 seconds */);

    if (errno != ETIMEDOUT)
    {
        DM ("trigger volume-up key abort: %s", strerror (errno));
        return NULL;
    }

    DM ("trigger volume-up key end");

    if(is_user_trial_rom_or_debug())
    {
#if 0
        // MTK solution
        property_get("ro.mediatek.platform", value, "");
        if(strlen(value))
        {
            // check enabled?
            if(access (mtk_dq_filename, F_OK) == 0)
            {
                vibrate_fd = open_vibrator ();
                vibrate_pattern (vibrate_fd, start_pattern);
                close_vibrator (vibrate_fd);

                system ("/system/bin/audio_setparameter DQ_dump=true");
            }
            return NULL;
        }
        else
        {
            // NV (T1)
            property_get("persist.audio.dq", value, "0");

            if (dir_create_recursive (LOG_DIR) >= 0)
            {
                if (atoi(value) == 1)
                {
                    vibrate_fd = open_vibrator ();
                    vibrate_pattern (vibrate_fd, start_pattern);
                    close_vibrator (vibrate_fd);

                    snprintf (log_path, sizeof (log_path), "audio_setparameter LVVEFA_TraCER_DumpRxInputTracesToFile=%s%s", LOG_DIR, "qxdm_ram_RxInput" TAG_DATETIME ".dm");
                    log_path [sizeof (log_path) - 1] = 0;
                    str_replace_tags (log_path);
                    system (log_path);

                    snprintf (log_path, sizeof (log_path), "audio_setparameter LVVEFA_TraCER_DumpRxOutTracesToFile=%s%s", LOG_DIR, "qxdm_ram_RxOutput" TAG_DATETIME ".dm");
                    log_path [sizeof (log_path) - 1] = 0;
                    str_replace_tags (log_path);
                    system (log_path);

                    snprintf (log_path, sizeof (log_path), "audio_setparameter LVVEFA_TraCER_DumpTxInputTracesToFile=%s%s", LOG_DIR, "qxdm_ram_TxInput" TAG_DATETIME ".dm");
                    log_path [sizeof (log_path) - 1] = 0;
                    str_replace_tags (log_path);
                    system (log_path);

                    snprintf (log_path, sizeof (log_path), "audio_setparameter LVVEFA_TraCER_DumpTxOutputTracesToFile=%s%s", LOG_DIR, "qxdm_ram_TxOutput" TAG_DATETIME ".dm");
                    log_path [sizeof (log_path) - 1] = 0;
                    str_replace_tags (log_path);
                    system (log_path);

                    snprintf (log_path, sizeof (log_path), "audio_setparameter LVVEFA_TraCER_DumpTxErTracesToFile=%s%s", LOG_DIR, "qxdm_ram_EchoRef" TAG_DATETIME ".dm");
                    log_path [sizeof (log_path) - 1] = 0;
                    str_replace_tags (log_path);
                    system (log_path);
                    return NULL;
                }
            }
        }

        if (access (qxdm_stat_path, F_OK) != 0)
        {

            if (access (qxdm_ram_buffer_path, R_OK) != 0)
            {
                DM ("cannot access [%s], skip...", qxdm_ram_buffer_path);
                return NULL;
            }

            DM ("dump qxdm from [%s] begin\n", qxdm_ram_buffer_path);
        }

    /*    enable = qxdm_read_flag ();
        if (enable != 3)
        {
            DM ("get flag = %d, do not dumping log.\n", enable);
            return NULL;
        }
    */
        /*
         * Requested by WSD Tom Lee. Inform modem when user triggered the audio qxdm dump function.
         */
        system_in_thread ("am broadcast -a com.htc.android.ssdtest.SEND_AT_COMMAND -n com.htc.android.ssdtest/.HtcEventReceiver -e command \"at@nosound=1\"");
#endif

        char data [PROPERTY_VALUE_MAX];
        int isDQLogDumped = 1;

        if ((data [0]))
        {
            memset (data, 0, sizeof (data));
            property_get ("ro.build.product", data, "");

            if (strcmp (data, "htc_miac") == 0)
            {
                isDQLogDumped = 0;
                DM ("The product, %s doesn't launch UTF to dump DQ logs.", data);
            }
        }

        if (isDQLogDumped == 1)
        {
            vibrate_fd = open_vibrator ();
            vibrate_pattern (vibrate_fd, start_pattern);
            close_vibrator (vibrate_fd);

            DM ("launch UTF to dump DQ log...");
            system_in_thread ("am broadcast -a com.htc.action.SCREEN_CAPTURE_BG");
        }

#if 0
        if (access (qxdm_stat_path, F_OK) == 0)
        {
            DM ("dump dynamic qxdm by send request to qxdm daemon\n");
            system_in_thread ("am broadcast -a com.htc.android.qxdm2sd.DQLOGGER --ei DumpLog 1");
        }
        else
        {
            if (qxdm_set_flag (1) == 0)
            {
                DM ("disable before dump: get flag = %d, origin = %d\n", qxdm_read_flag (), enable);

                qxdm_dump_to_file ();

                if (enable)
                {
                    qxdm_set_flag (enable);

                    DM ("re-enable: get flag = %d\n", qxdm_read_flag ());
                }
            }
        }
#endif
    }

    DM ("dump qxdm end\n");
    return NULL;
}

static void *thread_power (void *UNUSED_VAR (null))
{
    DM ("trigger power key begin");

    pthread_detach (pthread_self ());

    timed_wait (& power_lock, 6000 /* 6 seconds mini-bugreport */);

    if (errno != ETIMEDOUT)
    {
        DM ("trigger power key abort: %s", strerror (errno));
        return NULL;
    }

    DM ("trigger power key end");

    char cmd [PATH_MAX];
    int ret;

    snprintf (cmd, sizeof (cmd), "/system/bin/dumpstate -m -q -o /data/htclog/mini-bugreport_" TAG_DATETIME);
    cmd [sizeof (cmd) - 1] = 0;

    str_replace_tags (cmd);

    DM ("dump mini-bugreport begin [%s]\n", cmd);

    ret = system (cmd);

    DM ("dump mini-bugreport end [%d]\n", ret);

    return NULL;
}

static void *thread_main (void *UNUSED_VAR (null))
{
    const char *white_list [] = {
        "keypad",
        "Keypad",
        "keys",
        "pwrkey",
        "qpnp_pon",
        "hbtp_vm",
        NULL
    };

    const char *black_list [] = {
        "dummy",
        "projector",
        NULL
    };

    pthread_t power = (pthread_t) -1;
    pthread_t volup = (pthread_t) -1;

    struct input_event ie;
    int nr, trigger = -1, debug_trigger = -1;
    int timeout_ms;
    int *fds;

    prctl (PR_SET_NAME, (unsigned long) "dumpstate:key", 0, 0, 0);

    fds = open_input_devices (white_list, black_list);

    if (! fds)
        return NULL;

    DM ("got %d fds\n", fds [0]);

    timeout_ms = -1;

    while (! done)
    {
        nr = poll_multiple_wait (& pl, timeout_ms, & fds [1], fds [0]);

        /*
         * nr == 0 : timeout or poll_break
         */
        if (nr == 0)
        {
            if (done)
            {
                break;
            }

            DM ("timeout clear triggers\n");

            timeout_ms = -1;
            trigger = -1;
            debug_trigger = -1;

            continue;
        }

        /*
         * nr < 0 : error
         */
        if (nr < 0)
        {
            break;
        }

        DM ("got event from fds #%d\n", nr);

        while (read (fds [nr], & ie, sizeof (ie)) == sizeof (ie))
        {
            int is_combine_key = 0;

            DM ("Input Event ==> type=%d, code=%d, value=%d\n", ie.type, ie.code, ie.value);

            if (ie.type == EV_KEY)
            {
                DM ("fds #%d got EV_KEY, type = %d, code=%d, value=%d\n", nr, ie.type, ie.code, ie.value);

                if (ie.code == KEY_POWER)
                {
                    if ((ie.value != 0 /* KEY DOWN */))
                    {
                        if (power == (pthread_t) -1)
                        {
                            sem_init (& power_lock, 0, 0);

                            if (pthread_create (& power, NULL, thread_power, NULL) < 0)
                            {
                                DM ("pthread_create power: %s\n", strerror (errno));
                            }
                        }
                    }
                    else // if ((ie.value == 0 /* KEY UP */))
                    {
                        if (power != (pthread_t) -1)
                        {
                            sem_post (& power_lock);
                            power = -1;
                        }
                    }
                }
                else if (ie.code == KEY_VOLUMEUP)
                {
                    is_combine_key = 1; /* volume keys are also used for debug combination */

                    if ((ie.value != 0 /* KEY DOWN */))
                    {
                        if (volup == (pthread_t) -1)
                        {
                            sem_init (& volup_lock, 0, 0);

                            if (pthread_create (& volup, NULL, thread_volup, NULL) < 0)
                            {
                                DM ("pthread_create volup: %s\n", strerror (errno));
                            }
                        }
                    }
                    else // if ((ie.value == 0 /* KEY UP */))
                    {
                        if (volup != (pthread_t) -1)
                        {
                            sem_post (& volup_lock);
                            volup = -1;
                        }
                    }
                }
                else
                {
                    is_combine_key = 1; /* allow other keys can be used for combination */
                }

                if (! is_combine_key)
                    break;
            }

            /* ignore non-key and key down events */
            if ((ie.type != EV_KEY) || (ie.value != 0 /* not KEY UP */))
                break;

            if (power != (pthread_t) -1)
            {
                sem_post (& power_lock);
                power = -1;
            }

            if (ie.code != KEY_VOLUMEUP)
            {
                if (volup != (pthread_t) -1)
                {
                    sem_post (& volup_lock);
                    volup = -1;
                }
            }

            timeout_ms = HOTKEY_DEBUG_TIMEOUT_MS;

            trigger ++;

            debug_trigger ++;

            DM ("==> bugreport [%d], touchdebug [%d]\n", trigger, debug_trigger);

            /* up down up down up up down down down */
            switch (trigger)
            {
            case 0: if (ie.code != KEY_VOLUMEUP)    trigger = -1; break;
            case 1: if (ie.code != KEY_VOLUMEDOWN)    trigger = -1; break;
            case 2: if (ie.code != KEY_VOLUMEUP)    trigger = -1; break;
            case 3: if (ie.code != KEY_VOLUMEDOWN)    trigger = -1; break;
            case 4: if (ie.code != KEY_VOLUMEUP)    trigger = -1; break;
            case 5: if (ie.code != KEY_VOLUMEUP)    trigger = -1; break;
            case 6: if (ie.code != KEY_VOLUMEDOWN)    trigger = -1; break;
            case 7: if (ie.code != KEY_VOLUMEDOWN)    trigger = -1; break;
            case 8: if (ie.code != KEY_VOLUMEDOWN)  trigger = -1; else do_bugreport (NULL, 0); break;
            default: trigger = -1;
            }

            /* down up down up down down up up up */
            switch (debug_trigger)
            {
            case 0: if (ie.code != KEY_VOLUMEDOWN)    debug_trigger = -1; break;
            case 1: if (ie.code != KEY_VOLUMEUP)    debug_trigger = -1; break;
            case 2: if (ie.code != KEY_VOLUMEDOWN)    debug_trigger = -1; break;
            case 3: if (ie.code != KEY_VOLUMEUP)    debug_trigger = -1; break;
            case 4: if (ie.code != KEY_VOLUMEDOWN)    debug_trigger = -1; break;
            case 5: if (ie.code != KEY_VOLUMEDOWN)    debug_trigger = -1; break;
            case 6: if (ie.code != KEY_VOLUMEUP)    debug_trigger = -1; break;
            case 7: if (ie.code != KEY_VOLUMEUP)    debug_trigger = -1; break;
            case 8: if (ie.code != KEY_VOLUMEUP)    debug_trigger = -1; else do_hotkey_debug (); break;
            default: debug_trigger = -1;
            }

            break;
        }
    }

    close_input_devices (fds);
    fds = NULL;

    pthread_mutex_lock (& data_lock);

    done = 1;

    if (log_filename)
    {
        free (log_filename);
        log_filename = NULL;
    }

    pthread_mutex_unlock (& data_lock);
    return NULL;
}

int dumpstate_main (int server_socket)
{
    pthread_t working = (pthread_t) -1;

    char buffer [PATH_MAX + 16];
    int ret = 0;
	DM ("dumpstate_main\n");

    update_mfg_flag ();

    strcpy(qxdm_flag_path, find_diag_rb_node());

    /* start log */
    if (poll_open (& pl) < 0)
    {
        DM ("poll_open: %s\n", strerror (errno));
        goto end;
    }
    if (pthread_create (& working, NULL, thread_main, NULL) < 0)
    {
        DM ("pthread_create: %s\n", strerror (errno));
        goto end;
    }

    while (! done)
    {
        DM ("waiting connection ...\n");

        ret = wait_for_connection (server_socket);

        if (ret < 0)
        {
            DM ("accept client connection failed!\n");
            continue;
        }

        pthread_mutex_lock (& data_lock);
        commfd = ret;
        pthread_mutex_unlock (& data_lock);

        DM ("connection established.\n");

        for (;;)
        {
            memset (buffer, 0, sizeof (buffer));

            ret = read (commfd, buffer, sizeof (buffer));

            if (ret <= 0)
            {
                DM ("read command error (%d)! close connection!\n", ret);
                break;
            }

            buffer [sizeof (buffer) - 1] = 0;

            ret = 0;

            DM ("read command [%s].\n", buffer);

            if (CMP_CMD (buffer, CMD_ENDSERVER))
            {
                pthread_mutex_lock (& data_lock);
                done = 1;
                pthread_mutex_unlock (& data_lock);
                break;
            }

            if (CMP_CMD (buffer, CMD_GETVER))
            {
                strcpy (buffer, VERSION);
            }
            else if (CMP_CMD (buffer, LOG_GETPATH))
            {
                pthread_mutex_lock (& data_lock);
                if (! log_filename)
                {
                    strcpy (buffer, path);
                }
                else
                {
                    strcpy (buffer, log_filename);
                }
                pthread_mutex_unlock (& data_lock);
            }
            else if (CMP_CMD (buffer, LOG_EMERGENCY))
            {
                char prefix [16];
                MAKE_DATA (buffer, LOG_EMERGENCY);
                strncpy (prefix, buffer, sizeof (prefix));
                prefix [sizeof (prefix) - 1] = 0;
                do_emergency_dump (prefix, buffer, sizeof (buffer));
            }
            else
            {
                DM ("unknown command [%s]!\n", buffer);
                ret = -1;
                buffer [0] = 0;
            }

            /* command response */
            if (buffer [0] == 0)
                sprintf (buffer, "%d", ret);

            DM ("send response [%s].\n", buffer);

            pthread_mutex_lock (& data_lock);
            if (write (commfd, buffer, strlen (buffer)) != (ssize_t) strlen (buffer))
            {
                DM ("send response [%s] to client failed!\n", buffer);
            }
            pthread_mutex_unlock (& data_lock);
        }

        pthread_mutex_lock (& data_lock);
        close (commfd);
        commfd = -1;
        pthread_mutex_unlock (& data_lock);
    }

end:;
    done = 1;

    poll_break (& pl);

    if (working != (pthread_t) -1)
        pthread_join (working, NULL);

    poll_close (& pl);

    /* reset done flag */
    done = 0;

    return 0;
}
