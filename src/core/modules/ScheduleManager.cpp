#include "ScheduleManager.hpp"
#include "api/ThreadPool.hpp"

Scheduler& Scheduler::getInstance() {
    static std::unique_ptr<Scheduler> instance;
    if (!instance) {
        instance = std::make_unique<Scheduler>();
        KobeBryant::getInstance().getThreadPool().enqueue([&] {
            while (EXIST_FLAG) {
                std::unique_lock<std::mutex> lock(instance->mMtx);
                instance->mCv.wait(lock, [&] { return !instance->mTasks.empty(); });

                auto              now = std::chrono::high_resolution_clock::now();
                std::vector<Task> readyTasks;

                // Collect all tasks that are ready to run
                for (size_t i = 0; i < instance->mTasks.size(); ++i) {
                    auto& taskPtr = instance->mTasks[i];
                    if (taskPtr->cancelled.load()) {
                        instance->mTaskIndexMap.erase(taskPtr->id);
                        continue;
                    }

                    if (taskPtr->runTime <= now) {
                        readyTasks.push_back(taskPtr->task);
                        if (taskPtr->interval.count() > 0) {
                            taskPtr->runTime = now + taskPtr->interval;
                        } else {
                            instance->mTaskIndexMap.erase(taskPtr->id);
                            instance->mTasks.erase(instance->mTasks.begin() + i);
                            --i; // Adjust index after erase
                        }
                    }
                }

                // Notify the main thread to execute the collected tasks
                lock.unlock();
                for (auto& task : readyTasks) {
                    if (task) {
                        task(); // Execute the task in the main thread
                    }
                }
            }
            instance->mCv.notify_all();
        });
    }
    return *instance;
}

Scheduler::TaskID Scheduler::addDelayTask(std::chrono::milliseconds delay, Task const& task) {
    std::lock_guard<std::mutex> lock(mMtx);
    TaskID                      id       = mNextTaskID++;
    auto                        taskInfo = std::make_shared<TaskInfo>(
        id,
        std::move(task),
        std::chrono::steady_clock::now() + delay,
        std::chrono::milliseconds(0)
    );
    mTasks.push_back(taskInfo);
    mTaskIndexMap[id] = mTasks.size() - 1;
    mCv.notify_one();
    return id;
}

Scheduler::TaskID Scheduler::addRepeatTask(std::chrono::milliseconds interval, Task const& task) {
    std::lock_guard<std::mutex> lock(mMtx);
    TaskID                      id = mNextTaskID++;
    auto                        taskInfo =
        std::make_shared<TaskInfo>(id, std::move(task), std::chrono::steady_clock::now() + interval, interval);
    mTasks.push_back(taskInfo);
    mTaskIndexMap[id] = mTasks.size() - 1;
    mCv.notify_one();
    return id;
}

bool Scheduler::cancelTask(TaskID id) {
    std::lock_guard<std::mutex> lock(mMtx);
    auto                        it = mTaskIndexMap.find(id);
    if (it != mTaskIndexMap.end()) {
        mTasks[it->second]->cancelled.store(true);
        return true;
    }
    return false;
}

ScheduleManager& ScheduleManager::getInstance() {
    static std::unique_ptr<ScheduleManager> instance;
    if (!instance) {
        instance = std::make_unique<ScheduleManager>();
    }
    return *instance;
}

std::string ScheduleManager::getTaskOwner(size_t id) {
    if (mTaskIdMap.contains(id)) {
        return mTaskIdMap[id];
    }
    return {};
}

size_t ScheduleManager::addDelayTask(
    std::string const&           plugin,
    std::chrono::milliseconds    delay,
    std::function<void()> const& task
) {
    auto id = Scheduler::getInstance().addDelayTask(delay, task);
    mPluginTasks[plugin].insert(id);
    mTaskIdMap[id] = plugin;
    return id;
}

size_t ScheduleManager::addRepeatTask(
    std::string const&           plugin,
    std::chrono::milliseconds    interval,
    std::function<void()> const& task
) {
    auto id = Scheduler::getInstance().addRepeatTask(interval, task);
    mPluginTasks[plugin].insert(id);
    mTaskIdMap[id] = plugin;
    return id;
}

size_t ScheduleManager::addRepeatTask(
    std::string const&           plugin,
    std::chrono::milliseconds    interval,
    std::function<void()> const& task,
    uint64_t                     times
) {

    size_t id      = Scheduler::getInstance().addRepeatTask(interval, [=] {
        if (task) {
            task();
        }
        mTaskTimes[id]--;
        if (mTaskTimes[id] <= 0) {
            ScheduleManager::getInstance().cancelTask(plugin, id);
        }
    });
    mTaskTimes[id] = times;
    mPluginTasks[plugin].insert(id);
    mTaskIdMap[id] = plugin;
    return id;
}

bool ScheduleManager::cancelTask(std::string const& owner, size_t id) {
    auto plugin = getTaskOwner(id);
    if (owner == plugin) {
        mPluginTasks[plugin].erase(id);
        mTaskIdMap.erase(id);
        mTaskTimes.erase(id);
        return Scheduler::getInstance().cancelTask(id);
    }
    return false;
}

void ScheduleManager::removePluginTasks(std::string const& plugin) {
    for (auto& id : mPluginTasks[plugin]) {
        cancelTask(plugin, id);
    }
    mPluginTasks.erase(plugin);
}

void ScheduleManager::removeAllTasks() {
    for (auto& [id, plugin] : mTaskIdMap) {
        cancelTask(plugin, id);
    }
    mTaskIdMap.clear();
    mPluginTasks.clear();
    mTaskTimes.clear();
}