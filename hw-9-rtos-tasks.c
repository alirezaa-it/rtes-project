/*
 * فایل: hw-9-rtos-tasks.c
 * پروژه: سیستم چراغ راهنمای هوشمند تقاطع (Smart Traffic Light Controller)
 * دانشجو: علیرضا طاعتی
 * توضیح: تفکیک منطق پروژه به تسک‌های RTOS و حفاظت از منابع مشترک با Mutex / Semaphore
 */

#include <stdint.h>
#include <stdbool.h>

/* تعریف انتزاعی تایپ‌های RTOS برای استقلال از محیط شبیه‌سازی */
typedef void* rtos_mutex_t;
typedef void* rtos_semaphore_t;

extern void rtos_mutex_lock(rtos_mutex_t mutex);
extern void rtos_mutex_unlock(rtos_mutex_t mutex);
extern void rtos_sem_give(rtos_semaphore_t sem);
extern bool rtos_sem_take(rtos_semaphore_t sem, uint32_t timeout_ticks);
extern void rtos_delay_ms(uint32_t ms);

typedef struct {
    uint32_t ns_density;
    uint32_t ew_density;
    bool     emergency_active;
    bool     night_mode;
    bool     ped_request_pending;
    uint32_t current_phase_id;
    uint32_t output_lights_mask;
} shared_intersection_state_t;

static shared_intersection_state_t g_state = {
    .ns_density = 25U,
    .ew_density = 20U,
    .emergency_active = false,
    .night_mode = false,
    .ped_request_pending = false,
    .current_phase_id = 0U,
    .output_lights_mask = 0U
};

static rtos_mutex_t     g_state_mutex = (rtos_mutex_t)0x1001;
static rtos_semaphore_t g_emg_sem     = (rtos_semaphore_t)0x1002;

/*
 * جلوگیری از Race Condition و Deadlock:
 * - تمامی دسترسی‌ها به g_state از طریق یک Mutex واحد انجام می‌شود.
 * - فقط یک Mutex وجود دارد و ترتیب قفل‌گذاری همیشه یکسان است تا Deadlock رخ ندهد.
 */

/* Task 1: پایش وضعیت اضطراری و اولویت امدادی */
void Task_Emergency_Safety(void *params) {
    (void)params;
    for (;;) {
        if (rtos_sem_take(g_emg_sem, 50U)) {
            rtos_mutex_lock(g_state_mutex);
            g_state.emergency_active = true;
            g_state.current_phase_id = 99U; /* فاز اضطراری */
            g_state.output_lights_mask = (1U << 2) | (1U << 3) | (1U << 8);
            rtos_mutex_unlock(g_state_mutex);
        }
        rtos_delay_ms(20U);
    }
}

/* Task 2: نمونه‌برداری تراکم خودروها و وضعیت شب */
void Task_Sensor_Sampling(void *params) {
    (void)params;
    for (;;) {
        uint32_t sampled_ns = 30U;
        uint32_t sampled_ew = 18U;
        bool sampled_night = false;

        rtos_mutex_lock(g_state_mutex);
        g_state.ns_density = sampled_ns;
        g_state.ew_density = sampled_ew;
        g_state.night_mode = sampled_night;
        rtos_mutex_unlock(g_state_mutex);

        rtos_delay_ms(100U);
    }
}

/* Task 3: اجرای ماشین حالت اصلی و تعیین زمان‌بندی چراغ‌ها */
void Task_Traffic_FSM(void *params) {
    (void)params;
    uint32_t elapsed_ms = 0U;

    for (;;) {
        rtos_mutex_lock(g_state_mutex);
        bool emg = g_state.emergency_active;
        bool night = g_state.night_mode;
        bool ped = g_state.ped_request_pending;
        uint32_t d_ns = g_state.ns_density;
        uint32_t d_ew = g_state.ew_density;

        if (!emg) {
            uint32_t green_limit = (d_ns > d_ew + 20U) ? 8000U : 5000U;
            elapsed_ms += 100U;

            if (night) {
                g_state.current_phase_id = 8U;
                g_state.output_lights_mask = (1U << 1) | (1U << 3) | (1U << 8);
            } else if (ped && elapsed_ms >= 3000U) {
                g_state.current_phase_id = 7U;
                g_state.ped_request_pending = false;
                g_state.output_lights_mask = (1U << 0) | (1U << 3) | (1U << 6);
                elapsed_ms = 0U;
            } else if (elapsed_ms >= green_limit) {
                g_state.current_phase_id = (g_state.current_phase_id + 1U) % 4U;
                g_state.output_lights_mask = (1U << (g_state.current_phase_id % 6U));
                elapsed_ms = 0U;
            }
        }
        rtos_mutex_unlock(g_state_mutex);

        rtos_delay_ms(100U);
    }
}

/* Task 4: ثبت و پایش وضعیت خروجی‌های تقاطع */
void Task_Output_Monitor(void *params) {
    (void)params;
    uint32_t last_mask = 0U;

    for (;;) {
        rtos_mutex_lock(g_state_mutex);
        last_mask = g_state.output_lights_mask;
        rtos_mutex_unlock(g_state_mutex);

        (void)last_mask;
        rtos_delay_ms(200U);
    }
}
