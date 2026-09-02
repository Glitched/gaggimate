#ifndef HEAP_TRACE_H
#define HEAP_TRACE_H

// Compiled only in the `display-heaptrace` env (-DGM_HEAP_TRACE, CONFIG_HEAP_TRACING_STANDALONE=y). A global
// constructor in HeapTrace.cpp starts ESP-IDF's standalone heap tracer before initArduino() so every live
// allocation carries the call stack that made it; the route dumps those records for scripts/heap_trace_report.py.
#ifdef GM_HEAP_TRACE
class AsyncWebServer;
void heapTraceRegisterRoutes(AsyncWebServer &server);
#endif

#endif // HEAP_TRACE_H
