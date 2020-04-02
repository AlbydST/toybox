/* rtcwake.c - enter sleep state until given time.
 *
 * Copyright 2020 The Android Open Source Project

USE_RTCWAKE(NEWTOY(rtcwake, "(list-modes);(auto)a(device)d:(local)l(mode)m:(seconds)s#(time)t#(utc)u(verbose)v[!alu]", TOYFLAG_USR|TOYFLAG_BIN))

config RTCWAKE
  bool "rtcwake"
  default n
  help
    usage: rtcwake [-aluv] [-d FILE] [-m MODE] [-s SECS] [-t UNIX]

    Enter the given sleep state until the given time.

    -a	RTC uses time specified in /etc/adjtime
    -d FILE	Device to use (default /dev/rtc)
    -l	RTC uses local time
    -m	Mode (--list-modes to see those supported by your kernel):
    	  disable  Cancel current alarm
    	  freeze   Freeze processes, idle processors
    	  disk     S4: suspend to disk
    	  mem      S3: suspend to RAM
    	  no       Don't suspend, just set wakeup time
    	  off      S5: power off
    	  on       Don't suspend, poll RTC for alarm
    	  show     Don't suspend, just show current alarm
    	  standby  S1: default
    -s SECS	Wake SECS seconds from now
    -t UNIX	Wake UNIX seconds from epoch
    -u	RTC uses UTC
    -v	Verbose
*/

#define FOR_rtcwake
#include "toys.h"

GLOBALS(
  long long t, s;
  char *m, *d;
)

void rtcwake_main(void)
{
  struct rtc_wkalrm alarm = {};
  struct tm rtc_tm;
  time_t now, rtc_now, then;
  int fd, utc;

  if (FLAG(list_modes)) {
    xreadfile("/sys/power/state", toybuf, sizeof(toybuf));
    printf("off no on disable show %s", toybuf);
    return;
  }

  // util-linux defaults to "suspend", even though I don't have anything that
  // supports that (testing everything from a ~2010 laptop to a 2019 desktop).
  if (!TT.m) TT.m = "suspend";

  if (FLAG(u)) utc = 1;
  else if (FLAG(l)) utc = 0;
  else {
    xreadfile("/etc/adjtime", toybuf, sizeof(toybuf));
    utc = !!strstr(toybuf, "UTC");
  }
  if (FLAG(v)) xprintf("RTC time: %s\n", utc ? "UTC" : "local");

  if (!TT.d) TT.d = "/dev/rtc0";
  if (FLAG(v)) xprintf("Device: %s\n", TT.d);
  fd = xopen(TT.d, O_RDWR);

  now = time(0);
  xioctl(fd, RTC_RD_TIME, &rtc_tm);
  rtc_now = xmktime(&rtc_tm, utc);
  if (FLAG(v)) {
    xprintf("System time:\t%lld / %s", (long long)now, ctime(&now));
    xprintf("RTC time:\t%lld / %s", (long long)rtc_now, ctime(&rtc_now));
  }

  if (!strcmp(TT.m, "show")) { // Don't suspend, just show current alarm.
    xioctl(fd, RTC_WKALM_RD, &TT.alarm);
    if (!TT.alarm.enabled) xputs("alarm: off");
    else {
      if ((then = mktime((void *)&TT.alarm.time)) < 0) perror_exit("mktime");
      xprintf("alarm: on %s", ctime(&then));
    }
    goto done;
  } else if (!strcmp(TT.m, "disable")) { // Cancel current alarm.
    xioctl(fd, RTC_WKALM_RD, &TT.alarm);
    TT.alarm.enabled = 0;
    xioctl(fd, RTC_WKALM_SET, &TT.alarm);
    goto done;
  }

  if (FLAG(s)) {
    then = rtc_now + TT.s + 1; // strace shows util-linux adds 1.
  } else if (FLAG(t)) {
    then = TT.t + (rtc_now - now);
    if (then<=rtc_now) error_exit("rtc %lld >= %lld", (long long)rtc_now, TT.t);
  } else help_exit("-m %s needs -s or -t", TT.m);
  if (FLAG(v)) xprintf("Wake time:\t%lld / %s", (long long)then, ctime(&then));

  if (!(utc ? gmtime_r : localtime_r)(&then, (void *)&TT.alarm.time))
    error_exit(utc ? "gmtime_r failed" : "localtime_r failed");

  TT.alarm.enabled = 1;
  xioctl(fd, RTC_WKALM_SET, &TT.alarm);
  sync();

  xprintf("wakeup using \"%s\" from %s at %s", TT.m, TT.d, ctime(&then));
  msleep(10);

  if (!strcmp(TT.m, "no")) { // Don't suspend, just set wakeup time.
  } else if (!strcmp(TT.m, "on")) { // Don't suspend, poll RTC for alarm.
    unsigned long data = 0;

    if (FLAG(v)) xputs("Reading RTC...");
    while (!(data & RTC_AF)) {
      if (read(fd, &data, sizeof(data)) != sizeof(data)) perror_exit("read");
      if (FLAG(v)) xprintf("... %s: %lx\n", TT.d, data);
    }
  } else if (!strcmp(TT.m, "off")) {
    xexec((char *[]){"poweroff", 0});
  } else {
    // Everything else lands here for one final step. The write will fail with
    // EINVAL if the mode is not supported.
    int fd = xopen("/sys/power/state", O_WRONLY);

    xwrite(fd, TT.m, strlen(TT.m));
    close(fd);
  }
done:
  close(fd);
}
