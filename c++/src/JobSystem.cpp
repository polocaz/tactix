#include "JobSystem.hpp"
#include "spdlog/spdlog.h"

JobSystem::JobSystem(uint32_t requestedWorkers) {
    // 0 means auto: hardware concurrency, leaving 1 core for main thread and rendering.
    // hardware_concurrency() can return 0 when it's unable to determine the value; a bare
    // `std::max(1u, hc - 1)` underflows to 0xFFFFFFFF in that case since the subtraction
    // happens before max() ever sees it. Guard explicitly instead.
    const uint32_t hc = std::thread::hardware_concurrency();
    workerCount = (requestedWorkers > 0)
        ? requestedWorkers
        : (hc > 1 ? hc - 1 : 1u);

    spdlog::info("JobSystem: Starting {} worker threads", workerCount);
    
    // Spawn worker threads
    for (uint32_t i = 0; i < workerCount; ++i) {
        workers.emplace_back(&JobSystem::workerLoop, this);
    }
}

JobSystem::~JobSystem() {
    // Signal workers to stop
    running = false;
    queueCV.notify_all();
    
    // Wait for all workers to finish
    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    spdlog::info("JobSystem: Shutdown complete");
}

void JobSystem::submit(Job job) {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        jobQueue.push(std::move(job));
        activeJobs++;
    }
    queueCV.notify_one();
}

void JobSystem::waitAll() {
    std::unique_lock<std::mutex> lock(waitMutex);
    // activeJobs alone is the whole condition. submit() increments it under
    // queueMutex before pushing, and a worker decrements it only after its job
    // has finished running, so a zero count means the queue is drained AND
    // nothing is still executing.
    //
    // The predicate used to also read jobQueue.empty(). That was a data race:
    // jobQueue is guarded by queueMutex, not the waitMutex held here, so the
    // read ran concurrently with workers popping from it. The read was also
    // redundant, which is why removing it is the whole fix.
    waitCV.wait(lock, [this]() { return activeJobs.load() == 0; });
}

void JobSystem::workerLoop() {
    while (running) {
        Job job;
        
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCV.wait(lock, [this]() { 
                return !jobQueue.empty() || !running; 
            });
            
            if (!running && jobQueue.empty()) {
                break;
            }
            
            if (!jobQueue.empty()) {
                job = std::move(jobQueue.front());
                jobQueue.pop();
            }
        }
        
        if (job) {
            job();
            jobsExecuted++;
            
            // Decrement active jobs and notify waitAll if done
            if (--activeJobs == 0) {
                std::lock_guard<std::mutex> lock(waitMutex);
                waitCV.notify_all();
            }
        }
    }
}
