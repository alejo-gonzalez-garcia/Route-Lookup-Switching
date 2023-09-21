#include "utils.h"
#include "io.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <stdarg.h>

#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>

#define LARGE_TABLE_SIZE 0x1000000
#define SMALL_TABLE_SIZE 0x100
#define MISS 0 //0 represents the non-existance of an output interface. 


int main(int argc, char* argv[]){

    int totalAccessedTables = 0, prefixL, outputIntf, n_bits_to_fill, Table2_n_blocks = 0, auxiliar_prefix, reading = 0, iteration = 0;
	uint32_t table1_index = 0;
    uint32_t prefix = 0;
    uint32_t IPAddress;
	uint16_t table2_index = 0, table_entry = 0;
    double processedIPs = 0, accessedTables = 0, averageAccessedTables = 0;
    struct timespec initTime, finalTime;
    double searchingTime = 0, totalSearchingTime = 0, averageSearchingTime = 0;

    // Initializing the first table to size LARGE_TABLE_SIZE cleaning the memory space
    uint16_t * Table1 = (uint16_t *)calloc(LARGE_TABLE_SIZE, sizeof(uint16_t));

    /* Initialize the second table to size 0, we will reallocate it
       and make it larger only if needed */
    uint16_t * Table2 = (uint16_t *)malloc(0);

    if(Table1 == NULL){
        printf("Table1 was not correctly initialized!\n");
        return -3; 
    }

    if(Table2 == NULL){
        printf("Table2 was not correctly initialized!\n");
        return -4; 
    }

    if (argc != 3){ //Checking the correctness of the number of arguments
        printf("There must be 2 arguments! (FIB and InputPacketFile) but there are %d \n",argc-1);
        return -1;
    }

    //Now we initialize both files and check its correctness: 
    if(initializeIO(argv[1], argv[2]) != 0){
		printf("There has been an error reading the input files\n");
		return -2;
	}

//////////////////////////////////////////////////////////////////////////////////////////////////
                         /*Starting to read the Routing table*/
//////////////////////////////////////////////////////////////////////////////////////////////////

	while(reading != -1){ //While the input file has not ended, continue reading. 
		reading =  readFIBLine(&prefix, &prefixL, &outputIntf);

        if(reading != 0){
            printf("Success while reading the FIB Line!\n");
            break;
        }

	    table1_index = prefix >> 8;

        /*First of all we are going to deal with prefixes lower than 24 bits.*/
        if(prefixL <=  24){
            table_entry = (uint16_t) outputIntf;
            n_bits_to_fill = pow(2, 24-prefixL);
            iteration = 0;
            while(iteration < n_bits_to_fill){
			    memcpy(&Table1[table1_index + iteration], &table_entry, sizeof(uint16_t));
                iteration++;
            }
        }
        else{ // If the prefix lenght is greater than 24 : 
            uint16_t decissive_bit = Table1[table1_index] & (uint16_t) pow(2,15);
            if(decissive_bit != (uint16_t)0){
                /* We are looking for the first bit of the interface we were given
                on the first table, it is the decissive_bit to know whether 
                there is already an existent output interface to the second table.  
                */

                /* Now we get the interface of the second table, taking into account 
                 that the first bit was 1. */
   				table1_index = ((Table1[table1_index] ^ (uint16_t) pow(2,15)) - 1000) * SMALL_TABLE_SIZE;

				n_bits_to_fill = pow(2, 32-prefixL); /* Number of bits to fill*/ 
                table2_index = (uint16_t) (prefix & 0xFF); /*Getting the last 8 bits of the input prefix*/
				table_entry = (uint16_t)outputIntf; /* The number we are placing on each row of the table*/

                iteration = 0;
                while(iteration < n_bits_to_fill){
					memcpy(&Table2[(table1_index + table2_index) + iteration], &table_entry, sizeof(uint16_t));
                    iteration++;
                }

                /* We have filled the second table (that was already initialized) 
                with new received output interface by just updating the new addresses. . 
                */

            }else{ /*This is the case in which the second table for the specificed prefix
                    does not exist yet*/

                auxiliar_prefix = Table1[table1_index];

			    table_entry = Table2_n_blocks + 1000;
                uint16_t new_outputIntf = (table_entry | (uint16_t) pow(2,15));

                /* We are creating the new output interface taking into account that 
                the first bit has to be a one given that the desired interface must be
                in the small table. */

				table_entry = (uint16_t)new_outputIntf;
				memcpy(&Table1[table1_index], &table_entry, sizeof(uint16_t)); 
                /* Now we are filling the second table */
				table1_index = (SMALL_TABLE_SIZE * Table2_n_blocks);

                table2_index = (uint16_t) (prefix & 0xFF); /*Getting the last 8 bits of the input prefix*/
				n_bits_to_fill = pow(2, 32-prefixL);

                /*As we are now in the second table and it is the first time 
                that this prefix is longer than 24, we must allocate space for 
                it. */

                size_t size_table2 = (Table2_n_blocks + 1) * SMALL_TABLE_SIZE * sizeof(uint16_t);
				Table2 = realloc(Table2, size_table2);

                if(Table2 == NULL){
                   printf("Error reallocating the second table! \n");
                    return -5;
                }

                /*Now we are going to fill all the entries (256) of the just created table with the desired
                output interface provided in table 1*/
                iteration = 0;
                while(iteration < SMALL_TABLE_SIZE){
					memcpy(&Table2[table1_index+iteration], &auxiliar_prefix, sizeof(uint16_t));
                    iteration++;
                }

                /*Next step is to fill only the specified IPs with the new output interface provided. */
				table_entry = (uint16_t)outputIntf;
                iteration = 0;
                while(iteration < n_bits_to_fill){
					memcpy(&Table2[table1_index + table2_index + iteration], &table_entry, sizeof(uint16_t));
                    iteration++;
                }

            }
        
        }       

    }

//////////////////////////////////////////////////////////////////////////////////////////////////
                  /*Starting to assign an Output Interface to each Input IP*/
//////////////////////////////////////////////////////////////////////////////////////////////////


  reading = 0;
  
  /* After the route lookup tables have been filled, it´s time to 
  search on it the provide input addresses and compute the calculations. */
  while(reading == OK){
    reading = readInputPacketFileLine(&IPAddress);
    if(reading != 0){
        printf("Success while reading the Input Packet File Line!\n");
        break;
    }

    processedIPs++; // Increase in 1 the number of processed IPs. 
    accessedTables = 0; //Initialize the Number of Access tables for each IP. 
    table1_index = IPAddress >> 8;


    clock_gettime(0, &initTime); //Computing initial time 
    if(Table1[table1_index] == (uint16_t) 0){

        accessedTables++; // But we did access one table
        outputIntf = MISS; // We did not found any output interface.     


    }else if( (Table1[table1_index] & (uint16_t) pow(2,15)) != (uint16_t) 0) {
        /*If for this IP, there exists a second table with more than 24 bits, I go over it*/
        table2_index = (((Table1[table1_index] ^ (uint16_t) pow(2,15)) - 1000) * SMALL_TABLE_SIZE ) + (uint16_t)(IPAddress & 0xFF);

        /* The position of this IP will be the one of the second table plus the final 8 bits of my IP. */
        accessedTables =+ 2; /* Number of accessed tables increased by 2. */
        outputIntf = Table2[table2_index]; /* Selecting the output interface*/
    
    }else{
        /* If there does not exist an Ip of more than 24 bits in the first table, ie, 
        I have written a 0 on the first one, the next 15 bits are the output interface 
        directly. Remark that we have gonne from the largest to the smallest prefix. */
        accessedTables++;
        outputIntf = Table1[table1_index];
    }
		clock_gettime(0, &finalTime); //Computing final time

		printOutputLine(IPAddress, outputIntf, &initTime, &finalTime, &searchingTime, accessedTables);

		totalSearchingTime = totalSearchingTime + searchingTime; //Increasing total Time
        totalAccessedTables = totalAccessedTables + accessedTables; //Increasing Total Accesses

  }
    /*After finishing processing all the IPs, we compute the average and 
    call the printSummary function as requested.*/
	averageSearchingTime = totalSearchingTime/processedIPs;
	averageAccessedTables = totalAccessedTables/processedIPs;

	printSummary(processedIPs, averageAccessedTables, averageSearchingTime);


	free(Table1); free(Table2); freeIO();
    
    return 0;

}