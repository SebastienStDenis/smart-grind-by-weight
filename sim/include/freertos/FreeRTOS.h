/**
 * Host stand-in for FreeRTOS.
 *
 * Tasks become std::threads, queues become bounded blocking deques and
 * semaphores become recursive mutexes. Suspend/resume is cooperative: a
 * suspended task parks inside its next vTaskDelay, which is where every
 * firmware task loop yields.
 */
#pragma once

#include <cstddef>
#include <cstdint>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef void* TaskHandle_t;
typedef void* QueueHandle_t;
typedef void* SemaphoreHandle_t;
typedef uint32_t StackType_t;

struct StaticSemaphore_t {
    void* placeholder;
};

typedef void (*TaskFunction_t)(void*);

enum eTaskState {
    eRunning = 0,
    eReady,
    eBlocked,
    eSuspended,
    eDeleted,
    eInvalid
};

#define pdTRUE  ((BaseType_t)1)
#define pdFALSE ((BaseType_t)0)
#define pdPASS  pdTRUE
#define pdFAIL  pdFALSE

#define portTICK_PERIOD_MS 1u
#define portMAX_DELAY ((TickType_t)0xFFFFFFFFu)
#define configMAX_PRIORITIES 25
#define configTICK_RATE_HZ 1000

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define pdTICKS_TO_MS(ticks) ((uint32_t)(ticks))

/* Tasks */
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t stack_depth,
                                   void* parameters, UBaseType_t priority,
                                   TaskHandle_t* created_task, BaseType_t core_id);
BaseType_t xTaskCreate(TaskFunction_t fn, const char* name, uint32_t stack_depth,
                       void* parameters, UBaseType_t priority, TaskHandle_t* created_task);
void vTaskDelete(TaskHandle_t task);
void vTaskDelay(TickType_t ticks);
void vTaskDelayUntil(TickType_t* previous_wake_time, TickType_t increment);
void vTaskSuspend(TaskHandle_t task);
void vTaskResume(TaskHandle_t task);
TickType_t xTaskGetTickCount();
TaskHandle_t xTaskGetCurrentTaskHandle();
eTaskState eTaskGetState(TaskHandle_t task);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
BaseType_t xPortGetCoreID();

/* Queues */
QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size);
BaseType_t xQueueSend(QueueHandle_t queue, const void* item, TickType_t ticks_to_wait);
BaseType_t xQueueSendToBack(QueueHandle_t queue, const void* item, TickType_t ticks_to_wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void* buffer, TickType_t ticks_to_wait);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue);
BaseType_t xQueueReset(QueueHandle_t queue);
void vQueueDelete(QueueHandle_t queue);

/* Semaphores */
SemaphoreHandle_t xSemaphoreCreateMutex();
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* buffer);
SemaphoreHandle_t xSemaphoreCreateBinary();
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks_to_wait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore);
void vSemaphoreDelete(SemaphoreHandle_t semaphore);

/** Runs the task registered under the given name on the calling thread.
 *  Used for the UI task, because SDL requires the main thread on macOS. */
void sim_run_main_thread_task(const char* name);
/** Names the task that must not be spawned onto its own thread. */
void sim_set_main_thread_task(const char* name);
