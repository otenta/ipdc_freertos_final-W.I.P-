#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "parser.h"
#include "dallocate.h"
#include "global.h"
#include "xil_printf.h"
#include "FreeRTOS.h"
#include "task.h"

/* -------------------------------------------------------------*/
/* FUNCTION  free_cfgframe_object():                  	     	*/
/* It frees memory allocated to cfg objects. 			*/
/* ------------------------------------------------------------ */
void free_cfgframe_object(struct cfg_frame *cfg) {
    int j = 0;
    unsigned int phn, ann, dgn, num_pmu;
    struct dgnames *t_dgnames, *r_dgnames;

    num_pmu = to_intconvertor(cfg->num_pmu);

    while(j < num_pmu) {
    	vPortFree(cfg->pmu[j]->stn);
    	vPortFree(cfg->pmu[j]->idcode);
    	vPortFree(cfg->pmu[j]->data_format);
    	vPortFree(cfg->pmu[j]->fmt);

        phn = to_intconvertor(cfg->pmu[j]->phnmr);
        ann = to_intconvertor(cfg->pmu[j]->annmr);
        dgn = to_intconvertor(cfg->pmu[j]->dgnmr);

        if(phn != 0)
            free_2darray(cfg->pmu[j]->cnext->phnames, phn);
        if(ann != 0)
            free_2darray(cfg->pmu[j]->cnext->angnames, ann);

        if(dgn != 0) {
            t_dgnames = cfg->pmu[j]->cnext->first;

            while(t_dgnames != NULL) {
                r_dgnames = t_dgnames->dg_next;
                free_2darray(t_dgnames->dgn, 16);
                t_dgnames = r_dgnames;
            }
        }

        if(phn != 0)
            free_2darray(cfg->pmu[j]->phunit, phn);
        if(ann != 0)
            free_2darray(cfg->pmu[j]->anunit, ann);
        if(dgn != 0)
            free_2darray(cfg->pmu[j]->dgunit, dgn);

        vPortFree(cfg->pmu[j]->phnmr);
        vPortFree(cfg->pmu[j]->annmr);
        vPortFree(cfg->pmu[j]->dgnmr);
        vPortFree(cfg->pmu[j]->fnom);
        vPortFree(cfg->pmu[j]->cfg_cnt);

        j++;
    }

    vPortFree(cfg->framesize);
    vPortFree(cfg->idcode);
    vPortFree(cfg->soc);
    vPortFree(cfg->fracsec);
    vPortFree(cfg->time_base);
    vPortFree(cfg->data_rate);
    vPortFree(cfg->num_pmu);
    vPortFree(cfg);
}

/* -------------------------------------------------------------*/
/* FUNCTION  free_dataframe_object():                  	     	*/
/* It frees memory allocated to data objects. 			*/
/* -------------------------------------------------------------*/
void free_dataframe_object(struct data_frame *df) {
    int j = 0;

    while(j < df->num_pmu) {
        if(df->dpmu[j]->stat[1] == 0x0F) {
        	vPortFree(df->dpmu[j]->stat);
            j++;
            continue;
        }

        vPortFree(df->dpmu[j]->stat);
        vPortFree(df->dpmu[j]->freq);
        vPortFree(df->dpmu[j]->dfreq);

        if(df->dpmu[j]->phnmr != 0)
            free_2darray(df->dpmu[j]->phasors, df->dpmu[j]->phnmr);
        if(df->dpmu[j]->annmr != 0)
            free_2darray(df->dpmu[j]->analog, df->dpmu[j]->annmr);
        if(df->dpmu[j]->dgnmr != 0)
            free_2darray(df->dpmu[j]->digital, df->dpmu[j]->dgnmr);

        j++;
    }

    vPortFree(df->framesize);
    vPortFree(df->idcode);
    vPortFree(df->soc);
    vPortFree(df->fracsec);
    vPortFree(df);
}

/* -------------------------------------------------------------*/
/* FUNCTION  free_2darray:  					*/
/* It frees memory allocated to 2D Arrays. 			*/
/* -------------------------------------------------------------*/
void free_2darray(unsigned char** array, int n) {
    int i;
    for(i = 0; i < n; i++)
    	vPortFree(array[i]);
    vPortFree(array);
}
