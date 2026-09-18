#include "debug_utils.h"

#include <Arduino.h>
#include <vector>
#include <string>
#include <algorithm>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// For clarity, you can wrap the code in an #if ( portSTACK_GROWTH < 0 ) if needed.
static constexpr size_t BAR_WIDTH = 20;

// Helper function to create a bar (e.g., "[====>     ]").
std::string makeBar(uint8_t usagePercent)
{
    // Number of '=' to display
    size_t fillCount = (usagePercent * BAR_WIDTH) / 100;

    std::string bar;
    bar.reserve(BAR_WIDTH + 2); // '[' and ']'
    bar.push_back('[');

    // Fill '='
    bar.append(fillCount, '=');

    // Add '>' marker if not at 100%
    if (fillCount < BAR_WIDTH) {
        bar.push_back('>');
        // Fill remaining space with ' '
        if (BAR_WIDTH > fillCount + 1) {
            bar.append(BAR_WIDTH - fillCount - 1, ' ');
        }
    }
    bar.push_back(']');
    return bar;
}

void printAllTasksStackUsage()
{
    // 1) Get current tasks via system state
    const UBaseType_t maxTasks = uxTaskGetNumberOfTasks();
    if (maxTasks == 0) {
        Serial.println("No tasks found.");
        return;
    }

    std::vector<TaskStatus_t> taskStates(maxTasks);
    UBaseType_t taskCountSys = uxTaskGetSystemState(taskStates.data(), maxTasks, nullptr);

    // 2) Get snapshots (raw stack pointers)
    std::vector<TaskSnapshot_t> snapshots(maxTasks);
    UBaseType_t taskCountSnap;
    UBaseType_t snapshotOverflow; // This will hold the "overflow" if there are more tasks than we can store
    taskCountSnap = uxTaskGetSnapshotAll(snapshots.data(), maxTasks, &snapshotOverflow);

    Serial.println("Task Stack Usage:");

    // 3) For each snapshot, find the matching task status
    for (auto &snap : snapshots)
    {
        // pxTCB from the snapshot identifies the TCB pointer
        void* snapTCB = snap.pxTCB;

        // Find matching TaskStatus_t (comparing xHandle to pxTCB).
        // xHandle is essentially the TCB pointer from the kernel’s perspective.
        auto it = std::find_if(taskStates.begin(), taskStates.end(),
                               [snapTCB](const TaskStatus_t &ts) {
                                    return (void*)ts.xHandle == snapTCB;
                               });
        if (it == taskStates.end()) {
            // If not found, skip. (Might happen if tasks changed between calls.)
            continue;
        }

        TaskStatus_t &st = *it;

        // We have matched TaskStatus_t (st) and TaskSnapshot_t (snap)

        // Task name
        const char* name = (st.pcTaskName) ? st.pcTaskName : "???";

        // pxStackBase = lowest address of stack
        // pxEndOfStack = highest address of stack
        // pxTopOfStack = current stack pointer
        // On ESP32 (downward growth), total stack = (pxEndOfStack - pxStackBase + 1)
        // used stack = (pxEndOfStack - pxTopOfStack + 1)
        //
        // Because these are pointer differences in units of StackType_t (4 bytes on ESP32),
        // we can do direct pointer subtraction. However, we must static_cast to uintptr_t
        // or use reinterpret_cast to do correct pointer arithmetic. Each difference is in
        // "StackType_t" steps, not raw bytes, if the pointers are all StackType_t*.
        //
        // If they are all StackType_t*, pointer subtraction is automatically in
        // increments of (sizeof(StackType_t)).

        uint32_t totalWords = 0;
        uint32_t usedWords = 0;

        if (snap.pxEndOfStack > st.pxStackBase) {
            totalWords = static_cast<uint32_t>(snap.pxEndOfStack - st.pxStackBase + 1);
        }
        if (snap.pxTopOfStack < snap.pxEndOfStack) {
            usedWords = static_cast<uint32_t>(snap.pxEndOfStack - snap.pxTopOfStack + 1);
        }

        // Just in case we get weird pointer values (if a task ended, etc.)
        if (totalWords == 0 || usedWords > totalWords) {
            // Fallback to something safe or skip
            continue;
        }

        // usage % = (used / total) * 100
        uint8_t usagePercent = static_cast<uint8_t>(
            (static_cast<uint64_t>(usedWords) * 100ULL) / totalWords
        );

        // Optionally, you can cross-check with usStackHighWaterMark:
        // st.usStackHighWaterMark = min free words ever left
        // So "unused" per high-water is st.usStackHighWaterMark
        // Or we can just rely on the pointer arithmetic.

        // Build bar
        std::string bar = makeBar(usagePercent);

        // Print: "TaskName  [====>     ]  47%  used=123, total=512"
        Serial.printf("%-16s %s %3u%% (used=%u, total=%u)\n",
                      name,
                      bar.c_str(),
                      usagePercent,
                      usedWords,
                      totalWords);
    }
}