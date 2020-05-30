/*
 * author: giann <daniel.nguyen0105@gmail.com>
 * desc: lightweight publish / subcribe mechanism
 */

#include <stdio.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <string.h>
#include <ipc_sk_srv.h>
#include <list.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#include <common.h>

#define SRV_NAME_SZ     32
#define MODULE_NAME_SZ  32
#define WAIT_QUEUE_MAX  100
static const char *ipc_sk_srv_ver = "ipc_sk_srv: v00.00.01";
static pthread_t ipc_sk_msg_thread;

pthread_cond_t pubsub_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t pubsub_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct _topic_t
{
    struct list_head list;
    struct list_head queue_list;
    struct list_head sub_list;
    pthread_mutex_t topic_lock;
    char name[32];
}TOPIC_T;

typedef struct _data_t
{
    struct list_head list;
    char value[32];
} DATA_T;

typedef struct _subcriber_t
{
    struct list_head list;
    char subcriber[32];
} SUBCRIBER_T;

typedef struct ipc_sk_srv_t
{
    bool run_flg;
    int srv_sockfd;
    uint32_t cmd_num;
    uint32_t mod_num;
    uint32_t port;
    pthread_t ipc_sk_thread;
    pthread_t pubsub_thread;
    char srv_name[SRV_NAME_SZ];
    struct list_head list;
    struct list_head cmd_list;
    struct list_head mod_list;
    struct list_head topic_list;
    pthread_mutex_t srv_lock;
    sem_t  sem;
} IPC_SK_SRV_T;

typedef struct server_set_t 
{
    uint32_t srv_num;
    struct list_head gsrv_list;
    pthread_mutex_t gsrv_lock;
} SERVER_SET_T;

typedef struct sk_srv_cmd_t
{
    struct list_head list;
    IPC_SK_MSG_T *msg;
} SK_SRV_CMD_T;

typedef struct client_handle_T 
{
    IPC_SK_SRV_T *srv;
    int client_sockfd;
} CLIENT_HANDLE_T;

typedef struct ipc_sk_module_t
{
    char module_name[MODULE_NAME_SZ];
    struct list_head list;
} IPC_SK_MODULE_T;

static SERVER_SET_T gall_srv;

static IPC_SK_SRV_T *find_sk_srv_by_port(uint32_t port)
{
    IPC_SK_SRV_T *pos;
    pthread_mutex_lock(&gall_srv.gsrv_lock);

    // travel the list
    list_for_each_entry(pos, &gall_srv.gsrv_list, list)
    {
        // compare port
        if(pos->port == port)
        {
            pthread_mutex_unlock(&gall_srv.gsrv_lock);
            return pos;
        }
    }
    pthread_mutex_unlock(&gall_srv.gsrv_lock);
    return NULL;
}

static SK_SRV_CMD_T *alloc_sk_srv_cmd(void)
{
    SK_SRV_CMD_T *cmd = (SK_SRV_CMD_T *)malloc(sizeof(SK_SRV_CMD_T));
    if(cmd)
    {
        cmd->msg = (IPC_SK_MSG_T *)malloc(sizeof(IPC_SK_MSG_T));
        if(cmd->msg)
        {
            return cmd;
        }
        else
        {
            printf("unable to allocate mem for IPC_SK_MSG_T\n");
            free(cmd);
            cmd = NULL;
        }
    }
    else
    {
        printf("unable to allocate mem for SK_SRV_CMD_T\n");
        cmd = NULL;
    }
    return cmd;
}

static void free_sk_srv_cmd(SK_SRV_CMD_T *cmd)
{
    if(cmd)
    {
        if(cmd->msg)
        {
            free(cmd->msg);
            cmd->msg = NULL;
        }
        free(cmd);
        cmd = NULL;
    }
}

static void remove_all_subcribe(TOPIC_T *tobj)
{
    SUBCRIBER_T *spos, *spos_next = NULL;
    if(!tobj)
    {
        printf("invalid parm\n");
        return;
    }
    pthread_mutex_lock(&tobj->topic_lock);
    list_for_each_entry_safe(spos, spos_next, &tobj->sub_list, list)
    {
        // remove from topic_list
        list_del(&spos->list);
        // delete subcriber obj
        free(spos);
    }
    pthread_mutex_unlock(&tobj->topic_lock);
}

static void delete_topic_obj(TOPIC_T *tobj)
{
    if(!tobj)
    {
        return;
    }

    pthread_mutex_destroy(&tobj->topic_lock);
    list_del_init(&tobj->list);
    free(tobj);
}

static TOPIC_T *create_topic_obj(const char *topic_name, IPC_SK_SRV_T *srv)
{
    TOPIC_T *tobj = (TOPIC_T*)malloc(sizeof(TOPIC_T));
    if(!tobj)
    {
        printf("unable to malloc for pub\n");
        return NULL;
    }
    strncpy(tobj->name, topic_name, sizeof(tobj->name) -1);
    pthread_mutex_init(&tobj->topic_lock, NULL);
    list_add_tail(&tobj->list, &srv->topic_list);
    INIT_LIST_HEAD(&tobj->queue_list);
    INIT_LIST_HEAD(&tobj->sub_list);
    return tobj;
}


static void remove_all_topic(IPC_SK_SRV_T *srv)
{
    TOPIC_T *tpos, *tpos_next = NULL;
    if(!srv)
    {
        printf("invalid parm\n");
        return;
    }
    
    pthread_mutex_lock(&srv->srv_lock);
    list_for_each_entry_safe(tpos, tpos_next, &srv->topic_list, list)
    {
        // remove all subcriber of tobj
        remove_all_subcribe(tpos);
        // delete tobj
        delete_topic_obj(tpos);
    }
    pthread_mutex_unlock(&srv->srv_lock);
}
static void remove_all_sk_srv_cmd(IPC_SK_SRV_T *srv)
{
    SK_SRV_CMD_T *pos, *pos_next;

    if(!srv)
    {
        printf("invalid input\n");
        return;
    }

    pthread_mutex_lock(&srv->srv_lock);
    // travel the list
    list_for_each_entry_safe(pos, pos_next, &srv->cmd_list, list)
    {
        list_del(&pos->list);
        free_sk_srv_cmd(pos);
        srv->cmd_num --;
    }

    pthread_mutex_unlock(&srv->srv_lock);
}

static int create_socket(void)
{
    int sockfd = -1;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd == -1)
    {
        printf("failed to create tcp socket\n");
        return sockfd;
    }

    // set reuse flag and close-on-exec flag
    int sinsz = 1;

    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &sinsz, sizeof(int)) != 0)
    {
        printf("setsockopt failed %s\n", strerror(errno));
        goto __fail;
    }

    if(fcntl(sockfd, F_SETFD, FD_CLOEXEC) == -1)
    {
        printf("fcntl failed %s\n", strerror(errno));
        goto __fail;
    }

    return sockfd;
__fail:
    close(sockfd);
    sockfd = -1;
    return sockfd;
}

static int bind_with_port(int sockfd, uint32_t port)
{
    struct sockaddr_in server_address;
    memset(&server_address, 0x00, sizeof(struct sockaddr_in));

    // prepare for bind
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = inet_addr("127.0.0.1");

    // do bind
    if(bind(sockfd, (struct sockaddr *)&server_address, sizeof(struct sockaddr)) == -1)
    {
        printf("bind: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static int create_wait_queue(int sockfd, int backlog)
{
    // listen
    if(listen(sockfd, backlog) == -1)
    {
        printf("listen: %s\n",strerror(errno));
        return -1;
    }
    printf("waiting for connect ...\n");
    return 0;
}

static int do_connect(int sockfd, uint32_t port)
{
    struct sockaddr_in server_address;
    int sinsz = sizeof(struct sockaddr_in);

    // prepare for connect
    memset(&server_address, 0x00, sizeof(struct sockaddr_in));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = inet_addr("127.0.0.1");

    int ret = connect(sockfd, (struct sockaddr *)&server_address, (socklen_t)sinsz);
    if(ret)
    {
        printf("connect: %s\n",strerror(errno));
    }
    return ret;
}

// for socket client
static inline int sk_send_cmd(int sockfd, const char *cmd, uint32_t cmd_len)
{
    return send(sockfd, cmd, cmd_len, 0);
}

// for socket server
inline int sk_send_response(const char *res, int len, int sockfd)
{
    return send(sockfd, res, len, 0);
}

// for socket client
static int sk_get_res(int sockfd, char *res, uint32_t res_len, long int *tv_out)
{
    struct timeval *tv = NULL;

    if(!res)
    {
        printf("invalid input: res is null\n");
        return -1;
    }

    if(tv_out && *tv_out > 0)
    {
        tv = (struct timeval *)malloc(sizeof(struct timeval));
        if(tv)
        {
            tv->tv_sec = (*tv_out) / 1000;
            tv->tv_usec = ((*tv_out) % 1000) * 1000;
        }
    }
    else
    {
        // block
        tv = NULL;
    }

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sockfd, &read_fds);

    int ret = select(sockfd + 1, &read_fds, NULL, NULL, tv);
    int recv_len = 0;

    switch(ret)
    {
        case 0:
            // timeout
            printf("select : timeout\n");
            break;
        case 1:
            // get result success
            if(FD_ISSET(sockfd, &read_fds))
            {
                if(tv)
                {
                    *tv_out = tv->tv_sec * 1000;
                    *tv_out += tv->tv_usec / 1000;
                }
                recv_len = recv(sockfd, res, res_len, 0);
            }
            break;
        default:
            printf("select error\n");
            break;
    }
    if(tv)
    {
        free(tv);
        tv = NULL;
    }
    return recv_len;
}

static char *parse_cmd(char *recv_buf, int *offset_topic, int *offset_val)
{
    char *cmd = NULL;
    char *topic_name = NULL;

    if(!recv_buf)
    {
        printf("parse_cmd: recv_buf is NULL\n");
        return NULL;
    }

    cmd = strtok(recv_buf, DELIMIT);
    if(cmd)
    {
        *offset_topic = strlen(cmd) + strlen(DELIMIT);
    }
    
    // PARSE TOPIC NAME
    topic_name = strtok(recv_buf + *offset_topic, DELIMIT);
    if(topic_name)
    {
        *offset_val = *offset_topic + strlen(topic_name) + strlen(DELIMIT);
    }

    return cmd;
}



static int  handle_publish(const char *topic_name, const char *value, IPC_SK_SRV_T *srv)
{
    TOPIC_T *pos = NULL;
    TOPIC_T *match_topic = NULL;
    bool isNeedClean = false;

    if(!topic_name || !srv || !value)
    {
        printf("invalid input\n");
        return -1;
    }

        
    if(list_empty(&srv->topic_list))
    {
        match_topic = create_topic_obj(topic_name, srv);
        isNeedClean = true;
    }
    else
    {
        pthread_mutex_lock(&srv->srv_lock);
        list_for_each_entry(pos, &srv->topic_list, list)
        {
            if(pos && (strcmp(pos->name, topic_name) == 0))
            {
                match_topic = pos;
                break;
            }
        }
        pthread_mutex_unlock(&srv->srv_lock);
        // not match any topic obj
        if(!match_topic)
        {
            match_topic =  create_topic_obj(topic_name, srv);
            isNeedClean = true;
        }
    }

    
    if(match_topic)
    {
        // add value to topic's queue
        DATA_T *data_obj = (DATA_T *)malloc(sizeof(DATA_T));
        if(!data_obj)
        {
            // need delete topic obj
            if(isNeedClean)
            {
                delete_topic_obj(match_topic);
            }
            return -2;
        }

        strncpy(data_obj->value, value, sizeof(data_obj->value) -1);
        list_add_tail(&data_obj->list, &match_topic->queue_list);
    }
    else
    {
        printf("unable to create topic obj\n");
        return -3;
    }
    
    return 0; 
}

static int handle_subcribe(const char *topic_name, const char *subcriber, IPC_SK_SRV_T *srv)
{
    TOPIC_T *pos = NULL;
    TOPIC_T *match_topic = NULL;
    bool isNeedClean = false;

    if(!topic_name || !subcriber || !srv)
    {
        printf("invalid parm\n");
        return -1;
    }
    
    if(list_empty(&srv->topic_list))
    {
        match_topic = create_topic_obj(topic_name, srv);
        isNeedClean = true;
    }
    else
    {
        pthread_mutex_lock(&srv->srv_lock);
        list_for_each_entry(pos, &srv->topic_list, list)
        {
            if(pos && (strcmp(pos->name, topic_name) == 0))
            {
                isNeedClean = false;
                match_topic = pos;
                break;
            }
        }
        pthread_mutex_unlock(&srv->srv_lock);
        // not match any topic obj
        if(!match_topic)
        {
            match_topic =  create_topic_obj(topic_name, srv);
            isNeedClean = true;
        }
    }

    if(match_topic)
    {
        SUBCRIBER_T *sub_obj = (SUBCRIBER_T *)malloc(sizeof(SUBCRIBER_T));
        if(!sub_obj)
        {
            if(isNeedClean)
            {
                delete_topic_obj(match_topic);
            }
            return -2;
        }

        strncpy(sub_obj->subcriber, subcriber, sizeof(sub_obj->subcriber) - 1);
        //printf("****%s\n", sub_obj->subcriber);
        list_add_tail(&sub_obj->list, &match_topic->sub_list);
    }
    else
    {
        printf("unable to create_topic_obj\n");
        return -3;
    }
    return 0;
}

static int handle_unsubcribe(const char *topic_name, const char *subcriber, IPC_SK_SRV_T *srv)
{
    TOPIC_T *tpos, *tpos_next = NULL;
    SUBCRIBER_T *spos, *spos_next = NULL;
    if(!topic_name || !subcriber || !srv)
    {
        printf("invalid parm\n");
        return -1;
    }

    pthread_mutex_lock(&srv->srv_lock);
    list_for_each_entry_safe(tpos, tpos_next, &srv->topic_list, list)
    {
        if(strcmp(tpos->name, topic_name) == 0)
        {
            // match topic
            pthread_mutex_lock(&tpos->topic_lock);
            list_for_each_entry_safe(spos, spos_next, &tpos->sub_list, list)
            {
                if(strcmp(spos->subcriber, subcriber) == 0)
                {
                    printf("DBG: ubsubcribe topic name: %s from subcriber: %s\n",tpos->name, spos->subcriber);
                    list_del_init(&spos->list);
                    free(spos);

                    pthread_mutex_unlock(&tpos->topic_lock);
                    pthread_mutex_unlock(&srv->srv_lock);
                    return 0;
                }
            }
            pthread_mutex_unlock(&tpos->topic_lock);
        }
    }
    pthread_mutex_unlock(&srv->srv_lock);

   return -1; 
}

// for socket server
static int do_recv_and_parse(IPC_SK_SRV_T *srv, int client_sockfd)
{
    char recv_buf[2048] = {[0 ... (2047)] = 0};
    int recv_sz = 0;
    recv_sz = recv(client_sockfd, recv_buf, sizeof(recv_buf) - 1, 0);
    if(recv_sz <= 0)
    {
        printf("recv: %s\n", strerror(errno));
        return -1;
    }

    //printf("recv_buf: %s\n",recv_buf);

    // parse cmd
    char *cmd_type = NULL;
    int offset_topic = 0;
    int offset_val;
    int ret;
    cmd_type = parse_cmd(recv_buf, &offset_topic, &offset_val);
    if(!cmd_type)
    {
        printf("cmd_type is NULL\n");
        return -2;
    }

    // identify kind of command
    if(strcmp(cmd_type, "pub") == 0)
    {
        // pub action
        // identify topic name
        //printf("pub action\n");
        ret = handle_publish(recv_buf + offset_topic, recv_buf + offset_val,  srv);
        if(ret != 0)
        {
            printf("handle_publish failed\n");
            return -3;
        }
        pthread_cond_signal(&pubsub_cond);
        //printf("SIGNAL...\n");
    }
    else if(strcmp(cmd_type, "sub") == 0)
    {
        // sub action
        //printf("sub action\n");
        ret = handle_subcribe(recv_buf + offset_topic, recv_buf + offset_val, srv);
        if(ret != 0)
        {
            printf("handle_subcribe failed\n");
            return -4;
        }
    }
    else if(strcmp(cmd_type, "unsub") == 0)
    {
        // unsub action
        //printf("unsub action\n");
        ret = handle_unsubcribe(recv_buf + offset_topic, recv_buf + offset_val, srv);
        if(ret != 0)
        {
            printf("handle_unsubcribe failed\n");
            return -5;
        }
    }
    else
    {
        //printf("normal action\n");
        // travel the list . if match cmd then invoke the callback
        SK_SRV_CMD_T *pos, *match_msg = NULL;
        pthread_mutex_lock(&srv->srv_lock);
        list_for_each_entry(pos, &srv->cmd_list, list)
        {
            if(!strcmp(pos->msg->cmd, cmd_type))
            {
                match_msg = pos;
                break;
            }
        }
        pthread_mutex_unlock(&srv->srv_lock);

        // if match cmd then call cb
        if(match_msg)
        {
            offset_val = offset_topic;
            match_msg->msg->msg_cb(recv_buf + offset_val, recv_sz - offset_val, client_sockfd);
        }
        else
        {
            cmd_sk_show_working_module(client_sockfd);
        }
    }
    return 0;
}

static void *ipc_sk_msg_hdl(void *parm)
{
    int ret;
    CLIENT_HANDLE_T *cli = (CLIENT_HANDLE_T *)parm;
    if(!cli)
    {
        printf("invalid input: cli is null\n");
        return NULL;
    }
    sem_post(&cli->srv->sem);
    // detach current thread
    pthread_detach(pthread_self());
    ret = do_recv_and_parse(cli->srv, cli->client_sockfd);
    if(ret != 0)
    {
        printf("do_recv_and_parse failed %d\n",ret);
    }
    close(cli->client_sockfd);
    free(cli);
    cli = NULL;
    return NULL;
}

static void *ipc_sk_hdl(void *parm)
{
    int sinsz;
    IPC_SK_SRV_T *srv = (IPC_SK_SRV_T *)parm;
    if(!srv)
    {
        printf("invalid input\n");
        return NULL;
    }
    long int tid = syscall(SYS_gettid); 
    printf("lwp: %ld\n",tid);

    int srv_sockfd = srv->srv_sockfd;
    int client_sockfd = -1;
    struct sockaddr_in client_address;
    pthread_attr_t attr;

    memset(&client_address, 0x00, sizeof(struct sockaddr_in));

    while(srv->run_flg)
    {
        // do accept
        sinsz = sizeof(struct sockaddr);
        client_sockfd = accept(srv_sockfd, (struct sockaddr *)&client_address, (socklen_t *)&sinsz);
        CLIENT_HANDLE_T *cli = (CLIENT_HANDLE_T *)malloc(sizeof(CLIENT_HANDLE_T));
        if(!cli)
        {
            printf("unablee to allocate mem for CLIENT_HANDLE_T\n");
            continue;
        }
        cli->srv = srv;
        cli->client_sockfd = client_sockfd;

        // fork new thread to handle msg
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_attr_setstacksize(&attr, 100*1024);
        if(pthread_create(&ipc_sk_msg_thread, &attr , ipc_sk_msg_hdl, (void *)cli) != 0)
        {
            printf("failed to create ipc_sk_msg_thread\n");
            continue;
        }
        pthread_attr_destroy(&attr);
        // just make sure child thread run first
        sem_wait(&srv->sem);
    }

    return NULL;
}


void *pubsub_hdl(void *parm)
{
    TOPIC_T *pos, *pos_next = NULL;
    DATA_T *dpos, *dpos_next = NULL;
    SUBCRIBER_T *spos, *spos_next =NULL;

    char cmd[128]= {0};

    IPC_SK_SRV_T *srv = (IPC_SK_SRV_T *)parm;
    if(!srv)
    {
        printf("invalid parm\n");
        return NULL;
    }
    printf("pubsub entered...\n");

    while(srv->run_flg)
    {
        // handle pubsub
        if(list_empty(&srv->topic_list))
        {
            // sleep waiting
            pthread_mutex_lock(&pubsub_lock);
            //printf("pubsub hdl waiting1...\n");
            pthread_cond_wait(&pubsub_cond, &pubsub_lock);
            pthread_mutex_unlock(&pubsub_lock);
        }

        // travel the topic list
        pthread_mutex_lock(&srv->srv_lock);
        list_for_each_entry_safe(pos, pos_next, &srv->topic_list, list)
        {
            //printf("DBG: topic name -> %s\n",pos->name);
            // travel the data list
            pthread_mutex_lock(&pos->topic_lock);
            list_for_each_entry_safe(dpos, dpos_next, &pos->queue_list, list)
            {
                list_for_each_entry_safe(spos, spos_next, &pos->sub_list, list)
                {
                    // data : dpos->value;
                    // subcriber : spos->subcriber;
                    // construct cmd
                    //printf(":::%s\n", spos->subcriber);
                    sprintf(cmd, "%s%s#%s",spos->subcriber, pos->name, dpos->value);
                    //printf("SEND SEND SEND TO: %s\n",cmd);
                    cmd_sk_send(IPC_PORT, cmd, strlen(cmd) + 1, NULL, 0, NULL);
                }
                list_del(&dpos->list);
                // free data_obj
                free(dpos);
            }
            pthread_mutex_unlock(&pos->topic_lock);
        }
        pthread_mutex_unlock(&srv->srv_lock);

        pthread_mutex_lock(&pubsub_lock);              
        //printf("pubsub hdl waiting2...\n");
        pthread_cond_wait(&pubsub_cond, &pubsub_lock); 
        pthread_mutex_unlock(&pubsub_lock);             
    }

    return NULL;
}

int cmd_sk_srv_register(uint32_t port, const char *name)
{
    int srv_sockfd = 0;
    int ret = -1;

    // step 1: create socket
    srv_sockfd = create_socket();

    // step 2: bind sock with port

    ret = bind_with_port(srv_sockfd, port);
    if(ret < 0)
    {
        close(srv_sockfd);
        return ret;
    }

    // step 3: listen
    create_wait_queue(srv_sockfd, WAIT_QUEUE_MAX);

    // step 4: init srv ipc socket node
    IPC_SK_SRV_T *srv = (IPC_SK_SRV_T *)malloc(sizeof(IPC_SK_SRV_T));
    if(!srv)
    {
        printf("unable to allocate mem for IPC_SK_SRV_T\n");
        close(srv_sockfd);
        return -2;
    }

    // fill data for IPC_SK_SRV_T
    strncpy(srv->srv_name, name, SRV_NAME_SZ -1);
    srv->srv_sockfd = srv_sockfd;
    srv->port = port;
    srv->run_flg = true;
    srv->cmd_num = 0;
    srv->mod_num = 0;
    INIT_LIST_HEAD(&srv->cmd_list);
    INIT_LIST_HEAD(&srv->mod_list);
    INIT_LIST_HEAD(&srv->topic_list);
    // init srv's mutex
    pthread_mutex_init(&srv->srv_lock, NULL);
    // init srv's sem
    sem_init(&srv->sem, 0, 0);


    // step 5 : create thread handle message
    if(pthread_create(&srv->ipc_sk_thread, NULL, ipc_sk_hdl, (void *)srv) != 0)
    {
        printf("failed to create ipc_sk_thread");
        free(srv);
        srv = NULL;
        close(srv_sockfd);
        return -3;
    }
    // create thread to handle pub/sub
    if(pthread_create(&srv->pubsub_thread, NULL, pubsub_hdl, (void *)srv) != 0)
    {
        free(srv);
        srv = NULL;
        close(srv_sockfd);
        return -4;
    }

    // just in case we need more than one ipc socket server 
    if(!gall_srv.srv_num)
    {
        INIT_LIST_HEAD(&gall_srv.gsrv_list);
        pthread_mutex_init(&gall_srv.gsrv_lock, NULL);
    }

    pthread_mutex_lock(&gall_srv.gsrv_lock);
    list_add_tail(&srv->list, &gall_srv.gsrv_list);
    gall_srv.srv_num++;
    pthread_mutex_unlock(&gall_srv.gsrv_lock);

    return 0;

}

int cmd_sk_srv_unregister(uint32_t port)
{
    IPC_SK_SRV_T *pos, *pos_next = NULL;
    if(!gall_srv.srv_num)
    {
        printf("there is no server was register\n");
        return -1;
    }

    pthread_mutex_lock(&gall_srv.gsrv_lock);
    // travel the list
    list_for_each_entry_safe(pos, pos_next, &gall_srv.gsrv_list, list)
    {
        if(pos->port  == port)
        {
            // stop ipc_sk_thread
            pos->run_flg = false;
            pthread_cancel(pos->ipc_sk_thread);
            pthread_join(pos->ipc_sk_thread, NULL);
            pthread_cancel(pos->pubsub_thread);
            pthread_join(pos->pubsub_thread, NULL);

            // close activing srv socket
            close(pos->srv_sockfd);
            pos->srv_sockfd = -1;
            // remove all registered message
            remove_all_sk_srv_cmd(pos);
            remove_all_topic(pos);
            
            // remove from global list
            list_del(&pos->list);
            pthread_mutex_destroy(&pos->srv_lock);
            sem_destroy(&pos->sem);      
            free(pos);
            gall_srv.srv_num--;
            break;
        }
    }
    pthread_mutex_unlock(&gall_srv.gsrv_lock);

    // remove global resource
    if(!gall_srv.srv_num)
    {
        pthread_mutex_destroy(&gall_srv.gsrv_lock);
    }

    return 0;
}

// for socket client
int cmd_sk_send(uint32_t port, const char *cmd, uint32_t cmd_len,
                char *res, uint32_t res_len, long int *tv_out)
{
    int ret = -1;
    int sockfd = -1;

    if(!cmd)
    {
        printf("invalid input\n");
        return -3;
    }

    // step 1: create socket
    sockfd = create_socket();
    if(sockfd == -1)
    {
        printf("failed to create socket %d\n",ret);
        return -1;
    }

    // step 2: do connect
    ret = do_connect(sockfd, port);
    if(ret)
    {
        printf("do_connect fialed %d\n",ret);
        close(sockfd);
        return -2;
    }

    // step 3: send cmd
    ret = sk_send_cmd(sockfd, cmd, cmd_len);
    if(ret != cmd_len)
    {
        printf("size need to send: %d \t sent size: %d\n",cmd_len, ret);
    }

    // step 4: get response
    if((res_len > 0) && res)
    {
        ret = sk_get_res(sockfd, res, res_len, tv_out);
    }
    close(sockfd);

    return 0;
}

int cmd_sk_register_msg_handle(uint32_t port, IPC_SK_MSG_T *msg)
{
    IPC_SK_SRV_T *srv = NULL;
    if(!msg)
    {
        printf("invalid input: msg is null\n");
        return -1;
    }
    srv = find_sk_srv_by_port(port);
    if(!srv)
    {
        printf("find_sk_srv_by_port failed\n");
        return -2;
    }

    SK_SRV_CMD_T *cmd = alloc_sk_srv_cmd();
    if(!cmd)
    {
        printf("unable to alloc mem for cmd\n");
        return -3;
    }
    
    memcpy(cmd->msg, msg, sizeof(IPC_SK_MSG_T));


    // insert new cmd to srv's cmd list
    pthread_mutex_lock(&srv->srv_lock);
    list_add_tail(&cmd->list, &srv->cmd_list);
    srv->cmd_num ++;
    pthread_mutex_unlock(&srv->srv_lock);

    return 0;
}

int cmd_sk_unregister_msg_handle(uint32_t port, const char *cmd)
{
    IPC_SK_SRV_T *srv = NULL;
    SK_SRV_CMD_T *pos, *pos_next = NULL;
    if(!cmd)
    {
        printf("invalid input: cmd is null\n");
        return -1;
    }

    srv = find_sk_srv_by_port(port);
    if(!srv)
    {
        printf("find_sk_srv_by_port failed\n");
        return -2;
    }

    // travel through the list
    pthread_mutex_lock(&srv->srv_lock);
    list_for_each_entry_safe(pos, pos_next, &srv->cmd_list, list)
    {
        if(!strcmp(pos->msg->cmd, cmd))
        {
            srv->cmd_num--;
            if(srv->cmd_num == 0)
            {
                list_del(&pos->list);
            }
            else
            {
                list_del_init(&pos->list);
            }
            free_sk_srv_cmd(pos);
            break;
        }
    }
    pthread_mutex_unlock(&srv->srv_lock);
    return 0;
}

void cmd_sk_register_module(uint32_t port, const char *module_name)
{
    if(!module_name)
    {
        printf("invalid input: module_name is null\n");
        return;
    }
    IPC_SK_SRV_T *srv = find_sk_srv_by_port(port);
    if(!srv)
    {
        printf("find_sk_srv_by_port failed\n");
        return;
    }

    IPC_SK_MODULE_T *mod = (IPC_SK_MODULE_T *)malloc(sizeof(IPC_SK_MODULE_T));
    if(!mod)
    {
        printf("unable to alloc mem for mod\n");
        return;
    }
    memcpy(mod->module_name, module_name, MODULE_NAME_SZ -1);
    pthread_mutex_lock(&srv->srv_lock);
    list_add_tail(&mod->list, &srv->mod_list);
    srv->mod_num++;
    pthread_mutex_unlock(&srv->srv_lock);
}



void cmd_sk_unregister_module(uint32_t port, const char *module_name)
{
    if(!module_name)
    {
        printf("invalid input: module_name is null\n");
        return;
    }

    IPC_SK_SRV_T *srv = find_sk_srv_by_port(port);
    if(!srv)
    {
        printf("find_sk_srv_by_port failed\n");
        return;
    }

    IPC_SK_MODULE_T *pos, *pos_next = NULL;


    pthread_mutex_lock(&srv->srv_lock);
    // travel through the list
    list_for_each_entry_safe(pos, pos_next, &srv->mod_list, list)
    {
        if(!strcmp(pos->module_name, module_name))
        {
            srv->mod_num--;
            if(srv->mod_num == 0)
            {
                list_del(&pos->list);
            }
            else
            {
                list_del_init(&pos->list);
            }
            free(pos);
            pos = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&srv->srv_lock);
}


void cmd_sk_show_working_module(int sockfd)
{
    IPC_SK_MODULE_T *mod, *mod_next = NULL;
    if(!gall_srv.srv_num)
    {
        printf("no module exist\n");
        return;
    }

    char module_name[MODULE_NAME_SZ] = {[0 ... (MODULE_NAME_SZ -1)] = 0};
    IPC_SK_SRV_T *pos, *pos_next = NULL;

    pthread_mutex_lock(&gall_srv.gsrv_lock);
    list_for_each_entry_safe(pos, pos_next, &gall_srv.gsrv_list, list)
    {
        pthread_mutex_lock(&pos->srv_lock);
        list_for_each_entry_safe(mod, mod_next, &pos->mod_list, list)
        {
            snprintf(module_name, MODULE_NAME_SZ - 1, "%s\t[%s]\n", module_name, mod->module_name);
        }
        pthread_mutex_unlock(&pos->srv_lock);
    
    }
    pthread_mutex_unlock(&gall_srv.gsrv_lock);
    
    sk_send_response(module_name, strlen(module_name), sockfd);
}

const char *ipcsrv_get_version(void)
{
    return ipc_sk_srv_ver;
}

