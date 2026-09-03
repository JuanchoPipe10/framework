/**
 * @file local_ipc.h
 * @brief Local IPC client for the framework's Unix domain socket
 *
 * Lets any C program on the same device send status messages to the
 * running framework instance via /tmp/robot_fw.sock, without linking
 * against the rest of the framework.
 */
#ifndef LOCAL_IPC_H
#define LOCAL_IPC_H

/**
 * @brief Send a text message to the local framework instance
 * @param msg Message to send (e.g. "MOVING", "STOPPED",
 *            "OBSTACLE <dist>", "SIGN <id> <confidence>")
 *
 * Opens a fresh connection to /tmp/robot_fw.sock, writes msg
 * followed by '\n', and closes the connection. No-op if the
 * framework is not currently listening.
 */
void fw_notify(const char *msg);

#endif // LOCAL_IPC_H
