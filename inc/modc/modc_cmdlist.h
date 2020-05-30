#ifndef __MODC_CMD_LIST_H__
#define __MODC_CMD_LIST_H__

#ifdef __cplusplus
extern "C" {
#endif

static int modc_cmd1_cb(const char *status, unsigned int len, int sock);

static IPC_SK_MSG_T modc_ipc_cmdlist[] = {
    {"modc_cmd1",    modc_cmd1_cb},
};

#ifdef __cplusplus
}
#endif
#endif // __MODC_CMD_LIST_H__
