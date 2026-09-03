/**
 * @file local_ipc.c
 * @brief Local IPC client implementation
 */
#include "local_ipc.h"
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>

#define LOCAL_SOCK_PATH "/tmp/robot_fw.sock"

void fw_notify(const char *msg)
{
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) return;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, LOCAL_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        char buf[128];
        int len = snprintf(buf, sizeof(buf), "%s\n", msg);
        write(s, buf, len);
    }

    close(s);
}
