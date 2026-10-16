/* Test shim (LD_PRELOAD, Linux): UDP datagrams to any port but the Warpzone relay's
 * (RELAY_ONLY_PORT) are silently dropped, so two games on one machine can only reach
 * each other through the relay. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>

static int only_relay(int fd, const struct sockaddr *to)
{
    int type = 0;
    socklen_t len = sizeof type;
    if (!to || getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len) != 0 || type != SOCK_DGRAM) return 0;
    const char *v = getenv("RELAY_ONLY_PORT");
    int port = v ? atoi(v) : 3478, dest;
    if (to->sa_family == AF_INET) dest = ntohs(((const struct sockaddr_in *)to)->sin_port);
    else if (to->sa_family == AF_INET6) dest = ntohs(((const struct sockaddr_in6 *)to)->sin6_port);
    else return 0;
    return dest != port;
}

ssize_t sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *to, socklen_t tolen)
{
    static ssize_t (*next)(int, const void *, size_t, int, const struct sockaddr *, socklen_t);
    if (!next) *(void **)&next = dlsym(RTLD_NEXT, "sendto");
    if (only_relay(fd, to)) return (ssize_t)len;
    return next(fd, buf, len, flags, to, tolen);
}

ssize_t sendmsg(int fd, const struct msghdr *msg, int flags)
{
    static ssize_t (*next)(int, const struct msghdr *, int);
    if (!next) *(void **)&next = dlsym(RTLD_NEXT, "sendmsg");
    if (msg && only_relay(fd, msg->msg_name)) {
        ssize_t total = 0;
        for (size_t i = 0; i < msg->msg_iovlen; i++) total += (ssize_t)msg->msg_iov[i].iov_len;
        return total;
    }
    return next(fd, msg, flags);
}
