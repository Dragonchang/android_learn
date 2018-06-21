#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "headers/conf.h"

static CONF *conf = NULL;

int db_init (const char *filepath)
{
	if (! conf) conf = conf_new (filepath);
	return (conf == NULL) ? -1 : 0;
}

void db_destroy (void)
{
	if (conf) conf_destroy (conf);
	conf = NULL;
}

const char *db_get (const char *name, const char *default_value)
{
	return (conf && name) ? conf_get (conf, name, default_value) : NULL;
}

int db_set (const char *name, const char *value)
{
	return (conf && name) ? conf_set (conf, name, value) : -1;
}

int db_remove (const char *name)
{
	return (conf && name) ? conf_remove (conf, name) : -1;
}

void db_dump (void)
{
	if (conf) conf_dump (conf);
}
