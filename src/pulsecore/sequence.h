#ifndef foosequencehfoo
#define foosequencehfoo

/***
  This file is part of PulseAudio.

  Copyright (c) 2012 Intel Corporation
  Janos Kovacs <jankovac503@gmail.com>

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

typedef struct pa_sequence_list pa_sequence_list;
typedef struct pa_sequence_head pa_sequence_head;

#include <stdbool.h>
#include <stddef.h>

/* Return values:
 *      less than zero: entry1 should appear earlier than entry2
 *                zero: entry1 is equal to entry2
 *   greater than zero: entry1 should appear later than entry2 */
typedef int (*pa_sequence_compare)(pa_sequence_list *entry1, pa_sequence_list *entry2);


struct pa_sequence_list {
    pa_sequence_list *next, *prev;
};

struct pa_sequence_head {
    pa_sequence_list list;
    pa_sequence_compare compare;
};

#define PA_SEQUENCE_LIST_INIT(item)                             \
    do {                                                        \
        (item).next = &(item);                                  \
        (item).prev = &(item);                                  \
    } while(0)

#define PA_SEQUENCE_HEAD_INIT(head,cmpfunc)                     \
    do {                                                        \
        PA_SEQUENCE_LIST_INIT((head).list);                     \
        (head).compare = cmpfunc;                               \
    } while(0)


#define PA_SEQUENCE_LIST_ENTRY(ptr,type,member)                 \
    ((type *)((char *)ptr - (ptrdiff_t)((char *)(&((type *)0)->member) - (char *)0)))

#define PA_SEQUENCE_FOREACH(elem, head, type, member)           \
    for (elem = PA_SEQUENCE_LIST_ENTRY((head).list.next, type, member); \
         (elem) != PA_SEQUENCE_LIST_ENTRY(&(head).list, type, member); \
         elem = PA_SEQUENCE_LIST_ENTRY((elem)->member.next, type, member))

#define PA_SEQUENCE_FOREACH_SAFE(elem, n, head, type, member)   \
    for (elem = PA_SEQUENCE_LIST_ENTRY((head).list.next, type, member), n = PA_SEQUENCE_LIST_ENTRY((elem)->member.next, type, member); \
         (elem) != PA_SEQUENCE_LIST_ENTRY(&(head).list, type, member); \
         n = PA_SEQUENCE_LIST_ENTRY((elem = n)->member.next, type, member))

#define PA_SEQUENCE_FOREACH_ENTRY_SAFE(elem, n, head)           \
    for (elem = (head).list.next, n = (elem)->next; (elem) != &(head).list; n = (elem = n)->next)

#define PA_SEQUENCE_INSERT(head,elem) pa_sequence_insert(&(head), &(elem))

#define PA_SEQUENCE_REMOVE(elem)                                \
    do {                                                        \
        pa_sequence_list *_next = (elem).next;                  \
        pa_sequence_list *_prev = (elem).prev;                  \
        _prev->next = _next;                                    \
        _next->prev = _prev;                                    \
        (elem).next = (elem).prev = &(elem);                    \
    } while (0)

#define PA_SEQUENCE_IS_EMPTY(head)                              \
    ((head).list.next == &(head).list)

void pa_sequence_insert(pa_sequence_head *head, pa_sequence_list *elem);

/* Returns true if there were changes. */
bool pa_sequence_sort(pa_sequence_head *head);

#endif
