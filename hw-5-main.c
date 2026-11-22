/*
 * فایل: hw-5-main.c (نسخه اصلاح‌شده - کامل و قابل اجرا با شبیه‌سازی کامل سناریوها)
 * پروژه: سیستم چراغ راهنمای هوشمند تقاطع (Smart Traffic Light Controller)
 * دانشجو: علیرضا طاعتی
 * توضیح: پیاده‌سازی کامل و قابل اجرا به زبان C استاندارد؛ شامل FSM ایمن، جهت خودروی امدادی (NS/EW)،
 *          خروج ایمن با All-Red، سیاست اولویت رویدادها و اتصال شفاف به Memory Map و لایه شبیه‌سازی.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* ============================================================================
 * ۱. Memory Map (هماهنگ با hw-4-memory_map.md) و لایه شبیه‌سازی سخت‌افزار
 * ============================================================================ */
#define LIGHTS_CTRL_REG_ADDR      0x40000000U
#define LIGHTS_OUT_DATA_REG_ADDR  0x40000004U
#define LIGHTS_STATUS_REG_ADDR    0x40000008U
#define SENSORS_STATUS_REG_ADDR   0x40001000U
#define DENSITY_NS_REG_ADDR       0x40001004U
#define DENSITY_EW_REG_ADDR       0x40001008U
#define EMG_DIR_REG_ADDR          0x4000100CU

/* بیت‌های ثبات خروجی چراغ‌ها (LIGHTS_OUT_DATA_REG @ 0x40000004) */
#define BIT_NS_RED      (1U << 0)
#define BIT_NS_YELLOW   (1U << 1)
#define BIT_NS_GREEN    (1U << 2)
#define BIT_EW_RED      (1U << 3)
#define BIT_EW_YELLOW   (1U << 4)
#define BIT_EW_GREEN    (1U << 5)
#define BIT_PED_WALK    (1U << 6)
#define BIT_PED_STOP    (1U << 7)
#define BIT_SAFETY_LED  (1U << 8)

/* بیت‌های ثبات وضعیت سنسورها (SENSORS_STATUS_REG @ 0x40001000) */
#define BIT_PED_REQ     (1U << 0)
#define BIT_EMG_SENS    (1U << 1)
#define BIT_NIGHT_EN    (1U << 2)
#define BIT_SENSOR_OK   (1U << 3)

typedef enum {
    EMG_DIR_NONE = 0,
    EMG_DIR_NS   = 1,
    EMG_DIR_EW   = 2
} emg_direction_t;

typedef enum {
    STATE_NS_GREEN = 0,
    STATE_NS_YELLOW,
    STATE_ALL_RED_TO_EW,
    STATE_EW_GREEN,
    STATE_EW_YELLOW,
    STATE_ALL_RED_TO_NS,
    STATE_PEDESTRIAN_CROSS,
    STATE_NIGHT_MODE,
    STATE_EMERGENCY_PRIORITY
} traffic_state_t;

typedef struct {
    traffic_state_t current_state;
    emg_direction_t emg_direction;
    uint32_t        timer_ms;
    bool            ped_pending;
    bool            emg_exit_safety_needed;
    uint32_t        ns_green_duration_ms;
    uint32_t        ew_green_duration_ms;
} traffic_controller_t;

/* لایه شبیه‌سازی ثبات‌های سخت‌افزاری (در نبود میکروکنترلر فیزیکی) */
typedef struct {
    volatile uint32_t lights_ctrl;
    volatile uint32_t lights_out;
    volatile uint32_t lights_status;
    volatile uint32_t sensors_status;
    volatile uint32_t density_ns;
    volatile uint32_t density_ew;
    volatile uint32_t emg_dir;
} sim_mmio_bank_t;

static sim_mmio_bank_t g_sim_mmio = {
    .lights_ctrl    = 1U,
    .lights_out     = 0U,
    .lights_status  = 1U,
    .sensors_status = BIT_SENSOR_OK,
    .density_ns     = 20U,
    .density_ew     = 15U,
    .emg_dir        = EMG_DIR_NONE
};

static uint32_t hw_read_sensors_status(void) {
    (void)SENSORS_STATUS_REG_ADDR;
    return g_sim_mmio.sensors_status;
}

static uint32_t hw_read_density_ns(void) {
    (void)DENSITY_NS_REG_ADDR;
    return g_sim_mmio.density_ns;
}

static uint32_t hw_read_density_ew(void) {
    (void)DENSITY_EW_REG_ADDR;
    return g_sim_mmio.density_ew;
}

static emg_direction_t hw_read_emg_direction(void) {
    (void)EMG_DIR_REG_ADDR;
    if (g_sim_mmio.emg_dir == EMG_DIR_EW) {
        return EMG_DIR_EW;
    }
    if (g_sim_mmio.emg_dir == EMG_DIR_NS) {
        return EMG_DIR_NS;
    }
    return EMG_DIR_NONE;
}

static void hw_write_lights_output(uint32_t mask) {
    (void)LIGHTS_CTRL_REG_ADDR;
    (void)LIGHTS_OUT_DATA_REG_ADDR;
    (void)LIGHTS_STATUS_REG_ADDR;
    g_sim_mmio.lights_out = mask;
}

/* ============================================================================
 * ۲. خروجی ایمن و انتقال ترتیبی چراغ‌ها
 * ============================================================================ */
static uint32_t get_state_lights_mask(traffic_state_t state, emg_direction_t emg_dir) {
    switch (state) {
        case STATE_NS_GREEN:
            return BIT_NS_GREEN | BIT_EW_RED | BIT_PED_STOP;
        case STATE_NS_YELLOW:
            return BIT_NS_YELLOW | BIT_EW_RED | BIT_PED_STOP;
        case STATE_EW_GREEN:
            return BIT_NS_RED | BIT_EW_GREEN | BIT_PED_STOP;
        case STATE_EW_YELLOW:
            return BIT_NS_RED | BIT_EW_YELLOW | BIT_PED_STOP;
        case STATE_ALL_RED_TO_EW:
        case STATE_ALL_RED_TO_NS:
            return BIT_NS_RED | BIT_EW_RED | BIT_PED_STOP;
        case STATE_PEDESTRIAN_CROSS:
            return BIT_NS_RED | BIT_EW_RED | BIT_PED_WALK;
        case STATE_NIGHT_MODE:
            return BIT_NS_YELLOW | BIT_EW_RED | BIT_PED_STOP | BIT_SAFETY_LED;
        case STATE_EMERGENCY_PRIORITY:
            if (emg_dir == EMG_DIR_EW) {
                return BIT_NS_RED | BIT_EW_GREEN | BIT_PED_STOP | BIT_SAFETY_LED;
            }
            return BIT_NS_GREEN | BIT_EW_RED | BIT_PED_STOP | BIT_SAFETY_LED;
        default:
            return BIT_NS_RED | BIT_EW_RED | BIT_PED_STOP | BIT_SAFETY_LED;
    }
}

/*
 * سیاست اولویت رویدادها:
 * ۱. خودروی اضطراری (Emergency Priority) در مسیر درخواست‌دهنده (NS یا EW)
 * ۲. خروج ایمن از حالت اضطراری با All-Red و زمان ایمنی مشخص (1000ms)
 * ۳. خطای سنسور یا حالت شبانه (Night Mode)
 * ۴. درخواست عبور عابر پیاده (Pedestrian Crossing) پس از توقف ایمن خودروها
 * ۵. چرخه عادی تقاطع با رعایت ترتیب ایمن (سبز -> زرد -> قرمز کامل -> مسیر بعدی)
 */
void traffic_fsm_step(traffic_controller_t *ctrl, uint32_t dt_ms) {
    uint32_t status = hw_read_sensors_status();
    bool emg_active = ((status & BIT_EMG_SENS) != 0U);
    bool night_en   = ((status & BIT_NIGHT_EN) != 0U);
    bool ped_req    = ((status & BIT_PED_REQ) != 0U);
    bool sensor_ok  = ((status & BIT_SENSOR_OK) != 0U);

    if (ped_req && !emg_active && !night_en) {
        ctrl->ped_pending = true;
        g_sim_mmio.sensors_status &= ~BIT_PED_REQ;
    }

    uint32_t d_ns = hw_read_density_ns();
    uint32_t d_ew = hw_read_density_ew();
    ctrl->ns_green_duration_ms = (d_ns > d_ew + 20U) ? 8000U : 5000U;
    ctrl->ew_green_duration_ms = (d_ew > d_ns + 20U) ? 8000U : 5000U;

    ctrl->timer_ms += dt_ms;

    /* ۱. ورود یا ادامه حالت اضطراری با جهت مشخص (NS یا EW) */
    if (emg_active) {
        emg_direction_t req_dir = hw_read_emg_direction();
        if (req_dir == EMG_DIR_NONE) {
            req_dir = EMG_DIR_NS;
        }
        if (ctrl->current_state != STATE_EMERGENCY_PRIORITY || ctrl->emg_direction != req_dir) {
            ctrl->current_state = STATE_EMERGENCY_PRIORITY;
            ctrl->emg_direction = req_dir;
            ctrl->timer_ms = 0U;
            ctrl->emg_exit_safety_needed = true;
        }
        hw_write_lights_output(get_state_lights_mask(ctrl->current_state, ctrl->emg_direction));
        return;
    }

    /* ۲. خروج ایمن از حالت اضطراری از طریق All-Red */
    if (ctrl->current_state == STATE_EMERGENCY_PRIORITY && !emg_active) {
        ctrl->current_state = STATE_ALL_RED_TO_NS;
        ctrl->emg_direction = EMG_DIR_NONE;
        ctrl->timer_ms = 0U;
        ctrl->emg_exit_safety_needed = true;
        hw_write_lights_output(get_state_lights_mask(ctrl->current_state, EMG_DIR_NONE));
        return;
    }

    /* ۳. حالت شبانه یا خطای سنسور (پس از عبور از All-Red اگر قبلاً اضطراری بوده) */
    if ((night_en || !sensor_ok) && ctrl->current_state != STATE_ALL_RED_TO_NS && ctrl->current_state != STATE_ALL_RED_TO_EW) {
        if (ctrl->current_state != STATE_NIGHT_MODE) {
            ctrl->current_state = STATE_NIGHT_MODE;
            ctrl->timer_ms = 0U;
        }
        hw_write_lights_output(get_state_lights_mask(ctrl->current_state, EMG_DIR_NONE));
        return;
    }

    if (!night_en && sensor_ok && ctrl->current_state == STATE_NIGHT_MODE) {
        ctrl->current_state = STATE_ALL_RED_TO_NS;
        ctrl->timer_ms = 0U;
        hw_write_lights_output(get_state_lights_mask(ctrl->current_state, EMG_DIR_NONE));
        return;
    }

    /* ۴ و ۵. چرخه عادی ایمن و عبور عابر پیاده */
    switch (ctrl->current_state) {
        case STATE_NS_GREEN:
            if (ctrl->timer_ms >= ctrl->ns_green_duration_ms) {
                ctrl->current_state = STATE_NS_YELLOW;
                ctrl->timer_ms = 0U;
            }
            break;

        case STATE_NS_YELLOW:
            if (ctrl->timer_ms >= 2000U) {
                ctrl->current_state = STATE_ALL_RED_TO_EW;
                ctrl->timer_ms = 0U;
            }
            break;

        case STATE_ALL_RED_TO_EW:
            if (ctrl->timer_ms >= 1000U) {
                if (ctrl->ped_pending) {
                    ctrl->current_state = STATE_PEDESTRIAN_CROSS;
                    ctrl->ped_pending = false;
                } else {
                    ctrl->current_state = STATE_EW_GREEN;
                }
                ctrl->timer_ms = 0U;
            }
            break;

        case STATE_PEDESTRIAN_CROSS:
            if (ctrl->timer_ms >= 6000U) {
                ctrl->current_state = STATE_ALL_RED_TO_NS;
                ctrl->timer_ms = 0U;
            }
            break;

        case STATE_EW_GREEN:
            if (ctrl->timer_ms >= ctrl->ew_green_duration_ms) {
                ctrl->current_state = STATE_EW_YELLOW;
                ctrl->timer_ms = 0U;
            }
            break;

        case STATE_EW_YELLOW:
            if (ctrl->timer_ms >= 2000U) {
                ctrl->current_state = STATE_ALL_RED_TO_NS;
                ctrl->timer_ms = 0U;
            }
            break;

        case STATE_ALL_RED_TO_NS:
            if (ctrl->timer_ms >= 1000U) {
                ctrl->emg_exit_safety_needed = false;
                ctrl->current_state = STATE_NS_GREEN;
                ctrl->timer_ms = 0U;
            }
            break;

        default:
            ctrl->current_state = STATE_ALL_RED_TO_NS;
            ctrl->timer_ms = 0U;
            break;
    }

    hw_write_lights_output(get_state_lights_mask(ctrl->current_state, ctrl->emg_direction));
}

static const char* state_name(traffic_state_t s) {
    switch (s) {
        case STATE_NS_GREEN:         return "NS_GREEN";
        case STATE_NS_YELLOW:        return "NS_YELLOW";
        case STATE_ALL_RED_TO_EW:    return "ALL_RED_TO_EW";
        case STATE_EW_GREEN:         return "EW_GREEN";
        case STATE_EW_YELLOW:        return "EW_YELLOW";
        case STATE_ALL_RED_TO_NS:    return "ALL_RED_TO_NS";
        case STATE_PEDESTRIAN_CROSS: return "PEDESTRIAN_CROSS";
        case STATE_NIGHT_MODE:       return "NIGHT_MODE";
        case STATE_EMERGENCY_PRIORITY: return "EMERGENCY_PRIORITY";
        default:                     return "UNKNOWN";
    }
}

int main(void) {
    traffic_controller_t ctrl = {
        .current_state = STATE_NS_GREEN,
        .emg_direction = EMG_DIR_NONE,
        .timer_ms = 0U,
        .ped_pending = false,
        .emg_exit_safety_needed = false,
        .ns_green_duration_ms = 5000U,
        .ew_green_duration_ms = 5000U
    };

    hw_write_lights_output(get_state_lights_mask(ctrl.current_state, ctrl.emg_direction));

    for (int step = 1; step <= 14; ++step) {
        /* سناریوی درخواست عابر در گام ۳ */
        if (step == 3) {
            g_sim_mmio.sensors_status |= BIT_PED_REQ;
        }
        /* سناریوی خودروی امدادی از مسیر شرقی-غربی در گام ۶ */
        if (step == 6) {
            g_sim_mmio.emg_dir = EMG_DIR_EW;
            g_sim_mmio.sensors_status |= BIT_EMG_SENS;
        }
        /* خروج خودروی امدادی در گام ۸ و ورود به All-Red ایمن */
        if (step == 8) {
            g_sim_mmio.sensors_status &= ~BIT_EMG_SENS;
            g_sim_mmio.emg_dir = EMG_DIR_NONE;
        }
        /* سناریوی حالت شبانه در گام ۱۱ و خروج در گام ۱۳ */
        if (step == 11) {
            g_sim_mmio.sensors_status |= BIT_NIGHT_EN;
        }
        if (step == 13) {
            g_sim_mmio.sensors_status &= ~BIT_NIGHT_EN;
        }

        traffic_fsm_step(&ctrl, 1000U);
        printf("Step %02d | State: %-18s | Mask: 0x%03X\n",
               step, state_name(ctrl.current_state), (unsigned int)g_sim_mmio.lights_out);
    }

    return 0;
}
