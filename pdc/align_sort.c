#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/time.h>
#include "global.h"
#include "parser.h"
#include "connections.h"
#include "dallocate.h"
#include "xil_printf.h"
#include "FreeRTOS.h"
#include "task.h"
#include "align_sort.h"

#include <assert.h>

/* ------------------------------------------------------------------------------------ */
/*                        Functions in align_sort.c                                     */
/* ------------------------------------------------------------------------------------ */
/* 1. void time_align(struct data_frame *df)                                           */
/* 2. void assign_df_to_TSB(struct data_frame *df, int index)                           */
/* 3. void dispatch(void *pvParameters)                                                 */
/* 4. void sort_data_inside_TSB(int index)                                              */
/* 5. void clear_TSB(int index)                                                         */
/* 6. int create_dataframe(int index)                                                    */
/* 7. void create_cfgframe()                                                             */
/* ------------------------------------------------------------------------------------ */
extern SemaphoreHandle_t mutex_on_TSB;
int i, ab;

/* ---------------------------------------------------------------------------- */
/* FUNCTION  initializeTSB():                                                    */
/* ---------------------------------------------------------------------------- */
void initializeTSB() {
	xil_printf("mphke TSB initialize\n");
	int j;
	//TaskHandle_t Deteache_thread;

	/* Initially all the TSBs are unused */
	for (j = 0; j < MAXTSB; j++)
		TSB[j].used = 0;

	/* Create dispatch task */
	//xTaskCreate(dispatch, "DispatchTask", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY, &Deteache_thread);
}

/* ---------------------------------------------------------------------------- */
/* FUNCTION  get_TSB_index():                                                    */
/* ---------------------------------------------------------------------------- */
int get_TSB_index() {

	xil_printf("mphke TSB index\n");
	int j;
	//struct timeval timer_start;

	if (xSemaphoreTake(mutex_on_TSB, portMAX_DELAY) == pdTRUE) {
		for (j = 0; j < MAXTSB; j++) {
			if (TSB[j].used == 0) {
				TSB[j].used = -1;
				// gettimeofday(&timer_start, NULL);
				xil_printf("\nTSB[%d] occupied.\n", j);
				xSemaphoreGive(mutex_on_TSB);
				return j;
			}
		}
		xSemaphoreGive(mutex_on_TSB);
	}
	if (j == MAXTSB)
		return -1;
}

//* ---------------------------------------------------------------------------- */
/* FUNCTION  TSBwait(void* WT):                                                  */
/* ---------------------------------------------------------------------------- */
void* TSBwait(void* arg) {
	int ind = arg;
	TaskHandle_t dispatchTask;
	xil_printf("mphke TSB wait me index %d \n", ind);

	// struct waitTime *wt = (struct waitTime*) WT;
	//int ind = wt->index;

	TickType_t ticks = pdMS_TO_TICKS(2000);

	xil_printf("Wait time %lu ms, for TSB[%d]\n", ticks, ind);

	vTaskDelay(ticks);

	if (xSemaphoreTake(mutex_on_TSB, portMAX_DELAY) == pdTRUE) {
		xil_printf("\nWait time over for %d. ", ind);
		TSB[ind].used = 1;
		xil_printf("Now TSB[%d].used = %d\n", ind, TSB[ind].used);
		xSemaphoreGive(mutex_on_TSB);
	}
	//Dispatch task
	xTaskCreate(dispatch, "DispatchTask", configMINIMAL_STACK_SIZE * 2,
			(void*) ind, tskIDLE_PRIORITY, &dispatchTask);
	//xTaskCreate(TSBwait, "TSBWaitTask", configMINIMAL_STACK_SIZE*2, (void*)ind, tskIDLE_PRIORITY, &waitTask);

	vTaskDelete(NULL);

}

/* ---------------------------------------------------------------------------- */
/* FUNCTION  time_align():                                                       */
/* It searches for the correct TSB[index] where data frame df is to be           */
/* assigned. If the df has soc and fracsec which is older then soc and fracsec   */
/* of TSB[first] then we discard the data frame                                  */
/* ---------------------------------------------------------------------------- */
void time_align(struct data_frame *df) {
	xil_printf("mphke time align\n");
	int flag = 0, j;

	/* Take the mutex to protect access to TSB array */
	if (xSemaphoreTake(mutex_on_TSB, portMAX_DELAY) == pdTRUE) {
		for (j = 0; j < MAXTSB; j++) {
			if (TSB[j].used == -1) {
				if (!ncmp_cbyc((unsigned char *) TSB[j].soc, df->soc, 4)) {
					if (!ncmp_cbyc((unsigned char *) TSB[j].fracsec,
							df->fracsec, 3)) {
						flag = 1;
						break;
					}
				} else {
					continue;
				}
			}
		}
		xSemaphoreGive(mutex_on_TSB); // Release the mutex
	}

	if (flag) {
		/* Print message and assign data frame to TSB */
		xil_printf(
				"TSB[%d] is already available for sec = %ld and fsec = %ld.\n",
				j, to_long_int_convertor(df->soc),
				to_long_int_convertor(df->fracsec));
		assign_df_to_TSB(df, j);
	} else {
		int i = get_TSB_index();
		if (i == -1)
			xil_printf("No TSB is vPortFree right now?\n");
		else
			assign_df_to_TSB(df, i);
	}
}

/* ---------------------------------------------------------------------------- */
/* FUNCTION  assign_df_to_TSB():                               	     		*/
/* It assigns the arrived data frame df to TSB[index]							*/
/* ---------------------------------------------------------------------------- */
void assign_df_to_TSB(struct data_frame *df, int index) {
	TaskHandle_t waitTask;

	xil_printf("mphke TSB assign df to tsb index is %d", index);

	/* Check if the TSB is used for the first time. If so we need to
	 allocate memory to its member variables */
	if (TSB[index].soc == NULL) { // 1 if
		struct cfg_frame *temp_cfg = cfgfirst;

		TSB[index].soc = pvPortMalloc(5);
		TSB[index].fracsec = pvPortMalloc(5);

		memset(TSB[index].soc, '\0', 5);
		memset(TSB[index].fracsec, '\0', 5);

		copy_cbyc((unsigned char *) TSB[index].soc, df->soc, 4);
		copy_cbyc((unsigned char *) TSB[index].fracsec, df->fracsec, 4);

		TSB[index].first_data_frame = df;

		struct pmupdc_id_list *temp_pmuid;
		while (temp_cfg != NULL) {
			struct pmupdc_id_list *pmuid = pvPortMalloc(
					sizeof(struct pmupdc_id_list));
			pmuid->idcode = pvPortMalloc(3);
			memset(pmuid->idcode, '\0', 3);
			copy_cbyc((unsigned char *) pmuid->idcode, temp_cfg->idcode, 2);
			pmuid->num_pmu = to_intconvertor(temp_cfg->num_pmu);
			pmuid->nextid = NULL;

			if (TSB[index].idlist == NULL) {
				TSB[index].idlist = temp_pmuid;
			} else {
				temp_pmuid->nextid = pmuid;
				temp_pmuid = pmuid;
			}
			temp_cfg = temp_cfg->cfgnext;
		}

		temp_cfg = cfgfirst;
		if (temp_cfg != NULL) {
			//struct waitTime wt;
			//wt.index = index;
			//wt.wait_time = pdMS_TO_TICKS(2000);
			int ind = index;

			xTaskCreate(TSBwait, "TSBWaitTask", configMINIMAL_STACK_SIZE * 2,
					(void*) ind, tskIDLE_PRIORITY, &waitTask);
		}
	} else { // 1 if else
		struct cfg_frame *temp_cfg = cfgfirst;
		if (TSB[index].first_data_frame == NULL) { // 2 if
			copy_cbyc((unsigned char *) TSB[index].soc, df->soc, 4);
			copy_cbyc((unsigned char *) TSB[index].fracsec, df->fracsec, 4);

			TSB[index].first_data_frame = df;

			struct pmupdc_id_list *temp_pmuid;
			while (temp_cfg != NULL) {
				struct pmupdc_id_list *pmuid = pvPortMalloc(
						sizeof(struct pmupdc_id_list));
				pmuid->idcode = pvPortMalloc(3);
				memset(pmuid->idcode, '\0', 3);
				copy_cbyc((unsigned char *) pmuid->idcode, temp_cfg->idcode, 2);
				pmuid->num_pmu = to_intconvertor(temp_cfg->num_pmu);
				pmuid->nextid = NULL;

				if (TSB[index].idlist == NULL) {
					TSB[index].idlist = temp_pmuid;
				} else {
					temp_pmuid->nextid = pmuid;
					temp_pmuid = pmuid;
				}
				temp_cfg = temp_cfg->cfgnext;
			}

			temp_cfg = cfgfirst;
			if (temp_cfg != NULL) {
				// struct waitTime wt;
				// wt.index = index;
				//wt.wait_time = pdMS_TO_TICKS(2000);

				int ind = index;

				xTaskCreate(TSBwait, "TSBWaitTask",
						configMINIMAL_STACK_SIZE * 2, (void*) ind,
						tskIDLE_PRIORITY, &waitTask);
			}
		} else { // 2 if else
			xil_printf(
					"mphke TSB assign sthn else me to idio fracsec soc\n kai index %d",
					index);
			struct data_frame *temp_df, *check_df;

			check_df = TSB[index].first_data_frame;
			while (check_df != NULL) {
				if (!ncmp_cbyc(check_df->idcode, df->idcode, 2)) {
					free_dataframe_object(df);
					return;
				} else {
					check_df = check_df->dnext;
				}
			}

			temp_df = TSB[index].first_data_frame;
			while (temp_df->dnext != NULL) {
				temp_df = temp_df->dnext;
			}

			temp_df->dnext = df;
			xil_printf("telos else tsb assign\n");
		} // 2 if ends
	} // 1 if ends
}

/* ---------------------------------------------------------------------------- */
/* FUNCTION  sort_data_inside_TSB():                                          */
/* This function sorts the data frames in the TSB[index] in the order of the   */
/* Idcodes present in the 'struct pmupdc_id_list list' of the TSB[index]       */
/* ---------------------------------------------------------------------------- */
void sort_data_inside_TSB(int index) {

	xil_printf("mphke TSB sort me index %d\n", index);
	struct pmupdc_id_list *temp_list;
	struct data_frame *prev_df, *curr_df, *sorted_df, *r_df, *s_df, *last_df,
			*p_df;
	int match = 0;
	unsigned int id_check;

	xil_printf("phre TSB semaphore\n");

	/* Pointer track_df will hold the address of the last sorted data_frame object.
	 Thus we assign to the 'track_df->dnext ' the next sorted data_frame  object and so on */

	temp_list = TSB[index].idlist; /* Starting ID required for sorting */
	last_df = TSB[index].first_data_frame;
	p_df = TSB[index].first_data_frame;

	xil_printf("phre pointers sthn TSB sort\n");

	curr_df = last_df;
	sorted_df = prev_df = NULL;

	while (temp_list != NULL) { // 1 while

		xil_printf("mphke sth while tsb sort\n");

		match = 0;
		while (curr_df != NULL) { // 2. Traverse the pmu id in TSB and sort

			xil_printf("mphke sth deuterh while tsb sort\n");
			if (!ncmp_cbyc(curr_df->idcode, (unsigned char *) temp_list->idcode,
					2)) {
				xil_printf("mphke sth prwth if tsb sort\n");

				match = 1;
				break;

			} else {
				xil_printf("mphke sth prwth else tsb sort\n");

				prev_df = curr_df;
				curr_df = curr_df->dnext;

			}

		} // 2 while ends

		xil_printf("vghke apthn prwth while tsb sort\n");

		if (match == 1) {
			xil_printf("mphke if match == 1 tsb sort\n");

			if (prev_df == NULL) {
				xil_printf("mphke if prev_df==NULL tsb sort\n");

				r_df = curr_df;
				s_df = curr_df->dnext;
				if (sorted_df == NULL) {
					xil_printf("mphke if sorted_df =null tsb sort\n");

					sorted_df = r_df;
					TSB[index].first_data_frame = sorted_df;
				} else {
					xil_printf("mphke else tsb sort\n");

					sorted_df->dnext = r_df;
					sorted_df = r_df;
				}
				sorted_df->dnext = s_df;
				curr_df = last_df = s_df;

			} else {
				xil_printf("mphke sth deuterh else tsb sort\n");

				if (sorted_df == NULL) {

					r_df = curr_df;
					s_df = r_df->dnext;
					prev_df->dnext = s_df;
					sorted_df = r_df;
					TSB[index].first_data_frame = sorted_df;
					sorted_df->dnext = last_df;
					curr_df = last_df;
					prev_df = NULL;

				} else { //if(sorted_df != NULL) {

					r_df = curr_df;
					s_df = r_df->dnext;
					prev_df->dnext = s_df;
					sorted_df->dnext = r_df;
					sorted_df = r_df;
					sorted_df->dnext = last_df;
					curr_df = last_df;
					prev_df = NULL;
				}
			}

		} else {  // id whose data frame did not arrive No match

			xil_printf("mphke sth trith else tsb sort\n");

			unsigned char *idcode;
			idcode = pvPortMalloc(3);

			struct data_frame *df = pvPortMalloc(sizeof(struct data_frame));
			if (!df) {

				xil_printf("Not enough memory data_frame.\n");
			}
			df->dnext = NULL;

			xil_printf("mphke sto memory allocation  tsb sort\n");

			// Allocate memory for df->framesize
			df->framesize = pvPortMalloc(3);
			if (!df->framesize) {

				xil_printf("Not enough memory df->idcode\n");
				exit(1);
			}

			// Allocate memory for df->idcode
			df->idcode = pvPortMalloc(3);
			if (!df->idcode) {

				xil_printf("Not enough memory df->idcode\n");
				exit(1);
			}

			// Allocate memory for df->soc
			df->soc = pvPortMalloc(5);
			if (!df->soc) {

				xil_printf("Not enough memory df->soc\n");
				exit(1);
			}

			// Allocate memory for df->fracsec
			df->fracsec = pvPortMalloc(5);
			if (!df->fracsec) {

				xil_printf("Not enough memory df->fracsec\n");
				exit(1);
			}

			/* 16 for sync,fsize,idcode,soc,fracsec,checksum */
			unsigned int size = (16 + (temp_list->num_pmu) * 2)
					* sizeof(unsigned char);

			df->num_pmu = temp_list->num_pmu;

			//Copy FRAMESIZE
			int_to_ascii_convertor(size, df->framesize);
			df->framesize[2] = '\0';

			//Copy IDCODE
			copy_cbyc(df->idcode, (unsigned char *) temp_list->idcode, 2);
			df->idcode[2] = '\0';

			//Copy SOC
			copy_cbyc(df->soc, (unsigned char *) TSB[index].soc, 4);
			df->soc[4] = '\0';

			//Copy FRACSEC
			copy_cbyc(df->fracsec, (unsigned char *) TSB[index].fracsec, 4);
			df->fracsec[4] = '\0';

			df->dpmu = pvPortMalloc(
					temp_list->num_pmu * sizeof(struct data_for_each_pmu *));
			if (!df->dpmu) {

				xil_printf("Not enough memory df->dpmu[][]\n");
				exit(1);
			}

			for (i = 0; i < temp_list->num_pmu; i++) {

				xil_printf("mphke sth for gia ta pmus sort\n");

				df->dpmu[i] = pvPortMalloc(sizeof(struct data_for_each_pmu));
			}

			int j = 0;

			// PMU data has not come
			while (j < temp_list->num_pmu) {

				xil_printf("mphke sth while gia ta pmus tsb sort\n");

				df->dpmu[j]->stat = pvPortMalloc(3);
				if (!df->dpmu[j]->stat) {

					xil_printf("Not enough memory for df->dpmu[j]->stat\n");
				}

				df->dpmu[j]->stat[0] = 0x00;
				df->dpmu[j]->stat[1] = 0x0F;
				df->dpmu[j]->stat[2] = '\0';
				j++;
			}

			if (sorted_df == NULL) {

				r_df = df;
				sorted_df = r_df;
				TSB[index].first_data_frame = sorted_df;
				sorted_df->dnext = last_df;
				curr_df = last_df;
				prev_df = NULL;

			} else {

				r_df = df;
				sorted_df->dnext = r_df;
				sorted_df = r_df;
				sorted_df->dnext = last_df;
				curr_df = last_df;
				prev_df = NULL;
			}
		}

		temp_list = temp_list->nextid;  //go for next ID

	} // 1. while ends

	xil_printf("teleiwse h while tsb sort\n");

	p_df = TSB[index].first_data_frame;
	while (p_df != NULL) {

		id_check = to_intconvertor(p_df->idcode);
		p_df = p_df->dnext;
	}
}

/* ---------------------------------------------------------------------------- */
/* FUNCTION  clear_TSB():                                                        */
/* It clears TSB[index] and frees all data frame objects after the data frames  */
/* in TSB[index] have been dispatched to destination device                      */
/* ---------------------------------------------------------------------------- */
void clear_TSB(int index) { //


//	    for (int i = 0; i < MAXTSB; i++)
//	    {
	        // Free memory allocated for soc and fracsec
	    	vPortFree(TSB[index].soc);
	    	vPortFree(TSB[index].fracsec);
	        TSB[index].used = 0;                // Set used to 0 to mark as unused
	        TSB[index].first_data_frame = NULL; // Set first_data_frame to NULL
//	    }


//	unsigned long int tsb_soc, tsb_fracsec;
//	tsb_soc = to_long_int_convertor((unsigned char *) TSB[index].soc);
//	tsb_fracsec = to_long_int_convertor((unsigned char *) TSB[index].fracsec);
//
//	//TODO : this needs to be changed
//	memset(TSB[index].soc, '\0', 5);
//	memset(TSB[index].fracsec, '\0', 5);
//
//	struct pmupdc_id_list *t_list, *r_list;
//	t_list = TSB[index].idlist;
//
//	while (t_list != NULL) {
//
//		r_list = t_list->nextid;
//		vPortFree(t_list->idcode);
//		vPortFree(t_list);
//		t_list = r_list;
//	}
//
//	struct data_frame *t, *r;
//	t = TSB[index].first_data_frame;
//
//	while (t != NULL) {
//
//		r = t->dnext;
//		free_dataframe_object(t);
//		t = r;
//	}
//
//	TSB[index].first_data_frame = NULL;
//	TSB[index].idlist = NULL;
//
//	xSemaphoreTake(mutex_on_TSB, portMAX_DELAY);
//	TSB[index].used = 0;
//	xil_printf("ClearTSB for [%d] & used = %d.\n", index, TSB[index].used);
//	xSemaphoreGive(mutex_on_TSB);
}
//Rename this to something like prepareDispatch()
//The actual data transfer probably won't be performed here
void* dispatch(void* index) {
	unsigned int ind = index;
	static struct PhasorBufferMap *bufferMap = NULL;

	xil_printf("index sto dispatch is %d \n", ind);
	int flag = 0;

	struct data_frame *temp_df = TSB[ind].first_data_frame;
	struct DataMap *map = createDataMap();

	xSemaphoreGive(mutex_on_TSB);

	while (temp_df != NULL)
	    {

	        int j = 0;
	        int current_samples = 0;
	        int phnmr_max = 0;
	        int phnmr_min = 0;
	        while (j < temp_df->num_pmu)
	        {
	            phnmr_min = temp_df->dpmu[j]->phnmr;

	            if (phnmr_min > phnmr_max)
	            {
	                phnmr_max = phnmr_min;
	            }
	            j++;
	        }
	        xil_printf("PHNMR max is %d\n", phnmr_max);

	        struct DataSample *initialSample = createDataSample(temp_df->idcode, phnmr_max);

	        // THIS LOOKS LIKE IT OVERIDES PHASOR MEASUREMENTS. MAKE SURE WHEN ADDING UNDER SAME ID CODE THAT PHASORS ARE NOT OVERRIDEN
	        struct DataSample *sample = createSampleToAddToMap(temp_df, initialSample);

	        unsigned char *sampleIdcode = malloc(2 * sizeof(unsigned char));
	        sampleIdcode = strdup_free(sample->idcode);

	        current_samples = addDataSample(map, sample);

	        const unsigned char *idcode_to_retrieve = (unsigned char *)sampleIdcode;
	         xil_printf("print raw bytes for sample idcode\n");
	         printRawBytes(sampleIdcode);

	        struct DataSample *retrieved_sample = getDataSampleById(map, idcode_to_retrieve);

	        xil_printf("Current Samples for ID Code %u are %d\n", to_intconvertor(sampleIdcode), current_samples);
	        printDataSample(retrieved_sample);

	        temp_df = temp_df->dnext;
	    }

	xil_printf("Teleiwse h while\n");

//	xil_printf("Printing all map contents\n");
//
//	    printDataMap(map);

//	    destroyDataMap(map);

	    clear_TSB(ind);

	    // PROSOXH TA SAMPLES EINAI PER ID CODE

	    xil_printf("all done!");

	xil_printf("buffer map freed\n");

	xSemaphoreGive(mutex_on_TSB);

	xil_printf("dataframe created\n");
	vTaskDelete(NULL);
}

//REALLOC
void* freertos_realloc(void *ptr, size_t size) {
	unsigned char *new_ptr = pvPortMalloc(size);

	if (new_ptr != NULL) {
		size_t old_size = xPortGetFreeHeapSize() + size;
		if (ptr != NULL) {
			copy_cbyc(new_ptr, (unsigned char *) ptr, old_size); // Copy old data to new buffer
			vPortFree(ptr); // Free the old memory block
		}
	}

	return new_ptr;
}

int create_dataframe(int index) {
	int total_frame_size = 0;
	unsigned char temp[3];
	struct data_frame *temp_df;
	unsigned int fsize;
	uint16_t chk;
	unsigned char DATASYNC[3];
	unsigned char *dataframe;
	int PDC_IDCODE = 0;

	DATASYNC[0] = 0xaa;
	DATASYNC[1] = 0x01;
	DATASYNC[2] = '\0';

	xSemaphoreTake(mutex_on_TSB, portMAX_DELAY);

	xil_printf("mphke create dataframe");

	temp_df = TSB[index].first_data_frame;

	while (temp_df != NULL) {
		fsize = to_intconvertor(temp_df->idcode);
		fsize = to_intconvertor(temp_df->framesize);
		total_frame_size = total_frame_size + fsize;
		total_frame_size -= 16; // skip SYNC + FRAMESIZE + idcode + soc + fracsec + checksum
		temp_df = temp_df->dnext;
	}

	total_frame_size =
			total_frame_size
					+ 18/* SYNC + FRAMESIZE + idcode + soc + fracsec + checksum + outer stat */;

	dataframe = pvPortMalloc((total_frame_size + 1) * sizeof(char)); // Allocate memory for data frame
	if (!dataframe) {
		xil_printf("No enough memory for dataframe\n");
		xSemaphoreGive(mutex_on_TSB);
		return 0; // Return an error code or handle the error appropriately
	}
	dataframe[total_frame_size] = '\0';

	// Start the data frame creation
	int z = 0;
	byte_by_byte_copy(dataframe, DATASYNC, z, 2); // SYNC
	z += 2;

	memset(temp, '\0', 3);
	int_to_ascii_convertor(total_frame_size, temp);
	byte_by_byte_copy(dataframe, temp, z, 2); // FRAME SIZE
	z += 2;

	memset(temp, '\0', 3);
	int_to_ascii_convertor(PDC_IDCODE, temp);
	byte_by_byte_copy(dataframe, temp, z, 2); // PDC ID
	z += 2;

	byte_by_byte_copy(dataframe, (unsigned char *) TSB[index].soc, z, 4); //SOC
	z += 4;
	byte_by_byte_copy(dataframe, (unsigned char *) TSB[index].fracsec, z, 4); //FRACSEC
	z += 4;

	unsigned char stat[2]; //Outer Stat
	stat[0] = 0x00;
	stat[1] = 0x00;
	byte_by_byte_copy(dataframe, stat, z, 2); //outer stat
	z += 2;

	temp_df = TSB[index].first_data_frame;
	while (temp_df != NULL) { // 1

		int j = 0;
		while (j < temp_df->num_pmu) { // 2

			if (temp_df->dpmu[j]->stat[1] == 0x0f) {

				// Copy STAT
				byte_by_byte_copy(dataframe, temp_df->dpmu[j]->stat, z, 2);
				z += 2;
				j++;
				continue;
			}

			//Copy STAT
			byte_by_byte_copy(dataframe, temp_df->dpmu[j]->stat, z, 2);
			z += 2;

			int i = 0;

			//Copy Phasors
			if (temp_df->dpmu[j]->phnmr != 0) {

				if (temp_df->dpmu[j]->fmt->phasor == '1') {

					while (i < temp_df->dpmu[j]->phnmr) {

						byte_by_byte_copy(dataframe,
								temp_df->dpmu[j]->phasors[i], z, 8); // Phasors
						z += 8;
						i++;
					}

				} else {

					while (i < temp_df->dpmu[j]->phnmr) {

						byte_by_byte_copy(dataframe,
								temp_df->dpmu[j]->phasors[i], z, 4); // Phasors
						z += 4;
						i++;
					}
				}
			}

			//Copy FREQ
			if (temp_df->dpmu[j]->fmt->freq == '1') {

				byte_by_byte_copy(dataframe, temp_df->dpmu[j]->freq, z, 4); // FREQ
				z += 4;
				byte_by_byte_copy(dataframe, temp_df->dpmu[j]->dfreq, z, 4); // FREQ
				z += 4;

			} else {

				byte_by_byte_copy(dataframe, temp_df->dpmu[j]->freq, z, 2); // FREQ
				z += 2;
				byte_by_byte_copy(dataframe, temp_df->dpmu[j]->dfreq, z, 2); // FREQ
				z += 2;
			}

			// Copy Analogs
			if (temp_df->dpmu[j]->annmr != 0) {

				if (temp_df->dpmu[j]->fmt->analog == '1') {

					for (i = 0; i < temp_df->dpmu[j]->annmr; i++) {

						byte_by_byte_copy(dataframe,
								temp_df->dpmu[j]->analog[i], z, 4); // ANALOGS
						z += 4;
					}

				} else {

					for (i = 0; i < temp_df->dpmu[j]->annmr; i++) {

						byte_by_byte_copy(dataframe,
								temp_df->dpmu[j]->analog[i], z, 2); // ANALOGS
						z += 2;
					}
				}
			}

			i = 0;

			//Copy DIGITAL
			if (temp_df->dpmu[j]->dgnmr != 0) {

				while (i < temp_df->dpmu[j]->dgnmr) {

					byte_by_byte_copy(dataframe, temp_df->dpmu[j]->digital[i],
							z, 2); // DIGITAL
					z += 2;
					i++;
				}
			}
			j++;
		} // 2 while

		temp_df = temp_df->dnext;
	} // 1 while

	// Attach a checksum
	chk = compute_CRC(dataframe, z);
	dataframe[z++] = (chk >> 8) & ~(~0 << 8); /* CHKSUM high byte; */
	dataframe[z++] = (chk) & ~(~0 << 8); /* CHKSUM low byte;  */

	xSemaphoreGive(mutex_on_TSB);

	return z;
}

//////OLA APO KATW 4/11/24 META apo final tou code me to sample map buffer
char *strdup_free(const char *s) {
	size_t len = strlen(s) + 1;
	char *copy = pvPortMalloc(len);
	if (copy) {
		memcpy(copy, s, len);
	}
	return copy;
}

void printRawBytes(const unsigned char *str) {
	while (*str) {
		xil_printf("%02x ", *str++);
	}
	xil_printf("\n");
}

struct DataSample *createSampleToAddToMap(struct data_frame *df,
		struct DataSample *sample) {
	int match = 0, i, j = 0;
	int stat_status, config_change = 0;
	unsigned int t_id, num_pmu, phnmr, annmr, dgnmr;
	float fp_r, fp_i, fp_real, fp_imaginary, fp_analogs;
	long int f_r, f_i, f_analogs, f_freq, f_dfreq, l_soc, l_fracsec;
	short int s_analogs, s_freq, s_dfreq;
	float fp_freq, fp_dfreq;

	unsigned char *framesize, *idcode, *soc, *fracsec, *phasors, *freq, *d,
			*timequality, *dfreq;
	unsigned char *fp_left, *fp_right;
	unsigned char *f_left, *f_right;

	framesize = pvPortMalloc(3 * sizeof(unsigned char));
	idcode = pvPortMalloc(3 * sizeof(unsigned char));
	soc = pvPortMalloc(5 * sizeof(unsigned char));
	fracsec = pvPortMalloc(5 * sizeof(unsigned char));
	phasors = pvPortMalloc(9 * sizeof(unsigned char));
	freq = pvPortMalloc(5 * sizeof(unsigned char));
	dfreq = pvPortMalloc(5 * sizeof(unsigned char));

	memset(framesize, '\0', 3);
	memset(idcode, '\0', 3);
	memset(soc, '\0', 5);
	memset(fracsec, '\0', 5);
	memset(phasors, '\0', 9);
	memset(freq, '\0', 5);
	memset(dfreq, '\0', 5);

	fp_left = pvPortMalloc(5);
	fp_right = pvPortMalloc(5);
	f_left = pvPortMalloc(3);
	f_right = pvPortMalloc(3);

	memset(fp_left, '\0', 5);
	memset(fp_right, '\0', 5);
	memset(f_left, '\0', 3);
	memset(f_right, '\0', 3);

	copy_cbyc(idcode, df->idcode, 2);
	idcode[2] = '\0';
	t_id = to_intconvertor(idcode);
	// xil_printf("ID Code %d\n", t_id);

	copy_cbyc(soc, df->soc, 4);
	soc[4] = '\0';
	l_soc = to_long_int_convertor(soc);

	// LEAVE THIS OUT FOR NOW AND THE ROUNDING AS WELL, TBD IF WE NEED IT
	//  copy_cbyc(timequality, temp_df->fracsec, 1);
	//  timequality[1] = '\0';
	//  l_timequality = to_intconvertor(timequality);
	//  xil_printf("Time quality is %d", l_timequality);

	copy_cbyc(fracsec, df->fracsec, 3);
	fracsec[3] = '\0';
	l_fracsec = to_long_int_convertor(fracsec);

	sample->soc[0] = l_soc;
	sample->fracsec[0] = l_fracsec;
	sample->num_samples = 1;
	sample->idcode = strdup_free(idcode);

	// xil_printf("Number of PMUs is %d\n", df->num_pmu);

	while (j < df->num_pmu) {

		// Extract PHNMR, DGNMR, ANNMR
		phnmr = df->dpmu[j]->phnmr;
		annmr = df->dpmu[j]->annmr;
		dgnmr = df->dpmu[j]->dgnmr;

		// xil_printf("PMU %d -> phnmr: %d, annmr: %d, dgnmr: %d\n", j, phnmr, annmr, dgnmr);

		if (phnmr != 0) {

			if (df->dpmu[j]->fmt->phasor == 1) { // Floating

				for (i = 0; i < phnmr; i++) {

					memset(fp_left, '\0', 5);
					memset(fp_right, '\0', 5);
					copy_cbyc(fp_left, df->dpmu[j]->phasors[i], 4);
					fp_left[4] = '\0';

					copy_cbyc(fp_right, df->dpmu[j]->phasors[i] + 4, 4);
					fp_right[4] = '\0';

					fp_r = decode_ieee_single(fp_left);
					fp_i = decode_ieee_single(fp_right);

					if (df->dpmu[j]->fmt->polar == 1) { // POLAR

						/*fp_real = fp_r*cos(f_i);
						 fp_imaginary = fp_r*sin(f_i);

						 Commented by Gopal on 8th Aug 2012.
						 We want to store polar values in the table */

						fp_real = fp_r;
						fp_imaginary = fp_i;
						xil_printf("FP Real is %f and FP Imaginary is %f\n",
								fp_real, fp_imaginary);

						sample->phasors[i].real = fp_real;
						sample->phasors[i].imaginary = fp_imaginary;
					} else // RECTANGULAR
					{
//						fp_real = hypotf(fp_r, fp_i);
//						fp_imaginary = atan2f(fp_i, fp_r);
//						xil_printf("FP Real is %f and FP Imaginary is %f\n",
//								fp_real, fp_imaginary);
//
//						sample->phasors[i].real = fp_real;
//						sample->phasors[i].imaginary = fp_imaginary;
					}
				}
			}
			// JUST SKIP THIS, SINCE I DELETED PHUNIT, BUT COPY THIS TO VITIS
			//  else { // Fixed point

			//     for(i = 0;i < phnmr; i++){

			//         memset(f_left,'\0',3);
			//         memset(f_right,'\0',3);
			//         copy_cbyc (f_left,d,2);
			//         f_left[2] = '\0';
			//         d += 2;

			//         copy_cbyc(f_right,d,2);
			//         f_right[2] = '\0';
			//         d += 2;

			//         f_r = to_intconvertor(f_left);
			//         f_i = to_intconvertor(f_right);

			//         if(temp_df->dpmu[j]->fmt->polar == 1) { // POLAR

			//             fp_real = *temp_df->dpmu[j]->phunit[i] *f_r;
			//             fp_imaginary = f_i*1e-4; // Angle is in 10^4 radians
			//         }
			//         else // RACTANGULAR
			//         {
			//             fp_r = *temp_df->dpmu[j]->phunit[i] *f_r;
			//             fp_i = *temp_df->dpmu[j]->phunit[i] *f_i;

			//             fp_real = hypotf(fp_r,fp_i);
			//             fp_imaginary = atan2f(fp_i, fp_r);
			//         }
			//     }
			// }
		} // Phasors Insertion ends

		// Freq
		if (df->dpmu[j]->fmt->freq == 1) { // FLOATING

			memset(freq, '\0', 5);
			copy_cbyc(freq, df->dpmu[j]->freq, 4);
			freq[4] = '\0';

			memset(dfreq, '\0', 5);
			copy_cbyc(dfreq, df->dpmu[j]->dfreq, 4);
			dfreq[4] = '\0';

			fp_freq = decode_ieee_single(freq);
			fp_dfreq = decode_ieee_single(dfreq);
			// xil_printf("Freq is %f and dfreq is %f\n", fp_freq, fp_dfreq);

			sample->freq[0] = fp_freq; // THESE WILL BE CHANGED TO fp_freq AND fp_dfreq when i change the DataSample properties to match the types properly
			sample->dfreq[0] = fp_dfreq;
		} else { // FIXED

			memset(freq, '\0', 5);
			copy_cbyc(freq, df->dpmu[j]->freq, 2);
			freq[2] = '\0';

			memset(dfreq, '\0', 5);
			copy_cbyc(dfreq, df->dpmu[j]->dfreq, 2);
			dfreq[2] = '\0';
			s_freq = to_intconvertor(freq);
			s_dfreq = to_intconvertor(dfreq);

			fp_freq = s_freq * 1e-3; // freq is in mHz deviation from nominal
									 // DISABLED FOR THIS ONE, WILL SEE IF WE NEED IT
			//  if (temp_df->dpmu[j]->fnom == 0)
			//  		fp_freq = 60 + fp_freq;
			//  else
			fp_freq = 50 + fp_freq;
			fp_dfreq = s_dfreq * 1e-2; // dfreq is 100 times hz/sec
			// xil_printf("Freq is %f and dfreq is %f\n", fp_freq, fp_dfreq);

			sample->freq[0] = fp_freq; // THESE WILL BE CHANGED TO fp_freq AND fp_dfreq when i change the DataSample properties to match the types properly
			sample->dfreq[0] = fp_dfreq;
		}

		j++;
	}

	xil_printf(
			"createSampleToAddToMap finished! Data sample is ready to be added to map\n");
	return sample;
}

// Hash function to determine map index
unsigned int hash(const unsigned char *idcode) {
	unsigned int hash = 0;
	while (*idcode) {
		hash = (hash << 5) + *idcode++;
	}
	return hash % MAP_SIZE;
}

// Function to create a new data map
struct DataMap *createDataMap() {
	struct DataMap *map = pvPortMalloc(sizeof(struct DataMap));
	if (!map) {
		fprintf(stderr, "Memory allocation failed\n");
		exit(1);
	}
	memset(map->entries, 0, sizeof(map->entries));
	return map;
}

struct DataSample *createDataSample(unsigned char *idcode, int phnmr) {
	struct DataSample *sample = pvPortMalloc(sizeof(struct DataSample));
	if (!sample) {
		fprintf(stderr, "Memory allocation failed\n");
		exit(1);
	}

	sample->idcode = strdup_free(idcode);
	sample->num_samples = 1;

	// Allocate memory for the number of samples
	sample->phnmr = pvPortMalloc(1 * sizeof(int));
	sample->soc = pvPortMalloc(1 * sizeof(long int));
	sample->fracsec = pvPortMalloc(1 * sizeof(long int));
	sample->freq = pvPortMalloc(1 * sizeof(float));
	sample->dfreq = pvPortMalloc(1 * sizeof(float));

	// Initialize arrays
	for (int i = 0; i < 1; i++) {
		sample->phnmr[i] = phnmr; // Assuming all samples start with the same number of phasors
	}

	// Allocate memory for the phasors array
	sample->phasors = pvPortMalloc(phnmr * 1 * sizeof(struct Phasor));

	if (!sample->idcode || !sample->phnmr || !sample->soc || !sample->fracsec
			|| !sample->freq || !sample->dfreq || !sample->phasors) {
		fprintf(stderr, "Memory allocation failed\n");
		exit(1);
	}

	xil_printf("createDataSample finished! Initial data sample created\n");
	return sample;
}

// Function to print the contents of the map
void printDataMap(struct DataMap *map) {
	if (map != NULL) {
		for (int i = 0; i < MAP_SIZE; i++) {
			struct MapEntry *entry = map->entries[i];
			while (entry != NULL) {
				xil_printf("ID Code: %u\n",
						to_intconvertor(entry->sample->idcode));
				int phasor_offset = 0; // Initialize offset for each entry's samples
				for (int k = 0; k < entry->sample->num_samples; k++) {
					xil_printf("Sample %d:\n", k + 1);
					xil_printf("  SOC: %ld\n", entry->sample->soc[k]);
					xil_printf("  Fracsec: %ld\n", entry->sample->fracsec[k]);
					xil_printf("  Frequency: %f\n", entry->sample->freq[k]);
					xil_printf("  DFrequency: %f\n", entry->sample->dfreq[k]);
					xil_printf("  Phasors:\n");
					for (int j = 0; j < entry->sample->phnmr[k]; j++) // Use entry->sample->phnmr[k] for each sample
							{
						xil_printf("    Phasor %d - Real: %f, Imaginary: %f\n",
								j + 1,
								entry->sample->phasors[phasor_offset + j].real,
								entry->sample->phasors[phasor_offset + j].imaginary);
					}
					phasor_offset += entry->sample->phnmr[k]; // Update the offset for the next sample within the same entry
				}
				xil_printf("\n");
				entry = entry->next;
			}
		}
	} else {
		xil_printf("No data found in the map.\n");
	}
}

// Function to destroy a data sample
void freeDataSample(struct DataSample *sample) {
	if (sample) {
		vPortFree(sample->idcode);
		vPortFree(sample->soc);
		vPortFree(sample->fracsec);
		vPortFree(sample->freq);
		vPortFree(sample->dfreq);
		vPortFree(sample->phasors);
		vPortFree(sample);
	}
}

// Function to insert a data sample into the data map
void insertDataSample(struct DataMap *map, struct DataSample *sample) {
	if (map != NULL && sample != NULL && sample->idcode != NULL) {
		unsigned char hash = *(sample->idcode);
		struct MapEntry *entry = (struct MapEntry *) pvPortMalloc(
				sizeof(struct MapEntry));
		if (entry != NULL) {
			entry->sample = sample;
			entry->next = map->entries[hash];
			map->entries[hash] = entry;
		}
	}
}

// Function to destroy a data map
void destroyDataMap(struct DataMap *map) {
	if (map) {
		for (int i = 0; i < 256; i++) {
			struct MapEntry *entry = map->entries[i];
			while (entry) {
				struct MapEntry *temp = entry;
				entry = entry->next;
				freeDataSample(temp->sample);
				vPortFree(temp);
			}
		}
		vPortFree(map);
	}
}

int addDataSample(struct DataMap *map, struct DataSample *sample) {
	unsigned int index = hash(sample->idcode);
	struct MapEntry *entry = map->entries[index];
	struct MapEntry *prev = NULL;

	while (entry != NULL) {
		if (strcmp((char *) entry->sample->idcode, (char *) sample->idcode)
				== 0) {
			break;
		}
		prev = entry;
		entry = entry->next;
	}

	if (entry == NULL) {
		entry = pvPortMalloc(sizeof(struct MapEntry));
		if (!entry) {
			fprintf(stderr, "Memory allocation failed\n");
			exit(1);
		}
		entry->sample = sample;
		entry->next = NULL;
		if (prev == NULL) {
			map->entries[index] = entry;
		} else {
			prev->next = entry;
		}
		return 1; // Only one sample in the new entry
	} else {
		int current = entry->sample->num_samples;
		int total_phasors = 0;

		// Calculate the total number of phasors before adding the new sample
		for (int i = 0; i < current; i++) {
			total_phasors += entry->sample->phnmr[i];
		}

		// Reallocate arrays to accommodate the new sample
		entry->sample->soc = realloc(entry->sample->soc,
				(current + 1) * sizeof(long int));
		entry->sample->fracsec = realloc(entry->sample->fracsec,
				(current + 1) * sizeof(long int));
		entry->sample->freq = realloc(entry->sample->freq,
				(current + 1) * sizeof(float));
		entry->sample->dfreq = realloc(entry->sample->dfreq,
				(current + 1) * sizeof(float));
		entry->sample->phnmr = realloc(entry->sample->phnmr,
				(current + 1) * sizeof(int));
		entry->sample->phasors = realloc(entry->sample->phasors,
				(total_phasors + sample->phnmr[0]) * sizeof(struct Phasor));

		if (!entry->sample->soc || !entry->sample->fracsec
				|| !entry->sample->freq || !entry->sample->dfreq
				|| !entry->sample->phnmr || !entry->sample->phasors) {
			fprintf(stderr, "Memory reallocation failed\n");
			exit(1);
		}

		// Update the sample arrays with the new sample's data
		entry->sample->soc[current] = sample->soc[0];
		entry->sample->fracsec[current] = sample->fracsec[0];
		entry->sample->freq[current] = sample->freq[0];
		entry->sample->dfreq[current] = sample->dfreq[0];
		entry->sample->phnmr[current] = sample->phnmr[0];

		// Add the new sample's phasors to the correct position in the phasors array
		for (int k = 0; k < sample->phnmr[0]; k++) {
			entry->sample->phasors[total_phasors + k].real =
					sample->phasors[k].real;
			entry->sample->phasors[total_phasors + k].imaginary =
					sample->phasors[k].imaginary;
			xil_printf(
					"Phasors in assignment for index %d are real %f and imaginary %f\n",
					(total_phasors + k),
					entry->sample->phasors[total_phasors + k].real,
					entry->sample->phasors[total_phasors + k].imaginary);
		}

		entry->sample->num_samples++;

		// Free the temporary sample as its data has been added
		freeDataSample(sample);

		// Return the updated number of samples
		return entry->sample->num_samples;
	}
}

struct DataSample *getDataSampleById(struct DataMap *map,
		const unsigned char *idcode) {
	unsigned int index = hash(idcode);
	struct MapEntry *entry = map->entries[index];

	xil_printf("Looking for idcode: %d at index: %u\n",
			to_intconvertor((unsigned char *) idcode), index);

	while (entry != NULL) {
		xil_printf("Checking entry with idcode: %d\n",
				to_intconvertor(entry->sample->idcode));
		if (strcmp((char *) entry->sample->idcode, (char *) idcode) == 0) {
			return entry->sample;
		}
		entry = entry->next;
	}
	xil_printf("Entry for id code %d was not found in the map\n",
			to_intconvertor((unsigned char *) idcode));
	return NULL; // Entry not found
}

void printDataSample(struct DataSample *sample) {
	if (sample != NULL) {
		xil_printf("ID Code: %u\n", to_intconvertor(sample->idcode));
		int phasor_offset = 0; // To keep track of the start index of phasors for each sample
		for (int k = 0; k < sample->num_samples; k++) {
			xil_printf("Sample %d:\n", k + 1);
			xil_printf("  SOC: %ld\n", sample->soc[k]);
			xil_printf("  Fracsec: %ld\n", sample->fracsec[k]);
			xil_printf("  Frequency: %f\n", sample->freq[k]);
			xil_printf("  DFrequency: %f\n", sample->dfreq[k]);
			xil_printf("  Phasors:\n");
			for (int j = 0; j < sample->phnmr[k]; j++) // Use sample->phnmr[k] for each sample
					{
				xil_printf("    Phasor %d - Real: %f, Imaginary: %f\n", j + 1,
						sample->phasors[phasor_offset + j].real,
						sample->phasors[phasor_offset + j].imaginary);
			}
			phasor_offset += sample->phnmr[k]; // Update the offset for the next sample
		}
		xil_printf("\n");
	} else {
		xil_printf("No data found for the given ID code.\n");
	}
}

float decode_ieee_single(const void *v) {

	const unsigned char *data = v;
	int s, e;
	unsigned long src;
	long f;
	float value;

	src = ((unsigned long) data[0] << 24) | ((unsigned long) data[1] << 16)
			| ((unsigned long) data[2] << 8) | ((unsigned long) data[3]);

	s = (src & 0x80000000UL) >> 31;
	e = (src & 0x7F800000UL) >> 23;
	f = (src & 0x007FFFFFUL);

	if (e == 255 && f != 0) {
		/* NaN (Not a Number) */
		value = DBL_MAX;
	} else if (e == 255 && f == 0 && s == 1) {
		/* Negative infinity */
		value = -DBL_MAX;
	} else if (e == 255 && f == 0 && s == 0) {
		/* Positive infinity */
		value = DBL_MAX;
	} else if (e > 0 && e < 255) {
		/* Normal number */
		f += 0x00800000UL;
		if (s)
			f = -f;
		value = ldexp(f, e - 150);
	} else if (e == 0 && f != 0) {
		/* Denormal number */
		if (s)
			f = -f;
		value = ldexp(f, -149);
	} else if (e == 0 && f == 0 && s == 1) {
		/* Negative zero */
		value = 0;
	} else if (e == 0 && f == 0 && s == 0) {
		/* Positive zero */
		value = 0;
	} else {
		/* Never happens */
		xil_printf("s = %d, e = %d, f = %lu\n", s, e, f);
		assert(!"Woops, unhandled case in decode_ieee_single()");
	}

	return value;
}
