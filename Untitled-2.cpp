#include <iostream>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <map>
#include <memory>

struct Task {
    int id;
    int data;
};

class TaskQueue {
private:
    std::queue<Task> queue;
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<bool> stop{false};

public:
    void push(Task t) {
        std::lock_guard<std::mutex> lock(mtx);
        queue.push(t);
        cv.notify_one();
    }

    bool wait_pop(Task &t) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&] { return !queue.empty() || stop; });
        if (stop && queue.empty()) return false;
        t = queue.front();
        queue.pop();
        return true;
    }

    void signal_stop() {
        std::lock_guard<std::mutex> lock(mtx);
        stop = true;
        cv.notify_all();
    }
};

class ResultCollector {
private:
    std::vector<int> results;
    std::mutex mtx;

public:
    void collect(int result) {
        std::lock_guard<std::mutex> lock(mtx);
        results.push_back(result);
    }

    void print_results() {
        std::lock_guard<std::mutex> lock(mtx);
        std::cout << "Results collected:\n";
        for (int r : results)
            std::cout << r << " ";
        std::cout << std::endl;
    }
};

class WorkerNode {
public:
    WorkerNode(int id, TaskQueue &queue, ResultCollector &collector)
        : id(id), taskQueue(queue), resultCollector(collector), stop(false) {
        last_heartbeat = std::chrono::steady_clock::now();
    }

    void start() {
        workerThread = std::thread([this]() {
            while (!stop) {
                Task task;
                if (taskQueue.wait_pop(task)) {
                    int result = task.data * task.data;
                    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Simulate work
                    resultCollector.collect(result);
                    std::cout << "[Worker " << id << "] Completed Task " << task.id << std::endl;
                }
                send_heartbeat();
            }
        });
    }

    void stop_worker() {
        stop = true;
        taskQueue.signal_stop();
        if (workerThread.joinable())
            workerThread.join();
    }

    void send_heartbeat() {
        std::lock_guard<std::mutex> lock(mtx);
        last_heartbeat = std::chrono::steady_clock::now();
    }

    bool is_alive() {
        std::lock_guard<std::mutex> lock(mtx);
        return std::chrono::steady_clock::now() - last_heartbeat < std::chrono::seconds(2);
    }

    int get_id() const { return id; }

private:
    int id;
    TaskQueue &taskQueue;
    ResultCollector &resultCollector;
    std::thread workerThread;
    std::atomic<bool> stop;
    std::mutex mtx;
    std::chrono::steady_clock::time_point last_heartbeat; // Corrected atomic issue
};

class WorkerManager {
private:
    std::vector<std::unique_ptr<WorkerNode>> workers;
    std::mutex mtx;

public:
    void register_worker(int id, TaskQueue &queue, ResultCollector &collector) {
        std::lock_guard<std::mutex> lock(mtx);
        workers.emplace_back(std::make_unique<WorkerNode>(id, queue, collector));
        workers.back()->start();
    }

    void redistribute_tasks(TaskQueue &queue) {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto &worker : workers) {
            if (!worker->is_alive()) {
                std::cout << "[Worker " << worker->get_id() << "] FAILED! Redistributing tasks..." << std::endl;
                queue.push({worker->get_id(), worker->get_id() + 1});
            }
        }
    }

    void stop_all_workers() {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto &worker : workers)
            worker->stop_worker();
    }
};

class SecurityManager {
public:
    bool authenticate_worker(int worker_id) {
        std::lock_guard<std::mutex> lock(mtx);
        if (authorized_workers.find(worker_id) != authorized_workers.end()) {
            std::cout << "[Worker " << worker_id << "] Authenticated Successfully!" << std::endl;
            return true;
        }
        std::cout << "[Worker " << worker_id << "] Authentication FAILED!" << std::endl;
        return false;
    }

private:
    std::mutex mtx;
    std::map<int, bool> authorized_workers = {{1, true}, {2, true}, {3, true}, {4, true}};
};

int main() {
    TaskQueue taskQueue;
    ResultCollector resultCollector;
    WorkerManager workerManager;
    SecurityManager securityManager;

    for (int i = 1; i <= 4; ++i) {
        if (securityManager.authenticate_worker(i))
            workerManager.register_worker(i, taskQueue, resultCollector);
    }

    for (int i = 0; i < 10; ++i) {
        taskQueue.push({i, i + 1});
    }

    for (int i = 0; i < 5; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        workerManager.redistribute_tasks(taskQueue);
    }

    taskQueue.signal_stop();
    workerManager.stop_all_workers();
    resultCollector.print_results();
    return 0;
}