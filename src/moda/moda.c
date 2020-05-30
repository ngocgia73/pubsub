#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <ipc_sk_srv.h>

#include <moda.h>
#include <common.h>



void *moda_hdl(void *parm)
{
    char cmd[64] = {0};
    pthread_detach(pthread_self());
    printf("entered moda_hdl\n");

    strncpy(cmd, "sub#topica#moda", sizeof(cmd) -1);
    // do subcrible
    cmd_sk_send(IPC_PORT, cmd, strlen("sub#topica#moda"), NULL, 0, NULL);

    while(true)
    {
        // do somthing here
        // this is moda area
        sleep(10);
    }
    return NULL;
}
