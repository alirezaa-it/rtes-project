# تمرین جلسه ۳: ترکیب ماشین‌های حالت (Composition of State Machines)
**دانشجو:** علیرضا طاعتی  
**فایل تمرینی:** `hw-03-composed-fsm.md`

---

## معماری ترکیبی ماشین‌های حالت

در این مرحله، رفتار سیستم به دو ماشین حالت مجزا اما هماهنگ تقسیم می‌شود تا پیچیدگی کنترل کاهش یافته و ایمنی سیستم در شرایط بحرانی تضمین شود:

1. **ماشین حالت اول (`Traffic_Control_FSM` - منطق اصلی تقاطع):**  
   مسئول چرخش چراغ‌های مسیرهای شمالی–جنوبی و شرقی–غربی، تنظیم زمان‌بندی بر اساس تراکم و مدیریت عبور عابر پیاده و حالت شبانه است. این ماشین حالت خود دارای زیرحالت‌های سلسله‌مراتبی برای حالت عادی است.

2. **ماشین حالت دوم (`Safety_Emergency_FSM` - ناظر ایمنی و اولویت اضطراری):**  
   به صورت موازی و با اولویت بالاتر وضعیت اضطراری خودروهای امدادی و سلامت سیگنال‌ها را پایش می‌کند. هر زمان وضعیت اضطراری فعال شود یا خطای سیگنال رخ دهد، فرمان کنترل چراغ‌ها را به حالت ایمن یا مسیر اضطراری هدایت می‌کند.

---

## نوع ترکیب و حالت‌های ایمنی

* **ترکیب سلسله‌مراتبی (Hierarchical / Statecharts):** حالت `Normal_Operation` شامل زیرحالت‌های `NS_Green`، `NS_Yellow`، `EW_Green` و `EW_Yellow` است.
* **ترکیب همگام / ناظر (Synchronous Supervisor Composition):** در هر گام زمانی، ماشین ناظر ایمنی (`Safety_Emergency_FSM`) وضعیت اضطراری و خطا را بررسی کرده و خروجی نهایی چراغ‌ها را تعیین می‌کند.
* **حالت‌های ایمنی (Safety Modes):**
  * `Safe_All_Red`: توقف کامل همه جهت‌ها برای جلوگیری از برخورد در لحظه تغییر فاز یا خروج از حالت اضطراری.
  * `Safe_Fault_Hold`: نگه داشتن تقاطع در وضعیت ایمن در صورت بروز خطای ورودی/سخت‌افزاری.

---

## نمودار ترکیب ماشین‌های حالت (Mermaid)

```mermaid
stateDiagram-v2
    state "Traffic_Control_FSM (منطق اصلی)" as TCF {
        [*] --> Normal_Cycle
        state Normal_Cycle {
            [*] --> NS_Green
            NS_Green --> NS_Yellow : ns_done
            NS_Yellow --> EW_Green : y1_done
            EW_Green --> EW_Yellow : ew_done
            EW_Yellow --> NS_Green : y2_done
        }
        Normal_Cycle --> Ped_Phase : ped_req
        Ped_Phase --> Normal_Cycle : ped_done
        Normal_Cycle --> Night_Phase : night_on
        Night_Phase --> Normal_Cycle : night_off
    }

    state "Safety_Emergency_FSM (ناظر ایمنی و اضطراری)" as SEF {
        [*] --> Monitor_Normal
        Monitor_Normal --> Emergency_Active : emg_detected
        Emergency_Active --> Safe_All_Red : emg_ended
        Safe_All_Red --> Monitor_Normal : safety_clear
        Monitor_Normal --> Safe_Fault_Hold : sensor_fault
        Safe_Fault_Hold --> Safe_All_Red : fault_resolved
    }

    Monitor_Normal --> Emergency_Active : override_lights_for_emergency
    Emergency_Active --> Safe_All_Red : release_override_safely
```
