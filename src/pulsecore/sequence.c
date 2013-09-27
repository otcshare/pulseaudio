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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulsecore/macro.h>

#include "sequence.h"

void pa_sequence_insert(pa_sequence_head *head, pa_sequence_list *elem) {
    pa_sequence_list *after, *before;

    for (after = head->list.prev;  after != &head->list;  after = after->prev) {
        if (head->compare(elem, after) >= 0)
            break;
    }

    before = after->next;

    before->prev = elem;
    after->next = elem;

    elem->next = before;
    elem->prev = after;
}

bool pa_sequence_sort(pa_sequence_head *head) {
    pa_sequence_list *elem, *next, *after, *before;
    bool changed = false;

    pa_assert(head);

    PA_SEQUENCE_FOREACH_ENTRY_SAFE(elem, next, *head) {
        /* Detach elem temporarily from the list. */
        next->prev = elem->prev;
        elem->prev->next = next;

        /* Find the new location for elem. The items before the original
         * location are sorted, and the items after the original location are
         * not. We only need to compare elem to the sorted items, so we start
         * from elem->prev and continue towards the head of the list. */
        for (after = elem->prev; after != &head->list; after = after->prev) {
            if (head->compare(elem, after) >= 0) {
                if (after != elem->prev)
                    changed = true;

                break;
            }
        }

        /* Attach elem back to the list. */
        before = after->next;

        before->prev = elem;
        after->next = elem;

        elem->next = before;
        elem->prev = after;
    }

    return changed;
}
