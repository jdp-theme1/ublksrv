// SPDX-License-Identifier: MIT or GPL-2.0-only

#include <config.h>
#include "ublksrv_tgt.h"
#include "ublksrv_tgt_endian.h"
#include "cliserv.h"
#include "nbd.h"

#define NBD_MAX_NAME	512

#define NBD_OP_READ_REQ  0x80
#define NBD_OP_WRITE_REQ  0x81
#define NBD_OP_READ_REPLY  0x82

#define NBD_WRITE_TGT_STR(dev, jbuf, jbuf_size, name, val) do { \
	int ret;						\
	if (val)						\
		ret = ublksrv_json_write_target_str_info(jbuf,	\
				jbuf_size, name, val);		\
	else							\
		ret = 0;					\
	if (ret < 0)						\
		jbuf = ublksrv_tgt_realloc_json_buf(dev, &jbuf_size);	\
	else							\
		break;						\
} while (1)

enum nbd_recv_state {
	NBD_RECV_IDLE,
	NBD_RECV_REPLY,
	NBD_RECV_DATA,
};

struct nbd_queue_data {
	enum nbd_recv_state recv;
	unsigned short in_flight_ios;

	struct nbd_reply reply;
};

struct nbd_io_data {
	unsigned int cmd_cookie;
};

static inline struct nbd_queue_data *
nbd_get_queue_data(const struct ublksrv_queue *q)
{
	return (struct nbd_queue_data *)q->private_data;
}

static inline struct nbd_io_data *
io_tgt_to_nbd_data(const struct ublk_io_tgt *io)
{
	return (struct nbd_io_data *)(io + 1);
}

static void nbd_setup_tgt(struct ublksrv_dev *dev, int type, bool recovery,
		uint16_t *flags)
{
	struct ublksrv_tgt_info *tgt = &dev->tgt;
	const struct ublksrv_ctrl_dev_info *info =
		ublksrv_ctrl_get_dev_info(ublksrv_get_ctrl_dev(dev));
	int jbuf_size;
	char *jbuf = ublksrv_tgt_return_json_buf(dev, &jbuf_size);
	int i;

	const char *port = NBD_DEFAULT_PORT;
	uint16_t needed_flags = 0;
	uint32_t cflags = NBD_FLAG_C_FIXED_NEWSTYLE;

	char host_name[NBD_MAX_NAME] = {0};
	char exp_name[NBD_MAX_NAME] = {0};
	char unix_path[NBD_MAX_NAME] = {0};
	u64 size64 = 0;
	bool can_opt_go = true;

	/* todo: support tls */
	char *certfile = NULL;
	char *keyfile = NULL;
	char *cacertfile = NULL;
	char *tlshostname = NULL;
	bool tls = false;

	ublk_assert(jbuf);
	ublk_assert(type == UBLKSRV_TGT_TYPE_NBD);
	ublk_assert(!recovery || info->state == UBLK_S_DEV_QUIESCED);

	ublksrv_json_read_target_str_info(jbuf, NBD_MAX_NAME, "host",
			host_name);
	ublksrv_json_read_target_str_info(jbuf, NBD_MAX_NAME, "unix",
			unix_path);
	ublksrv_json_read_target_str_info(jbuf, NBD_MAX_NAME, "export_name",
			exp_name);

	//fprintf(stderr, "%s: host %s unix %s exp_name %s\n", __func__,
	//		host_name, unix_path, exp_name);
	for (i = 0; i < info->nr_hw_queues; i++) {
		int sock;
		unsigned int opts = 0;

		if (strlen(unix_path))
			sock = openunix(unix_path);
		else
			sock = opennet(host_name, port, false);

		if (sock >= 0)
			negotiate(&sock, &size64, flags, exp_name,
					needed_flags, cflags, opts, certfile,
					keyfile, cacertfile, tlshostname, tls,
					can_opt_go);

		tgt->fds[i + 1] = sock;
		//fprintf(stderr, "%s:%s size %luMB flags %x sock %d\n",
		//		hostname, port, size64 >> 20, *flags, sock);
	}

	tgt->dev_size = size64;
	tgt->tgt_ring_depth = info->queue_depth;
	tgt->nr_fds = info->nr_hw_queues;
	tgt->extra_ios = 1;	//one extra slot for receiving nbd reply

	tgt->io_data_size = sizeof(struct ublk_io_tgt) +
		sizeof(struct nbd_io_data);
}

static void nbd_parse_flags(struct ublk_params *p, uint16_t flags, uint32_t bs)
{
	__u32 attrs = 0;

	ublksrv_log(LOG_INFO, "%s: negotiated flags %x\n", __func__, flags);

	if (flags & NBD_FLAG_READ_ONLY)
		attrs |= UBLK_ATTR_READ_ONLY;
	if (flags & NBD_FLAG_SEND_FLUSH) {
		if (flags & NBD_FLAG_SEND_FUA)
			attrs |= UBLK_ATTR_FUA;
		else
			attrs |= UBLK_ATTR_VOLATILE_CACHE;
	}

	p->basic.attrs |= attrs;

	if (flags & NBD_FLAG_SEND_TRIM) {
		p->discard.discard_granularity = bs;
		p->discard.max_discard_sectors = UINT_MAX >> 9;
		p->discard.max_discard_segments	= 1;
		p->types |= UBLK_PARAM_TYPE_DISCARD;
        }
}

static int nbd_init_tgt(struct ublksrv_dev *dev, int type, int argc,
		char *argv[])
{
	static const struct option nbd_longopts[] = {
		{ "host",	required_argument, 0, 0},
		{ "unix",	required_argument, 0, 0},
		{ "export_name",	required_argument, 0, 0},
		{ NULL }
	};
	struct ublksrv_tgt_info *tgt = &dev->tgt;
	const struct ublksrv_ctrl_dev_info *info =
		ublksrv_ctrl_get_dev_info(ublksrv_get_ctrl_dev(dev));
	int jbuf_size;
	char *jbuf = ublksrv_tgt_return_json_buf(dev, &jbuf_size);
	struct ublksrv_tgt_base_json tgt_json = {
		.type = type,
	};
	int ret;
	int opt;
	int option_index = 0;
	unsigned char bs_shift = 9;
	const char *host_name = NULL;
	const char *unix_path = NULL;
	const char *exp_name = NULL;
	uint16_t flags = 0;

	strcpy(tgt_json.name, "nbd");

	if (type != UBLKSRV_TGT_TYPE_NBD)
		return -1;

	while ((opt = getopt_long(argc, argv, "-:f:",
				  nbd_longopts, &option_index)) != -1) {
		if (opt < 0)
			break;
		if (opt > 0)
			continue;

		//fprintf(stderr, "option %s", nbd_longopts[option_index].name);
		//if (optarg)
		//    fprintf(stderr, " with arg %s", optarg);
		//printf("\n");
		if (!strcmp(nbd_longopts[option_index].name, "host"))
		      host_name = optarg;
		if (!strcmp(nbd_longopts[option_index].name, "unix"))
		      unix_path = optarg;
		if (!strcmp(nbd_longopts[option_index].name, "export_name"))
			exp_name = optarg;
	}

	ublksrv_json_write_dev_info(ublksrv_get_ctrl_dev(dev), jbuf, jbuf_size);
	NBD_WRITE_TGT_STR(dev, jbuf, jbuf_size, "host", host_name);
	NBD_WRITE_TGT_STR(dev, jbuf, jbuf_size, "unix", unix_path);
	NBD_WRITE_TGT_STR(dev, jbuf, jbuf_size, "export_name", exp_name);

	nbd_setup_tgt(dev, type, false, &flags);

	tgt_json.dev_size = tgt->dev_size;
	ublksrv_json_write_target_base_info(jbuf, jbuf_size, &tgt_json);

	struct ublk_params p = {
		.types = UBLK_PARAM_TYPE_BASIC,
		.basic = {
			.logical_bs_shift	= bs_shift,
			.physical_bs_shift	= 12,
			.io_opt_shift		= 12,
			.io_min_shift		= bs_shift,
			.max_sectors		= info->max_io_buf_bytes >> 9,
			.dev_sectors		= tgt->dev_size >> 9,
		},
	};

	nbd_parse_flags(&p, flags, 1U << bs_shift);

	do {
		ret = ublksrv_json_write_params(&p, jbuf, jbuf_size);
		if (ret < 0)
			jbuf = ublksrv_tgt_realloc_json_buf(dev, &jbuf_size);
	} while (ret < 0);

	return 0;
}

static int nbd_recovery_tgt(struct ublksrv_dev *dev, int type)
{
	uint16_t flags = 0;

	nbd_setup_tgt(dev, type, true, &flags);

	return 0;
}

static int req_to_nbd_cmd_type(const struct ublksrv_io_desc *iod)
{
	switch (ublksrv_get_op(iod)) {
	case UBLK_IO_OP_DISCARD:
		return NBD_CMD_TRIM;
	case UBLK_IO_OP_FLUSH:
		return NBD_CMD_FLUSH;
	case UBLK_IO_OP_WRITE:
		return NBD_CMD_WRITE;
	case UBLK_IO_OP_READ:
		return NBD_CMD_READ;
	default:
		return -1;
	}
}

static inline bool is_recv_io(const struct ublksrv_queue *q,
		const struct ublk_io_data *data)
{
	return data->tag >= q->q_depth;
}

#define NBD_COOKIE_BITS 32
static inline u64 nbd_cmd_handle(const struct ublksrv_queue *q,
		const struct ublk_io_data *data,
		const struct nbd_io_data *nbd_data)
{
	u64 cookie = nbd_data->cmd_cookie;

	return (cookie << NBD_COOKIE_BITS) | ublk_unique_tag(q->q_id, data->tag);
}

static inline u32 nbd_handle_to_cookie(u64 handle)
{
	return (u32)(handle >> NBD_COOKIE_BITS);
}

static inline u32 nbd_handle_to_tag(u64 handle)
{
	return (u32)handle;
}

static inline void __nbd_build_req(const struct ublksrv_queue *q,
		const struct ublk_io_data *data,
		const struct nbd_io_data *nbd_data,
		u32 type, struct nbd_request *req)
{
	u32 nbd_cmd_flags = 0;
	u64 handle;

	if (data->iod->op_flags & UBLK_IO_F_FUA)
		nbd_cmd_flags |= NBD_CMD_FLAG_FUA;

	req->type = htonl(type | nbd_cmd_flags);

	if (type != NBD_CMD_FLUSH) {
		req->from = cpu_to_be64((u64)data->iod->start_sector << 9);
		req->len = htonl(data->iod->nr_sectors << 9);
	}

	handle = nbd_cmd_handle(q, data, nbd_data);
	memcpy(req->handle, &handle, sizeof(handle));
}

static inline void nbd_start_recv(const struct ublksrv_queue *q,
		void *buf, int len, int tag, bool reply)
{
	struct io_uring_sqe *sqe = io_uring_get_sqe(q->ring_ptr);
	unsigned int op = reply ? NBD_OP_READ_REPLY : UBLK_IO_OP_READ;

	io_uring_prep_recv(sqe, q->q_id + 1, buf, len, MSG_WAITALL);
	io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE);

	/* bit63 marks us as tgt io */
	sqe->user_data = build_user_data(tag, op, 0, 1);

	ublksrv_log(LOG_INFO, "%s: queue recv %s"
				"(qid %d tag %u, target: %d, user_data %llx)\n",
			__func__, reply ? "reply" : "io",
			q->q_id, tag, 1, sqe->user_data);
}

static void nbd_recv_reply(const struct ublksrv_queue *q)
{
	struct nbd_queue_data *q_data = nbd_get_queue_data(q);

	if (q_data->recv != NBD_RECV_IDLE)
		return;

	if (!q_data->in_flight_ios)
		return;

	q_data->recv = NBD_RECV_REPLY;
	nbd_start_recv(q, &q_data->reply, sizeof(q_data->reply),
			q->q_depth, true);
}

static void nbd_recv_io(const struct ublksrv_queue *q,
		const struct ublk_io_data *data)
{
	struct nbd_queue_data *q_data = nbd_get_queue_data(q);

	if (q_data->recv != NBD_RECV_REPLY)
		return;

	q_data->recv = NBD_RECV_DATA;
	nbd_start_recv(q, (void *)data->iod->addr,
			data->iod->nr_sectors << 9, data->tag, false);
}

static int nbd_queue_send_req(const struct ublksrv_queue *q,
		const struct ublk_io_data *data,
		const struct nbd_request *req,
		struct io_uring_sqe *sqe)
{
#if 0
        struct iovec iov[2] = {
		[0] = {
			.iov_base = (void *)req,
			.iov_len = sizeof(*req),
		},
		[1] = {
			.iov_base = (void *)data->iod->addr,
			.iov_len = data->iod->nr_sectors << 9,
		},
	};
        struct msghdr msg = {0};

	msg.msg_iov = iov;
	msg.msg_iovlen = 2;

	io_uring_prep_sendmsg(sqe, q->q_id + 1, &msg, 0);
#else
	struct io_uring_sqe *sqe2 = io_uring_get_sqe(q->ring_ptr);

	if (!sqe2)
		return -ENOMEM;

	io_uring_prep_send(sqe, q->q_id + 1, req, sizeof(*req), MSG_MORE);
	io_uring_sqe_set_flags(sqe, IOSQE_CQE_SKIP_SUCCESS |
			IOSQE_FIXED_FILE | IOSQE_IO_LINK);
	sqe->user_data = build_user_data(data->tag, NBD_OP_WRITE_REQ, 1, 1);

	io_uring_prep_send(sqe2, q->q_id + 1, (void *)data->iod->addr,
			data->iod->nr_sectors << 9, 0);
	io_uring_sqe_set_flags(sqe2, IOSQE_FIXED_FILE);
	sqe2->user_data = build_user_data(data->tag, UBLK_IO_OP_WRITE, 0, 1);

	return 0;
#endif
}

static int nbd_queue_req(const struct ublksrv_queue *q,
		const struct ublk_io_data *data,
		const struct nbd_request *req)
{
	struct nbd_queue_data *q_data = nbd_get_queue_data(q);
	const struct ublksrv_io_desc *iod = data->iod;
	struct io_uring_sqe *sqe = io_uring_get_sqe(q->ring_ptr);
	unsigned ublk_op = ublksrv_get_op(iod);
	int ret = 0;

	if (!sqe)
		return 0;

	q_data->in_flight_ios += 1;
	switch (ublk_op) {
	case UBLK_IO_OP_READ:
		io_uring_prep_send(sqe, q->q_id + 1, req, sizeof(*req), 0);
		io_uring_sqe_set_flags(sqe, IOSQE_CQE_SKIP_SUCCESS |
				IOSQE_FIXED_FILE);
		sqe->user_data = build_user_data(data->tag, NBD_OP_READ_REQ, 0, 1);
		break;
	case UBLK_IO_OP_WRITE:
		ret = nbd_queue_send_req(q, data, req, sqe);
		break;
	case UBLK_IO_OP_FLUSH:
	case UBLK_IO_OP_DISCARD:
		io_uring_prep_send(sqe, q->q_id + 1, req, sizeof(*req),
				MSG_WAITALL);
		io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE);
		sqe->user_data = build_user_data(data->tag, ublk_op, 0, 1);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret < 0) {
		q_data->in_flight_ios -= 1;
		return ret;
	}

	nbd_recv_reply(q);

	ublksrv_log(LOG_INFO, "%s: queue io op %d(%llu %x %llx)"
				" (qid %d tag %u, cmd_op %u target: %d, user_data %llx)\n",
			__func__, ublk_op, data->iod->start_sector,
			data->iod->nr_sectors, sqe->addr,
			q->q_id, data->tag, ublk_op, 1, sqe->user_data);

	return 1;
}

static void nbd_handle_recv_io(const struct ublksrv_queue *q,
		int type)
{
	struct nbd_queue_data *q_data = nbd_get_queue_data(q);

	if (type == NBD_CMD_READ)
		ublk_assert(q_data->recv == NBD_RECV_DATA);
	else
		ublk_assert(q_data->recv == NBD_RECV_REPLY);

	q_data->recv = NBD_RECV_IDLE;
	nbd_recv_reply(q);
}

static co_io_job __nbd_handle_io_async(const struct ublksrv_queue *q,
		const struct ublk_io_data *data, struct ublk_io_tgt *io)
{
	struct nbd_request req = {.magic = htonl(NBD_REQUEST_MAGIC)};
	struct nbd_queue_data *q_data = nbd_get_queue_data(q);
	struct nbd_io_data *nbd_data = io_tgt_to_nbd_data(io);
	int type = req_to_nbd_cmd_type(data->iod);
	int ret = -EIO;

	if (type == -1)
		goto exit;

	nbd_data->cmd_cookie += 1;

	__nbd_build_req(q, data, nbd_data, type, &req);

again:
	ret = nbd_queue_req(q, data, &req);
	if (!ret)
		ret = -ENOMEM;
	if (ret < 0)
		goto exit;

	co_await__suspend_always(data->tag);
	if (io->tgt_io_cqe->res == -EAGAIN)
		goto again;
	ret = io->tgt_io_cqe->res;

exit:
	ublksrv_complete_io(q, data->tag, ret);
	q_data->in_flight_ios -= 1;
	nbd_handle_recv_io(q, type);

	co_return;
}

static int nbd_handle_io_async(const struct ublksrv_queue *q,
		const struct ublk_io_data *data)
{
	struct ublk_io_tgt *io = __ublk_get_io_tgt_data(data);

	io->co = __nbd_handle_io_async(q, data, io);

	return 0;
}

static void nbd_handle_recv_reply(const struct ublksrv_queue *q,
		const struct ublk_io_data *data, struct ublk_io_tgt *io,
		const struct io_uring_cqe *cqe)
{
	struct nbd_queue_data *q_data = nbd_get_queue_data(q);
	struct nbd_io_data *nbd_data;
	u64 handle;
	int tag, hwq;
	unsigned ublk_op;

	ublk_assert(q_data->recv == NBD_RECV_REPLY);

	/* we are receiving reply for every read IO, so simply retry */
	if (cqe->res < 0) {
retry:
		q_data->recv = NBD_RECV_IDLE;
		nbd_recv_reply(q);
		return;
	}
	ublk_assert(cqe->res == sizeof(struct nbd_reply));

	memcpy(&handle, q_data->reply.handle, sizeof(handle));
	tag = nbd_handle_to_tag(handle);
	hwq = ublk_unique_tag_to_hwq(tag);
	tag = ublk_unique_tag_to_tag(tag);

	if (tag >= q->q_depth)
		goto retry;

	if (hwq != q->q_id)
		goto retry;


	data = ublksrv_queue_get_io_data(q, tag);
	io = __ublk_get_io_tgt_data(data);
	nbd_data = io_tgt_to_nbd_data(io);

	if (nbd_data->cmd_cookie != nbd_handle_to_cookie(handle))
		goto retry;

	ublk_op = ublksrv_get_op(data->iod);

	if (ublk_op == UBLK_IO_OP_READ)
		nbd_recv_io(q, data);
	else {
		int err = ntohl(q_data->reply.error);
		struct io_uring_cqe fake_cqe;

		if (err) {
			fake_cqe.res = -EIO;
		} else {
			if (ublk_op == UBLK_IO_OP_WRITE)
				fake_cqe.res = data->iod->nr_sectors << 9;
			else
				fake_cqe.res = 0;
		}

		io->tgt_io_cqe = &fake_cqe;
		io->co.resume();
	}
}

static void nbd_tgt_io_done(const struct ublksrv_queue *q,
		const struct ublk_io_data *data,
		const struct io_uring_cqe *cqe)
{
	struct nbd_queue_data *q_data = nbd_get_queue_data(q);
	int tag = user_data_to_tag(cqe->user_data);
	struct ublk_io_tgt *io = __ublk_get_io_tgt_data(data);

	ublk_assert(tag == data->tag);

	if (is_recv_io(q, data)) {
		ublk_assert(q_data->recv == NBD_RECV_REPLY);
		nbd_handle_recv_reply(q, data, io, cqe);
	} else {
		if (user_data_to_op(cqe->user_data) == UBLK_IO_OP_READ ||
				cqe->res < 0) {
			io->tgt_io_cqe = cqe;
			io->co.resume();
		}
	}
}

static void nbd_deinit_tgt(const struct ublksrv_dev *dev)
{
	const struct ublksrv_tgt_info *tgt = &dev->tgt;
	const struct ublksrv_ctrl_dev_info *info =
		ublksrv_ctrl_get_dev_info(ublksrv_get_ctrl_dev(dev));
	int i;

	for (i = 0; i < info->nr_hw_queues; i++) {
		int fd = tgt->fds[i + 1];

		shutdown(fd, SHUT_RDWR);
		close(fd);
	}
}

static void nbd_usage_for_add(void)
{
	printf("           nbd: --host=$HOST [--port=$PORT] | --unix=$UNIX_PATH\n");
}

static int nbd_init_queue(const struct ublksrv_queue *q,
		void **queue_data_ptr)
{
	struct nbd_queue_data *data =
		(struct nbd_queue_data *)calloc(sizeof(*data), 1);

	if (!data)
		return -ENOMEM;

	data->recv = NBD_RECV_IDLE;

	*queue_data_ptr = (void *)data;
	return 0;
}

static void nbd_deinit_queue(const struct ublksrv_queue *q)
{
	struct nbd_queue_data *data = nbd_get_queue_data(q);

	free(data);
}

struct ublksrv_tgt_type  nbd_tgt_type = {
	.handle_io_async = nbd_handle_io_async,
	.tgt_io_done = nbd_tgt_io_done,
	.usage_for_add	=  nbd_usage_for_add,
	.init_tgt = nbd_init_tgt,
	.deinit_tgt = nbd_deinit_tgt,
	.type	= UBLKSRV_TGT_TYPE_NBD,
	.name	=  "nbd",
	.recovery_tgt = nbd_recovery_tgt,
	.init_queue = nbd_init_queue,
	.deinit_queue = nbd_deinit_queue,
};

static void tgt_nbd_init() __attribute__((constructor));

static void tgt_nbd_init(void)
{
	ublksrv_register_tgt_type(&nbd_tgt_type);
}

