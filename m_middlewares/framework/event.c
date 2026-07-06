//
// Created by maximillian on 2026-06-21.
//

#include "event.h"

void event_init(event_t* e)
{
    e->flags = 0;
}

void event_send(event_t* e, uint32_t flags)
{
    e->flags |= flags;
}

bool event_recv(event_t* e, uint32_t mask, bool all, bool clear)
{
    uint32_t f = e->flags;
    bool ok = all ? ((f & mask) == mask) : ((f & mask) != 0);
    if (ok && clear) {
        e->flags &= ~mask;
    }
    return ok;
}
