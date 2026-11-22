# راهنمای ثبت کامیت‌ها و انتشار نسخه نهایی (`v1.0`) در GitHub
**دانشجو:** علیرضا طاعتی  
**ریپازیتوری:** `rtes-project`

---

## دستورهای راه‌اندازی ریپازیتوری و ثبت کامیت‌های گام‌به‌گام

```bash
git init
git branch -M main
git remote add origin https://github.com/<YOUR-USERNAME>/rtes-project.git

# ثبت تمرین جلسه ۱ و README اولیه
git add README.md hw-01-fsm.md
git commit -m "hw-01: add initial README and basic FSM for smart traffic light"

# ثبت تمرین جلسات ۲ و ۳
git add hw-02-extended-fsm.md hw-03-composed-fsm.md
git commit -m "hw-02 hw-03: add extended FSM and composed state machines"

# ثبت تمرین جلسات ۴، ۵ و ۶
git add hw-4-memory_map.md hw-5-main.c hw-6-memory_budget.md
git commit -m "hw-04..06: add memory map, initial C FSM implementation, and memory budget"

# ثبت تمرین جلسات ۸، ۹، ۱۰ و ۱۱
git add hw-8-isr.c hw-9-rtos-tasks.c hw-10-scheduling.md hw-11-wcet-opt.md
git commit -m "hw-08..11: add ISRs, RTOS tasks, scheduling analysis, and WCET optimization"

# ثبت راهنمای انتشار و پوش به شاخه main
git add git_release_guide.md
git commit -m "docs: add git release guide"
git push -u origin main
```

---

## ثبت تگ نهایی `v1.0` در پایان پروژه

```bash
git tag -a v1.0 -m "Final release v1.0 - Smart Traffic Light Controller (RTES)"
git push origin v1.0
```
