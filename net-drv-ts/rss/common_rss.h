/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 OKTET Labs Ltd. All rights reserved. */

/** @file
 * @brief Test API for RSS tests
 *
 * Declarations of TAPI for RSS tests.
 */

#ifndef __TS_NET_DRV_COMMON_RSS_H__
#define __TS_NET_DRV_COMMON_RSS_H__

#include "net_drv_test.h"
#include "tapi_cfg_if_rss.h"
#include "tapi_bpf.h"
#include "tapi_bpf_rxq_stats.h"
#include "te_rpc_bpf.h"
#include "te_toeplitz.h"

/**
 * Send a few packets and check whether expected Rx queue received them.
 * This function clears XDP hook statistics before sending.
 *
 * @note If both @p sender_addr and @p receiver_addr are @c NULL,
 *       the function assumes that XDP hook is already configured.
 *       If at least one of these parameters is not @c NULL,
 *       XDP hook is reconfigured to count only packets matching
 *       provided addresses and ports.
 *
 * @param sender_rpcs     Sender RPC server
 * @param sender_s        Sender socket
 * @param sender_addr     Sender address (may be @c NULL)
 * @param receiver_rpcs   Receiver RPC server
 * @param receiver_s      Receiver socket
 * @param receiver_addr   Receiver address (may be @c NULL)
 * @param sock_type       Socket type (@c RPC_SOCK_STREAM,
 *                        @c RPC_SOCK_DGRAM)
 * @param exp_queue       Expected RSS queue ID
 * @param bpf_id          Id of XDP hook used to count packets
 * @param jumbo           If @c TRUE, use bigger payload to trigger multi-seg
 * @param vpref           Prefix to use in verdicts
 *
 * @return Status code.
 */
extern te_errno net_drv_rss_send_check_stats(
                                       rcf_rpc_server *sender_rpcs,
                                       int sender_s,
                                       const struct sockaddr *sender_addr,
                                       rcf_rpc_server *receiver_rpcs,
                                       int receiver_s,
                                       const struct sockaddr *receiver_addr,
                                       rpc_socket_type sock_type,
                                       unsigned int exp_queue,
                                       unsigned int bpf_id,
                                       bool jumbo,
                                       const char *vpref);

/**
 * Send a few packets, retrieve Rx queues statistics.
 * This function clears XDP hook statistics before sending.
 *
 * @note If both @p sender_addr and @p receiver_addr are @c NULL,
 *       the function assumes that XDP hook is already configured.
 *       If at least one of these parameters is not @c NULL,
 *       XDP hook is reconfigured to count only packets matching
 *       provided addresses and ports.
 *
 * @param sender_rpcs     Sender RPC server
 * @param sender_s        Sender socket
 * @param sender_addr     Sender address (may be @c NULL)
 * @param receiver_rpcs   Receiver RPC server
 * @param receiver_s      Receiver socket
 * @param receiver_addr   Receiver address (may be @c NULL)
 * @param sock_type       Socket type (@c RPC_SOCK_STREAM,
 *                        @c RPC_SOCK_DGRAM)
 * @param bpf_id          Id of XDP hook used to count packets
 * @param jumbo           If @c TRUE, use bigger payload to trigger multi-seg
 * @param stats           Array with retrieved statistics
 * @param stats_count     Number of elements in the retrieved array
 * @param vpref           Prefix to use in verdicts
 *
 * @return Status code.
 */
extern te_errno net_drv_rss_send_get_stats(
                                       rcf_rpc_server *sender_rpcs,
                                       int sender_s,
                                       const struct sockaddr *sender_addr,
                                       rcf_rpc_server *receiver_rpcs,
                                       int receiver_s,
                                       const struct sockaddr *receiver_addr,
                                       rpc_socket_type sock_type,
                                       unsigned int bpf_id,
                                       bool jumbo,
                                       tapi_bpf_rxq_stats **stats,
                                       unsigned int *stats_count,
                                       const char *vpref);

/**
 * Check whether expected RSS hash function is enabled; if not,
 * try to change configuration.
 *
 * @param ta            Test Agent name
 * @param if_name       Network interface name
 * @param rss_context   RSS context
 * @param func_name     Function name
 *
 * @return Status code.
 */
extern te_errno net_drv_rss_check_set_hfunc(const char *ta,
                                            const char *if_name,
                                            unsigned int rss_context,
                                            const char *func_name);

/**
 * Make sure that specific RSS hash function is used.
 *
 * @param _ta           Test Agent name
 * @param _if_name      Interface name
 * @param _rss_ctx      RSS context
 * @param _func_name    Function name
 * @param _term_macro   Macro used to terminate the test if requested
 *                      function is not known or not supported
 *                      (TEST_SUCCESS, TEST_STOP, etc)
 */
#define NET_DRV_RSS_CHECK_SET_HFUNC(_ta, _if_name, _rss_ctx, _func_name, \
                                    _term_macro) \
    do {                                                            \
        te_errno _rc;                                               \
                                                                    \
        _rc = net_drv_rss_check_set_hfunc(_ta, _if_name, _rss_ctx,  \
                                          _func_name);              \
        if (_rc != 0)                                               \
        {                                                           \
            if (_rc == TE_RC(TE_TAPI, TE_ENOENT))                   \
            {                                                       \
                RING_VERDICT("Hash function %s is not known",       \
                             _func_name);                           \
                _term_macro;                                        \
            }                                                       \
            else if (_rc == TE_RC(TE_TAPI, TE_EOPNOTSUPP))          \
            {                                                       \
                RING_VERDICT("Hash function %s is not supported",   \
                             _func_name);                           \
                _term_macro;                                        \
            }                                                       \
            else                                                    \
            {                                                       \
                TEST_STOP;                                          \
            }                                                       \
        }                                                           \
    } while (0)

/**
 * Get RSS hash key.
 *
 * @param ta            Test Agent name
 * @param if_name       Network interface name
 * @param rss_context   RSS context
 * @param key           Where to save pointer to hash key
 *                      (should be released by caller)
 * @param len           Where to save key length
 */
static inline void
net_drv_rss_get_check_hkey(const char *ta, const char *if_name,
                           unsigned int rss_context, uint8_t **key,
                           size_t *len)
{
    te_errno rc;

    rc = tapi_cfg_if_rss_hash_key_get(ta, if_name, rss_context,
                                      key, len);
    if (rc != 0)
    {
        if (rc == TE_RC(TE_CS, TE_EOPNOTSUPP))
            TEST_SKIP("Getting RSS hash key is not supported");
        else
            TEST_VERDICT("Failed to get RSS hash key, rc=%r", rc);
    }

    RING("RSS hash key: %Tm", *key, *len);
}

/** Helper structure for RSS tests */
typedef struct net_drv_rss_ctx {
    const char *ta; /**< Test Agent name */
    const char *if_name; /**< Interface name */
    unsigned int rss_ctx; /**< RSS context id */
    unsigned int indir_table_size; /**< Size of indirection table */
    unsigned int rx_queues; /**< Number of Rx queues */
    uint8_t *hash_key; /**< Hash key */
    size_t key_len; /**< Length of hash key */
    te_toeplitz_hash_cache *cache; /**< Cache used for computing Toeplitz
                                        hash */
    te_toeplitz_hash_variant hash_variant; /**< Variant of Toeplitz
                                                algorithm to use */
} net_drv_rss_ctx;

/** Initializer for net_drv_rss_ctx */
#define NET_DRV_RSS_CTX_INIT \
    {                                                                     \
        .ta = "", .if_name = "", .rss_ctx = 0, .indir_table_size = 0,     \
        .rx_queues = 0, .hash_key = NULL, .key_len = 0, .cache = NULL,    \
        .hash_variant = TE_TOEPLITZ_HASH_STANDARD                         \
    }

/**
 * Fill fields of RSS test context.
 *
 * @note This function can print verdict and jump to cleanup.
 *
 * @param ctx       Context structure to initialize
 * @param ta        Test agent name
 * @param if_name   Interface name
 * @param rss_ctx   RSS context id
 */
extern void net_drv_rss_ctx_prepare(net_drv_rss_ctx *ctx,
                                    const char *ta,
                                    const char *if_name,
                                    unsigned int rss_ctx);

/**
 * Release resources allocated for RSS test context.
 *
 * @param ctx     Pointer to context structure
 */
extern void net_drv_rss_ctx_release(net_drv_rss_ctx *ctx);

/**
 * Change hash key used for Toeplitz hash computation.
 *
 * @param ctx       Pointer to context structure
 * @param hash_key  New hash key
 * @param key_len   Key length
 *
 * @return Status code.
 */
extern te_errno net_drv_rss_ctx_change_key(net_drv_rss_ctx *ctx,
                                           uint8_t *hash_key,
                                           unsigned int key_len);

/**
 * Predict Toeplitz hash value, indirection table index and
 * Rx queue id for given pairs of addresses/ports.
 *
 * @param ctx         RSS test context
 * @param src_addr    Source address/port
 * @param dst_addr    Destination address/port
 * @param hash_out    Where to save hash value (may be @c NULL)
 * @param idx_out     Where to save indirection table index (may be @c NULL)
 * @param queue_out   Where to save Rx queue id (may be @c NULL)
 *
 * @return Status code.
 */
extern te_errno net_drv_rss_predict(net_drv_rss_ctx *ctx,
                                    const struct sockaddr *src_addr,
                                    const struct sockaddr *dst_addr,
                                    unsigned int *hash_out,
                                    unsigned int *idx_out,
                                    unsigned int *queue_out);

/**
 * List of values for rule location test parameter, to be used together
 * with TEST_GET_ENUM_PARAM().
 */
#define NET_DRV_RX_RULE_LOC \
    { "first", TAPI_CFG_RX_RULE_FIRST },      \
    { "last", TAPI_CFG_RX_RULE_LAST },        \
    { "any", TAPI_CFG_RX_RULE_ANY },          \
    { "specific", 0 }

/**
 * Check that size of Rx rules table can be read and it is not zero.
 * Stop the test if this check fails.
 *
 * @param ta          Test Agent name
 * @param if_name     Interface name
 * @param table_size  Retrieved table size (may be @c NULL)
 */
extern void net_drv_rx_rules_check_table_size(const char *ta,
                                              const char *if_name,
                                              uint32_t *table_size);

/**
 * Check that special insert locations are supported.
 * Skip the test if it is not the case.
 *
 * @param ta          Test Agent name
 * @param if_name     Interface name
 */
extern void net_drv_rx_rules_check_spec_loc(const char *ta,
                                            const char *if_name);

/**
 * Find appropriate location for a new rule, prefering
 * TAPI_CFG_RX_RULE_ANY if special insert locations are
 * supported.
 *
 * @param ta          Test Agent name
 * @param if_name     Interface name
 * @param location    Found location
 *
 * @return Status code.
 */
extern te_errno net_drv_rx_rules_find_loc(const char *ta,
                                          const char *if_name,
                                          int64_t *location);

/**
 * Add a new TCP/UDP Rx classification rule at any available location.
 *
 * @param ta          Test Agent name
 * @param if_name     Interface name
 * @param sock_type   Socket type (@c RPC_SOCK_STREAM or @c RPC_SOCK_DGRAM)
 * @param src_addr    Source address (may be @c NULL)
 * @param src_mask    Source address mask (may be @c NULL)
 * @param dst_addr    Destination address (may be @c NULL)
 * @param dst_mask    Destination address mask (may be @c NULL)
 * @param queue       Rx queue to which to redirect packets
 * @param rule_name   Rule name to print in verdicts (may be empty)
 */
extern void net_drv_add_tcpudp_rx_rule(const char *ta, const char *if_name,
                                       rpc_socket_type sock_type,
                                       const struct sockaddr *src_addr,
                                       const struct sockaddr *src_mask,
                                       const struct sockaddr *dst_addr,
                                       const struct sockaddr *dst_mask,
                                       unsigned int queue,
                                       const char *rule_name);

/** Structure describing AF_XDP socket */
typedef struct net_drv_xdp_sock {
    /** Queue ID used to bind this socket */
    unsigned int queue_id;
    /** Whether this socket queue has Tx capability */
    bool tx_capable;
    /** Memory allocated for UMEM */
    rpc_ptr mem;
    /** Pointer to UMEM structure on TA */
    rpc_ptr umem;
    /** Pointer to socket structure on TA */
    rpc_ptr sock;
    /** Socket FD */
    int fd;
} net_drv_xdp_sock;

/** Initializer for net_drv_xdp_sock */
#define NET_DRV_XDP_SOCK_INIT \
    {                         \
        .queue_id = 0,        \
        .tx_capable = false,  \
        .mem = RPC_NULL,      \
        .umem = RPC_NULL,     \
        .sock = RPC_NULL,     \
        .fd = -1,             \
    }

/** Configuration parameters for AF_XDP sockets */
typedef struct net_drv_xdp_cfg {
    /** Frame length */
    unsigned int frame_len;
    /** Number of frames in UMEM */
    size_t frames_num;
    /** Frames reserved for receiving */
    unsigned int rx_frames;
    /** Size of FILL ring */
    unsigned int fill_size;
    /** Size of COMPLETION ring */
    unsigned int comp_size;
    /** Size of Rx ring */
    unsigned int rx_size;
    /** Size of Tx ring */
    unsigned int tx_size;
    /** Flags passed when binding AF_XDP socket */
    uint32_t bind_flags;
} net_drv_xdp_cfg;

/** Default configuration for AF_XDP sockets */
#define NET_DRV_XDP_CFG_DEF \
    {                         \
        .frame_len = 1 << 12, \
        .frames_num = 128,    \
        .rx_frames = 64,      \
        .fill_size = 128,     \
        .comp_size = 128,     \
        .rx_size = 128,       \
        .tx_size = 128        \
    }

/** List of possible XDP copy mode flags for TEST_GET_ENUM_PARAM() */
#define NET_DRV_XDP_COPY_MODE \
    { "none", 0 },                          \
    { "copy", RPC_XDP_BIND_COPY },          \
    { "zerocopy", RPC_XDP_BIND_ZEROCOPY }

/** Wait until AF_XDP sockets are fully configured */
#define NET_DRV_XDP_WAIT_SOCKS \
    do {                                                              \
        te_motivated_msleep(500, "make sure that AF_XDP sockets "     \
                            "are ready to receive data");             \
    } while (0)

/**
 * Adjust number of Rx frames and size of Rx ring taking
 * into account current size of Rx ring. Intel NICs drivers
 * (X710, E810) do not work correctly unless we add enough
 * buffers to FILL ring.
 *
 * @param ta        Test Agent name.
 * @param if_name   Interface name.
 * @param cfg       XDP configuration structure where to adjust fields.
 *
 * @return Status code.
 */
extern te_errno net_drv_xdp_adjust_rx_size(const char *ta,
                                           const char *if_name,
                                           net_drv_xdp_cfg *cfg);

/**
 * Get current total number of Rx and Tx queues for a network interface.
 * The totals include dedicated and combined channels.
 *
 * @param ta            Test Agent name.
 * @param if_name       Interface name.
 * @param rx_queues     Where to save total Rx queues number.
 * @param tx_queues     Where to save total Tx queues number.
 *
 * @return Status code.
 */
extern te_errno net_drv_xdp_get_queues_num(const char *ta,
                                           const char *if_name,
                                           unsigned int *rx_queues,
                                           unsigned int *tx_queues);

/**
 * Create and configure AF_XDP socket.
 *
 * @param rpcs        RPC server
 * @param if_name     Interface name
 * @param queue_id    Rx queue index
 * @param cfg         Configuration parameters for UMEM and socket
 * @param map_fd      XSK map file descriptor
 * @param sock        Created socket data
 *
 * @return Status code.
 */
extern te_errno net_drv_xdp_create_sock(rcf_rpc_server *rpcs,
                                        const char *if_name,
                                        unsigned int queue_id,
                                        net_drv_xdp_cfg *cfg, int map_fd,
                                        net_drv_xdp_sock *sock);

/**
 * Destroy AF_XDP socket created with net_drv_xdp_create_sock().
 *
 * @param rpcs        RPC server
 * @param sock        Pointer to socket structure
 *
 * @return Status code.
 */
extern te_errno net_drv_xdp_destroy_sock(rcf_rpc_server *rpcs,
                                         net_drv_xdp_sock *sock);

/**
 * Create array of AF_XDP sockets. For every socket use Rx queue of the
 * same index and set entry of XSK map with the same index to its file
 * descriptor.
 *
 * @note This function prints verdict and exits in case of failure.
 *
 * @param rpcs        RPC server
 * @param if_name     Interface name
 * @param cfg         Configuration parameters for UMEM and socket
 * @param map_fd      XSK map file descriptor
 * @param socks_num   Number of sockets to create
 * @param socks_out   Created sockets array
 */
extern void net_drv_xdp_create_socks(rcf_rpc_server *rpcs,
                                     const char *if_name,
                                     net_drv_xdp_cfg *cfg, int map_fd,
                                     unsigned int socks_num,
                                     net_drv_xdp_sock **socks_out);

/**
 * Destroy array of AF_XDP sockets created with net_drv_xdp_create_socks().
 *
 * @param rpcs        RPC server
 * @param socks       Array of sockets
 * @param socks_num   Number of sockets
 *
 * @return Status code.
 */
extern te_errno net_drv_xdp_destroy_socks(rcf_rpc_server *rpcs,
                                          net_drv_xdp_sock *socks,
                                          unsigned int socks_num);

/**
 * Send a packet from a UDP socket. Check that the expected AF_XDP socket
 * gets that packet on a peer. Construct a reply by inverting addresses
 * and ports of the received packet. Send it back to the UDP socket over
 * the same AF_XDP socket if it has Tx capability, otherwise use another
 * Tx-capable AF_XDP socket. Check that the UDP socket gets it and its
 * payload is the same as in the previously sent packet.
 *
 * @note This function can print verdict and fail the test.
 *
 * @param rpcs_udp            RPC server with UDP socket
 * @param s_udp               UDP socket
 * @param dst_addr            Destination address and port
 * @param rpcs_xdp            RPC server with AF_XDP sockets
 * @param socks               Array of AF_XDP sockets
 * @param socks_num           Number of sockets in the array
 * @param exp_socket          Index of the AF_XDP socket which should
 *                            receive the packet
 */
extern void net_drv_xdp_echo(rcf_rpc_server *rpcs_udp, int s_udp,
                             const struct sockaddr *dst_addr,
                             rcf_rpc_server *rpcs_xdp,
                             net_drv_xdp_sock *socks,
                             unsigned int socks_num, unsigned int exp_sock,
                             bool jumbo);

#endif /* !__TS_NET_DRV_COMMON_RSS_H__ */
