/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <init.h>

#if defined(CONFIG_BSD_LIBRARY) && defined(CONFIG_NET_SOCKETS_OFFLOAD)
#include <net/socket.h>
#endif

#include <logging/log.h>
LOG_MODULE_REGISTER(board_nonsecure, CONFIG_BOARD_LOG_LEVEL);

#define AT_CMD_MAX_READ_LENGTH 128
#define AT_CMD_LEN(cmd) (sizeof(cmd) - 1)
#define AT_CMD_COEX2 "AT%XCOEX2=1,0,1574,1577,1,1710,2200"
#define AT_CMD_TRACE "AT%XMODEMTRACE=0"

static int pca10015_coex_configure(void)
{
#if defined(CONFIG_BSD_LIBRARY) && defined(CONFIG_NET_SOCKETS_OFFLOAD)
	int at_socket_fd;
	int buffer;
	u8_t read_buffer[AT_CMD_MAX_READ_LENGTH];

	at_socket_fd = socket(AF_LTE, 0, NPROTO_AT);
	if (at_socket_fd == -1) {
		LOG_ERR("AT socket could not be opened");
		return -EFAULT;
	}

	LOG_DBG("AT CMD: %s", log_strdup(AT_CMD_TRACE));
	buffer = send(at_socket_fd, AT_CMD_TRACE, AT_CMD_LEN(AT_CMD_TRACE), 0);
	if (buffer != AT_CMD_LEN(AT_CMD_TRACE)) {
		LOG_ERR("XMODEMTRACE command failed");
		close(at_socket_fd);
		__ASSERT_NO_MSG(false);
		return -EIO;
	}

	buffer = recv(at_socket_fd, read_buffer, AT_CMD_MAX_READ_LENGTH, 0);
	LOG_DBG("AT RESP: %s", log_strdup(read_buffer));
	if ((buffer < 2) || (memcmp("OK", read_buffer, 2 != 0))) {
		LOG_ERR("XMODEMTRACE received unexpected response");
		close(at_socket_fd);
		__ASSERT_NO_MSG(false);
		return -EIO;
	}

	LOG_DBG("AT CMD: %s", log_strdup(AT_CMD_COEX2));
	buffer = send(at_socket_fd, AT_CMD_COEX2, AT_CMD_LEN(AT_CMD_COEX2), 0);
	if (buffer != AT_CMD_LEN(AT_CMD_COEX2)) {
		LOG_ERR("COEX2 command failed");
		close(at_socket_fd);
		return -EIO;
	}

	buffer = recv(at_socket_fd, read_buffer, AT_CMD_MAX_READ_LENGTH, 0);
	LOG_DBG("AT RESP: %s", log_strdup(read_buffer));
	if ((buffer < 2) || (memcmp("OK", read_buffer, 2 != 0))) {
		LOG_ERR("COEX2 command failed");
		close(at_socket_fd);
		return -EIO;
	}

	LOG_DBG("COEX2 successfully configured");

	close(at_socket_fd);
#endif
	return 0;
}

static int pca10015_board_init(struct device *dev)
{
	int err;

	err = pca10015_coex_configure();
	if (err) {
		LOG_ERR("pca10015_coex_configure failed with error: %d", err);
		return err;
	}

	return 0;
}

SYS_INIT(pca10015_board_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
