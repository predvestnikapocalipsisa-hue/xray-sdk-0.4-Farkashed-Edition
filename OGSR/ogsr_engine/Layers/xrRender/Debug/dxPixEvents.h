#pragma once

#ifdef TRACY_ENABLE

constexpr size_t MAX_PIX_EVENTS{1024};

struct pix_event_stats
{
    size_t freq;
    size_t begin;
    size_t end;
    size_t stack;
    xr_string name;
};

struct pix_events_perf
{
    size_t count;
    std::array<pix_event_stats, MAX_PIX_EVENTS> events;
};

void PIXEventsBeginRendering();
void PIXEventsEndRendering();

size_t PIXEventsPushEvent(const u32 context_id, const char* name);
void PIXEventsPopEvent(const u32 context_id, const size_t index);

const pix_events_perf& PIXEventsStatistics();

#endif