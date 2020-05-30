#ifndef __MODB_CMD_LIST_H__
#define __MODB_CMD_LIST_H__

#ifdef __cplusplus
extern "C" {
#endif

static int modbtopica_cmd_cb(const char *status, unsigned int len, int sock);

static IPC_SK_MSG_T modb_ipc_cmdlist[] = {
    {"modbtopica",    modbtopica_cmd_cb},
};

#ifdef __cplusplus
}
#endif
#endif // __MODA_CMD_LIST_H__
