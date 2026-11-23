// wwe2k16 - sampling profiler for finding where load time goes (dev aid, off unless WWE2K16_PROFILE=<seconds>).
//
// For <seconds>, starting 0.3 s after the call, every ~1 ms each thread of the process is stopped for an instant and its
// instruction pointer is read. At the end the file named by WWE2K16_PROFILE_FILE (default profile.txt) gets one line per place:
//   count  module+offset  thread-name
// The offsets of wwe2k16_native.exe are resolved with the linker map: tools/resolve_profile.py.

#include "profile_sampler.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#endif

#include <rex/logging.h>

namespace {
#ifdef _WIN32
// Walks the stack of a thread from a saved context and collects up to `max` return addresses inside [exe_base, exe_end) as
// offsets. SEH-guarded: the thread has run on since the context was read, so its stack may no longer match.
int WalkExeFrames(CONTEXT walk, uint64_t exe_base, uint64_t exe_end, uint64_t* out, int max) {
  int n = 0;
  __try {
    for (int depth = 0; depth < 96 && n < max; ++depth) {
      if (walk.Rip >= exe_base && walk.Rip < exe_end) {
        if (n == 0 || out[n - 1] != walk.Rip - exe_base) out[n++] = walk.Rip - exe_base;
      }
      DWORD64 image_base = 0;
      PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(walk.Rip, &image_base, nullptr);
      if (!fe) break;
      PVOID handler_data = nullptr;
      DWORD64 establisher = 0;
      RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, walk.Rip, fe, &walk, &handler_data, &establisher, nullptr);
      if (!walk.Rip) break;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
  return n;
}
#endif
}  // namespace

namespace wwe2k16 {

void StartProfileSamplerFromEnvironment() {
#ifdef _WIN32
  const char* env = std::getenv("WWE2K16_PROFILE");
  if (!env || !*env) return;
  const double seconds = std::atof(env);
  if (seconds <= 0) return;
  const char* file = std::getenv("WWE2K16_PROFILE_FILE");
  const std::string out = file ? file : "profile.txt";
  std::thread([seconds, out] {
    using Clock = std::chrono::steady_clock;
    const DWORD self = GetCurrentThreadId();
    const DWORD pid = GetCurrentProcessId();
    const char* delay = std::getenv("WWE2K16_PROFILE_DELAY");  // seconds to wait before sampling (default 0.3)
    std::this_thread::sleep_for(std::chrono::duration<double>(delay ? std::atof(delay) : 0.3));
    const char* interval = std::getenv("WWE2K16_PROFILE_INTERVAL_MS");  // pause between sweeps (default 1)
    const int interval_ms = interval ? std::max(1, std::atoi(interval)) : 1;
    const auto end = Clock::now() + std::chrono::duration<double>(seconds);
    // (module, offset, thread name) -> count
    std::map<std::tuple<std::string, uint64_t, std::string>, uint64_t> hist;
    std::map<std::tuple<std::string, std::string, std::string>, uint64_t> chains;  // (waiting|running, frames, thread)
    std::unordered_map<DWORD, std::string> names;
    std::vector<DWORD> tids;
    auto next_refresh = Clock::now();
    uint64_t samples = 0;
    while (Clock::now() < end) {
      if (Clock::now() >= next_refresh) {
        next_refresh = Clock::now() + std::chrono::milliseconds(200);
        tids.clear();
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap != INVALID_HANDLE_VALUE) {
          THREADENTRY32 te{};
          te.dwSize = sizeof(te);
          for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
            if (te.th32OwnerProcessID == pid && te.th32ThreadID != self) tids.push_back(te.th32ThreadID);
          CloseHandle(snap);
        }
      }
      for (DWORD tid : tids) {
        HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid);
        if (!th) continue;
        if (SuspendThread(th) != static_cast<DWORD>(-1)) {
          CONTEXT ctx{};
          ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
          const bool got = GetThreadContext(th, &ctx) != 0;
          ResumeThread(th);
          if (got) {
            auto it = names.find(tid);
            if (it == names.end()) {
              std::string name = "?";
              PWSTR desc = nullptr;
              if (SUCCEEDED(GetThreadDescription(th, &desc)) && desc) {
                char buf[96] = {};
                WideCharToMultiByte(CP_UTF8, 0, desc, -1, buf, sizeof(buf) - 1, nullptr, nullptr);
                if (buf[0]) name = buf;
                LocalFree(desc);
              }
              for (char& c : name) if (c == ' ') c = '_';
              it = names.emplace(tid, name + "#" + std::to_string(tid)).first;
            }
            // A thread stopped outside the game's own code (waiting in ntdll, in the runtime...): walk up its stack to the first
            // frame inside the executable, so the sample says which game function it is in (or waiting from).
            bool from_caller = false;
            std::vector<uint64_t> chain;
            {
              static const uint64_t exe_base = reinterpret_cast<uint64_t>(GetModuleHandleA(nullptr));
              static const uint64_t exe_end = exe_base + reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                  exe_base + reinterpret_cast<const IMAGE_DOS_HEADER*>(exe_base)->e_lfanew)->OptionalHeader.SizeOfImage;
              uint64_t frames[8];
              const int n = WalkExeFrames(ctx, exe_base, exe_end, frames, 8);
              chain.assign(frames, frames + n);
              if (!chain.empty() && !(ctx.Rip >= exe_base && ctx.Rip < exe_end)) {
                ctx.Rip = exe_base + chain[0];
                from_caller = true;
              }
            }
            HMODULE mod = nullptr;
            std::string modname = "?";
            uint64_t offset = ctx.Rip;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCSTR>(ctx.Rip), &mod) && mod) {
              char path[MAX_PATH] = {};
              GetModuleFileNameA(mod, path, MAX_PATH);
              modname = path;
              const size_t slash = modname.find_last_of("\\/");
              if (slash != std::string::npos) modname = modname.substr(slash + 1);
              offset = ctx.Rip - reinterpret_cast<uint64_t>(mod);
            }
            if (from_caller) modname += "(waiting-in)";
            ++hist[{modname, offset, it->second}];
            if (!chain.empty()) {
              std::string key;
              for (uint64_t c : chain) key += (key.empty() ? "" : ">") + [&] { char b[24]; std::snprintf(b, sizeof(b), "%llx", (unsigned long long)c); return std::string(b); }();
              ++chains[{from_caller ? "waiting" : "running", key, it->second}];
            }
            ++samples;
          }
        }
        CloseHandle(th);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
    if (FILE* f = std::fopen(out.c_str(), "w")) {
      for (const auto& [key, count] : hist)
        std::fprintf(f, "%llu %s+%llx %s\n", static_cast<unsigned long long>(count), std::get<0>(key).c_str(),
                     static_cast<unsigned long long>(std::get<1>(key)), std::get<2>(key).c_str());
      for (const auto& [key, count] : chains)
        std::fprintf(f, "%llu chain:%s:%s %s\n", static_cast<unsigned long long>(count), std::get<0>(key).c_str(),
                     std::get<1>(key).c_str(), std::get<2>(key).c_str());
      std::fclose(f);
    }
    REXLOG_INFO("[wwe2k16-profile] {} samples over {} s written to {}", samples, seconds, out);
  }).detach();
#endif
}

}  // namespace wwe2k16
