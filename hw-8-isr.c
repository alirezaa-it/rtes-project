/*
 * فایل: hw-8-isr.c (نسخه اصلاح‌شده - هماهنگ با hw-9-rtos-tasks.c و hw-4-memory_map.md)
 * پروژه: سیستم چراغ راهنمای هوشمند تقاطع (Smart Traffic Light Controller)
 * دانشجو: علیرضا طاعتی
 * توضیح: روتین‌های سرویس وقفه (ISR) کوتاه و قطعی برای تایمر، دکمه عابر و خودروی امدادی،
 *          نحوه پاک‌سازی پرچم وقفه (W1C)، اولویت وقفه‌ها و قرارداد اعلان رویداد به RTOS.
 */

#include <stdint.h>
#include <stdbool.h>

/* آدرس‌های فرضی ثبات‌های وقفه مطابق hw-4-memory_map.md */
#define IRQ_ENABLE_REG_ADDR   0x40003000U
#define IRQ_PENDING_REG_ADDR  0x40003004U

/* بیت‌های وقفه و ماسک رویدادها */
#define IRQ_BIT_TIMER         (1U << 0)
#define IRQ_BIT_PED_BTN       (1U << 1)
#define IRQ_BIT_EMG_ON        (1U << 2)
#define IRQ_BIT_EMG_OFF       (1U << 3)

#define EVT_TIMER_TICK        (1U << 0)
#define EVT_PED_REQUEST       (1U << 1)
#define EVT_EMG_ACTIVE        (1U << 2)
#define EVT_EMG_RELEASE       (1U << 3)

/* اولویت عددی وقفه‌ها (عدد بزرگ‌تر = اولویت بالاتر) */
#define IRQ_PRIO_EMERGENCY    4U
#define IRQ_PRIO_TIMER        3U
#define IRQ_PRIO_PED_BUTTON   2U

/* ثبات‌های شبیه‌سازی‌شده برای پاک‌سازی پرچم وقفه (Write-1-to-Clear / Mask) */
volatile uint32_t sim_irq_enable_reg  = IRQ_BIT_TIMER | IRQ_BIT_PED_BTN | IRQ_BIT_EMG_ON | IRQ_BIT_EMG_OFF;
volatile uint32_t sim_irq_pending_reg = 0U;
volatile uint32_t g_isr_tick_count    = 0U;

/* قرارداد اعلان رویداد به RTOS (تعریف‌شده در hw-9-rtos-tasks.c) */
extern void rtos_notify_from_isr(uint32_t event_mask);

static inline void clear_irq_pending_bit(uint32_t bit_mask) {
    (void)IRQ_PENDING_REG_ADDR;
    (void)IRQ_ENABLE_REG_ADDR;
    /* شبیه‌سازی پاک‌سازی پرچم وقفه پس از ثبت رویداد */
    sim_irq_pending_reg &= ~bit_mask;
}

/*
 * ISR خودروی اضطراری (اولویت 4 - بالاترین اولویت):
 * فقط پرچم را پاک می‌کند و رویداد را به EmergencyTask / RTOS اعلام می‌کند.
 */
void Emergency_IRQHandler(void) {
    clear_irq_pending_bit(IRQ_BIT_EMG_ON);
    rtos_notify_from_isr(EVT_EMG_ACTIVE);
}

/*
 * ISR پایان حضور خودروی اضطراری (اولویت 4):
 * درخواست خروج ایمن را به RTOS اعلام می‌کند.
 */
void EmergencyClear_IRQHandler(void) {
    clear_irq_pending_bit(IRQ_BIT_EMG_OFF);
    rtos_notify_from_isr(EVT_EMG_RELEASE);
}

/*
 * ISR تایمر سیستم (اولویت 3):
 * شمارنده تیک را افزایش داده و رویداد تیک زمانی را ارسال می‌کند.
 */
void Timer_IRQHandler(void) {
    g_isr_tick_count++;
    clear_irq_pending_bit(IRQ_BIT_TIMER);
    rtos_notify_from_isr(EVT_TIMER_TICK);
}

/*
 * ISR دکمه درخواست عبور عابر پیاده (اولویت 2):
 * درخواست عبور عابر را ثبت کرده و به RTOS اطلاع می‌دهد.
 */
void PedButton_IRQHandler(void) {
    clear_irq_pending_bit(IRQ_BIT_PED_BTN);
    rtos_notify_from_isr(EVT_PED_REQUEST);
}

/*
 * رفتار در صورت رخداد هم‌زمان چند وقفه:
 * کنترلر وقفه‌ها (مانند NVIC) ابتدا ISR با اولویت بالاتر (مثلاً خودروی امدادی یا تایمر) را
 * اجرا می‌کند. هر ISR کوتاه است، پرچم مربوطه را پاک می‌کند و پردازش اصلی را به Task می‌سپارد؛
 * بنابراین وقفه‌های با اولویت پایین‌تر پس از اتمام سریع ISR بالاتر بلافاصله سرویس می‌شوند.
 */
