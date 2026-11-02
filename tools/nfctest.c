/*
 * Tests the kernel NFC stack without any NFC packages: powers up nfc0, polls for
 * tags, and reports what it finds. Tag IDs are printed as a length only.
 *
 *   nfctest [seconds]
 *
 * aarch64-linux-gnu-gcc -static -O2 -o nfctest nfctest.c
 */
#include <errno.h>
#include <linux/genetlink.h>
#include <linux/netlink.h>
#include <linux/nfc.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define ALIGN4(n) (((n) + 3) & ~3)

static int sock;
static __u16 family;
static __u32 seq;

static void put_attr(char *buf, __u32 *len, __u16 type, const void *data, __u16 size)
{
	struct nlattr *a = (struct nlattr *)(buf + *len);

	a->nla_type = type;
	a->nla_len = NLA_HDRLEN + size;
	memcpy((char *)a + NLA_HDRLEN, data, size);
	*len += ALIGN4(a->nla_len);
}

static int send_cmd(__u16 fam, __u8 cmd, __u16 flags, const char *attrs, __u32 alen)
{
	char buf[256] = {};
	struct nlmsghdr *n = (struct nlmsghdr *)buf;
	struct genlmsghdr *g = (struct genlmsghdr *)NLMSG_DATA(n);

	n->nlmsg_type = fam;
	n->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | flags;
	n->nlmsg_seq = ++seq;
	g->cmd = cmd;
	g->version = 1;
	memcpy((char *)g + GENL_HDRLEN, attrs, alen);
	n->nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN + alen);
	return send(sock, buf, n->nlmsg_len, 0);
}

static int ack(void)
{
	char buf[8192];
	int r = recv(sock, buf, sizeof(buf), 0);
	struct nlmsghdr *n = (struct nlmsghdr *)buf;

	for (; r > 0 && NLMSG_OK(n, (unsigned int)r); n = NLMSG_NEXT(n, r))
		if (n->nlmsg_type == NLMSG_ERROR)
			return ((struct nlmsgerr *)NLMSG_DATA(n))->error;
	return 0;
}

static struct nlattr *next_attr(struct nlattr *a)
{
	return (struct nlattr *)((char *)a + ALIGN4(a->nla_len));
}

/* Finds the NFC family and its events multicast group */
static int resolve(__u32 *group)
{
	char attrs[64], buf[8192];
	__u32 alen = 0;
	int r;
	struct nlmsghdr *n = (struct nlmsghdr *)buf;

	put_attr(attrs, &alen, CTRL_ATTR_FAMILY_NAME, NFC_GENL_NAME, sizeof(NFC_GENL_NAME));
	send_cmd(GENL_ID_CTRL, CTRL_CMD_GETFAMILY, 0, attrs, alen);
	r = recv(sock, buf, sizeof(buf), 0);
	if (r <= 0 || n->nlmsg_type == NLMSG_ERROR)
		return -1;

	struct nlattr *a = (struct nlattr *)((char *)NLMSG_DATA(n) + GENL_HDRLEN);
	char *end = buf + n->nlmsg_len;

	for (; (char *)a < end; a = next_attr(a)) {
		if ((a->nla_type & NLA_TYPE_MASK) == CTRL_ATTR_FAMILY_ID)
			family = *(__u16 *)((char *)a + NLA_HDRLEN);
		if ((a->nla_type & NLA_TYPE_MASK) != CTRL_ATTR_MCAST_GROUPS)
			continue;
		struct nlattr *g = (struct nlattr *)((char *)a + NLA_HDRLEN);
		char *gend = (char *)a + a->nla_len;

		for (; (char *)g < gend; g = next_attr(g)) {
			struct nlattr *f = (struct nlattr *)((char *)g + NLA_HDRLEN);
			char *fend = (char *)g + g->nla_len;
			__u32 id = 0;
			const char *name = "";

			for (; (char *)f < fend; f = next_attr(f)) {
				if ((f->nla_type & NLA_TYPE_MASK) == CTRL_ATTR_MCAST_GRP_ID)
					id = *(__u32 *)((char *)f + NLA_HDRLEN);
				if ((f->nla_type & NLA_TYPE_MASK) == CTRL_ATTR_MCAST_GRP_NAME)
					name = (char *)f + NLA_HDRLEN;
			}
			if (!strcmp(name, NFC_GENL_MCAST_EVENT_NAME))
				*group = id;
		}
	}
	recv(sock, buf, sizeof(buf), MSG_DONTWAIT);
	return family ? 0 : -1;
}

static void show_targets(__u32 idx)
{
	char attrs[32], buf[8192];
	__u32 alen = 0;

	put_attr(attrs, &alen, NFC_ATTR_DEVICE_INDEX, &idx, sizeof(idx));
	send_cmd(family, NFC_CMD_GET_TARGET, NLM_F_DUMP, attrs, alen);
	for (;;) {
		int r = recv(sock, buf, sizeof(buf), 0);
		struct nlmsghdr *n = (struct nlmsghdr *)buf;

		if (r <= 0)
			return;
		for (; NLMSG_OK(n, (unsigned int)r); n = NLMSG_NEXT(n, r)) {
			if (n->nlmsg_type == NLMSG_DONE || n->nlmsg_type == NLMSG_ERROR)
				return;
			struct nlattr *a = (struct nlattr *)((char *)NLMSG_DATA(n) + GENL_HDRLEN);
			char *end = (char *)n + n->nlmsg_len;

			for (; (char *)a < end; a = next_attr(a)) {
				void *d = (char *)a + NLA_HDRLEN;

				if (a->nla_type == NFC_ATTR_PROTOCOLS)
					printf("  protocols mask: %#x\n", *(__u32 *)d);
				if (a->nla_type == NFC_ATTR_TARGET_SENS_RES)
					printf("  SENS_RES: %#x\n", *(__u16 *)d);
				if (a->nla_type == NFC_ATTR_TARGET_SEL_RES)
					printf("  SEL_RES: %#x\n", *(__u8 *)d);
				if (a->nla_type == NFC_ATTR_TARGET_NFCID1)
					printf("  ID length: %d bytes\n", a->nla_len - NLA_HDRLEN);
			}
		}
	}
}

int main(int argc, char **argv)
{
	int secs = argc > 1 ? atoi(argv[1]) : 30;
	struct sockaddr_nl sa = { .nl_family = AF_NETLINK };
	char attrs[64], buf[8192];
	__u32 alen = 0, idx = 0, group = 0, protos;
	int r, found = 0;
	time_t end;

	sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
	if (sock < 0 || bind(sock, (struct sockaddr *)&sa, sizeof(sa)))
		return perror("netlink"), 1;
	if (resolve(&group))
		return fprintf(stderr, "no NFC family: is the nfc module loaded?\n"), 1;

	put_attr(attrs, &alen, NFC_ATTR_DEVICE_INDEX, &idx, sizeof(idx));
	send_cmd(family, NFC_CMD_DEV_UP, 0, attrs, alen);
	r = ack();
	printf("device up: %s\n", r ? strerror(-r) : "ok");
	if (r && r != -EALREADY)
		return 1;

	setsockopt(sock, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP, &group, sizeof(group));

	protos = NFC_PROTO_JEWEL_MASK | NFC_PROTO_MIFARE_MASK | NFC_PROTO_FELICA_MASK |
		 NFC_PROTO_ISO14443_MASK | NFC_PROTO_ISO14443_B_MASK | NFC_PROTO_ISO15693_MASK;
	put_attr(attrs, &alen, NFC_ATTR_IM_PROTOCOLS, &protos, sizeof(protos));
	send_cmd(family, NFC_CMD_START_POLL, 0, attrs, alen);
	r = ack();
	printf("start poll: %s\n", r ? strerror(-r) : "ok");
	if (r)
		goto down;

	printf("polling for %d s, hold a tag or card to the phone\n", secs);
	fflush(stdout);
	end = time(NULL) + secs;
	while (time(NULL) < end && !found) {
		struct pollfd p = { .fd = sock, .events = POLLIN };

		if (poll(&p, 1, 1000) <= 0)
			continue;
		r = recv(sock, buf, sizeof(buf), 0);
		struct nlmsghdr *n = (struct nlmsghdr *)buf;

		for (; r > 0 && NLMSG_OK(n, (unsigned int)r); n = NLMSG_NEXT(n, r)) {
			if (n->nlmsg_type != family)
				continue;
			if (((struct genlmsghdr *)NLMSG_DATA(n))->cmd == NFC_EVENT_TARGETS_FOUND)
				found = 1;
		}
	}
	if (found) {
		printf("TAG FOUND\n");
		show_targets(idx);
	} else {
		printf("no tag seen\n");
		alen = 0;
		put_attr(attrs, &alen, NFC_ATTR_DEVICE_INDEX, &idx, sizeof(idx));
		send_cmd(family, NFC_CMD_STOP_POLL, 0, attrs, alen);
		ack();
	}
down:
	alen = 0;
	put_attr(attrs, &alen, NFC_ATTR_DEVICE_INDEX, &idx, sizeof(idx));
	send_cmd(family, NFC_CMD_DEV_DOWN, 0, attrs, alen);
	ack();
	printf("device down\n");
	return !found;
}
