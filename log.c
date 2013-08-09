#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <syslog.h>

void log_err(const char* message) {
   time_t now;
   time(&now);
   FILE * fp=fopen(LOG_FILE_PATH,"w");
   fprintf(fp, "%s%s\n", ctime(&now), message);
   fclose(fp);

   openlog (RFID_LOG, LOG_AUTH, LOG_NOTICE);
   syslog (LOG_NOTICE,"%s", message);
   closelog();
}
