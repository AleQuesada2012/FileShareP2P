#include "common/net.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    char local_ip[46];

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <remote_host> <remote_port>\n", argv[0]);
        return 2;
    }

    if (net_get_local_ip(argv[1], argv[2], local_ip, sizeof(local_ip)) != 0) {
        fprintf(stderr,
                "get_local_ip: could not resolve local address for %s:%s: %s\n",
                argv[1],
                argv[2],
                strerror(errno));
        return 1;
    }

    puts(local_ip);
    return 0;
}
