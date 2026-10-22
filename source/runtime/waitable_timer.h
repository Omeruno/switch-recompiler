#pragma once
#include "handle_table.h"
namespace rt {
class GuestScheduler;
void CancelTimer(TimerObject& timer,GuestScheduler* scheduler);
void PollWaitableTimers(HandleTable& handles,GuestScheduler* scheduler);
void PollWaitableTimers();
bool HasActiveWaitableTimers(HandleTable& handles);
}
