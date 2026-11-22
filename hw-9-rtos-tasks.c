/*
 * File: hw-9-rtos-tasks.c
 * Project: Smart Traffic Light Controller
 *
 * Educational RTOS application layer.
 *
 * IMPORTANT:
 * This file intentionally uses an RTOS adapter API declared below.
 * The adapter must be implemented by the selected RTOS (or by a simulator).
 * Mutex blocking, priority inheritance, semaphore blocking, task creation,
 * and scheduling are NOT emulated with volatile variables in this file.
 *
 * Priority convention: a larger numeric value means a higher priority.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* --------------------------- Shared definitions -------------------------- */

#define BIT_NS_RED          (1U << 0)
#define BIT_NS_YELLOW       (1U << 1)
#define BIT_NS_GREEN        (1U << 2)
#define BIT_EW_RED          (1U << 3)
#define BIT_EW_YELLOW       (1U << 4)
#define BIT_EW_GREEN        (1U << 5)
#define BIT_PED_WALK        (1U << 6)
#define BIT_PED_STOP        (1U << 7)
#define BIT_SAFETY_LED      (1U << 8)

#define BIT_PED_REQ         (1U << 0)
#define BIT_EMG_SENS        (1U << 1)
#define BIT_NIGHT_EN        (1U << 2)
#define BIT_SENSOR_OK       (1U << 3)

#define EMG_DIR_NONE        0U
#define EMG_DIR_NS          1U
#define EMG_DIR_EW          2U

#define PHASE_NS_GREEN      0U
#define PHASE_NS_YELLOW     1U
#define PHASE_ALL_RED_EW    2U
#define PHASE_EW_GREEN      3U
#define PHASE_EW_YELLOW     4U
#define PHASE_ALL_RED_NS    5U
#define PHASE_PED_CROSS     6U
#define PHASE_NIGHT_MODE    7U
#define PHASE_EMG_PRIORITY  8U

#define PRIO_EMERGENCY_TASK       4U
#define PRIO_PEDESTRIAN_TASK      3U
#define PRIO_TRAFFIC_CONTROL_TASK 2U
#define PRIO_SENSOR_TASK          1U

#define PERIOD_EMERGENCY_MS       20U
#define PERIOD_PEDESTRIAN_MS      50U
#define PERIOD_TRAFFIC_CONTROL_MS 100U
#define PERIOD_SENSOR_MS          100U

#define LIGHTS_OUT_DATA_REG_ADDR  0x40000004U
#define SENSORS_STATUS_REG_ADDR   0x40001000U
#define DENSITY_NS_REG_ADDR       0x40001004U
#define DENSITY_EW_REG_ADDR       0x40001008U
#define EMG_DIR_REG_ADDR          0x4000100CU

/* ------------------------------ RTOS adapter ----------------------------- */

/*
 * Required adapter semantics:
 * - mutex_create(PRIORITY_INHERITANCE) creates an ownership-aware mutex.
 * - mutex_take blocks (or returns false on timeout); it never steals ownership.
 * - mutex_give is called only by the owning task.
 * - semaphore_take blocks until a token arrives or timeout expires.
 * - event_set_from_isr is ISR-safe and requests a context switch when needed.
 * - task_create and scheduler_start are supplied by the chosen RTOS.
 *
 * The following declarations are an interface contract, not an RTOS
 * implementation. Map them to FreeRTOS, Zephyr, or another RTOS API.
 */
typedef void *rtos_mutex_t;
typedef void *rtos_semaphore_t;
typedef void *rtos_task_t;

enum { RTOS_MUTEX_PRIORITY_INHERITANCE = 1U };

extern rtos_mutex_t rtos_mutex_create(uint32_t attributes);
extern bool rtos_mutex_take(rtos_mutex_t mutex, uint32_t timeout_ms);
extern void rtos_mutex_give(rtos_mutex_t mutex);

extern rtos_semaphore_t rtos_semaphore_create(uint32_t initial_count,
                                               uint32_t maximum_count);
extern bool rtos_semaphore_take(rtos_semaphore_t sem, uint32_t timeout_ms);
extern void rtos_semaphore_give(rtos_semaphore_t sem);
extern void rtos_semaphore_give_from_isr(rtos_semaphore_t sem,
                                         bool *higher_priority_task_woken);

extern bool rtos_task_create(void (*entry)(void *),
                             const char *name,
                             uint32_t stack_words,
                             void *argument,
                             uint32_t priority,
                             rtos_task_t *created_task);
extern void rtos_scheduler_start(void);
extern void rtos_delay_ms(uint32_t delay_ms);
extern uint32_t rtos_get_tick_ms(void);

/* ---------------------------- Simulated MMIO ----------------------------- */

/*
 * These variables model registers for host-side simulation only.
 * They are not physical MMIO addresses and must not be used on a target MCU.
 */
volatile uint32_t sim_lights_out  = 0U;
volatile uint32_t sim_sensors_reg = BIT_SENSOR_OK;
volatile uint32_t sim_density_ns  = 20U;
volatile uint32_t sim_density_ew  = 15U;
volatile uint32_t sim_emg_dir_reg = EMG_DIR_NS;

static uint32_t mmio_read_sensors(void)
{
    (void)SENSORS_STATUS_REG_ADDR;
    return sim_sensors_reg;
}

static uint32_t mmio_read_density_ns(void)
{
    (void)DENSITY_NS_REG_ADDR;
    return sim_density_ns;
}

static uint32_t mmio_read_density_ew(void)
{
    (void)DENSITY_EW_REG_ADDR;
    return sim_density_ew;
}

static uint32_t mmio_read_emergency_direction(void)
{
    (void)EMG_DIR_REG_ADDR;
    if (sim_emg_dir_reg == EMG_DIR_NS || sim_emg_dir_reg == EMG_DIR_EW)
        return sim_emg_dir_reg;
    return EMG_DIR_NONE;
}

static void mmio_write_lights(uint32_t mask)
{
    (void)LIGHTS_OUT_DATA_REG_ADDR;
    sim_lights_out = mask;
}

/* ----------------------------- Shared state ------------------------------ */

typedef struct {
    uint32_t ns_density;
    uint32_t ew_density;
    bool emergency_active;
    uint32_t emergency_direction;
    bool emergency_release_pending;
    bool night_mode;
    bool sensor_ok;
    bool pedestrian_pending;
    uint32_t phase;
    uint32_t phase_started_ms;
} intersection_state_t;

static intersection_state_t g_state = {
    .ns_density = 20U,
    .ew_density = 15U,
    .emergency_active = false,
    .emergency_direction = EMG_DIR_NONE,
    .emergency_release_pending = false,
    .night_mode = false,
    .sensor_ok = true,
    .pedestrian_pending = false,
    .phase = PHASE_NS_GREEN,
    .phase_started_ms = 0U
};

static rtos_mutex_t g_state_mutex;
static rtos_semaphore_t g_emergency_event;
static rtos_semaphore_t g_pedestrian_event;
static rtos_semaphore_t g_control_event;

/* Event bits must match hw-8-isr.c. */
#define EVT_TIMER_TICK   (1U << 0)
#define EVT_PED_REQUEST  (1U << 1)
#define EVT_EMG_ACTIVE   (1U << 2)
#define EVT_EMG_RELEASE  (1U << 3)

/* Called by ISR code. The adapter operation must be ISR-safe. */
void rtos_notify_from_isr(uint32_t event_mask)
{
    bool higher_priority_task_woken = false;

    if ((event_mask & EVT_EMG_ACTIVE) != 0U)
        rtos_semaphore_give_from_isr(g_emergency_event,
                                     &higher_priority_task_woken);

    if ((event_mask & EVT_EMG_RELEASE) != 0U)
        rtos_semaphore_give_from_isr(g_emergency_event,
                                     &higher_priority_task_woken);

    if ((event_mask & EVT_PED_REQUEST) != 0U)
        rtos_semaphore_give_from_isr(g_pedestrian_event,
                                     &higher_priority_task_woken);

    if ((event_mask & EVT_TIMER_TICK) != 0U)
        rtos_semaphore_give_from_isr(g_control_event,
                                     &higher_priority_task_woken);

    rtos_semaphore_give_from_isr(g_control_event,
                                 &higher_priority_task_woken);

    /* The adapter performs the RTOS-specific yield-from-ISR operation. */
}

/* --------------------------- Output/state helpers ------------------------ */

static uint32_t output_for_phase(uint32_t phase, uint32_t emg_direction)
{
    switch (phase) {
    case PHASE_NS_GREEN:
        return BIT_NS_GREEN | BIT_EW_RED | BIT_PED_STOP;
    case PHASE_NS_YELLOW:
        return BIT_NS_YELLOW | BIT_EW_RED | BIT_PED_STOP;
    case PHASE_ALL_RED_EW:
    case PHASE_ALL_RED_NS:
        return BIT_NS_RED | BIT_EW_RED | BIT_PED_STOP;
    case PHASE_EW_GREEN:
        return BIT_NS_RED | BIT_EW_GREEN | BIT_PED_STOP;
    case PHASE_EW_YELLOW:
        return BIT_NS_RED | BIT_EW_YELLOW | BIT_PED_STOP;
    case PHASE_PED_CROSS:
        return BIT_NS_RED | BIT_EW_RED | BIT_PED_WALK;
    case PHASE_NIGHT_MODE:
        /* Defined project assumption: flashing is represented as steady yellow. */
        return BIT_NS_YELLOW | BIT_EW_RED | BIT_PED_STOP | BIT_SAFETY_LED;
    case PHASE_EMG_PRIORITY:
        if (emg_direction == EMG_DIR_EW)
            return BIT_NS_RED | BIT_EW_GREEN | BIT_PED_STOP | BIT_SAFETY_LED;
        if (emg_direction == EMG_DIR_NS)
            return BIT_NS_GREEN | BIT_EW_RED | BIT_PED_STOP | BIT_SAFETY_LED;
        return BIT_NS_RED | BIT_EW_RED | BIT_PED_STOP | BIT_SAFETY_LED;
    default:
        return BIT_NS_RED | BIT_EW_RED | BIT_PED_STOP | BIT_SAFETY_LED;
    }
}

static uint32_t elapsed_in_phase(uint32_t now, uint32_t started)
{
    /* Unsigned subtraction remains valid across tick-counter wraparound. */
    return now - started;
}

/* ------------------------------- RTOS tasks ------------------------------ */

void EmergencyTask(void *argument)
{
    (void)argument;

    for (;;) {
        (void)rtos_semaphore_take(g_emergency_event, PERIOD_EMERGENCY_MS);

        const uint32_t status = mmio_read_sensors();
        const bool active = (status & BIT_EMG_SENS) != 0U;
        const uint32_t direction = mmio_read_emergency_direction();

        if (rtos_mutex_take(g_state_mutex, 5U)) {
            if (active) {
                g_state.emergency_active = true;
                g_state.emergency_direction =
                    (direction == EMG_DIR_NONE) ? EMG_DIR_NS : direction;
                g_state.emergency_release_pending = false;
            } else if (g_state.emergency_active) {
                g_state.emergency_active = false;
                g_state.emergency_direction = EMG_DIR_NONE;
                g_state.emergency_release_pending = true;
            }
            rtos_mutex_give(g_state_mutex);
            rtos_semaphore_give(g_control_event);
        }
    }
}

void PedestrianTask(void *argument)
{
    (void)argument;

    for (;;) {
        const bool notified =
            rtos_semaphore_take(g_pedestrian_event, PERIOD_PEDESTRIAN_MS);
        const bool button_active =
            (mmio_read_sensors() & BIT_PED_REQ) != 0U;

        if (notified || button_active) {
            if (rtos_mutex_take(g_state_mutex, 5U)) {
                if (!g_state.emergency_active && !g_state.night_mode)
                    g_state.pedestrian_pending = true;
                rtos_mutex_give(g_state_mutex);
                rtos_semaphore_give(g_control_event);
            }

            /* Clear only the simulated request bit; real hardware is device-specific. */
            if (button_active)
                sim_sensors_reg &= ~BIT_PED_REQ;
        }
    }
}

void SensorTask(void *argument)
{
    (void)argument;

    for (;;) {
        const uint32_t status = mmio_read_sensors();
        const uint32_t ns = mmio_read_density_ns();
        const uint32_t ew = mmio_read_density_ew();

        if (rtos_mutex_take(g_state_mutex, 5U)) {
            g_state.ns_density = ns;
            g_state.ew_density = ew;
            g_state.night_mode = (status & BIT_NIGHT_EN) != 0U;
            g_state.sensor_ok = (status & BIT_SENSOR_OK) != 0U;
            rtos_mutex_give(g_state_mutex);
            rtos_semaphore_give(g_control_event);
        }

        rtos_delay_ms(PERIOD_SENSOR_MS);
    }
}

void TrafficControlTask(void *argument)
{
    (void)argument;

    for (;;) {
        uint32_t now = rtos_get_tick_ms();
        uint32_t output_mask = BIT_NS_GREEN | BIT_EW_RED | BIT_PED_STOP;

        (void)rtos_semaphore_take(g_control_event,
                                  PERIOD_TRAFFIC_CONTROL_MS);

        now = rtos_get_tick_ms();

        if (rtos_mutex_take(g_state_mutex, 5U)) {
            const uint32_t elapsed =
                elapsed_in_phase(now, g_state.phase_started_ms);

            /* Highest precedence: emergency vehicle and safe emergency release. */
            if (g_state.emergency_active) {
                if (g_state.phase != PHASE_EMG_PRIORITY) {
                    g_state.phase = PHASE_EMG_PRIORITY;
                    g_state.phase_started_ms = now;
                }
            } else if (g_state.phase == PHASE_EMG_PRIORITY ||
                       g_state.emergency_release_pending) {
                g_state.phase = PHASE_ALL_RED_NS;
                g_state.phase_started_ms = now;
                g_state.emergency_release_pending = false;
            } else if (!g_state.sensor_ok || g_state.night_mode) {
                g_state.phase = PHASE_NIGHT_MODE;
                g_state.phase_started_ms = now;
            } else if (g_state.phase == PHASE_NIGHT_MODE) {
                g_state.phase = PHASE_ALL_RED_NS;
                g_state.phase_started_ms = now;
            } else {
                const uint32_t ns_green_ms =
                    (g_state.ns_density > g_state.ew_density + 20U)
                    ? 8000U : 5000U;
                const uint32_t ew_green_ms =
                    (g_state.ew_density > g_state.ns_density + 20U)
                    ? 8000U : 5000U;

                switch (g_state.phase) {
                case PHASE_NS_GREEN:
                    if (elapsed >= ns_green_ms) {
                        g_state.phase = PHASE_NS_YELLOW;
                        g_state.phase_started_ms = now;
                    }
                    break;
                case PHASE_NS_YELLOW:
                    if (elapsed >= 2000U) {
                        g_state.phase = PHASE_ALL_RED_EW;
                        g_state.phase_started_ms = now;
                    }
                    break;
                case PHASE_ALL_RED_EW:
                    if (elapsed >= 1000U) {
                        if (g_state.pedestrian_pending) {
                            g_state.phase = PHASE_PED_CROSS;
                            g_state.pedestrian_pending = false;
                        } else {
                            g_state.phase = PHASE_EW_GREEN;
                        }
                        g_state.phase_started_ms = now;
                    }
                    break;
                case PHASE_EW_GREEN:
                    if (elapsed >= ew_green_ms) {
                        g_state.phase = PHASE_EW_YELLOW;
                        g_state.phase_started_ms = now;
                    }
                    break;
                case PHASE_EW_YELLOW:
                    if (elapsed >= 2000U) {
                        g_state.phase = PHASE_ALL_RED_NS;
                        g_state.phase_started_ms = now;
                    }
                    break;
                case PHASE_ALL_RED_NS:
                    if (elapsed >= 1000U) {
                        g_state.phase = PHASE_NS_GREEN;
                        g_state.phase_started_ms = now;
                    }
                    break;
                case PHASE_PED_CROSS:
                    if (elapsed >= 6000U) {
                        g_state.phase = PHASE_ALL_RED_NS;
                        g_state.phase_started_ms = now;
                    }
                    break;
                default:
                    g_state.phase = PHASE_ALL_RED_NS;
                    g_state.phase_started_ms = now;
                    break;
                }
            }

            output_mask = output_for_phase(g_state.phase,
                                             g_state.emergency_direction);
            rtos_mutex_give(g_state_mutex);
            mmio_write_lights(output_mask);
        }
    }
}

/* ------------------------- Initialization / startup ---------------------- */

bool traffic_rtos_start(void)
{
    rtos_task_t emergency_task;
    rtos_task_t pedestrian_task;
    rtos_task_t traffic_task;
    rtos_task_t sensor_task;

    g_state_mutex = rtos_mutex_create(RTOS_MUTEX_PRIORITY_INHERITANCE);
    g_emergency_event = rtos_semaphore_create(0U, 1U);
    g_pedestrian_event = rtos_semaphore_create(0U, 4U);
    g_control_event = rtos_semaphore_create(0U, 16U);

    if (g_state_mutex == NULL || g_emergency_event == NULL ||
        g_pedestrian_event == NULL || g_control_event == NULL)
        return false;

    if (!rtos_task_create(EmergencyTask, "EmergencyTask", 512U, NULL,
                          PRIO_EMERGENCY_TASK, &emergency_task))
        return false;
    if (!rtos_task_create(PedestrianTask, "PedestrianTask", 512U, NULL,
                          PRIO_PEDESTRIAN_TASK, &pedestrian_task))
        return false;
    if (!rtos_task_create(TrafficControlTask, "TrafficControlTask", 768U, NULL,
                          PRIO_TRAFFIC_CONTROL_TASK, &traffic_task))
        return false;
    if (!rtos_task_create(SensorTask, "SensorTask", 512U, NULL,
                          PRIO_SENSOR_TASK, &sensor_task))
        return false;

    rtos_scheduler_start();
    return true; /* Normally unreachable if scheduler_start does not return. */
}
