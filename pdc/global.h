#include "FreeRTOS.h"
#include "semphr.h"
#include <stdio.h>

#define MAX_STRING_SIZE 5000

/* ---------------------------------------------------------------- */
/*                         Global Variables                         */
/* ---------------------------------------------------------------- */

extern SemaphoreHandle_t mutex_cfg;  /* To lock cfg data objects */
extern SemaphoreHandle_t mutex_file;  /* To lock PMU Setup File */
extern SemaphoreHandle_t mutex_Lower_Layer_Details;  /* To lock objects of connection table that hold lower layer PMU/PDC ip and protocol */
extern SemaphoreHandle_t mutex_Upper_Layer_Details;  /* To lock objects of connection table that hold upper layer PDC ip and protocol */
extern SemaphoreHandle_t mutex_status_change;

extern unsigned char *cfgframe, *dataframe;

extern int UL_UDP_sockfd, UL_TCP_sockfd; /* socket descriptors */
extern TaskHandle_t UDP_thread, TCP_thread, p_thread, Deteache_thread;

extern FILE *fp_log, *fp_updc;
extern char tname[20];

/* iPDC Setup File path globally */
extern char ipdcFolderPath[200];
extern char ipdcFilePath[200];

/* -------------------------------------------------------------------- */
/*                  Global Database Variables                          */
/* -------------------------------------------------------------------- */

extern int DB_sockfd, DB_addr_len;

extern int PDC_IDCODE, TCPPORT, UDPPORT;
extern long int TSBWAIT;
extern char dbserver_ip[20];

extern unsigned char DATASYNC[3], CFGSYNC[3], CMDSYNC[3], CMDDATASEND[3], CMDDATAOFF[3], CMDCFGSEND[3];
