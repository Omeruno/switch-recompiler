#include "message_loop.h"
#include "seh_dispatch.h"
#include "host_functions.h" // TryHandleHostCall, HostCallOutcome
#include "../cpu/interpreter.h"
#include "../runtime/process.h"
#include "../runtime/guest_fault.h"
#include "../runtime/guest_execution.h"
#include "../decoder/x86_decoder.h"
#include <cstdio>

namespace win32 {

MessageQueue g_messageQueue;

void MessageQueue::Post(uint32_t hwnd, uint32_t message, uint32_t wParam, uint32_t lParam) {
    queue_.push_back({hwnd, message, wParam, lParam});
}

bool MessageQueue::Pop(QueuedMessage& out) {
    if (queue_.empty()) return false;
    out = queue_.front();
    queue_.pop_front();
    return true;
}

bool MessageQueue::PopFiltered(uint32_t filterMin, uint32_t filterMax, QueuedMessage& out) {
    // Реальный контракт Win32: filterMin==0 && filterMax==0 означает "любые
    // сообщения", иначе — только те, чей message попадает в [filterMin,filterMax]
    bool noFilter = (filterMin == 0 && filterMax == 0);
    for (auto it = queue_.begin(); it != queue_.end(); ++it) {
        if (noFilter || (it->message >= filterMin && it->message <= filterMax)) {
            out = *it;
            queue_.erase(it);
            return true;
        }
    }
    return false;
}

bool MessageQueue::PeekFiltered(uint32_t filterMin, uint32_t filterMax, QueuedMessage& out) const {
    // Та же фильтрация, что и PopFiltered, но БЕЗ queue_.erase — сообщение
    // остаётся в очереди для последующего настоящего Pop/PopFiltered.
    bool noFilter = (filterMin == 0 && filterMax == 0);
    for (const auto& item : queue_) {
        if (noFilter || (item.message >= filterMin && item.message <= filterMax)) {
            out = item;
            return true;
        }
    }
    return false;
}

void MessageQueue::InvalidateWindow(uint32_t hwnd) {
    needsPaint_[hwnd] = true;
}

bool MessageQueue::AnyWindowNeedsPaint(uint32_t& outHwnd) const {
    for (const auto& entry : needsPaint_) {
        if (entry.second) { outHwnd = entry.first; return true; }
    }
    return false;
}

bool MessageQueue::ConsumeNeedsPaint(uint32_t hwnd) {
    auto it = needsPaint_.find(hwnd);
    if (it == needsPaint_.end() || !it->second) return false;
    it->second = false;
    return true;
}

void MessageQueue::RequestQuit() {
    quitRequested_ = true;
}

TimerRegistry g_timerRegistry;

void TimerRegistry::SetTimer(uint32_t hwnd, uint32_t timerId, uint32_t intervalMs, uint32_t nowTick) {
    for (auto& t : timers_) {
        if (t.hwnd == hwnd && t.timerId == timerId) {
            t.intervalMs = intervalMs;
            t.lastFireTick = nowTick;
            return;
        }
    }
    timers_.push_back({hwnd, timerId, intervalMs, nowTick});
}

void TimerRegistry::KillTimer(uint32_t hwnd, uint32_t timerId) {
    for (auto it = timers_.begin(); it != timers_.end(); ++it) {
        if (it->hwnd == hwnd && it->timerId == timerId) {
            timers_.erase(it);
            return;
        }
    }
}

bool TimerRegistry::CheckDue(uint32_t nowTick, uint32_t& outHwnd, uint32_t& outTimerId) {
    for (auto& t : timers_) {
        // nowTick - lastFireTick через uint32_t арифметику — корректно
        // переживает переполнение счётчика тиков (как и должен GetTickCount)
        uint32_t elapsed = nowTick - t.lastFireTick;
        if (elapsed >= t.intervalMs) {
            t.lastFireTick = nowTick;
            outHwnd = t.hwnd;
            outTimerId = t.timerId;
            return true;
        }
    }
    return false;
}

// Глубина вложенности вызовов CallGuestFunctionStdcall — если WndProc
// зациклится сам на себя (например, через UpdateWindow/InvalidateRect
// внутри обработки клика), это исчерпает даже большой стек. Ловим это
// ЯВНО, с понятным сообщением, вместо аппаратного краха.
static int g_nestedCallDepth = 0;
constexpr int kMaxNestedCallDepth = 32;

// Базовый адрес стека игрового потока — выставляется один раз в main.cpp.
// Задел на будущее (пригодилось при диагностике переполнения стека — не
// подтвердилось, но оставляем на случай, если понадобится снова).
void* g_threadStackBase = nullptr;

uint32_t CallGuestFunctionStdcall(cpu::CpuState& cpu, cpu::IGuestMemory& mem,
                                   uint32_t functionAddr, const std::vector<uint32_t>& args,
                                   uint32_t fsBase, uint32_t gsBase, int maxSteps) {
    if (rt::GuestFaultPending() || !rt::GuestExecution::Checkpoint()) return 0;
    rt::GuestThread* owner=rt::g_process ? rt::g_process->Scheduler().Current() : nullptr;
    uint32_t fallbackDepth=0;
    // Unscheduled v1 callbacks retain their existing recursion accounting.
    uint32_t* depth=owner ? &owner->callbackDepth : &fallbackDepth;
    if (!owner) fallbackDepth=static_cast<uint32_t>(g_nestedCallDepth);
    ++*depth;
    if (!owner) g_nestedCallDepth=static_cast<int>(*depth);
    if (*depth > kMaxNestedCallDepth) {
        cpu::FatalError(
            "CallGuestFunctionStdcall: превышена максимальная глубина вложенности "
            "(похоже на реальную рекурсию — WndProc зациклился сам на себя, "
            "например через UpdateWindow/InvalidateRect внутри обработки клика)");
    }
    if (rt::GuestExecution::Active() && owner) fsBase=gsBase=owner->teb;

    // Реальный x86 никогда сам не сгенерирует этот адрес — используем его как
    // "фейковый" адрес возврата, по которому поймём, что вложенная функция
    // реально выполнила RET и вернула нам управление.
    constexpr uint32_t SENTINEL = 0xE0000000u;

    // Пушим аргументы в ОБРАТНОМ порядке (последний — первым), как и
    // положено stdcall: первый аргумент должен оказаться по [esp+4] после call
    for (auto it = args.rbegin(); it != args.rend(); ++it) {
        cpu.ESP() -= 4;
        mem.Write32(cpu.ESP(), *it);
    }
    cpu.ESP() -= 4;
    mem.Write32(cpu.ESP(), SENTINEL);
    cpu.eip = functionAddr;

    cpu::Interpreter nestedInterp(mem, fsBase, gsBase);

    int steps = 0;
    uint32_t lastEip = functionAddr;
    while (cpu.eip != SENTINEL) {
        if (g_guestExitRequested) break; // процесс завершается: разматываемся
        if (rt::GuestFaultPending() || !rt::GuestExecution::Checkpoint()) break;
        if (steps >= maxSteps) {
            cpu::FatalError(
                "Вложенный вызов гостевой функции не вернулся (превышен лимит "
                "шагов) — возможен баг в WndProc или в нашей эмуляции");
        }

        // Тот же самый случай, что и во внешнем цикле: eip==0 означает
        // "гостевой код попытался вызвать ещё не реализованный импорт" —
        // без этой проверки Step() попытается читать память по адресу 0 и
        // упадёт с гораздо менее информативным сообщением об ошибке.
        if (cpu.eip == 0) {
            // A null transfer is still an access violation inside a protected
            // loader callback, not an unconditional host-process termination.
            if(rt::GuestFaultBoundaryActive()) {
                cpu::CpuException fault{};
                fault.code=cpu::EXC_ACCESS_VIOLATION;fault.eip=0;fault.faultAddress=0;
                if(seh::DispatchCpuFault(cpu,mem,fsBase,fault)==seh::DispatchResult::Handled &&
                   !rt::GuestFaultPending()) {++steps;continue;}
                if(g_guestExitRequested || !rt::GuestExecution::Checkpoint())break;
                if(rt::CaptureGuestFault(fault.code,0,0))break;
            }
            // Preserve the *last executed instruction*, not the zero target.
            // This is essential to distinguish a missing import from an
            // invalid function pointer or a broken x86 indirect call.
            cpu::g_lastKnownEip = lastEip;
            cpu::g_fatalPreviousEip = lastEip;
            cpu::g_fatalStackPointer = cpu.ESP();
            cpu::g_fatalModule[0] = 0;
            cpu::g_fatalModuleOffset = 0;
            cpu::g_fatalInstructionBytes[0] = 0;
            cpu::g_fatalStackCount = 0;
            for (unsigned i = 0; i < 8; ++i)
                cpu::g_fatalRegisters[i] = cpu.regs[i];
            // Read only validated guest pages. FatalMemoryError must not
            // recursively trigger another fatal error during diagnosis.
            if (mem.IsRangeMapped(lastEip, 12)) {
                size_t at = 0;
                for (unsigned i = 0; i < 12; ++i) {
                    int count = std::snprintf(cpu::g_fatalInstructionBytes + at,
                        sizeof(cpu::g_fatalInstructionBytes) - at,
                        "%02X%s", mem.Read8(lastEip + i), i == 11 ? "" : " ");
                    if (count < 0 || static_cast<size_t>(count) >= sizeof(cpu::g_fatalInstructionBytes) - at) break;
                    at += static_cast<size_t>(count);
                }
            }
            for (unsigned i = 0; i < 8; ++i) {
                uint64_t addr = static_cast<uint64_t>(cpu.ESP()) + 4u * i;
                if (addr > 0xFFFFFFFFull || !mem.IsRangeMapped(static_cast<uint32_t>(addr), 4)) break;
                cpu::g_fatalStackWords[cpu::g_fatalStackCount++] = mem.Read32(static_cast<uint32_t>(addr));
            }
            if (rt::g_process) {
                if (rt::Module* mod = rt::g_process->FindByAddress(lastEip)) {
                    std::snprintf(cpu::g_fatalModule, sizeof(cpu::g_fatalModule),
                                  "%s", mod->name.c_str());
                    cpu::g_fatalModuleOffset = lastEip - mod->base;
                }
            }
            if (cpu::g_consoleAvailable) {
                std::printf("[DEBUG] Вложенный вызов: последняя исполненная инструкция была на 0x%x\n", lastEip);
            }
            cpu::FatalError(
                "Внутри вложенного вызова гостевой код перешёл на адрес 0 "
                "(причина пока неизвестна: импорт, указатель или состояние CPU)");
        }

        HostCallOutcome outcome;
        if (TryHandleHostCall(cpu, mem, outcome)) {
            steps++;
            continue;
        }

        lastEip = cpu.eip;

        cpu::Instruction instr;
        auto result = nestedInterp.Step(cpu, &instr);
        cpu::g_lastKnownEip = cpu.eip; // после шага: при сбое внутри следующего Step() здесь адрес сбойной инструкции

        if (result == cpu::StepResult::UnknownOpcode || result == cpu::StepResult::Exception) {
            const cpu::CpuException& fault = nestedInterp.LastException();
            if (seh::DispatchCpuFault(cpu, mem, fsBase, fault) == seh::DispatchResult::Handled &&
                !rt::GuestFaultPending()) {
                ++steps;
                continue;
            }
            if(g_guestExitRequested || !rt::GuestExecution::Checkpoint())break;
            const uint32_t code=fault.code?fault.code:cpu::EXC_ILLEGAL_INSTRUCTION;
            if(rt::CaptureGuestFault(code,fault.eip?fault.eip:lastEip,fault.faultAddress))break;
        }
        if (result == cpu::StepResult::UnknownOpcode) {
            if (cpu::g_consoleAvailable) {
                std::printf("[DEBUG] Неизвестный опкод 0x%02x на адресе 0x%x (внутри вложенного вызова)\n",
                            instr.rawOpcodeForDebug, cpu.eip);
            }
            cpu::FatalError(
                "Неизвестный опкод внутри вложенного вызова гостевой функции "
                "(WndProc и т.п.)");
        }
        if (result == cpu::StepResult::Exception) {
            const cpu::CpuException& e = nestedInterp.LastException();
            cpu::g_lastKnownEip = e.eip;
            cpu::g_fatalGuestCode = e.code;
            cpu::g_fatalGuestAddress = e.faultAddress;
            if (cpu::g_consoleAvailable) {
                std::printf("[EXC] Исключение 0x%08x на 0x%x (адрес 0x%x) внутри вложенного вызова\n",
                            e.code, e.eip, e.faultAddress);
            }
            cpu::FatalError("Исключение гостя внутри вложенного вызова (WndProc и т.п.)");
        }
        // StepResult::Halted тоже просто пропускаем как "продолжить" — HLT
        // внутри настоящего WndProc не встречается, но не должен нас подвесить
        steps++;
    }

    --*depth;
    if (!owner) g_nestedCallDepth=static_cast<int>(*depth);
    return cpu.Get(cpu::Reg32::EAX);
}

} // namespace win32
