#include <net/sock.h>
#include <linux/sockptr.h>
#include <linux/socket.h>

void foo(void);

void
foo(void)
{
	struct socket *sock = NULL;
	int optval = 0;
	int optlen = sizeof(optval);

	(void) do_sock_getsockopt(sock, false, SOL_SOCKET, SO_TYPE,
				  KERNEL_SOCKPTR(&optval), KERNEL_SOCKPTR(&optlen));
}
