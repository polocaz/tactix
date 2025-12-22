#pragma once
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

// Simple job system for parallel entity updates (Design Doc §6)
class JobSystem {
public:
    using Job = std::function<void()>;
    
    JobSystem();
    ~JobSystem();
    
    // Submit a job to be executed by worker threads
    void submit(Job job);
    
    // Wait for all submitted jobs to complete (barrier pattern, Design Doc §6.3)
    void waitAll();
    
    // Metrics
    uint32_t getWorkerCount() const { return workerCount; }
    uint32_t getJobsExecuted() const { return jobsExecuted.load(); }
    void resetJobCounter() { jobsExecuted = 0; }

private:
    uint32_t workerCount;
    std::vector<std::thread> workers;
    
    std::queue<Job> jobQueue;
    std::mutex queueMutex;
    std::condition_variable queueCV;
    
    std::atomic<bool> running{true};
    std::atomic<uint32_t> activeJobs{0};
    std::atomic<uint32_t> jobsExecuted{0};
    
    std::mutex waitMutex;
    std::condition_variable waitCV;
    
    void workerLoop();
};
