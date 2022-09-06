#include "LoopThread.h"
#include "Utility.h"

namespace WAMR_EXT_NS {
    LoopThread::LoopThread(const char *threadName) : m_threadName(threadName) {}

    LoopThread::~LoopThread() {
        Stop();
    }

    void LoopThread::Start() {
        std::lock_guard<std::mutex> _al(m_lock);
        if (m_bRunning)
            return;
        uv_loop_init(&m_uvloop);
        uv_async_init(&m_uvloop, &m_uvAsync, nullptr);
        m_pThread = std::make_shared<std::thread>([this]() {
            Utility::SetCurrentThreadName(m_threadName.c_str());
            uv_run(&m_uvloop, UV_RUN_DEFAULT);
        });
        m_bRunning = true;
    }

    void LoopThread::Stop() {
        std::lock_guard<std::mutex> _al(m_lock);
        if (!m_bRunning)
            return;
        uv_walk(&m_uvloop, [](uv_handle_t* pUVHandle, void*) {
            uv_close(pUVHandle, &LoopThread::DestroyUVHandleData);
        }, nullptr);
        uv_async_send(&m_uvAsync);
        m_pThread->join();
        int ret = uv_loop_close(&m_uvloop);
        assert(ret == 0);
        m_pThread.reset();
        m_bRunning = false;
    }

    bool LoopThread::PostTimerTask(const std::function<void()> &cb, uint64_t delay, uint64_t repeatInterval) {
        std::lock_guard<std::mutex> _al(m_lock);
        if (!m_bRunning)
            return false;
        return DoPostTimerTask(cb, delay, repeatInterval);
    }

    bool LoopThread::DoPostTimerTask(const std::function<void()> &cb, uint64_t delay, uint64_t repeatInterval) {
        auto* pData = new UVHandleTimerData(this, &m_uvloop, cb);
        if (uv_timer_start(&pData->uvHandle.timer, [](uv_timer_t* pUVTimer) {
            auto* pData = (UVHandleTimerData*)pUVTimer;
            pData->cb();
            uv_update_time(pUVTimer->loop);
            if (uv_timer_get_repeat(pUVTimer) == 0)
                uv_close((uv_handle_t*)pUVTimer, &LoopThread::DestroyUVHandleData);
        }, delay, repeatInterval) != 0) {
            delete pData;
            return false;
        }
        uv_async_send(&m_uvAsync);
        return true;
    }

    void LoopThread::DestroyUVHandleData(uv_handle_t *pUVHandle) {
        if (pUVHandle->type == UV_TIMER) {
            auto *pData = (UVHandleTimerData*)pUVHandle;
            delete pData;
        } else if (pUVHandle->type == UV_ASYNC) {
            return;
        } else {
            assert(false);
        }
    }
}
