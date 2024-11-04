/*
 * dbparser.h
 *
 *  Created on: 25 ??? 2022
 *      Author: pithi
 */

#ifndef SRC_DBPARSER_H_
#define SRC_DBPARSER_H_
int dataparser(unsigned char data[]) {

	struct cfg_frame *temp_cfg;
	int match = 0,i,j = 0;
	int stat_status,config_change = 0;
	unsigned int t_id,num_pmu,phnmr,annmr,dgnmr;
	float fp_r,fp_i,fp_real,fp_imaginary,fp_analogs;
	long int f_r,f_i,f_analogs,f_freq,f_dfreq,l_soc,l_fracsec;
	short int s_analogs, s_freq, s_dfreq;
	float fp_freq,fp_dfreq;

	unsigned char *sync,*framesize,*idcode,*soc,*fracsec,*timequality,*stat,*phasors,*analogs,*digital,*freq,*dfreq,*d;
	unsigned char *fp_left,*fp_right;
	unsigned char *f_left,*f_right;
	char *cmd;

	cmd = malloc(500);
	sync = malloc(3*sizeof(unsigned char));
	framesize = malloc(3*sizeof(unsigned char));
	idcode = malloc(3*sizeof(unsigned char));
	soc = malloc(5*sizeof(unsigned char));
	fracsec  = malloc(5*sizeof(unsigned char));
	timequality  = malloc(2*sizeof(unsigned char));
	stat = malloc(3*sizeof(unsigned char));
	phasors = malloc(9*sizeof(unsigned char));
	analogs = malloc(5*sizeof(unsigned char));
	digital = malloc(3*sizeof(unsigned char));
	freq = malloc(5*sizeof(unsigned char));
	dfreq = malloc(5*sizeof(unsigned char));

	memset(cmd,'\0',500);
	memset(sync,'\0',3);
	memset(framesize,'\0',3);
	memset(idcode,'\0',3);
	memset(soc,'\0',5);
	memset(fracsec,'\0',5);
	memset(timequality,'\0',2);
	memset(stat,'\0',3);
	memset(phasors,'\0',9);
	memset(analogs,'\0',5);
	memset(digital,'\0',3);
	memset(freq,'\0',5);
	memset(dfreq,'\0',5);

	fp_left = malloc(5);
	fp_right = malloc(5);
	f_left = malloc(3);
	f_right = malloc(3);

	memset(fp_left,'\0',5);
	memset(fp_right,'\0',5);
	memset(f_left,'\0',3);
	memset(f_right,'\0',3);

	d = data;

	//Skip SYN
	d += 2;

	//SEPARATE FRAMESIZE
	copy_cbyc (framesize,d,2);
	framesize[2] = '\0';
	d += 2;

	//SEPARATE IDCODE
	copy_cbyc (idcode,d,2);
	idcode[2] ='\0';
	d += 2;

	pthread_mutex_lock(&mutex_cfg);
	// Check for the IDCODE in Configuration Frame
	temp_cfg = cfgfirst;
	t_id = to_intconvertor(idcode);
	printf("ID Code %d\n",t_id);

	while(temp_cfg != NULL){

		if(t_id == temp_cfg->idcode) {

			match = 1;
			break;

		} else {

			temp_cfg = temp_cfg->cfgnext;

		}
	}
	pthread_mutex_unlock(&mutex_cfg);

	pthread_mutex_lock(&mutex_MYSQL_CONN_ON_DATA);

	if(match){	// idcode matches with cfg idcode

		printf("Inside DATAPARSER, data frame and matched with CFG.\n");

		// Allocate Memeory For Each PMU
		num_pmu = temp_cfg->num_pmu;

		//Copy SOC
		copy_cbyc (soc,d,4);
		soc[4] = '\0';
		l_soc = to_long_int_convertor(soc);
		d += 4;

		//Copy FRACSEC
        //First seprate the first Byte of Time Quality Flags
		copy_cbyc (timequality,d,1);
		timequality[1] = '\0';
		d += 1;

          //First seprate the next 3-Byte of Actual Fraction of Seconds
        copy_cbyc (fracsec,d,3);
        fracsec[3] = '\0';
        l_fracsec = to_long_int_convertor1(fracsec);
        l_fracsec = roundf((l_fracsec*1e6)/(temp_cfg->time_base));
        d += 3;

		// Separate the data for each PMU
		while(j < num_pmu) {

			copy_cbyc (stat,d,2);
			stat[2] = '\0';
			d += 2;

			// Check Stat Word for each data block
			stat_status = check_statword(stat);


			// If the data has not arrived
			if(stat_status == 16) {

				memset(stat,'\0',3);
				j++;
				continue;

			} else if((stat_status == 14)||(stat_status == 10)) {

				memset(stat,'\0',3);
				config_change = stat_status;
				j++;
				continue;
			}

			// Extract PHNMR, DGNMR, ANNMR
			phnmr = temp_cfg->pmu[j]->phnmr;
			annmr = temp_cfg->pmu[j]->annmr;
			dgnmr = temp_cfg->pmu[j]->dgnmr;

			pthread_mutex_lock(&mutex_phasor_buffer);

			//Phasors
			if(phnmr != 0) {

				if(temp_cfg->pmu[j]->fmt->phasor == 1) { // Floating

					for(i = 0;i<phnmr;i++){

						memset(fp_left,'\0',5);
						memset(fp_right,'\0',5);
						copy_cbyc (fp_left,d,4);
						fp_left[4] = '\0';
						d += 4;

						copy_cbyc(fp_right,d,4);
						fp_right[4] = '\0';
						d += 4;

						fp_r = decode_ieee_single(fp_left);
						fp_i = decode_ieee_single(fp_right);

                        if(temp_cfg->pmu[j]->fmt->polar == 1) { // POLAR

                            /*fp_real = fp_r*cos(f_i);
                              fp_imaginary = fp_r*sin(f_i);

                              Commented by Gopal on 8th Aug 2012.
                              We want to store polar values in the table */

                            fp_real = fp_r;
                            fp_imaginary = fp_i;
                        }
                        else // RECTANGULAR
                        {
                            fp_real = hypotf(fp_r,fp_i);
                            fp_imaginary = atan2f(fp_i, fp_r);
                        }

                        memset(cmd,'\0',500);
                        sprintf(cmd," %d,%d,%ld,%ld,\"%s\",%f,%f\n",temp_cfg->idcode,temp_cfg->pmu[j]->idcode,l_soc,l_fracsec,temp_cfg->pmu[j]->cnext->phnames[i],fp_real,fp_imaginary);

                        dataCollectInBuffer(cmd, phasorBuff, 1);
                    }
                }
                else { // Fixed point

                    for(i = 0;i < phnmr; i++){

                        memset(f_left,'\0',3);
                        memset(f_right,'\0',3);
                        copy_cbyc (f_left,d,2);
                        f_left[2] = '\0';
                        d += 2;

                        copy_cbyc(f_right,d,2);
                        f_right[2] = '\0';
                        d += 2;

                        f_r = to_intconvertor(f_left);
                        f_i = to_intconvertor(f_right);

                        if(temp_cfg->pmu[j]->fmt->polar == 1) { // POLAR

                            fp_real = *temp_cfg->pmu[j]->phunit[i] *f_r;
                            fp_imaginary = f_i*1e-4; // Angle is in 10^4 radians
                        }
                        else // RACTANGULAR
                        {
                            fp_r = *temp_cfg->pmu[j]->phunit[i] *f_r;
                            fp_i = *temp_cfg->pmu[j]->phunit[i] *f_i;

                            fp_real = hypotf(fp_r,fp_i);
                            fp_imaginary = atan2f(fp_i, fp_r);
                        }

                        memset(cmd,'\0',500);
                        sprintf(cmd," %d,%d,%ld,%ld,\"%s\",%f,%f\n",temp_cfg->idcode,temp_cfg->pmu[j]->idcode,l_soc,l_fracsec,temp_cfg->pmu[j]->cnext->phnames[i],fp_real,fp_imaginary);

                        dataCollectInBuffer(cmd, phasorBuff,1);
                    }
                }
			}// Phasors Insertion ends

			//Freq
			if(temp_cfg->pmu[j]->fmt->freq == 1) { // FLOATING

				memset(freq,'\0',5);
				copy_cbyc (freq,d,4);
				freq[4] = '\0';
				d += 4;

				memset(dfreq,'\0',5);
				copy_cbyc (dfreq,d,4);
				dfreq[4] = '\0';
				d += 4;

				fp_freq = decode_ieee_single(freq);
				fp_dfreq = decode_ieee_single(dfreq);

			} else { // FIXED

				memset(freq,'\0',5);
				copy_cbyc (freq,d,2);
				freq[2] = '\0';
				d += 2;

				memset(dfreq,'\0',5);
				copy_cbyc (dfreq,d,2);
				dfreq[2] = '\0';
				d += 2;
				s_freq = to_intconvertor(freq);
				s_dfreq = to_intconvertor(dfreq);

                		fp_freq = s_freq*1e-3; // freq is in mHz deviation from nominal
                		if (temp_cfg->pmu[j]->fnom == 0)
                    			fp_freq = 60 + fp_freq;
                		else
                    			fp_freq = 50 + fp_freq;
                		fp_dfreq = s_dfreq*1e-2; // dfreq is 100 times hz/sec
            		}

            		memset(cmd,'\0',500);
            		sprintf(cmd," %d,%d,%ld,%ld,%f,%f\n",temp_cfg->idcode,temp_cfg->pmu[j]->idcode,l_soc,l_fracsec,fp_freq,fp_dfreq);

			dataCollectInBuffer(cmd, frequencyBuff,2); // Freq Insert Ends

			//Analogs
			if(annmr != 0) {

				if(temp_cfg->pmu[j]->fmt->analog == 1) { // FLOATING

					for(i = 0; i < annmr; i++){

						memset(analogs,'\0',5);
						copy_cbyc(analogs,d,4);
						d += 4;
						analogs[4] = '\0';

						fp_analogs = decode_ieee_single(analogs);
                        			fp_analogs =  *temp_cfg->pmu[j]->anunit[i]*fp_analogs;;
                        			memset(cmd,'\0',500);

                        			sprintf(cmd," %d,%d,%ld,%ld,\"%s\",%f\n",temp_cfg->idcode,temp_cfg->pmu[j]->idcode,l_soc,l_fracsec,temp_cfg->pmu[j]->cnext->angnames[i],fp_analogs);

                        			dataCollectInBuffer(cmd, analogBuff,3);
                    			}

				} else { // FIXED

					for(i = 0; i < annmr; i++){

                        			memset(analogs,'\0',5);
                        			copy_cbyc (analogs,d,2);
                        			d += 2;

						analogs[2] = '\0';
						s_analogs = to_intconvertor(analogs);
						fp_analogs = *temp_cfg->pmu[j]->anunit[i]*s_analogs ;

                        			memset(cmd,'\0',500);
						sprintf(cmd," %d,%d,%ld,%ld,\"%s\",%f\n",temp_cfg->idcode,temp_cfg->pmu[j]->idcode,l_soc,l_fracsec,temp_cfg->pmu[j]->cnext->angnames[i],fp_analogs);

                        			dataCollectInBuffer(cmd, analogBuff,3);
					}
				}
			} // Insertion for Analog done here.

			// Digital
			if(dgnmr != 0) {

				unsigned int dgword;

				for(i = 0; i<dgnmr; i++) {

					memset(digital,'\0',3);
					copy_cbyc (digital,d,2);
					d += 2;
					digital[2] = '\0';
					dgword = to_intconvertor(digital);

                    			memset(cmd,'\0',500);
                    			sprintf(cmd," %d,%d,%ld,%ld,%u\n",temp_cfg->idcode,temp_cfg->pmu[j]->idcode,l_soc,l_fracsec,dgword);

                    			dataCollectInBuffer(cmd, digitalBuff,4);
				}
			} // Insertion for Digital done here.

               struct timeval tv;
               long local_soc, local_fsec,ms_diff,s_diff;

               /* Obtain the time of day, and convert it to a tm struct. */
               gettimeofday (&tv, NULL);

               local_soc = tv.tv_sec;
               local_fsec = tv.tv_usec;
               s_diff = (tv.tv_sec - l_soc);
               ms_diff = (tv.tv_usec - l_fracsec);

	       // Formula to calculate the exact delay in micro between data frame inside-time and
	       // system receive time at which that data frame received.
               ms_diff = ((s_diff == 0) ? ((ms_diff > 0) ? ms_diff : -1*ms_diff) : ((s_diff == 1) ? (1000000-l_fracsec+tv.tv_usec) : ((1000000*(s_diff-1))+(1000000-l_fracsec+tv.tv_usec))));

               memset(cmd,'\0',500);
               sprintf(cmd," %d,%d,%ld,%ld,%ld,%ld,%ld\n",temp_cfg->idcode,temp_cfg->pmu[j]->idcode,l_soc,l_fracsec,local_soc,local_fsec,ms_diff);
               dataCollectInBuffer(cmd, delayBuff,5);

		 	pthread_mutex_unlock(&mutex_phasor_buffer);
			j++;
		} //While ends

	} else {

		printf("NO CFG for data frames\n");
	}

	pthread_mutex_unlock(&mutex_MYSQL_CONN_ON_DATA);

	free(cmd);
	free(sync);
	free(framesize);
	free(idcode);
	free(soc);
	free(fracsec);
	free(timequality);
	free(stat);
	free(phasors);
	free(analogs);
	free(digital);
	free(freq);
	free(dfreq);

	free(fp_left);
	free(fp_right);
	free(f_left);
	free(f_right);

	if((config_change == 14) ||(config_change == 10))
		return config_change;
	else return stat_status;
}

float decode_ieee_single(const void *v) {

	const unsigned char *data = v;
	int s, e;
	unsigned long src;
	long f;
	float value;

	src = ((unsigned long)data[0] << 24) |
			((unsigned long)data[1] << 16) |
			((unsigned long)data[2] << 8) |
			((unsigned long)data[3]);

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
		if (s) f = -f;
		value = ldexp(f, e - 150);
	} else if (e == 0 && f != 0) {
		/* Denormal number */
		if (s) f = -f;
		value = ldexp(f, -149);
	} else if (e == 0 && f == 0 && s == 1) {
		/* Negative zero */
		value = 0;
	} else if (e == 0 && f == 0 && s == 0) {
		/* Positive zero */
		value = 0;
	} else {
		/* Never happens */
		printf("s = %d, e = %d, f = %lu\n", s, e, f);
		assert(!"Woops, unhandled case in decode_ieee_single()");
	}

	return value;
}


#endif /* SRC_DBPARSER_H_ */
