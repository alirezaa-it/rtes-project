/*
 * فایل: hw-9-rtos-tasks.c (نسخه اصلاح‌شده - هماهنگ با hw-4، hw-5، hw-8 و hw-10)
 * پروژه: سیستم چراغ راهنمای هوشمند تقاطع (Smart Traffic Light Controller)
 * دانشجو: علیرضا طاعتی
 * توضیح: تسک‌های RTOS (EmergencyTask، TrafficControlTask، PedestrianTask، SensorTask)،
 *          ساخت و مقداردهی اولیه واقعی Mutex و Semaphore، ارسال رویداد از ISR و تسک‌ها به TrafficControlTask،
 *          و استفاده از تعاریف مشترک بیت‌ها، جهت خودروی امدادی و ثبات‌های Memory Map.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 * ۱. تعاریف مشترک بیت‌های چراغ‌ها، وضعیت‌ها، جهت امدادی و ثبات‌های MMIO
 * ============================================================================ */
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

/* اولویت‌های عددی تسک‌ها (عدد بزرگ‌تر = اولویت بالاتر) */
#define PRIO_EMERGENCY_TASK         4U
#define PRIO_TRAFFIC_CONTROL_TASK   3U
#define PRIO_PEDESTRIAN_TASK        2U
#define PRIO_SENSOR_TASK            1U

/* دوره‌های تناوب تسک‌ها بر حسب میلی‌ثانیه (مطابق hw-10-scheduling.md) */
#define PERIOD_EMERGENCY_MS         20U
#define PERIOD_PEDESTRIAN_MS        50U
#define PERIOD_TRAFFIC_CONTROL_MS   100U
#define PERIOD_SENSOR_MS            100U

/* آدرس‌های فرضی ثبات‌ها مطابق hw-4-memory_map.md و hw-5-main.c */
#define LIGHTS_CTRL_REG_ADDR      0x40000000U
#define LIGHTS_OUT_DATA_REG_ADDR  0x40000004U
#define SENSORS_STATUS_REG_ADDR   0x40001000U
#define DENSITY_NS_REG_ADDR       0x40001004U
#define DENSITY_EW_REG_ADDR       0x40001008U
#define EMG_DIR_REG_ADDR          0x4000100CU

/* ============================================================================
 * ۲. سازوکار ساخت، مقداردهی و مدیریت Mutex و Semaphore در RTOS
 * ============================================================================ */
typedef struct {
    volatile uint32_t locked;
    volatile uint32_t owner_prio;
    uint32_t          ceiling_prio;
} rtos_mutex_obj_t;

typedef struct {
    volatile int32_t  count;
    int32_t           max_count;
} rtos_sem_obj_t;

typedef rtos_mutex_obj_t* rtos_mutex_t;
typedef rtos_sem_obj_t*   rtos_sem_t;

static rtos_mutex_obj_t g_state_mutex_storage;
static rtos_sem_obj_t   g_emg_sem_storage;
static rtos_sem_obj_t   g_ped_sem_storage;
static rtos_sem_obj_t   g_ctrl_notify_sem_storage;

static rtos_mutex_t     g_state_mutex     = NULL;
static rtos_sem_t       g_emg_sem         = NULL;
static rtos_sem_t       g_ped_sem         = NULL;
static rtos_sem_t       g_ctrl_notify_sem = NULL;

static rtos_mutex_t rtos_mutex_init_obj(rtos_mutex_obj_t *obj, uint32_t ceiling_prio) {
    obj->locked = 0U;
    obj->owner_prio = 0U;
    obj->ceiling_prio = ceiling_prio;
    return obj;
}

static rtos_sem_t rtos_sem_init_obj(rtos_sem_obj_t *obj, int32_t initial_count, int32_t max_count) {
    obj->count = initial_count;
    obj->max_count = max_count;
    return obj;
}

static void rtos_mutex_lock(rtos_mutex_t mutex, uint32_t caller_prio) {
    if (mutex == NULL) {
        return;
    }
    if (mutex->locked != 0U && mutex->owner_prio < caller_prio) {
        mutex->owner_prio = caller_prio; /* اعمال Priority Inheritance */
    }
    mutex->locked = 1U;
    mutex->owner_prio = caller_prio;
}

static void rtos_mutex_unlock(rtos_mutex_t mutex) {
    if (mutex == NULL) {
        return;
    }
    mutex->owner_prio = 0U;
    mutex->locked = 0U;
}

static bool rtos_sem_take(rtos_sem_t sem, uint32_t timeout_ms) {
    (void)timeout_ms;
    if (sem == NULL) {
        return false;
    }
    if (sem->count > 0) {
        sem->count--;
        return true;
    }
    return false;
}

static void rtos_sem_give(rtos_sem_t sem) {
    if (sem == NULL) {
        return;
    }
    if (sem->count < sem->max_count) {
        sem->count++;
    }
}

static void rtos_delay_ms(uint32_t ms) {
    (void)ms;
}

/* ============================================================================
 * ۳. وضعیت مشترک سیستم و لایه دسترسی / شبیه‌سازی Memory Map
 * ============================================================================ */
typedef struct {
    uint32_t ns_density;
    uint32_t ew_density;
    bool     emergency_active;
    uint32_t emergency_direction;
    bool     emergency_release_pending;
    bool     night_mode;
    bool     ped_request_pending;
    bool     event_emergency_changed;
    bool     event_pedestrian_changed;
    bool     event_sensor_changed;
    uint32_t current_phase;
    uint32_t output_lights_mask;
} shared_intersection_state_t;

static shared_intersection_state_t g_state = {
    .ns_density = 20U,
    .ew_density = 15U,
    .emergency_active = false,
    .emergency_direction = EMG_DIR_NONE,
    .emergency_release_pending = false,
    .night_mode = false,
    .ped_request_pending = false,
    .event_emergency_changed = false,
    .event_pedestrian_changed = false,
    .event_sensor_changed = false,
    .current_phase = PHASE_NS_GREEN,
    .output_lights_mask = BIT_NS_GREEN | BIT_EW_RED | BIT_PED_STOP
};

/* بافرهای شبیه‌سازی ثبات‌های سخت‌افزاری (در محیط بدون سخت‌افزار فیزیکی) */
volatile uint32_t sim_lights_ctrl   = 1U;
volatile uint32_t sim_lights_out    = 0U;
volatile uint32_t sim_sensors_reg   = BIT_SENSOR_OK;
volatile uint32_t sim_density_ns    = 20U;
volatile uint32_t sim_density_ew    = 15U;
volatile uint32_t sim_emg_dir_reg   = EMG_DIR_NS;

uint32_t rtos_mmio_read_sensors_status(void) {
    (void)SENSORS_STATUS_REG_ADDR;
    return sim_sensors_reg;
}

uint32_t rtos_mmio_read_density_ns(void) {
    (void)DENSITY_NS_REG_ADDR;
    return sim_density_ns;
}

uint32_t rtos_mmio_read_density_ew(void) {
    (void)DENSITY_EW_REG_ADDR;
    return sim_density_ew;
}

uint32_t rtos_mmio_read_emergency_dir(void) {
    (void)EMG_DIR_REG_ADDR;
    uint32_t dir = sim_emg_dir_reg;
    if (dir != EMG_DIR_NS && dir != EMG_DIR_EW) {
        return EMG_DIR_NS;
    }
    return dir;
}

void rtos_mmio_write_lights_mask(uint32_t mask) {
    (void)LIGHTS_CTRL_REG_ADDR;
    (void)LIGHTS_OUT_DATA_REG_ADDR;
    sim_lights_out = mask;
}

/* ============================================================================
 * ۴. مقداردهی اولیه RTOS و API اعلان رویداد از ISR و تسک‌ها
 * ============================================================================ */
void rtos_sync_init(void) {
    g_state_mutex     = rtos_mutex_init_obj(&g_state_mutex_storage, PRIO_EMERGENCY_TASK);
    g_emg_sem         = rtos_sem_init_obj(&g_emg_sem_storage, 0, 1);
    g_ped_sem         = rtos_sem_init_obj(&g_ped_sem_storage, 0, 4);
    g_ctrl_notify_sem = rtos_sem_init_obj(&g_ctrl_notify_sem_storage, 0, 16);
}

void rtos_notify_from_isr(uint32_t event_mask) {
    if ((event_mask & (1U << 2)) != 0U) {
        rtos_sem_give(g_emg_sem);
    }
    if ((event_mask & (1U << 3)) != 0U) {
        g_state.emergency_release_pending = true;
    }
    if ((event_mask & (1U << 1)) != 0U) {
        rtos_sem_give(g_ped_sem);
    }
    rtos_sem_give(g_ctrl_notify_sem);
}

static void notify_traffic_control(void) {
    rtos_sem_give(g_ctrl_notify_sem);
}

/* ============================================================================
 * ۵. تسک‌های RTOS
 * ============================================================================ */

/* EmergencyTask (اولویت 4 - دوره 20ms): مدیریت ورود و خروج خودروی امدادی */
void EmergencyTask(void *params) {
    (void)params;
    for (;;) {
        bool emg_triggered = rtos_sem_take(g_emg_sem, PERIOD_EMERGENCY_MS);
        uint32_t status = rtos_mmio_read_sensors_status();
        bool hw_emg = ((status & BIT_EMG_SENS) != 0U);
        uint32_t dir = rtos_mmio_read_emergency_dir();

        rtos_mutex_lock(g_state_mutex, PRIO_EMERGENCY_TASK);
        if (emg_triggered || hw_emg) {
            g_state.emergency_active = true;
            g_state.emergency_direction = dir;
            g_state.emergency_release_pending = false;
            g_state.event_emergency_changed = true;
            rtos_mutex_unlock(g_state_mutex);
            notify_traffic_control();
        } else if (g_state.emergency_release_pending || (!hw_emg && g_state.emergency_active)) {
            g_state.emergency_active = false;
            g_state.emergency_direction = EMG_DIR_NONE;
            g_state.emergency_release_pending = false;
            g_state.event_emergency_changed = true;
            rtos_mutex_unlock(g_state_mutex);
            notify_traffic_control();
        } else {
            rtos_mutex_unlock(g_state_mutex);
        }

        rtos_delay_ms(PERIOD_EMERGENCY_MS);
    }
}

/* PedestrianTask (اولویت 2 - دوره 50ms): دریافت و ثبت درخواست عبور عابر پیاده */
void PedestrianTask(void *params) {
    (void)params;
    for (;;) {
        bool sem_got = rtos_sem_take(g_ped_sem, PERIOD_PEDESTRIAN_MS);
        uint32_t status = rtos_mmio_read_sensors_status();
        bool hw_ped = ((status & BIT_PED_REQ) != 0U);

        if (sem_got || hw_ped) {
            rtos_mutex_lock(g_state_mutex, PRIO_PEDESTRIAN_TASK);
            if (!g_state.emergency_active && !g_state.night_mode) {
                g_state.ped_request_pending = true;
                g_state.event_pedestrian_changed = true;
            }
            rtos_mutex_unlock(g_state_mutex);

            if (hw_ped) {
                sim_sensors_reg &= ~BIT_PED_REQ;
            }
            notify_traffic_control();
        }

        rtos_delay_ms(PERIOD_PEDESTRIAN_MS);
    }
}

/* SensorTask (اولویت 1 - دوره 100ms): نمونه‌برداری تراکم مسیرها و حالت شب */
void SensorTask(void *params) {
    (void)params;
    for (;;) {
        uint32_t d_ns = rtos_mmio_read_density_ns();
        uint32_t d_ew = rtos_mmio_read_density_ew();
        uint32_t status = rtos_mmio_read_sensors_status();
        bool night_en = ((status & BIT_NIGHT_EN) != 0U);

        rtos_mutex_lock(g_state_mutex, PRIO_SENSOR_TASK);
        g_state.ns_density = d_ns;
        g_state.ew_density = d_ew;
        g_state.night_mode = night_en;
        g_state.event_sensor_changed = true;
        rtos_mutex_unlock(g_state_mutex);

        notify_traffic_control();
        rtos_delay_ms(PERIOD_SENSOR_MS);
    }
}

/* TrafficControlTask (اولویت 3 - دوره 100ms): اجرای FSM اصلی و کنترل چراغ‌ها */
void TrafficControlTask(void *params) {
    (void)params;
    uint32_t phase_elapsed_ms = 0U;

    for (;;) {
        (void)rtos_sem_take(g_ctrl_notify_sem, PERIOD_TRAFFIC_CONTROL_MS);

        rtos_mutex_lock(g_state_mutex, PRIO_TRAFFIC_CONTROL_TASK);

        bool emg_active = g_state.emergency_active;
        uint32_t emg_dir = g_state.emergency_direction;
        bool night_en = g_state.night_mode;
        bool ped_req = g_state.ped_request_pending;
        uint32_t d_ns = g_state.ns_density;
        uint32_t d_ew = g_state.ew_density;

        g_state.event_emergency_changed = false;
        g_state.event_pedestrian_changed = false;
        g_state.event_sensor_changed = false;

        phase_elapsed_ms += PERIOD_TRAFFIC_CONTROL_MS;

        if (emg_active) {
            g_state.current_phase = PHASE_EMG_PRIORITY;
            phase_elapsed_ms = 0U;
            if (emg_dir == EMG_DIR_EW) {
                g_state.output_lights_mask = BIT_NS_RED | BIT_EW_GREEN | BIT_PED_STOP | BIT_SAFETY_LED;
            } else {
                g_state.output_lights_mask = BIT_NS_GREEN | BIT_EW_RED | BIT_PED_STOP | BIT_SAFETY_LED;
            }
        } else if (g_state.current_phase == PHASE_EMG_PRIORITY) {
            g_state.current_phase = PHASE_ALL_RED_NS;
            phase_elapsed_ms = 0U;
            g_state.output_lights_mask = BIT_NS_RED | BIT_EW_RED | BIT_PED_STOP | BIT_SAFETY_LED;
        } else if (night_en) {
            g_state.current_phase = PHASE_NIGHT_MODE;
            phase_elapsed_ms = 0U;
            g_state.output_lights_mask = BIT_NS_YELLOW | BIT_EW_RED | BIT_PED_STOP | BIT_SAFETY_LED;
        } else if (ped_req && g_state.current_phase == PHASE_ALL_RED_EW) {
            g_state.current_phase = PHASE_PED_CROSS;
            g_state.ped_request_pending = false;
            phase_elapsed_ms = 0U;
            g_state.output_lights_mask = BIT_NS_RED | BIT_EW_RED | BIT_PED_WALK;
        } else {
            uint32_t ns_green_limit = (d_ns > d_ew + 20U) ? 8000U : 5000U;
            uint32_t ew_green_limit = (d_ew > d_ns + 20U) ? 8000U : 5000U;

            switch (g_state.current_phase) {
                case PHASE_NS_GREEN:
                    g_state.output_lights_mask = BIT_NS_GREEN | BIT_EW_RED | BIT_PED_STOP;
                    if (phase_elapsed_ms >= ns_green_limit) {
                        g_state.current_phase = PHASE_NS_YELLOW;
                        phase_elapsed_ms = 0U;
                    }
                    break;

                case PHASE_NS_YELLOW:
                    g_state.output_lights_mask = BIT_NS_YELLOW | BIT_EW_RED | BIT_PED_STOP;
                    if (phase_elapsed_ms >= 2000U) {
                        g_state.current_phase = PHASE_ALL_RED_EW;
                        phase_elapsed_ms = 0U;
                    }
                    break;

                case PHASE_ALL_RED_EW:
                    g_state.output_lights_mask = BIT_NS_RED | BIT_EW_RED | BIT_PED_STOP;
                    if (phase_elapsed_ms >= 1000U) {
                        g_state.current_phase = PHASE_EW_GREEN;
                        phase_elapsed_ms = 0U;
                    }
                    break;

                case PHASE_EW_GREEN:
                    g_state.output_lights_mask = BIT_NS_RED | BIT_EW_GREEN | BIT_PED_STOP;
                    if (phase_elapsed_ms >= ew_green_limit) {
                        g_state.current_phase = PHASE_EW_YELLOW;
                        phase_elapsed_ms = 0U;
                    }
                    break;

                case PHASE_EW_YELLOW:
                    g_state.output_lights_mask = BIT_NS_RED | BIT_EW_YELLOW | BIT_PED_STOP;
                    if (phase_elapsed_ms >= 2000U) {
                        g_state.current_phase = PHASE_ALL_RED_NS;
                        phase_elapsed_ms = 0U;
                    }
                    break;

                case PHASE_ALL_RED_NS:
                    g_state.output_lights_mask = BIT_NS_RED | BIT_EW_RED | BIT_PED_STOP;
                    if (phase_elapsed_ms >= 1000U) {
                        g_state.current_phase = PHASE_NS_GREEN;
                        phase_elapsed_ms = 0U;
                    }
                    break;

                case PHASE_PED_CROSS:
                    g_state.output_lights_mask = BIT_NS_RED | BIT_EW_RED | BIT_PED_WALK;
                    if (phase_elapsed_ms >= 6000U) {
                        g_state.current_phase = PHASE_ALL_RED_NS;
                        phase_elapsed_ms = 0U;
                    }
                    break;

                case PHASE_NIGHT_MODE:
                default:
                    g_state.current_phase = PHASE_NS_GREEN;
                    phase_elapsed_ms = 0U;
                    g_state.output_lights_mask = BIT_NS_GREEN | BIT_EW_RED | BIT_PED_STOP;
                    break;
            }
        }

        uint32_t final_mask = g_state.output_lights_mask;
        rtos_mutex_unlock(g_state_mutex);

        rtos_mmio_write_lights_mask(final_mask);
        rtos_delay_ms(PERIOD_TRAFFIC_CONTROL_MS);
    }
}
