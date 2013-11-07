#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <syslog.h>

void log_err(const char* message) {
	char s[1000];
	time_t t = time(NULL);
	struct tm * p = localtime(&t);
    strftime(s, 1000, "%A %H:%M %B %d %Y", p);

   FILE * fp=fopen(LOG_FILE_PATH,"a+");
   fprintf(fp, "%s|%s\n", s, message);
   fclose(fp);

   openlog (RFID_LOG, LOG_AUTH, LOG_NOTICE);
   syslog (LOG_NOTICE,"%s", message);
   closelog();
}
