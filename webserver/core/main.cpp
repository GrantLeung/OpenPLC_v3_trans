//-----------------------------------------------------------------------------
// Copyright 2018 Thiago Alves
// This file is part of the OpenPLC Software Stack.
//
// OpenPLC is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// OpenPLC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with OpenPLC.  If not, see <http://www.gnu.org/licenses/>.
//------
//
// This is the main file for the OpenPLC. It contains the initialization
// procedures for the hardware, network and the main loop
// Thiago Alves, Jun 2018
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>
#define HAVE_STRUCT_TIMESPEC
#include <pthread.h>
#pragma comment(lib, "x64/pthreadVC2.lib")
#include <time.h>
#include <signal.h>
#include <stdlib.h>
//#include <unistd.h>
//#include <sys/mman.h>
#include <Mmsystem.h>
#pragma comment(lib, "Winmm.lib")
#include <Windows.h>
#pragma comment(lib, "wsock32.lib")

#include "iec_types.h"
#include "ladder.h"

#define OPLC_CYCLE          50000000

extern int opterr;
//extern int common_ticktime__;
IEC_BOOL __DEBUG;

static int tick = 0;
pthread_mutex_t bufferLock; //mutex for the internal buffers
pthread_mutex_t logLock; //mutex for the internal log
uint8_t run_openplc = 1; //Variable to control OpenPLC Runtime execution
unsigned char log_buffer[1000000]; //A very large buffer to store all logs
int log_index = 0;
int log_counter = 0;

//-----------------------------------------------------------------------------
// Helper function - Makes the running thread sleep for the ammount of time
// in milliseconds
//-----------------------------------------------------------------------------
void sleep_until(struct timespec *ts, int delay)
{
    ts->tv_nsec += delay;
    if(ts->tv_nsec >= 1000*1000*1000)
    {
        ts->tv_nsec -= 1000*1000*1000;
        ts->tv_sec++;
    }
	nanosleep(ts, NULL);
	//revised 10-18 by lianggh
	//clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, ts,  NULL);
}

//-----------------------------------------------------------------------------
// Helper function - Makes the running thread sleep for the ammount of time
// in milliseconds
//-----------------------------------------------------------------------------
void sleepms(int milliseconds)
{
	struct timespec ts;
	ts.tv_sec = milliseconds / 1000;
	ts.tv_nsec = (milliseconds % 1000) * 1000000;
	nanosleep(&ts, NULL);
}

//-----------------------------------------------------------------------------
// Helper function - Logs messages and print them on the console
//-----------------------------------------------------------------------------
void log(unsigned char *logmsg)
{
	pthread_mutex_init(&logLock,NULL);
    pthread_mutex_lock(&logLock); //lock mutex
    printf("%s", logmsg);
    for (int i = 0; logmsg[i] != '\0'; i++)
    {
        log_buffer[log_index] = logmsg[i];
        log_index++;
        log_buffer[log_index] = '\0';
    }
    
    log_counter++;
    if (log_counter >= 1000)
    {
        /*Store current log on a file*/
        log_counter = 0;
        log_index = 0;
    }
    pthread_mutex_unlock(&logLock); //unlock mutex
}

//-----------------------------------------------------------------------------
// Interactive Server Thread. Creates the server to listen to commands on
// localhost
//-----------------------------------------------------------------------------
void *interactiveServerThread(void *arg)
{
    startInteractiveServer(43628);
}

//-----------------------------------------------------------------------------
// Verify if pin is present in one of the ignored vectors
//-----------------------------------------------------------------------------
bool pinNotPresent(int *ignored_vector, int vector_size, int pinNumber)
{
    for (int i = 0; i < vector_size; i++)
    {
        if (ignored_vector[i] == pinNumber)
            return false;
    }
    
    return true;
}

//-----------------------------------------------------------------------------
// Disable all outputs
//-----------------------------------------------------------------------------
void disableOutputs()
{
    //Disable digital outputs
    for (int i = 0; i < BUFFER_SIZE; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            if (bool_output[i][j] != NULL) *bool_output[i][j] = 0;
        }
    }
    
    //Disable byte outputs
    for (int i = 0; i < BUFFER_SIZE; i++)
    {
        if (byte_output[i] != NULL) *byte_output[i] = 0;
    }
    
    //Disable analog outputs
    for (int i = 0; i < BUFFER_SIZE; i++)
    {
        if (int_output[i] != NULL) *int_output[i] = 0;
    }
}

int main(int argc,char **argv)
{
    unsigned char log_msg[1000];

    sprintf(log_msg, "OpenPLC Runtime starting...\n");

    log(log_msg);

    //======================================================
    //                 PLC INITIALIZATION
    //======================================================
    time(&start_time);
    pthread_t interactive_thread;
    pthread_create(&interactive_thread, NULL, interactiveServerThread, NULL);
    config_init__();
	//printf("glueVars step\n");
    glueVars();
	
    //======================================================
    //               MUTEX INITIALIZATION
    //======================================================
	if (pthread_mutex_init(&bufferLock, NULL) != 0)
	{
		printf("Mutex init failed\n");
		exit(1);
	}

    //======================================================
    //              HARDWARE INITIALIZATION
    //======================================================
    initializeHardware();
    initializeMB();
    initCustomLayer();
    updateBuffersIn();
    updateCustomIn();
    updateBuffersOut();
    updateCustomOut();

    //======================================================
    //          PERSISTENT STORAGE INITIALIZATION
    //======================================================
    //readPersistentStorage();
    //pthread_t persistentThread;
    //pthread_create(&persistentThread, NULL, persistentStorage, NULL);

#ifdef __linux__
    //======================================================
    //              REAL-TIME INITIALIZATION
    //======================================================
    // Set our thread to real time priority
    struct sched_param sp;
    sp.sched_priority = 30;
    printf("Setting main thread priority to RT\n");
    if(pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp))
    {
        printf("WARNING: Failed to set main thread to real-time priority\n");
    }

    // Lock memory to ensure no swapping is done.
    printf("Locking main thread memory\n");
    if(mlockall(MCL_FUTURE|MCL_CURRENT))
    {
        printf("WARNING: Failed to lock memory\n");
    }
#endif

	//gets the starting point for the clock
	printf("Getting current time\n");
	struct timespec timer_start;
	timer_start.tv_sec = timeGetTime() / 1000;
	timer_start.tv_nsec = (timeGetTime() % 1000) * 1000000;
	//clock_gettime(CLOCK_MONOTONIC, &timer_start);

	//======================================================
	//                    MAIN LOOP
	//======================================================
	while(run_openplc)
	{
		//make sure the buffer pointers are correct and
		//attached to the user variables
		glueVars();
        
        querySlaveDevices(); //query data from all slave devices
		
		updateBuffersIn(); //read input image
		//pthread_mutex_init(&bufferLock, NULL);//lock mutex init
		pthread_mutex_lock(&bufferLock); //lock mutex lock
		updateCustomIn();
        updateBuffersIn_MB(); //update input image table with data from slave devices
		config_run__(tick++); // execute plc program logic
		updateCustomOut();
        updateBuffersOut_MB(); //update slave devices with data from the output image table
		pthread_mutex_unlock(&bufferLock); //unlock mutex

		updateBuffersOut(); //write output image
        
		updateTime();

		sleep_until(&timer_start, common_ticktime__);
	}
    
    //======================================================
	//             SHUTTING DOWN OPENPLC RUNTIME
	//======================================================
    pthread_join(interactive_thread, NULL);
    printf("Disabling outputs\n");
    disableOutputs();
    updateCustomOut();
    updateBuffersOut();
    printf("Shutting down OpenPLC Runtime...\n");
    exit(0);
}
