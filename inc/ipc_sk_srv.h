#ifndef __MY_IPC_SRV_H__
#define __MY_IPC_SRV_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define DELIMIT     "#"

typedef int (ipc_sk_msg_cb)(const char *arg, uint32_t arg_len, int sockfd);

typedef struct ipc_sk_msg_t
{
    char *cmd;              // point to command
    ipc_sk_msg_cb *msg_cb;  // cmd's dead with call back
} IPC_SK_MSG_T;

/*
 * desc : register a ipc socket server 
 * port[IN]: server's port number
 * name[IN]: server's name
 */ 
int cmd_sk_srv_register(uint32_t port, const char *name);

/*
 * desc: ipc sk srv unregister . remove srv from set
 * port[IN]: server's port number
 * return : 0 if success otherwise return -1
 */
int cmd_sk_srv_unregister(uint32_t port);

int cmd_sk_send(uint32_t port, const char *cmd, uint32_t cmd_len,
                char *res, uint32_t res_len, long int *tv_out);

int cmd_sk_register_msg_handle(uint32_t port, IPC_SK_MSG_T *msg);

int cmd_sk_unregister_msg_handle(uint32_t port, const char *cmd);

void cmd_sk_register_module(uint32_t port, const char *module_name);

void cmd_sk_unregister_module(uint32_t port, const char *module_name);

int sk_send_response(const char *res, int len, int sockfd);

void cmd_sk_show_working_module(int sockfd);

const char *ipcsrv_get_version(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // __MY_IPC_SRV_H__
