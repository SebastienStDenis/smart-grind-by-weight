/**
 * FreeRTOS primitives on host threads.
 *
 * Suspension is cooperative: a suspended task parks the next time it calls
 * vTaskDelay/vTaskDelayUntil, which every firmware task loop does once per
 * cycle. Core pinning is ignored; macOS schedules the threads.
 */

#include <freertos/FreeRTOS.h>

#include <Arduino.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

struct SimTask {
    std::string name;
    TaskFunction_t function = nullptr;
    void* parameter = nullptr;
    std::thread thread;
    std::atomic<bool> suspended{false};
    std::atomic<bool> deleted{false};
    std::mutex park_mutex;
    std::condition_variable park_cv;
};

struct SimQueue {
    std::mutex mutex;
    std::condition_variable not_empty;
    std::condition_variable not_full;
    std::deque<std::vector<uint8_t>> items;
    size_t item_size = 0;
    size_t capacity = 0;
};

struct SimSemaphore {
    std::recursive_timed_mutex mutex;
};

std::mutex& registry_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<std::unique_ptr<SimTask>>& registry() {
    static std::vector<std::unique_ptr<SimTask>> tasks;
    return tasks;
}

thread_local SimTask* current_task = nullptr;

std::string& main_thread_task_name() {
    static std::string name;
    return name;
}

/** Blocks while the calling task is suspended. */
void park_if_suspended() {
    SimTask* task = current_task;
    if (!task) return;

    std::unique_lock<std::mutex> lock(task->park_mutex);
    task->park_cv.wait(lock, [task] { return !task->suspended.load(); });
}

void task_entry(SimTask* task) {
    current_task = task;
    task->function(task->parameter);
}

SimTask* as_task(TaskHandle_t handle) {
    return static_cast<SimTask*>(handle);
}

}  // namespace

void sim_set_main_thread_task(const char* name) {
    main_thread_task_name() = name ? name : "";
}

void sim_run_main_thread_task(const char* name) {
    SimTask* target = nullptr;
    {
        std::lock_guard<std::mutex> lock(registry_mutex());
        for (auto& task : registry()) {
            if (task->name == name) {
                target = task.get();
                break;
            }
        }
    }

    if (!target) {
        Serial.printf("[SIM] No task named '%s' was created - nothing to run\n", name);
        return;
    }

    task_entry(target);
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t stack_depth,
                                   void* parameters, UBaseType_t priority,
                                   TaskHandle_t* created_task, BaseType_t core_id) {
    (void)stack_depth;
    (void)priority;
    (void)core_id;

    auto task = std::make_unique<SimTask>();
    task->name = name ? name : "task";
    task->function = fn;
    task->parameter = parameters;

    SimTask* raw = task.get();
    {
        std::lock_guard<std::mutex> lock(registry_mutex());
        registry().push_back(std::move(task));
    }

    if (created_task) *created_task = raw;

    /* The UI task is handed to the main thread instead, because SDL windows
     * and their event queue must live there on macOS. */
    if (raw->name != main_thread_task_name()) {
        raw->thread = std::thread(task_entry, raw);
    }

    return pdPASS;
}

BaseType_t xTaskCreate(TaskFunction_t fn, const char* name, uint32_t stack_depth,
                       void* parameters, UBaseType_t priority, TaskHandle_t* created_task) {
    return xTaskCreatePinnedToCore(fn, name, stack_depth, parameters, priority, created_task, 0);
}

void vTaskDelete(TaskHandle_t handle) {
    SimTask* task = handle ? as_task(handle) : current_task;
    if (!task) return;

    task->deleted.store(true);
    vTaskResume(task);

    if (task->thread.joinable() && task->thread.get_id() != std::this_thread::get_id()) {
        task->thread.detach();
    }
}

void vTaskDelay(TickType_t ticks) {
    park_if_suspended();
    if (ticks > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ticks));
    } else {
        std::this_thread::yield();
    }
}

void vTaskDelayUntil(TickType_t* previous_wake_time, TickType_t increment) {
    park_if_suspended();

    if (!previous_wake_time) {
        vTaskDelay(increment);
        return;
    }

    TickType_t target = *previous_wake_time + increment;
    TickType_t now = xTaskGetTickCount();

    /* A task that overran its period runs again immediately, as FreeRTOS does. */
    if ((int32_t)(target - now) > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(target - now));
    }

    *previous_wake_time = target;
}

void vTaskSuspend(TaskHandle_t handle) {
    SimTask* task = handle ? as_task(handle) : current_task;
    if (!task) return;
    task->suspended.store(true);
}

void vTaskResume(TaskHandle_t handle) {
    SimTask* task = as_task(handle);
    if (!task) return;

    {
        std::lock_guard<std::mutex> lock(task->park_mutex);
        task->suspended.store(false);
    }
    task->park_cv.notify_all();
}

TickType_t xTaskGetTickCount() {
    return (TickType_t)millis();
}

TaskHandle_t xTaskGetCurrentTaskHandle() {
    return current_task;
}

eTaskState eTaskGetState(TaskHandle_t handle) {
    SimTask* task = as_task(handle);
    if (!task) return eInvalid;
    if (task->deleted.load()) return eDeleted;
    return task->suspended.load() ? eSuspended : eRunning;
}

UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t handle) {
    (void)handle;
    return 4096;
}

BaseType_t xPortGetCoreID() {
    return 0;
}

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size) {
    auto* queue = new SimQueue();
    queue->capacity = length;
    queue->item_size = item_size;
    return queue;
}

static BaseType_t queue_send(QueueHandle_t handle, const void* item, TickType_t ticks_to_wait) {
    auto* queue = static_cast<SimQueue*>(handle);
    if (!queue || !item) return pdFAIL;

    std::unique_lock<std::mutex> lock(queue->mutex);
    if (queue->items.size() >= queue->capacity) {
        if (ticks_to_wait == 0) return pdFAIL;

        auto has_room = [queue] { return queue->items.size() < queue->capacity; };
        if (ticks_to_wait == portMAX_DELAY) {
            queue->not_full.wait(lock, has_room);
        } else if (!queue->not_full.wait_for(lock, std::chrono::milliseconds(ticks_to_wait), has_room)) {
            return pdFAIL;
        }
    }

    const uint8_t* bytes = static_cast<const uint8_t*>(item);
    queue->items.emplace_back(bytes, bytes + queue->item_size);
    lock.unlock();
    queue->not_empty.notify_one();
    return pdPASS;
}

BaseType_t xQueueSend(QueueHandle_t handle, const void* item, TickType_t ticks_to_wait) {
    return queue_send(handle, item, ticks_to_wait);
}

BaseType_t xQueueSendToBack(QueueHandle_t handle, const void* item, TickType_t ticks_to_wait) {
    return queue_send(handle, item, ticks_to_wait);
}

BaseType_t xQueueReceive(QueueHandle_t handle, void* buffer, TickType_t ticks_to_wait) {
    auto* queue = static_cast<SimQueue*>(handle);
    if (!queue || !buffer) return pdFAIL;

    std::unique_lock<std::mutex> lock(queue->mutex);
    if (queue->items.empty()) {
        if (ticks_to_wait == 0) return pdFAIL;

        auto has_item = [queue] { return !queue->items.empty(); };
        if (ticks_to_wait == portMAX_DELAY) {
            queue->not_empty.wait(lock, has_item);
        } else if (!queue->not_empty.wait_for(lock, std::chrono::milliseconds(ticks_to_wait), has_item)) {
            return pdFAIL;
        }
    }

    std::memcpy(buffer, queue->items.front().data(), queue->item_size);
    queue->items.pop_front();
    lock.unlock();
    queue->not_full.notify_one();
    return pdPASS;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t handle) {
    auto* queue = static_cast<SimQueue*>(handle);
    if (!queue) return 0;

    std::lock_guard<std::mutex> lock(queue->mutex);
    return (UBaseType_t)queue->items.size();
}

BaseType_t xQueueReset(QueueHandle_t handle) {
    auto* queue = static_cast<SimQueue*>(handle);
    if (!queue) return pdFAIL;

    std::lock_guard<std::mutex> lock(queue->mutex);
    queue->items.clear();
    return pdPASS;
}

void vQueueDelete(QueueHandle_t handle) {
    delete static_cast<SimQueue*>(handle);
}

SemaphoreHandle_t xSemaphoreCreateMutex() {
    return new SimSemaphore();
}

SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* buffer) {
    (void)buffer;
    return new SimSemaphore();
}

SemaphoreHandle_t xSemaphoreCreateBinary() {
    return new SimSemaphore();
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t ticks_to_wait) {
    auto* semaphore = static_cast<SimSemaphore*>(handle);
    if (!semaphore) return pdFAIL;

    if (ticks_to_wait == portMAX_DELAY) {
        semaphore->mutex.lock();
        return pdPASS;
    }
    if (ticks_to_wait == 0) {
        return semaphore->mutex.try_lock() ? pdPASS : pdFAIL;
    }
    return semaphore->mutex.try_lock_for(std::chrono::milliseconds(ticks_to_wait)) ? pdPASS : pdFAIL;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t handle) {
    auto* semaphore = static_cast<SimSemaphore*>(handle);
    if (!semaphore) return pdFAIL;

    semaphore->mutex.unlock();
    return pdPASS;
}

void vSemaphoreDelete(SemaphoreHandle_t handle) {
    delete static_cast<SimSemaphore*>(handle);
}
