#include "stdafx.h"

#ifdef TRACY_ENABLE

#include "dxPixEvents.h"

using Microsoft::WRL::ComPtr;

constexpr size_t MAX_STACK_COUNT{64};

struct pix_event_state
{
    size_t parent;
    size_t stack_idx;
    xr_string name;
    ComPtr<ID3D11Query> disjoint;
    ComPtr<ID3D11Query> begin;
    ComPtr<ID3D11Query> end;
};

struct pix_frame_state
{
    size_t counter;
    xr_vector<pix_event_state> events;
};

struct pix_events_state
{
    size_t frame;
    size_t stack_idx;
    std::array<size_t, MAX_STACK_COUNT> stack;
    pix_frame_state states[2];
    pix_events_perf perf;
};

static pix_events_state events_state;

static void InvalidateQueries()
{
    for (auto& state : events_state.states)
    {
        if (state.events.size() < MAX_PIX_EVENTS)
        {
            state.events.clear();
            state.events.resize(MAX_PIX_EVENTS);

            for (size_t i{}; i < MAX_PIX_EVENTS; i++)
            {
                auto& event = state.events[i];

                D3D11_QUERY_DESC desc{};
                desc.Query = {D3D11_QUERY_TIMESTAMP_DISJOINT};
                desc.MiscFlags = 0;

                R_CHK(HW.pDevice->CreateQuery(&desc, event.disjoint.GetAddressOf()));
                desc.Query = D3D11_QUERY_TIMESTAMP;
                R_CHK(HW.pDevice->CreateQuery(&desc, event.begin.GetAddressOf()));
                R_CHK(HW.pDevice->CreateQuery(&desc, event.end.GetAddressOf()));
            }
        }
    }
}

void PIXEventsBeginRendering()
{
    InvalidateQueries();

    events_state.frame++;
    auto& curr_state = events_state.states[events_state.frame % 2];

    if (events_state.frame >= 2)
    {
        events_state.perf.count = 0;

        for (size_t i{}; i < curr_state.counter; i++)
        {
            size_t begin{}, end{};
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint_data{};
            auto& event = curr_state.events[i];

            {
                ZoneScopedN("PIXEvents_BeginRendering/wait");

                while (HW.get_context(CHW::IMM_CTX_ID)->GetData(event.begin.Get(), &begin, sizeof(begin), 0) != S_OK)
                    ;
                while (HW.get_context(CHW::IMM_CTX_ID)->GetData(event.end.Get(), &end, sizeof(end), 0) != S_OK)
                    ;
                while (HW.get_context(CHW::IMM_CTX_ID)->GetData(event.disjoint.Get(), &disjoint_data, sizeof(disjoint_data), 0) != S_OK)
                    ;
            }

            auto& perf_event = events_state.perf.events[events_state.perf.count];
            perf_event.begin = begin;
            perf_event.end = end;
            perf_event.freq = disjoint_data.Frequency;
            perf_event.stack = event.stack_idx;
            perf_event.name = event.name;

            events_state.perf.count++;
        }
    }

    curr_state.counter = 0;

    PIXEventsPushEvent(CHW::IMM_CTX_ID, "Frame");
}

void PIXEventsEndRendering()
{
    PIXEventsPopEvent(CHW::IMM_CTX_ID, 0);
}

size_t PIXEventsPushEvent(const u32 context_id, const char* name)
{
    auto& curr_state = events_state.states[events_state.frame % 2];
    const auto index = curr_state.counter;
    R_ASSERT(curr_state.counter < MAX_PIX_EVENTS);
    auto& event = curr_state.events[index];

    event.name = name;
    event.stack_idx = events_state.stack_idx;
    event.parent = events_state.stack[events_state.stack_idx++];
    events_state.stack[events_state.stack_idx] = index;

    HW.get_context(context_id)->Begin(event.disjoint.Get());
    HW.get_context(context_id)->End(event.begin.Get());

    curr_state.counter++;
    return index;
}

void PIXEventsPopEvent(const u32 context_id, const size_t index)
{
    auto& curr_state = events_state.states[events_state.frame % 2];
    R_ASSERT(index < MAX_PIX_EVENTS);
    auto& event = curr_state.events[index];

    HW.get_context(context_id)->End(event.end.Get());
    HW.get_context(context_id)->End(event.disjoint.Get());

    R_ASSERT(events_state.stack_idx > 0);
    events_state.stack_idx--;
}

const pix_events_perf& PIXEventsStatistics() { return events_state.perf; }

#endif