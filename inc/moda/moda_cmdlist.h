#ifndef __MODA_CMD_LIST_H__
#define __MODA_CMD_LIST_H__

static int modatopica_cmd_cb(const char *status, unsigned int len, int sock);

static IPC_SK_MSG_T moda_ipc_cmdlist[] = {
    {"modatopica",    modatopica_cmd_cb}
};

#endif // __MODA_CMD_LIST_H__
