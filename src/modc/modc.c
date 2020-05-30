#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <ipc_sk_srv.h>

#include <modc.h>
#include <common.h>

void *modc_hdl(void *parm)
{
    char cmd[64] = {0};
    pthread_detach(pthread_self());
    printf("entered modc_hdl\n");
    sleep(10);
    while(true)
    {
        //do publish
        printf("do pub: topic_name: %s -> value: %s\n","topica","demo_val");
        strncpy(cmd, "pub#topica#demo_val", sizeof(cmd) -1);
        cmd_sk_send(IPC_PORT, cmd, strlen("pub#topica#demo_val"), NULL, 0, NULL);
        sleep(5);
    }

    return NULL;
}
