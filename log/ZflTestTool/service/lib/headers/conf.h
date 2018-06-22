#ifndef _SSD_CONF_DATABASE_H_
#define _SSD_CONF_DATABASE_H_

#if __cplusplus
extern "C" {
#endif

#include <pthread.h>

#include "glist.h"

typedef struct {
	pthread_mutex_t data_lock;
	char *path;
	GLIST *pair_list;
} CONF;

typedef struct {
	char *name;
	char *value;
} CONF_PAIR;

typedef struct {
	int count;
	CONF_PAIR *pairs;
} CONF_KEYHOLDER;

extern CONF *	conf_load_from_file (const char *filepath);
extern int	conf_save_to_file (CONF *conf);

extern CONF *		conf_new (const char *filepath);
extern void		conf_destroy (CONF *conf);
extern const char *	conf_get (CONF *conf, const char *name, const char *default_value);
extern int		conf_set (CONF *conf, const char *name, const char *value);
extern int		conf_remove (CONF *conf, const char *name);
extern void		conf_remove_all (CONF *conf);
extern void		conf_sort (CONF *conf, int (* compare_func) (const char *, const char *));
extern void		conf_dump (CONF *conf);
extern int		conf_count (CONF *conf);
extern int		conf_get_pair (CONF *conf, int index, char *buffer, int len);

extern CONF_KEYHOLDER *	keyholder_new (int count, CONF_PAIR *pairs);
extern void		keyholder_destroy (CONF_KEYHOLDER *kh);
extern int		keyholder_conf_set_default (CONF_KEYHOLDER *kh, CONF *conf);
extern int		keyholder_conf_set (CONF_KEYHOLDER *kh, CONF *conf, int index, const char *value);
extern const char *	keyholder_conf_get (CONF_KEYHOLDER *kh, CONF *conf, int index, const char *default_value);
extern void		keyholder_dump (CONF_KEYHOLDER *conf);

/*
 * below macros are used to declare a CONF_PAIR structure with an index variable quickly.
 *
 * ex.
 * 	DELCARE_CONF_PAIR_AND_INDEX (0, Test, "Hello World!");
 *
 * is equal to:
 *
 * 	int idx_Test = 0;
 * 	CONF_PAIR pair_Test = {"Test", "Hello World!"};
 *
 * with these macros, the parameter "name" should not contain any special characters because it's used in variable naming.
 */
#define DECLARE_CONF_PAIR(name,value)\
	CONF_PAIR pair_##name = {#name, value}
#define DECLARE_CONF_PAIR_STATIC(name,value)\
	static CONF_PAIR pair_##name = {#name, value}
#define DECLARE_CONF_PAIR_AND_INDEX(idx,name,value)\
	const int idx_##name = idx;\
	CONF_PAIR pair_##name = {#name, value}
#define DECLARE_CONF_PAIR_AND_INDEX_STATIC(idx,name,value)\
	static const int idx_##name = idx;\
	static CONF_PAIR pair_##name = {#name, value}
#define DECLARE_CONF_PAIR_AND_INDEX_WITH_PREFIX(prefix,idx,name,value)\
	const int prefix##idx_##name = idx;\
	CONF_PAIR prefix##pair_##name = {#name, value}
#define DECLARE_CONF_PAIR_AND_INDEX_WITH_PREFIX_STATIC(prefix,idx,name,value)\
	static const int prefix##idx_##name = idx;\
	static CONF_PAIR prefix##pair_##name = {#name, value}

#if __cplusplus
}
#endif

#endif
