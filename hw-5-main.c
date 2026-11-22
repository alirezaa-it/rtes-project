/*
 * فایل: hw-5-main.c
 * پروژه: سیستم چراغ راهنمای هوشمند تقاطع (Smart Traffic Light Controller)
 * دانشجو: علیرضا طاعتی
 * توضیح: پیاده‌سازی اولیه منطق FSM، نگاشت ثبات‌های فرضی و شبیه‌سازی اجرای گام‌به‌گام به زبان C استاندارد
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* آدرس‌های فرضی ثبات‌ها مطابق memory_map.md */
#define LIGHTS_CTRL_REG_ADDR      0x40000000U
#define LIGHTS_OUT_DATA_REG_ADDR  0x40000004U
#define SENSORS_STATUS_REG_ADDR   0x40001000U
#define DENSITY_NS_REG_ADDR       0x40001004U
#define DENSITY_EW_REG_ADDR       0x40001008U

/* بیت‌های خروجی چراغ‌ها */
#define BIT_NS_RED      (1U << 0)
#define BIT_NS_YELLOW   (1U << 1)
#define BIT_NS_GREEN    (1U << 2)
#define BIT_EW_RED      (1U << 3)
#define BIT_EW_YELLOW   (1U << 4)
#define BIT_EW_GREEN    (1U << 5)
#define BIT_PED_WALK    (1U << 6)
#define BIT_PED_STOP    (1U << 7)
#define BIT_SAFETY_LED  (1U << 8)

/* بیت‌های وضعیت ورودی‌ها */
#define BIT_PED_REQ     (1U << 0)
#define BIT_EMG_SENS    (1U << 1)
#define BIT_NIGHT_EN    (1U << 2)

/* بافرهای شبیه‌سازی ثبات‌های سخت‌افزاری برای تست بدون سخت‌افزار فیزیکی */
static volatile uint32_t sim_lights_ctrl = 1U;
static volatile uint32_t sim_lights_out  = 0U;
static volatile uint32_t sim_sensors_reg = 0U;
static volatile uint32_t sim_density_ns  = 20U;
static volatile uint32_t sim_density_ew  = 15U;

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
    uint32_t timer_ms;
    bool ped_pending;
    uint32_t ns_green_duration_ms;
    uint32_t ew_green_duration_ms;
} traffic_controller_t;

static void write_lights_output(uint32_t mask) {
    sim_lights_out = mask;
    (void)LIGHTS_CTRL_REG_ADDR;
    (void)LIGHTS_OUT_DATA_REG_ADDR;
}

static uint32_t read_sensors_status(void) {
    (void)SENSORS_STATUS_REG_ADDR;
    return sim_sensors_reg;
}

static uint32_t read_density_ns(void) {
    (void)DENSITY_NS_REG_ADDR;
    return sim_density_ns;
}

static uint32_t read_density_ew(void) {
    (void)DENSITY_EW_REG_ADDR;
    return sim_density_ew;
}

static void apply_state_outputs(traffic_state_t state) {
    switch (state) {
        case STATE_NS_GREEN:
            write_lights_output(BIT_NS_GREEN | BIT_EW_RED | BIT_PED_STOP);
            break;
        case STATE_NS_YELLOW:
            write_lights_output(BIT_NS_YELLOW | BIT_EW_RED | BIT_PED_STOP);
            break;
        case STATE_EW_GREEN:
            write_lights_output(BIT_NS_RED | BIT_EW_GREEN | BIT_PED_STOP);
            break;
        case STATE_EW_YELLOW:
            write_lights_output(BIT_NS_RED | BIT_EW_YELLOW | BIT_PED_STOP);
            break;
        case STATE_ALL_RED_TO_EW:
        case STATE_ALL_RED_TO_NS:
            write_lights_output(BIT_NS_RED | BIT_EW_RED | BIT_PED_STOP);
            break;
        case STATE_PEDESTRIAN_CROSS:
            write_lights_output(BIT_NS_RED | BIT_EW_RED | BIT_PED_WALK);
            break;
        case STATE_NIGHT_MODE:
            write_lights_output(BIT_NS_YELLOW | BIT_EW_RED | BIT_PED_STOP | BIT_SAFETY_LED);
            break;
        case STATE_EMERGENCY_PRIORITY:
            write_lights_output(BIT_NS_GREEN | BIT_EW_RED | BIT_PED_STOP | BIT_SAFETY_LED);
            break;
        default:
            write_lights_output(BIT_NS_RED | BIT_EW_RED | BIT_PED_STOP | BIT_SAFETY_LED);
            break;
    }
}

static void update_dynamic_durations(traffic_controller_t *ctrl) {
    uint32_t d_ns = read_density_ns();
    uint32_t d_ew = read_density_ew();

    ctrl->ns_green_duration_ms = (d_ns > d_ew + 20U) ? 8000U : 5000U;
    ctrl->ew_green_duration_ms = (d_ew > d_ns + 20U) ? 8000U : 5000U;
}

void traffic_fsm_step(traffic_controller_t *ctrl, uint32_t dt_ms) {
    uint32_t status = read_sensors_status();
    bool emg_active = ((status & BIT_EMG_SENS) != 0U);
    bool night_en   = ((status & BIT_NIGHT_EN) != 0U);
    bool ped_req    = ((status & BIT_PED_REQ) != 0U);

    if (ped_req) {
        ctrl->ped_pending = true;
        sim_sensors_reg &= ~BIT_PED_REQ;
    }

    ctrl->timer_ms += dt_ms;
    update_dynamic_durations(ctrl);

    /* اولویت اضطراری خودروهای امدادی */
    if (emg_active) {
        if (ctrl->current_state != STATE_EMERGENCY_PRIORITY) {
            ctrl->current_state = STATE_EMERGENCY_PRIORITY;
            ctrl->timer_ms = 0U;
        }
        apply_state_outputs(ctrl->current_state);
        return;
    }

    if (ctrl->current_state == STATE_EMERGENCY_PRIORITY && !emg_active) {
        ctrl->current_state = STATE_ALL_RED_TO_NS;
        ctrl->timer_ms = 0U;
        apply_state_outputs(ctrl->current_state);
        return;
    }

    /* حالت شبانه */
    if (night_en && ctrl->current_state != STATE_NIGHT_MODE) {
        ctrl->current_state = STATE_NIGHT_MODE;
        ctrl->timer_ms = 0U;
        apply_state_outputs(ctrl->current_state);
        return;
    }

    if (!night_en && ctrl->current_state == STATE_NIGHT_MODE) {
        ctrl->current_state = STATE_NS_GREEN;
        ctrl->timer_ms = 0U;
        apply_state_outputs(ctrl->current_state);
        return;
    }

    /* چرخه حالت‌های عادی و عبور عابر */
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
                ctrl->current_state = STATE_NS_GREEN;
                ctrl->timer_ms = 0U;
            }
            break;

        default:
            ctrl->current_state = STATE_NS_GREEN;
            ctrl->timer_ms = 0U;
            break;
    }

    apply_state_outputs(ctrl->current_state);
}

int main(void) {
    traffic_controller_t ctrl = {
        .current_state = STATE_NS_GREEN,
        .timer_ms = 0U,
        .ped_pending = false,
        .ns_green_duration_ms = 5000U,
        .ew_green_duration_ms = 5000U
    };

    apply_state_outputs(ctrl.current_state);

    /* شبیه‌سازی چند گام زمانی برای بررسی انتقال حالت‌ها */
    for (int step = 0; step < 12; ++step) {
        if (step == 3) {
            sim_sensors_reg |= BIT_PED_REQ;
        }
        if (step == 7) {
            sim_sensors_reg |= BIT_EMG_SENS;
        }
        if (step == 9) {
            sim_sensors_reg &= ~BIT_EMG_SENS;
        }

        traffic_fsm_step(&ctrl, 1000U);
        printf("Step %d -> State: %d, Lights Mask: 0x%03X\n",
               step, (int)ctrl.current_state, (unsigned int)sim_lights_out);
    }

    (void)sim_lights_ctrl;
    return 0;
}
