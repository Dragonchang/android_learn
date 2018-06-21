#define	LOG_TAG	"STT:conf"

#define	TEST_LOCALLY	0
/*
 * To TEST_LOCALLY, you need also compile glist.c and str.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#if TEST_LOCALLY
#define	fLOGD	printf
#define	fLOGE	printf
#else
#include <cutils/log.h>
#endif

#include "libcommon.h"
#include "headers/str.h"
#include "headers/glist.h"
#include "headers/conf.h"

static int _compare_data_with_pair (void *name, void *pair)
{
	CONF_PAIR *p = pair;
	if (name && p && p->name) return strcmp (name, p->name);
	return -1;
}

static void _free_pair (void *pair)
{
	CONF_PAIR *p = pair;
	if (p)
	{
		if (p->name) free (p->name);
		if (p->value) free (p->value);
		p->name = NULL;
		p->value = NULL;
		free (p);
	}
}

static CONF_PAIR *_alloc_pair (const char *name, const char *value)
{
	CONF_PAIR *p = malloc (sizeof (CONF_PAIR));
	if (p)
	{
		p->name = strdup (name);
		p->value = strdup (value);
		if ((! p->name) || ! (p->value))
		{
			_free_pair (p);
			p = NULL;
		}
	}
	return p;
}

void conf_destroy (CONF *conf)
{
	if (conf)
	{
		pthread_mutex_lock (& conf->data_lock);
		glist_clear (& conf->pair_list, _free_pair);
		if (conf->path) free (conf->path);
		conf->path = NULL;
		conf->pair_list = NULL;
		pthread_mutex_unlock (& conf->data_lock);
		free (conf);
	}
}

CONF *conf_new (const char *filepath)
{
	CONF *conf = malloc (sizeof (CONF));
	if (conf)
	{
		pthread_mutex_init (& conf->data_lock, NULL);
		conf->path = strdup (filepath);
		conf->pair_list = NULL;
		if (! conf->path)
		{
			conf_destroy (conf);
			conf = NULL;
		}
	}
	return conf;
}

const char *conf_get (CONF *conf, const char *name, const char *default_value)
{
	CONF_PAIR *p;
	if ((! conf) || (! name)) return default_value;
	pthread_mutex_lock (& conf->data_lock);
	p = glist_get (& conf->pair_list, glist_find_ex (& conf->pair_list, (void *) name, _compare_data_with_pair));
	pthread_mutex_unlock (& conf->data_lock);
	return (p == (CONF_PAIR *) -1) ? default_value : p->value;
}

int conf_set (CONF *conf, const char *name, const char *value)
{
	CONF_PAIR *p = NULL;
	if ((! conf) || (! name) || (! value)) return -1;
	pthread_mutex_lock (& conf->data_lock);
	if ((p = glist_get (& conf->pair_list, glist_find_ex (& conf->pair_list, (void *) name, _compare_data_with_pair))) != (CONF_PAIR *) -1)
	{
		char *new = strdup (value);
		if (! new)
		{
			pthread_mutex_unlock (& conf->data_lock);
			return -1;
		}
		free (p->value);
		p->value = new;
		pthread_mutex_unlock (& conf->data_lock);
		return 0;
	}
	if ((p = _alloc_pair (name, value)) == NULL)
		goto failed;
	if (glist_add (& conf->pair_list, p) < 0)
		goto failed;
	pthread_mutex_unlock (& conf->data_lock);
	return 0;
failed:;
	_free_pair (p);
	pthread_mutex_unlock (& conf->data_lock);
	return -1;
}

int conf_remove (CONF *conf, const char *name)
{
	int idx;
	if ((! conf) || (! name)) return -1;
	pthread_mutex_lock (& conf->data_lock);
	idx = glist_find_ex (& conf->pair_list, (void *) name, _compare_data_with_pair);
	if (idx < 0)
	{
		pthread_mutex_unlock (& conf->data_lock);
		return -1;
	}
	glist_delete (& conf->pair_list, idx, _free_pair);
	pthread_mutex_unlock (& conf->data_lock);
	return 0;
}

void conf_remove_all (CONF *conf)
{
	if (conf)
	{
		pthread_mutex_lock (& conf->data_lock);
		glist_clear (& conf->pair_list, _free_pair);
		conf->pair_list = NULL;
		pthread_mutex_unlock (& conf->data_lock);
	}
}

static int (* _compare) (const char *, const char *) = NULL;

static int conf_compare (void *p1, void *p2)
{
	if ((! _compare) || (! p1) || (! p2))
		return 0;

	if (((CONF_PAIR *) p1)->name && ((CONF_PAIR *) p2)->name)
		return _compare (((CONF_PAIR *) p1)->name, ((CONF_PAIR *) p2)->name);

	if (((CONF_PAIR *) p1)->name == ((CONF_PAIR *) p2)->name /* == NULL */)
		return 0;

	if (((CONF_PAIR *) p1)->name /* && p2->name == NULL */)
		return 1;

	/* p1->name == NULL && p2->name != NULL */
	return -1;
}

void conf_sort (CONF *conf, int (* compare_func) (const char *, const char *))
{
	if (compare_func)
	{
		_compare = compare_func;
	}
	if (conf)
	{
		pthread_mutex_lock (& conf->data_lock);
		glist_sort (& conf->pair_list, conf_compare);
		pthread_mutex_unlock (& conf->data_lock);
	}
}

int conf_save_to_file (CONF *conf)
{
	FILE *fp;
	GLIST *node;
	CONF_PAIR *p;
	if (! conf) return -1;
	pthread_mutex_lock (& conf->data_lock);
	if ((fp = fopen (conf->path, "wb")) == NULL)
	{
		fLOGE ("%s: %s\n", conf->path, strerror (errno));
		pthread_mutex_unlock (& conf->data_lock);
		return -1;
	}
	for (node = conf->pair_list; node; node = GLIST_NEXT (node))
	{
		if ((p = (CONF_PAIR *) GLIST_DATA (node)) != NULL)
		{
			fprintf (fp, "%s = %s\n", p->name, p->value);
		}
	}
	fclose (fp);
	pthread_mutex_unlock (& conf->data_lock);
	chmod (conf->path, DEFAULT_FILE_MODE);
	return 0;
}

CONF *conf_load_from_file (const char *filepath)
{
	CONF *conf = conf_new (filepath);
	if (conf)
	{
		FILE *fp;
		char buffer [512];
		char *ptr;
		if ((fp = fopen (filepath, "rb")) == NULL)
		{
			fLOGD ("%s: %s\n", filepath, strerror (errno));
			conf_destroy (conf);
			conf = NULL;
		}
		else
		{
			while (fgets (buffer, sizeof (buffer), fp) != NULL)
			{
				strtrim (buffer);
				if ((buffer [0] == 0) || (buffer [0] == '#'))
					continue;
				if ((ptr = strchr (buffer, '=')) == NULL)
				{
					fLOGD_IF ("%s: invalid config [%s]\n", filepath, buffer);
					continue;
				}
				*ptr ++ = 0;
				conf_set (conf, strtrim (buffer), strtrim (ptr));
			}
			fclose (fp);
		}
	}
	return conf;
}

void conf_dump (CONF *conf)
{
	GLIST *node;
	CONF_PAIR *p;

	if (! conf) return;

	pthread_mutex_lock (& conf->data_lock);

	fLOGD_IF ("conf [%s]\n", conf->path);

	for (node = conf->pair_list; node; node = GLIST_NEXT (node))
	{
		if ((p = (CONF_PAIR *) GLIST_DATA (node)) != NULL)
		{
			fLOGD_IF ("  [%s]=[%s]\n", p->name, p->value);
		}
		else
		{
			fLOGD_IF ("  !! null member !!\n");
		}
	}
	pthread_mutex_unlock (& conf->data_lock);
}

int conf_count (CONF *conf)
{
	int count = 0;

	if (conf)
	{
		pthread_mutex_lock (& conf->data_lock);
		count = glist_length (& conf->pair_list);
		pthread_mutex_unlock (& conf->data_lock);
	}
	return count;
}

int conf_get_pair (CONF *conf, int index, char *buffer, int len)
{
	int ret = -1;
	if (conf)
	{
		CONF_PAIR *p;
		pthread_mutex_lock (& conf->data_lock);
		p = glist_get (& conf->pair_list, index);
		if (p && (p != (void *) -1))
		{
			if (buffer && (len > 0))
			{
				snprintf (buffer, len, "%s=%s", p->name ? p->name : "", p->value ? p->value : "");
			}
			ret = (p->name ? strlen (p->name) : 0) + (p->value ? strlen (p->value) : 0) + 2 /* '=' and null terminated */;
		}
		pthread_mutex_unlock (& conf->data_lock);
	}
	return ret;
}

void keyholder_destroy (CONF_KEYHOLDER *kh)
{
	if (kh)
	{
		if (kh->pairs)
		{
			int i;

			for (i = 0; i < kh->count; i ++)
			{
				if (kh->pairs [i].name) free (kh->pairs [i].name);
				if (kh->pairs [i].value) free (kh->pairs [i].value);
			}

			free (kh->pairs);
		}
		free (kh);
	}
}

CONF_KEYHOLDER *keyholder_new (int count, CONF_PAIR *pairs)
{
	CONF_KEYHOLDER *kh = NULL;
	CONF_PAIR *pair;
	int i;

	if ((count <= 0) || (! pairs))
		goto err;

	kh = malloc (sizeof (CONF_KEYHOLDER));

	if (kh)
	{
		kh->count = count;
		kh->pairs = malloc (count * sizeof (CONF_PAIR));

		if (! kh->pairs)
			goto err;

		memset (kh->pairs, 0, count * sizeof (CONF_PAIR));

		for (i = 0; i < count; i ++)
		{
			pair = & pairs [i];

			if (! pair->name)
				goto err;

			if (! pair->value)
				pair->value = "";

			kh->pairs [i].name = strdup (pair->name);
			kh->pairs [i].value = strdup (pair->value);
		}
	}

	return kh;

err:;
	keyholder_destroy (kh);
	return NULL;
}

const char *keyholder_conf_get (CONF_KEYHOLDER *kh, CONF *conf, int index, const char *default_value)
{
	if (kh && kh->pairs && conf && (index >= 0) && (index < kh->count))
	{
		return conf_get (conf, kh->pairs [index].name, default_value ? default_value : kh->pairs [index].value);
	}
	return default_value;
}

int keyholder_conf_set (CONF_KEYHOLDER *kh, CONF *conf, int index, const char *value)
{
	if (kh && kh->pairs && conf && (index >= 0) && (index < kh->count))
	{
		return conf_set (conf, kh->pairs [index].name, value);
	}
	return -1;
}

int keyholder_conf_set_default (CONF_KEYHOLDER *kh, CONF *conf)
{
	if (kh && kh->pairs && conf)
	{
		int i;

		for (i = 0; i < kh->count; i ++)
		{
			conf_set (conf, kh->pairs [i].name, kh->pairs [i].value);
		}
	}
	return -1;
}

void keyholder_dump (CONF_KEYHOLDER *kh)
{
	if (kh)
	{
		int i;

		fLOGD_IF ("keyholder count=%d, pairs=%p", kh->count, kh->pairs);

		for (i = 0; i < kh->count; i ++)
		{
			fLOGD_IF ("keyholder %d: [%s] default=[%s]", i, kh->pairs [i].name, kh->pairs [i].value);
		}
	}
}

#if TEST_LOCALLY
int main (int argc, char **argv)
{
	CONF *conf;

	if (argc > 1)
	{
		conf = conf_load_from_file (argv [1]);
		if (conf)
		{
			conf_dump (conf);
			conf_destroy (conf);
			return 0;
		}
		printf ("load failed!\n");
		return -1;
	}

	conf = conf_new ("local.conf");

	if (conf)
	{
		conf_set (conf, "123", "456");
		conf_set (conf, "hello", "world !");
		conf_set (conf, "test.test1", "2");
		conf_set (conf, "test.test", "1");
		conf_set (conf, "test", "test");

		printf ("conf_get test.test = %s\n", conf_get (conf, "test.test", NULL));

		conf_remove (conf, "test.test");

		printf ("conf_get test.test = %s\n", conf_get (conf, "test.test", NULL));

		conf_save_to_file (conf);

		conf_dump (conf);
		conf_destroy (conf);
		return 0;
	}

	printf ("conf_new() failed!\n");
	return -1;
}
#endif
