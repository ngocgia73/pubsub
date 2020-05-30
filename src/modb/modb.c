#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <ipc_sk_srv.h>

#include <modb.h>
#include <common.h>


void *modb_hdl(void *parm)
{
    char cmd[64] = {0};
    pthread_detach(pthread_self());
    printf("entered modb_hdl\n");
    
    // do subcrible
    strncpy(cmd, "sub#topica#modb", sizeof(cmd) -1);
    cmd_sk_send(IPC_PORT, cmd, strlen("sub#topica#modb"), NULL, 0, NULL);
    while(true)
    {
        sleep(30);
        // do unsubcrible
        strncpy(cmd, "unsub#topica#modb", sizeof(cmd) -1);
        cmd_sk_send(IPC_PORT, cmd, strlen("unsub#topica#modb"), NULL, 0, NULL);

send_to_modc:
        strncpy(cmd, "modc_cmd1#demo_val2", sizeof(cmd) -1);
        cmd_sk_send(IPC_PORT, cmd, strlen("moda_cmd1#demo_val2"), NULL, 0, NULL);
        sleep(4);
        goto send_to_modc;
    }
    return NULL;
}
