#ifndef	_SERVICE_LINKED_LIST_H_
#define	_SERVICE_LINKED_LIST_H_

typedef struct _g_list_ {
	struct _g_list_	*next;
	void		*member;
} GLIST;

#define	GLIST_NEW(x)	GLIST*x=NULL
#define	GLIST_NEXT(x)	(((GLIST *) x)->next)
#define	GLIST_DATA(x)	(((GLIST *) x)->member)

extern int glist_length (GLIST **head);
extern int glist_clear (GLIST **head, void (* free_func) (void *));
extern int glist_add (GLIST **head, void *member);
extern int glist_delete (GLIST **head, int index, void (* free_func) (void *));
extern int glist_append (GLIST **head, void *member);
extern void *glist_get (GLIST **head, int index);
extern void *glist_set (GLIST **head, int index, void *member);
extern int glist_find (GLIST **head, void *member);
extern int glist_find_ex (GLIST **head, void *member, int (* compare_func) (void *, void *));
extern int glist_sort (GLIST **head, int (* compare_func) (void *, void *));

extern void glist_dump (GLIST **head, void (*dump) (void *, long), long option);

#endif	/* _SERVICE_LINKED_LIST_H_ */
