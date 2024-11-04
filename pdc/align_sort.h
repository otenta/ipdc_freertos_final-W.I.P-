#include "FreeRTOS.h"
#include "semphr.h"
#include <stdio.h>

#define MAXTSB 20
//15/5 changes
//#define MAXSMB 30

//2/11 CHANGES

#define MAP_SIZE 256
#define MAX_SAMPLES 40

/* ---------------------------------------------------------------- */
/*                  Global Data Structures                        */
/* ---------------------------------------------------------------- */

/* Data Structure for Time Stamp Buffer */
struct TimeStampBuffer {
    unsigned char *soc;
    unsigned char *fracsec;
    int used;   // 0 for unused, -1 for used, and 1 is for ready to dispatch?
    struct pmupdc_id_list *idlist;
    struct data_frame *first_data_frame;
} TSB[MAXTSB];

struct pmupdc_id_list {
	unsigned char *idcode;
    int num_pmu;
    struct pmupdc_id_list *nextid;
};

struct waitTime {
    int index;
    int wait_time;
};

/* ---------------------------------------------------------------- */
/*         Id code Phasor Measurement Mapper Prototypes             */
/* ---------------------------------------------------------------- */
//2/1 CHANGES
struct Phasor {
    float real;
    float imaginary;
};

struct DataSample {
    unsigned char *idcode;
    long int *soc;       // Array to store multiple values
    long int *fracsec;   // Array to store multiple values
    float *freq;      // Array to store multiple values
    float *dfreq;     // Array to store multiple values
    int *phnmr;
    int num_samples;           // Track number of samples
    struct Phasor *phasors; // Array of phasors
};

//Structure to hold map entry
struct MapEntry {
    struct DataSample *sample;
    struct MapEntry *next;
};

//Structure to hold data map
struct DataMap {
    struct MapEntry *entries[MAP_SIZE];
};


//2/1 CHANGES

//////////////////////////////////////////////////////////////////
//struct PhasorBuffer {
//    unsigned char **phasors;
//   // unsigned char *soc;
//   // unsigned  char *fracsec;
//    int size;
//    int capacity;
//};
//
////struct PhasorBufferMap {
////    unsigned int *idcode;
////    struct PhasorBuffer *buffer;
////    struct PhasorBufferMap *next;
////};
//
////15/5 changes
//struct SampleMapBuffer
//{
//    unsigned int *idcode;
//    int used;
//    struct PhasorBuffer *buffer;
//    struct SampleMapBuffer *next;
//} SMB[30];
//////////15/5 changes

//struct PhasorBuffer *initPhasorBuffer();

/* ---------------------------------------------------------------- */
/*   Function Prototypes For sending data to PL                     */
/* ---------------------------------------------------------------- */
//2/1 CHANGES

struct DataSample *getDataSamples(struct DataMap *map, unsigned char *idcode);
void insertDataSample(struct DataMap *map, struct DataSample *sample);
struct DataMap *createDataMap();
void printRawBytes(const unsigned char *str);
struct DataSample *getDataSampleById(struct DataMap *map, const unsigned char *idcode);
unsigned int hash(const unsigned char *idcode);
void destroyDataMap(struct DataMap *map);
struct DataSample *createDataSample(unsigned char *idcode, int phnmr);
void freeDataSample(struct DataSample *sample);
int addDataSample(struct DataMap *map, struct DataSample *sample);
void printDataMap(struct DataMap *map);
void printDataSample(struct DataSample *sample);
void copy_cbyc(unsigned char dst[], unsigned char *s, int size);
char *strdup_free(const char *s);
float decode_ieee_single(const void *v);
//float hypotf(float x, float y);
//float atan2f(float y, float x);
//float atanf(float x);
//float sqrtf(float number);
void *memset(void *ptr, int value, size_t num);
int strcmp(const char *str1, const char *str2);
//REAL DEAL MAP
struct DataSample *createSampleToAddToMap(struct data_frame *df, struct DataSample *sample);
//2/1 CHANGES


//////////////////////////////////////////////////////////////////
//15/5 changes
//void initializeSampleBuffer();
//int addDataFramesToMap(unsigned int index);
//void retrievePhasorsById(unsigned int idcode);
//void printSMB();
//void cleanup();
//void populateTSBrandom();
//void populateDataFrames();
//void printTSB();
///////////////////
//void addPhasorMeasurement(struct PhasorBuffer *buffer, unsigned char *phasor);

//void addToMapAndAddMeasurement(struct PhasorBufferMap **map, unsigned *idcode, unsigned char *phasor);

void* freertos_realloc(void *ptr, size_t size);

/* ---------------------------------------------------------------- */
/*                     Function Prototypes                         */
/* ---------------------------------------------------------------- */

void time_align(struct data_frame *df);

void assign_df_to_TSB(struct data_frame *df, int index);

void* dispatch(void* index);

void sort_data_inside_TSB(int index);

void clear_TSB(int index);

int create_dataframe(int index);

int create_cfgframe();

void initializeTSB();

int get_TSB_index();

void* TSBwait(void* index);
