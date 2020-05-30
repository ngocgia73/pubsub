/*
 * author: giann <daniel.nguyen0105@gmail.com>
 * desc: demo publish / subcribe mechanism
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include <common.h>
#include <cmd_handler.h>
#include <ipc_sk_srv.h>

#include <moda.h>
#include <modb.h>
#include <modc.h>

pthread_t moda_thread;
pthread_t modb_thread;
pthread_t modc_thread;

int main(int argc, int *argv[])
{
    int ret;
    // register server
    ret = cmd_sk_srv_register(IPC_PORT, "DEMO_PUBSUB");

    if(ret == 0)
    {
        moda_ipc_register();
        modb_ipc_register();
        modc_ipc_register();
    }
    else
    {
        printf("bug\n");
        return -99;
    }

    if(pthread_create(&moda_thread, NULL, moda_hdl, NULL) < 0)
    {
        printf("unable to create moda_thread\n");
        return -1;
    }

    if(pthread_create(&modb_thread, NULL, modb_hdl, NULL) < 0)
    {
        printf("unable to create moda_thread\n");
        return -2;
    }

    if(pthread_create(&modc_thread, NULL, modc_hdl, NULL) < 0)
    {
        printf("unable to create moda_thread\n");
        return -1;
    }


    while(1)
    {
        printf("main ...\n");
        sleep(60);
        
        break;
    }
    cmd_sk_srv_unregister(IPC_PORT); 
    return 0;
}
