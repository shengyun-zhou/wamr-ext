#pragma once
#include "BaseDef.h"
#include <thread>
#include <uv.h>

namespace WAMR_EXT_NS{
    class LoopThread {
    public:
        LoopThread(const char* threadName);
        ~LoopThread();

        void Start();
        void Stop();
        bool PostTimerTask(const std::function<void()>& cb, uint64_t delay, uint64_t repeatInterval);
    private:
        struct UVHandleBaseData {
            union {
                uv_timer_t timer;
            } uvHandle;
            LoopThread* pThisThread;

            UVHandleBaseData(LoopThread* _pThisThread) : pThisThread(_pThisThread) {}
            UVHandleBaseData(const UVHandleBaseData&) = delete;
            UVHandleBaseData& operator=(const UVHandleBaseData&) = delete;
        };

        struct UVHandleTimerData : public UVHandleBaseData {
            std::function<void()> cb;
            UVHandleTimerData(LoopThread* pThisThread, uv_loop_t* pLoop, const std::function<void()>& _cb) :
                UVHandleBaseData(pThisThread), cb(_cb) {
                uv_timer_init(pLoop, &uvHandle.timer);
            }
        };

        static void DestroyUVHandleData(uv_handle_t* pUVHandle);
        bool DoPostTimerTask(const std::function<void()>& cb, uint64_t delay, uint64_t repeatInterval);

        std::string m_threadName;
        std::shared_ptr<std::thread> m_pThread;
        std::mutex m_lock;
        bool m_bRunning{false};
        uv_loop_t m_uvloop;
        uv_async_t m_uvAsync;
    };
}
