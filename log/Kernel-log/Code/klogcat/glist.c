

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "headers/glist.h"

int glist_length (GLIST **head)
{
	GLIST *p;
	int len;
	for (len = 0, p = *head; p; p = p->next, len ++);
	return len;
}

int glist_clear (GLIST **head, void (* free_func) (void *))
{
	GLIST *p;
	for (p = *head; p; p = *head)
	{
		*head = p->next;
		if (free_func) free_func (p->member);
		free (p);
	}
	return 0;
}

static GLIST *new_node (void *member)
{
	GLIST *p = (GLIST *) malloc (sizeof (GLIST));
	if (p)
	{
		p->next = NULL;
		p->member = member;
	}
	return p;
}

int glist_add (GLIST **head, void *member)
{
	GLIST *p = new_node (member);
	if (! p)
		return -1;
	p->next = *head;
	*head = p;
	return 0;
}

int glist_append (GLIST **head, void *member)
{
	GLIST *p;
	if (glist_add (head, member) < 0)
		return -1;
	for (p = *head; p && p->next; p = p->next);
	if (p != *head)
	{
		p->next = *head;
		p = *head;
		*head = p->next;
		p->next = NULL;
	}
	return 0;
}

void *glist_get (GLIST **head, int index)
{
	GLIST *p;
	int i;
	if (index < 0)
		return (void *) -1;
	for (p = *head, i = 0; p && (i < index); p = p->next, i ++);
	if (! p)
		return (void *) -1;
	return p->member;
}

void *glist_set (GLIST **head, int index, void *member)
{
	GLIST *p;
	void *old;
	int i;
	if (index < 0)
		return (void *) -1;
	for (p = *head, i = 0; p && (i < index); p = p->next, i ++);
	if (! p)
		return (void *) -1;
	old = p->member;
	p->member = member;
	return old;
}

int glist_find (GLIST **head, void *member)
{
	GLIST *p;
	int i;
	for (p = *head, i = 0; p; p = p->next, i ++)
		if (p->member == member)
			return i;
	return -1;
}

int glist_find_ex (GLIST **head, void *member, int (* compare_func) (void *, void *))
{
	GLIST *p;
	int i;
	if (! compare_func)
		return glist_find (head, member);
	for (p = *head, i = 0; p; p = p->next, i ++)
		if (compare_func (member, p->member) == 0)
			return i;
	return -1;
}

int glist_sort (GLIST **head, int (* compare_func) (void *, void *))
{
	GLIST *p1, *p2;
	void *tmp;
	if (! compare_func)
		return -1;
	for (p1 = *head; p1; p1 = p1->next) for (p2 = p1; p2; p2 = p2->next) if (compare_func (p1->member, p2->member) > 0)
	{
		tmp = p1->member;
		p1->member = p2->member;
		p2->member = tmp;
	}
	return 0;
}

int glist_delete (GLIST **head, int index, void (* free_func) (void *))
{
	GLIST *p, *pp;
	int i;
	for (p = *head, pp = NULL, i = 0; p && (i < index); pp = p, p = p->next, i ++);
	if (! p) return -1;
	if (pp) pp->next = p->next; else *head = p->next;
	if (free_func) free_func (p->member);
	free (p);
	return 0;
}

void glist_dump (GLIST **head, void (*dump) (void *, long), long option)
{
	GLIST *p;
	for (p = *head; p; p = p->next)
		dump (p->member, option);
}

#if 0 // test
static void dump_node (void *member, long is_string)
{
	if (is_string)
	{
		printf ("\t%s\n", (const char *) member);
	}
	else
	{
		printf ("\t%lu\n", (unsigned long) member);
	}
}

static void glist_dump2 (GLIST **head, long is_string)
{
	printf ("%d:\n", glist_length (head));
	glist_dump (head, dump_node, is_string);
}

static int my_strsort (void *p1, void *p2)
{
	return strcmp ((const char *) p1, (const char *) p2);
}

static int my_strfind (void *p1, void *p2)
{
	return strcmp ((const char *) p1, (const char *) p2);
}

int main (void)
{
	GLIST_NEW (head);

	glist_add (& head, (void *) 1);
	glist_add (& head, (void *) 2);
	glist_add (& head, (void *) 3);
	glist_dump2 (& head, 0);
	printf (".. get index %d == %d\n", 3, (int) glist_get (& head, 3));
	printf (".. get index %d == %d\n", 2, (int) glist_get (& head, 2));
	printf (".. get index %d == %d\n", 1, (int) glist_get (& head, 1));
	printf (".. get index %d == %d\n", 0, (int) glist_get (& head, 0));
	glist_set (& head, 0, (void *) 4);
	printf (".. get index %d == %d after set to 4\n", 0, (int) glist_get (& head, 0));
	glist_delete (& head, 1, NULL);
	glist_dump2 (& head, 0);
	glist_clear (& head, NULL);

	glist_append (& head, (void *) 1);
	glist_append (& head, (void *) 2);
	glist_append (& head, (void *) 3);
	glist_dump2 (& head, 0);
	printf (".. find 2, got index %d\n", glist_find (& head, (void *) 2));
	glist_clear (& head, NULL);

	glist_append (& head, (void *) "test");
	glist_append (& head, (void *) "the");
	glist_append (& head, (void *) "sorting");
	glist_append (& head, (void *) "function");
	glist_sort (& head, my_strsort);
	glist_dump2 (& head, 1);
	printf (".. find \"test\", got index %d\n", glist_find_ex (& head, (void *) "test", my_strfind));
	glist_clear (& head, NULL);
	return 0;
}
#endif

