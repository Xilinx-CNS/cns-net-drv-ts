/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 OKTET Labs Ltd. All rights reserved. */

/** @file
 * @brief Test API for RSS tests
 *
 * Implementation of TAPI for RSS tests.
 */

#include "common_rss.h"
#include "tapi_bpf.h"
#include "tapi_bpf_rxq_stats.h"
#include "tapi_cfg_rx_rule.h"
#include "tapi_cfg_if.h"
#include "tapi_rpc_bpf.h"
#include "tapi_mem.h"
#include "tapi_file.h"
#include "te_ipstack.h"

/* Minimum number of packets to send when testing RSS */
#define RSS_TEST_MIN_PKTS_NUM 3

/* Maximum number of packets to send when testing RSS */
#define RSS_TEST_MAX_PKTS_NUM 30

/*
 * Common code for configuring XDP hook and sending/receiving
 * some packets.
 */
static te_errno
send_check_common(rcf_rpc_server *sender_rpcs, int sender_s,
                  const struct sockaddr *sender_addr,
                  rcf_rpc_server *receiver_rpcs, int receiver_s,
                  const struct sockaddr *receiver_addr,
                  rpc_socket_type sock_type, unsigned int bpf_id,
                  unsigned int *sent_pkts, const char *vpref)
{
    unsigned int i;
    unsigned int pkts_num;
    te_errno rc;

    if (sender_addr != NULL || receiver_addr != NULL)
    {
        int af = (sender_addr != NULL ? sender_addr->sa_family :
                                        receiver_addr->sa_family);

        rc = tapi_bpf_rxq_stats_reset(receiver_rpcs->ta, bpf_id);
        if (rc != 0)
            return rc;

        rc = tapi_bpf_rxq_stats_set_params(
                 receiver_rpcs->ta, bpf_id, af,
                 sender_addr, receiver_addr,
                 sock_type == RPC_SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP,
                 TRUE);
        if (rc != 0)
            return rc;
    }
    else
    {

        rc = tapi_bpf_rxq_stats_clear(receiver_rpcs->ta, bpf_id);
        if (rc != 0)
            return rc;
    }

    pkts_num = rand_range(RSS_TEST_MIN_PKTS_NUM, RSS_TEST_MAX_PKTS_NUM);

    for (i = 0; i < pkts_num; i++)
    {
        net_drv_send_recv_check(sender_rpcs, sender_s,
                                receiver_rpcs, receiver_s, vpref);
    }

    *sent_pkts = pkts_num;
    return 0;
}

/* See description in common_rss.h */
te_errno
net_drv_rss_send_check_stats(rcf_rpc_server *sender_rpcs, int sender_s,
                             const struct sockaddr *sender_addr,
                             rcf_rpc_server *receiver_rpcs, int receiver_s,
                             const struct sockaddr *receiver_addr,
                             rpc_socket_type sock_type, unsigned int exp_queue,
                             unsigned int bpf_id, const char *vpref)
{
    te_errno rc;
    unsigned int pkts_num;

    rc = send_check_common(sender_rpcs, sender_s, sender_addr,
                           receiver_rpcs, receiver_s, receiver_addr,
                           sock_type, bpf_id, &pkts_num, vpref);
    if (rc != 0)
        return rc;

    return tapi_bpf_rxq_stats_check_single(receiver_rpcs->ta, bpf_id,
                                           exp_queue, pkts_num,
                                           sock_type, vpref);
}

/* See description in common_rss.h */
te_errno
net_drv_rss_send_get_stats(rcf_rpc_server *sender_rpcs, int sender_s,
                           const struct sockaddr *sender_addr,
                           rcf_rpc_server *receiver_rpcs, int receiver_s,
                           const struct sockaddr *receiver_addr,
                           rpc_socket_type sock_type, unsigned int bpf_id,
                           tapi_bpf_rxq_stats **stats,
                           unsigned int *stats_count,
                           const char *vpref)
{
    te_errno rc;
    unsigned int i;
    uint64_t got_pkts = 0;
    unsigned int pkts_num;

    rc = send_check_common(sender_rpcs, sender_s, sender_addr,
                           receiver_rpcs, receiver_s, receiver_addr,
                           sock_type, bpf_id, &pkts_num, vpref);
    if (rc != 0)
        return rc;

    rc = tapi_bpf_rxq_stats_read(receiver_rpcs->ta, bpf_id, stats,
                                 stats_count);
    if (rc != 0)
        return rc;

    tapi_bpf_rxq_stats_print(NULL, *stats, *stats_count);

    for (i = 0; i < *stats_count; i++)
        got_pkts += (*stats)[i].pkts;

    if (got_pkts == 0)
        TEST_VERDICT("%s: no packets was detected for any Rx queue", vpref);
    else if (got_pkts < pkts_num)
        TEST_VERDICT("%s: too few packets were detected", vpref);

    return 0;
}

/* See description in common_rss.h */
te_errno
net_drv_rss_check_set_hfunc(const char *ta,
                            const char *if_name,
                            unsigned int rss_context,
                            const char *func_name)
{
    te_errno rc;
    tapi_cfg_if_rss_hfunc *hfuncs = NULL;
    unsigned int hfuncs_count;
    unsigned int i;
    te_bool found = FALSE;
    te_bool need_change = FALSE;

    rc = tapi_cfg_if_rss_hfuncs_get(ta, if_name, rss_context,
                                    &hfuncs, &hfuncs_count);
    if (rc != 0)
        return rc;

    for (i = 0; i < hfuncs_count; i++)
    {
        if (strcmp(hfuncs[i].name, func_name) == 0)
        {
            found = TRUE;
            if (!hfuncs[i].enabled)
                need_change = TRUE;
        }
        else if (hfuncs[i].enabled)
        {
            need_change = TRUE;
        }
    }

    free(hfuncs);

    if (!found)
        return TE_RC(TE_TAPI, TE_ENOENT);

    if (!need_change)
        return 0;

    rc = tapi_cfg_if_rss_hfunc_set_single_local(
                              ta, if_name, rss_context, func_name);
    if (rc != 0)
        return rc;

    rc = tapi_cfg_if_rss_hash_indir_commit(ta, if_name, rss_context);
    if (TE_RC_GET_ERROR(rc) == TE_EOPNOTSUPP)
        return TE_RC(TE_TAPI, TE_EOPNOTSUPP);

    return rc;
}

/* See description in common_rss.h */
void
net_drv_rss_ctx_prepare(net_drv_rss_ctx *ctx, const char *ta,
                        const char *if_name, unsigned int rss_ctx)
{
    te_errno rc = 0;
    int hash_var;
    int rx_queues;

    ctx->ta = ta;
    ctx->if_name = if_name;
    ctx->rss_ctx = rss_ctx;

    net_drv_rss_get_check_hkey(ta, if_name, rss_ctx, &ctx->hash_key,
                               &ctx->key_len);

    rc = tapi_cfg_if_rss_rx_queues_get(ta, if_name, &rx_queues);
    if (rc != 0)
    {
        ERROR("%s(): failed to get number of Rx queues, rc=%r",
              __FUNCTION__, rc);
        goto cleanup;
    }

    ctx->rx_queues = rx_queues;

    rc = tapi_cfg_if_rss_indir_table_size(ta, if_name, rss_ctx,
                                          &ctx->indir_table_size);
    if (rc != 0)
    {
        ERROR("%s(): failed to get indirection table size, rc=%r",
              __FUNCTION__, rc);
        goto cleanup;
    }

    ctx->cache = te_toeplitz_cache_init_size(ctx->hash_key, ctx->key_len);
    if (ctx->cache == NULL)
    {
        ERROR("%s(): failed to allocate Toeplitz hash cache", __FUNCTION__);
        rc = TE_ENOMEM;
        goto cleanup;
    }

    rc = cfg_get_instance_int_fmt(&hash_var, "%s",
                                  "/local:/iut_toeplitz_variant:");
    if (rc != 0)
    {
        ERROR("%s(): failed to get Toeplitz hash variant", __FUNCTION__);
        rc = TE_EINVAL;
        goto cleanup;
    }

    ctx->hash_variant = hash_var;

cleanup:

    if (rc != 0)
    {
        net_drv_rss_ctx_release(ctx);
        TEST_STOP;
    }
}

/* See description in common_rss.h */
void
net_drv_rss_ctx_release(net_drv_rss_ctx *ctx)
{
    free(ctx->hash_key);
    te_toeplitz_hash_fini(ctx->cache);
    ctx->hash_key = NULL;
    ctx->cache = NULL;
}

/* See description in common_rss.h */
te_errno
net_drv_rss_ctx_change_key(net_drv_rss_ctx *ctx, uint8_t *hash_key,
                           unsigned int key_len)
{
    uint8_t *key_dup = NULL;
    te_toeplitz_hash_cache *new_cache = NULL;

    key_dup = TE_ALLOC(key_len);
    if (key_dup == NULL)
    {
        ERROR("%s(): cannot allocate memory for hash key", __FUNCTION__);
        return TE_ENOMEM;
    }
    memcpy(key_dup, hash_key, key_len);

    new_cache = te_toeplitz_cache_init_size(hash_key, key_len);
    if (new_cache == NULL)
    {
        ERROR("%s(): failed to allocate Toeplitz hash cache", __FUNCTION__);
        free(key_dup);
        return TE_ENOMEM;
    }

    free(ctx->hash_key);
    ctx->hash_key = key_dup;
    ctx->key_len = key_len;

    te_toeplitz_hash_fini(ctx->cache);
    ctx->cache = new_cache;

    return 0;
}

/* See description in common_rss.h */
te_errno
net_drv_rss_predict(net_drv_rss_ctx *ctx,
                    const struct sockaddr *src_addr,
                    const struct sockaddr *dst_addr,
                    unsigned int *hash_out,
                    unsigned int *idx_out,
                    unsigned int *queue_out)
{
    unsigned int idx;
    int queue;
    uint32_t hash;
    te_errno rc;

    rc = te_toeplitz_hash_sa(ctx->cache, src_addr, dst_addr,
                             ctx->hash_variant, &hash);
    if (rc != 0)
        return rc;

    if (hash_out != NULL)
        *hash_out = hash;

    if (idx_out == NULL && queue_out == NULL)
        return 0;

    idx = hash % ctx->indir_table_size;
    if (idx_out != NULL)
        *idx_out = idx;

    if (queue_out == NULL)
        return 0;

    rc = tapi_cfg_if_rss_indir_get(ctx->ta, ctx->if_name, ctx->rss_ctx,
                                   idx, &queue);
    if (rc != 0)
        return rc;

    *queue_out = queue;
    return 0;
}

/* See description in common_rss.h */
void
net_drv_rx_rules_check_table_size(const char *ta, const char *if_name,
                                  uint32_t *table_size)
{
    te_errno rc;
    uint32_t size;

    rc = tapi_cfg_rx_rule_table_size_get(ta, if_name, &size);
    if (rc != 0)
    {
        if (rc == TE_RC(TE_CS, TE_ENOENT))
            TEST_SKIP("Rx rules are not supported");
        else
            TEST_VERDICT("Failed to get size of Rx rules table: %r", rc);
    }
    else if (size == 0)
    {
        TEST_SKIP("Rx rules table has zero size");
    }

    if (table_size != NULL)
        *table_size = size;
}

/* See description in common_rss.h */
void
net_drv_rx_rules_check_spec_loc(const char *ta, const char *if_name)
{
    te_errno rc;
    te_bool spec_loc;

    rc = tapi_cfg_rx_rule_spec_loc_get(ta, if_name, &spec_loc);
    if (rc != 0)
    {
        TEST_VERDICT("Failed to check support of special insert "
                     "locations: %r", rc);
    }

    if (!spec_loc)
    {
        TEST_SKIP("Special insert locations are not supported for "
                  "Rx rules");
    }
}

/* See description in common_rss.h */
te_errno
net_drv_rx_rules_find_loc(const char *ta, const char *if_name,
                          int64_t *location)
{
    te_errno rc;
    te_bool spec_loc;

    rc = tapi_cfg_rx_rule_spec_loc_get(ta, if_name, &spec_loc);
    if (rc != 0)
        return rc;

    if (spec_loc)
    {
        *location = TAPI_CFG_RX_RULE_ANY;
        return 0;
    }

    return tapi_cfg_rx_rule_find_location(ta, if_name, 0, 0, location);
}

/* See description in common_rss.h */
void
net_drv_add_tcpudp_rx_rule(const char *ta, const char *if_name,
                           rpc_socket_type sock_type,
                           const struct sockaddr *src_addr,
                           const struct sockaddr *src_mask,
                           const struct sockaddr *dst_addr,
                           const struct sockaddr *dst_mask,
                           unsigned int queue, const char *rule_name)
{
    te_errno rc;
    int64_t location;
    tapi_cfg_rx_rule_flow flow_type;
    int af;

    if (src_addr != NULL)
    {
        af = src_addr->sa_family;
    }
    else if (dst_addr != NULL)
    {
        af = dst_addr->sa_family;
    }
    else
    {
        TEST_FAIL("%s(): both src_addr and dst_addr cannot be NULL",
                  __FUNCTION__);
    }

    flow_type = tapi_cfg_rx_rule_flow_by_socket(af, sock_type);

    rc = net_drv_rx_rules_find_loc(ta, if_name, &location);
    if (rc != 0)
    {
        if (rc == TE_RC(TE_CS, TE_ENOENT))
            TEST_SKIP("Rx rules are not supported");
        else
            TEST_VERDICT("Failed to find location for a new rule: %r", rc);
    }

    CHECK_RC(tapi_cfg_rx_rule_add(ta, if_name, location, flow_type));

    CHECK_RC(tapi_cfg_rx_rule_fill_ip_addrs_ports(
                                         ta, if_name, location,
                                         af, src_addr, src_mask,
                                         dst_addr, dst_mask));

    CHECK_RC(tapi_cfg_rx_rule_rx_queue_set(ta, if_name, location, queue));

    rc = tapi_cfg_rx_rule_commit(ta, if_name, location);
    if (rc != 0)
    {
        TEST_VERDICT("Failed to add %s%sRx rule: %r", rule_name,
                     te_str_is_null_or_empty(rule_name) ? "" : " ",
                     rc);
    }
}

/* See description in common_rss.h */
te_errno
net_drv_xdp_adjust_rx_size(const char *ta, const char *if_name,
                           net_drv_xdp_cfg *cfg)
{
    te_errno rc;
    int64_t number;

    rc = tapi_cfg_if_get_ring_size(ta, if_name, TRUE,
                                   &number);
    if (rc != 0)
        return rc;

    cfg->rx_frames = number;
    cfg->rx_size = number;
    cfg->fill_size = number;
    cfg->frames_num = number + cfg->tx_size;

    return 0;
}

/* See description in common_rss.h */
te_errno
net_drv_xdp_create_sock(rcf_rpc_server *rpcs, const char *if_name,
                        unsigned int queue_id, net_drv_xdp_cfg *cfg,
                        int map_fd, net_drv_xdp_sock *sock)
{
    int rc;
    size_t mem_len = cfg->frames_num * cfg->frame_len;
    tarpc_xsk_umem_config umem_conf = { 0, };
    tarpc_xsk_socket_config sock_conf = { 0, };
    te_bool success = FALSE;

    umem_conf.fill_size = cfg->fill_size;
    umem_conf.comp_size = cfg->comp_size;
    umem_conf.frame_size = cfg->frame_len;

    sock_conf.rx_size = cfg->rx_size;
    sock_conf.tx_size = cfg->tx_size;
    sock_conf.libxdp_flags = RPC_XSK_LIBXDP_FLAGS__INHIBIT_PROG_LOAD;
    sock_conf.bind_flags = cfg->bind_flags;

    *sock = (net_drv_xdp_sock)NET_DRV_XDP_SOCK_INIT;

    RPC_AWAIT_ERROR(rpcs);
    rc = rpc_posix_memalign(rpcs, &sock->mem, cfg->frame_len, mem_len);
    if (rc < 0)
    {
        ERROR_VERDICT("posix_memalign() failed: " RPC_ERROR_FMT,
                      RPC_ERROR_ARGS(rpcs));
        goto cleanup;
    }

    RPC_AWAIT_ERROR(rpcs);
    rc = rpc_xsk_umem__create(rpcs, sock->mem, mem_len, &umem_conf,
                              &sock->umem);
    if (rc < 0)
    {
        ERROR_VERDICT("xsk_umem__create() failed: " RPC_ERROR_FMT,
                      RPC_ERROR_ARGS(rpcs));
        goto cleanup;
    }

    RPC_AWAIT_ERROR(rpcs);
    rc = rpc_xsk_rx_fill_simple(rpcs, sock->umem, if_name, queue_id,
                                cfg->rx_frames);
    if (rc < 0)
    {
        ERROR_VERDICT("rpc_xsk_rx_fill_simple() failed: " RPC_ERROR_FMT,
                      RPC_ERROR_ARGS(rpcs));
        goto cleanup;
    }
    else if (rc == 0)
    {
        ERROR_VERDICT("rpc_xsk_rx_fill_simple() could not submit "
                      "any buffers");
        goto cleanup;
    }

    RPC_AWAIT_ERROR(rpcs);
    rc = rpc_xsk_socket__create(rpcs, if_name, queue_id,
                                sock->umem, TRUE,
                                &sock_conf, &sock->sock);
    if (rc < 0)
    {
        ERROR_VERDICT("xsk_socket__create() failed: " RPC_ERROR_FMT,
                      RPC_ERROR_ARGS(rpcs));
        goto cleanup;
    }
    sock->fd = rc;

    RPC_AWAIT_ERROR(rpcs);
    rc = rpc_xsk_map_set(rpcs, map_fd, queue_id, sock->fd);
    if (rc < 0)
    {
        ERROR_VERDICT("rpc_xsk_map_set() failed: " RPC_ERROR_FMT,
                      RPC_ERROR_ARGS(rpcs));
        goto cleanup;
    }

    success = TRUE;
cleanup:

    if (!success)
    {
        net_drv_xdp_destroy_sock(rpcs, sock);

        return TE_EFAIL;
    }

    return 0;
}

/* See description in common_rss.h */
te_errno
net_drv_xdp_destroy_sock(rcf_rpc_server *rpcs, net_drv_xdp_sock *sock)
{
    te_errno rc;

    if (sock == NULL)
        return 0;

    if (sock->sock != RPC_NULL)
    {
        RPC_AWAIT_ERROR(rpcs);
        rc = rpc_xsk_socket__delete(rpcs, sock->sock);
        if (rc < 0)
        {
            ERROR("xsk_socket__delete() failed: " RPC_ERROR_FMT,
                  RPC_ERROR_ARGS(rpcs));
            return RPC_ERRNO(rpcs);
        }

        sock->sock = RPC_NULL;
        sock->fd = -1;
    }

    if (sock->umem != RPC_NULL)
    {
        RPC_AWAIT_ERROR(rpcs);
        rc = rpc_xsk_umem__delete(rpcs, sock->umem);
        if (rc < 0)
        {
            ERROR("xsk_umem__delete() failed: " RPC_ERROR_FMT,
                  RPC_ERROR_ARGS(rpcs));
            return RPC_ERRNO(rpcs);
        }

        sock->umem = RPC_NULL;
    }

    if (sock->mem != RPC_NULL)
    {
        rpc_free(rpcs, sock->mem);
        sock->mem = RPC_NULL;
    }

    return 0;
}

/* See description in common_rss.h */
void
net_drv_xdp_create_socks(rcf_rpc_server *rpcs, const char *if_name,
                         net_drv_xdp_cfg *cfg, int map_fd,
                         unsigned int socks_num,
                         net_drv_xdp_sock **socks_out)
{
    te_errno rc;
    unsigned int i = 0;
    unsigned int j = 0;
    net_drv_xdp_sock *socks = NULL;
    te_bool success = FALSE;

    socks = tapi_calloc(socks_num, sizeof(*socks));

    for (i = 0; i < socks_num; i++)
    {
        rc = net_drv_xdp_create_sock(rpcs, if_name, i, cfg, map_fd,
                                     &socks[i]);
        if (rc != 0)
            goto cleanup;
    }

    success = TRUE;
cleanup:

    if (!success)
    {
        for (j = 0; j < i; j++)
            net_drv_xdp_destroy_sock(rpcs, &socks[j]);

        free(socks);
        TEST_STOP;
    }

    *socks_out = socks;
}

/* See description in common_rss.h */
te_errno
net_drv_xdp_destroy_socks(rcf_rpc_server *rpcs, net_drv_xdp_sock *socks,
                          unsigned int socks_num)
{
    unsigned int i;
    te_errno rc;
    te_errno res = 0;

    if (socks == NULL)
        return 0;

    for (i = 0; i < socks_num; i++)
    {
        rc = net_drv_xdp_destroy_sock(rpcs, &socks[i]);
        if (res == 0)
            res = rc;
    }

    free(socks);
    return res;
}

/* See description in common_rss.h */
void
net_drv_xdp_echo(rcf_rpc_server *rpcs_udp, int s_udp,
                 const struct sockaddr *dst_addr,
                 rcf_rpc_server *rpcs_xdp, net_drv_xdp_sock *socks,
                 unsigned int socks_num, unsigned int exp_sock)
{
#define MAX_DATA_LEN 1024
#define MAX_PKT_LEN (MAX_DATA_LEN + 200)
    uint8_t send_buf[MAX_DATA_LEN];
    uint8_t recv_buf[MAX_PKT_LEN];
    int send_len;
    int recv_len;
    int rc;
    unsigned int i;
    te_bool success = FALSE;

    struct rpc_pollfd *fds = NULL;
    te_bool readable;

    struct sockaddr_storage recv_addr;
    socklen_t recv_addr_len;

    fds = tapi_calloc(socks_num, sizeof(*fds));
    for (i = 0; i < socks_num; i++)
    {
        fds[i].fd = socks[i].fd;
        fds[i].events = RPC_POLLIN;
    }

    send_len = rand_range(1, MAX_DATA_LEN);
    te_fill_buf(send_buf, send_len);

    rc = rpc_sendto(rpcs_udp, s_udp, send_buf, send_len,
                    0, dst_addr);
    if (rc != send_len)
        TEST_FAIL("sendto() returned unexpected number");

    rc = rpc_poll(rpcs_xdp, fds, socks_num, TAPI_WAIT_NETWORK_DELAY);
    if (rc == 0)
    {
        ERROR_VERDICT("poll() did not report events for AF_XDP sockets");
        goto finish;
    }
    else if (rc > 1)
    {
        ERROR_VERDICT("poll() reported events for multiple AF_XDP sockets");
        goto finish;
    }

    for (i = 0; i < socks_num; i++)
    {
        if (fds[i].revents != 0)
            break;
    }

    if (i == socks_num)
    {
        ERROR_VERDICT("poll() returned positive number but all revents "
                      "fields are zero");
        goto finish;
    }

    if (i != exp_sock)
    {
        ERROR_VERDICT("poll() reported events for unexpected AF_XDP "
                      "socket");
        goto finish;
    }

    if (fds[i].revents != RPC_POLLIN)
    {
        ERROR_VERDICT("poll() reported unexpected events %s",
                      poll_event_rpc2str(fds[i].revents));
        goto finish;
    }

    recv_len = rpc_xsk_receive_simple(rpcs_xdp, socks[i].sock,
                                      recv_buf, sizeof(recv_buf));
    if (recv_len <= send_len)
    {
        ERROR_VERDICT("AF_XDP socket received too few bytes");
        goto finish;
    }

    rc = te_ipstack_mirror_udp_packet(recv_buf, recv_len);
    if (rc != 0)
    {
        ERROR("Failed to construct an echo response");
        goto finish;
    }

    RPC_AWAIT_ERROR(rpcs_xdp);
    rc = rpc_xsk_send_simple(rpcs_xdp, socks[i].sock,
                             recv_buf, recv_len);
    if (rc < 0)
    {
        ERROR_VERDICT("xsk_send_simple() failed with error " RPC_ERROR_FMT,
                      RPC_ERROR_ARGS(rpcs_xdp));
        goto finish;
    }

    RPC_GET_READABILITY(readable, rpcs_udp, s_udp,
                        TAPI_WAIT_NETWORK_DELAY);
    if (!readable)
    {
        ERROR_VERDICT("UDP socket did not become readable after "
                      "echo packet was sent to it");
        goto finish;
    }

    recv_addr_len = sizeof(recv_addr);
    recv_len = rpc_recvfrom(rpcs_udp, s_udp, recv_buf,
                            sizeof(recv_buf), 0,
                            SA(&recv_addr), &recv_addr_len);
    if (recv_len != send_len ||
        memcmp(recv_buf, send_buf, send_len) != 0)
    {
        ERROR_VERDICT("UDP socket got back unexpected data");
        goto finish;
    }

    if (tapi_sockaddr_cmp(dst_addr, SA(&recv_addr)) != 0)
    {
        ERROR_VERDICT("Source address of received echo packet does "
                      "not match destination address");
        goto finish;
    }

    success = TRUE;

finish:

    free(fds);

    if (!success)
        TEST_STOP;

#undef MAX_DATA_LEN
#undef MAX_PKT_LEN
}
