#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include <ipc_sk_srv.h>
#include <common.h>
#include <cmd_handler.h>
#include <modc_cmdlist.h>

#define RES_LEN     1024


// define callback function
static int modc_cmd1_cb(const char *status, unsigned int len, int sock)
{
    char res[RES_LEN] = {[0 ... (RES_LEN -1)] = 0};

    // this cmd don't care about req from client
    if(!status)
    {
        printf("status is NULL\n");
        return -1;
    }

    printf("**DBG: modc received: %s\n",status);

    // execute command
#if 0 
    // no need to response
    memcpy(res, "res from cv cmd1", strlen("res from cv cmd1"));
    
    if(sock)
    {
        sk_send_response(res, strlen(res), sock);
    }
#endif
    return 0;
}

// for cumunicate to multimedia module 
void modc_ipc_register(void)
{
   int i;
   // step 1: register msg and their handler
   for(i = 0; i < sizeof(modc_ipc_cmdlist)/ sizeof(IPC_SK_MSG_T); i++)
   {
        cmd_sk_register_msg_handle(IPC_PORT, &modc_ipc_cmdlist[i]);
   }

   // step 2: register module to ipc socket server
   cmd_sk_register_module(IPC_PORT, "modc");

}

