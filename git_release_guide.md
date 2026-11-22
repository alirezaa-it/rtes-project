# راهنمای گردش‌کار Git و انتشار نسخه نهایی (`v1.0`)

این راهنما برای ثبت واقعی و مرحله‌ای تغییرات پروژه Smart Traffic Light Controller است.

## ۱. بررسی وضعیت مخزن

```bash
git status
git branch --show-current
git remote -v
```

اگر مخزن از قبل ساخته شده و remote دارد، `git init` یا افزودن دوباره remote لازم نیست.

## ۲. ثبت تمرین هر جلسه

طبق دستور درس، هر تمرین باید پس از تدریس همان جلسه و در فایل جداگانه ثبت و Push شود. نمونه برای جلسه پنجم:

```bash
git add hw-5-main.c
git commit -m "hw-05: implement traffic light FSM in C"
git push origin main
```

برای جلسه‌های دیگر، نام فایل و پیام Commit را متناسب با همان تمرین تغییر دهید. از جمع‌کردن همه تمرین‌های چندین جلسه برای یک Commit پایانی خودداری کنید.

## ۳. ثبت اصلاحات فعلی

پس از جایگزینی فایل‌های اصلاح‌شده، ابتدا تغییرات را بررسی کنید:

```bash
git diff --check
git diff
git status
```

سپس فایل‌ها را به‌صورت مشخص اضافه و Commit کنید؛ برای مثال:

```bash
git add README.md hw-9-rtos-tasks.c hw-10-scheduling.md git_release_guide.md
git commit -m "docs and RTOS: correct task synchronization and scheduling analysis"
git push origin main
```

فقط فایل‌هایی را در `git add` وارد کنید که واقعاً تغییر داده‌اید.

## ۴. ساخت تگ نهایی

تگ را فقط وقتی بسازید که همه فایل‌ها بازبینی شده، برنامه‌های قابل اجرا تست شده و نسخه نهایی آماده تحویل باشد:

```bash
git tag -a v1.0 -m "Final release v1.0 - Smart Traffic Light Controller"
git push origin v1.0
```

اگر تگ از قبل وجود دارد، قبل از هر اقدامی آن را بررسی کنید:

```bash
git tag --list
git show v1.0
```

## ۵. نکته درباره تاریخچه

برای رعایت الزام فعالیت مستمر، Commitهای واقعی را در زمان انجام کار ثبت کنید. تاریخ Commit یا سابقه Push را به‌صورت دستی جعل نکنید؛ اگر ثبت‌های قبلی دیر انجام شده‌اند، از این پس روند منظم و شفاف را ادامه دهید.
