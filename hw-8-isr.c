/*
 * فایل: hw-8-isr.c
 * پروژه: سیستم چراغ راهنمای هوشمند تقاطع (Smart Traffic Light Controller)
 * دانشجو: علیرضا طاعتی
 * توضیح: روتین‌های سرویس وقفه (ISR) برای رویدادهای حساس به زمان در برابر روش Polling
 */

#include <stdint.h>
#include <stdbool.h>

/* پرچم‌های اتمیک مشترک بین ISR و تسک‌های RTOS */
static volatile uint32_t g_isr_tick_count      = 0U;
static volatile bool     g_ped_irq_flag        = false;
static volatile bool     g_emg_irq_flag        = false;
static volatile bool     g_emg_clear_irq_flag  = false;

/* شبیه‌سازی ثبات‌های وقفه */
static volatile uint32_t sim_irq_pending_reg   = 0U;

#define IRQ_BIT_TIMER     (1U << 0)
#define IRQ_BIT_PED_BTN   (1U << 1)
#define IRQ_BIT_EMG_ON    (1U << 2)
#define IRQ_BIT_EMG_OFF   (1U << 3)

/* شبیه‌سازی سیگنال بیدار کردن تسک RTOS از داخل ISR */
extern void rtos_notify_from_isr(uint32_t event_mask);

#define EVENT_TIMER_TICK   (1U << 0)
#define EVENT_PED_REQUEST  (1U << 1)
#define EVENT_EMG_ACTIVE   (1U << 2)
#define EVENT_EMG_RELEASE  (1U << 3)

/*
 * ISR 1: وقفه تایمر سیستم (بالاترین اولویت زمانی برای حفظ تیک بی‌درنگ)
 * کوتاه و سریع طراحی شده تا تاخیر وقفه (Interrupt Latency) کم بماند.
 */
void Timer_IRQHandler(void) {
    g_isr_tick_count++;
    sim_irq_pending_reg &= ~IRQ_BIT_TIMER;
    rtos_notify_from_isr(EVENT_TIMER_TICK);
}

/*
 * ISR 2: وقفه فشردن دکمه درخواست عبور عابر پیاده
 */
void PedButton_IRQHandler(void) {
    g_ped_irq_flag = true;
    sim_irq_pending_reg &= ~IRQ_BIT_PED_BTN;
    rtos_notify_from_isr(EVENT_PED_REQUEST);
}

/*
 * ISR 3: وقفه تشخیص خودروی اضطراری (آمبولانس / آتش‌نشانی)
 * دارای اولویت بالاتر نسبت به دکمه عابر پیاده
 */
void Emergency_IRQHandler(void) {
    g_emg_irq_flag = true;
    g_emg_clear_irq_flag = false;
    sim_irq_pending_reg &= ~IRQ_BIT_EMG_ON;
    rtos_notify_from_isr(EVENT_EMG_ACTIVE);
}

/*
 * ISR 4: وقفه پایان حضور خودروی اضطراری در محدوده تقاطع
 */
void EmergencyClear_IRQHandler(void) {
    g_emg_clear_irq_flag = true;
    g_emg_irq_flag = false;
    sim_irq_pending_reg &= ~IRQ_BIT_EMG_OFF;
    rtos_notify_from_isr(EVENT_EMG_RELEASE);
}

/*
 * مقایسه کوتاه: چرا ISR برای رویدادهای اضطراری و دکمه عابر بهتر از Polling است؟
 * - در Polling، تا زمان رسیدن حلقه به بررسی وضعیت، ممکن است تأخیر رخ دهد یا CPU بیهوده مشغول شود.
 * - در ISR، رویداد بلافاصله ثبت شده و تسک مربوطه بیدار می‌شود؛ در عین حال منطق سنگین به Task منتقل می‌شود.
 */
