/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 OKTET Labs Ltd. All rights reserved. */

/** @file
 * @brief Test Suite prologue
 *
 * Prologue for RSS tests.
 */

#ifndef DOXYGEN_TEST_SPEC

/** Logging subsystem entity name */
#define TE_TEST_NAME    "rss/prologue"

#include "net_drv_test.h"
#include "te_toeplitz.h"
#include "tapi_bpf.h"
#include "tapi_cfg_if_rss.h"
#include "tapi_bpf_rxq_stats.h"
#include "tapi_cfg_if.h"
#include "common_rss.h"

#define RSS_MULTI_SEG_MTU 9000

/* Send some packets and find out which Rx queue received them. */
static void
send_get_rx_queue(rcf_rpc_server *sender_rpcs, int sender_s,
                  rcf_rpc_server *receiver_rpcs, int receiver_s,
                  unsigned int bpf_id, bool jumbo, unsigned int *queue)
{
    unsigned int i;
    static unsigned int pkts_num = 10;

    tapi_bpf_rxq_stats *stats = NULL;
    unsigned int stats_count;
    long int got_queue = -1;
    unsigned int reg_pkts = 0;

    CHECK_RC(tapi_bpf_rxq_stats_clear(receiver_rpcs->ta, bpf_id));

    for (i = 0; i < pkts_num; i++)
    {
        net_drv_send_recv_ext_check(sender_rpcs, sender_s,
                                    receiver_rpcs, receiver_s,
                                    jumbo, "");
    }

    CHECK_RC(tapi_bpf_rxq_stats_read(receiver_rpcs->ta, bpf_id, &stats,
                                     &stats_count));

    tapi_bpf_rxq_stats_print(NULL, stats, stats_count);

    for (i = 0; i < stats_count; i++)
    {
        if (stats[i].pkts > 0)
        {
            if (got_queue >= 0)
                TEST_VERDICT("Packets were received over more than one queue");
            else
                got_queue = stats[i].rx_queue;

            reg_pkts += stats[i].pkts;
        }
    }

    if (reg_pkts == 0)
    {
        TEST_VERDICT("No packets was registered for any Rx queue");
    }
    else if (reg_pkts != pkts_num)
    {
        ERROR("%u packets were registered instead of %u",
              reg_pkts, pkts_num);
        TEST_VERDICT("Unexpected number of packets was registered");
    }

    *queue = got_queue;
}

/*
 * Make sure that indirection table mentions more than one Rx queue,
 * so that different RSS hash values can lead to selection of
 * different Rx queues.
 */
static void
check_fix_indir_table(const char *ta, const char *if_name,
                      unsigned int rx_queues, unsigned int table_size)
{
    int first_entry = -1;
    int entry;
    te_bool fix_indir_table = TRUE;
    unsigned int i;

    CHECK_RC(tapi_cfg_if_rss_indir_get(ta, if_name, 0, 0,
                                       &first_entry));
    for (i = 1; i < table_size; i++)
    {
        CHECK_RC(tapi_cfg_if_rss_indir_get(ta, if_name, 0, i,
                                           &entry));

        if (first_entry != entry)
        {
            fix_indir_table = FALSE;
            break;
        }
    }

    if (fix_indir_table)
    {
        CHECK_RC(tapi_cfg_if_rss_fill_indir_table(ta, if_name, 0,
                                                  0, rx_queues - 1));
        CHECK_RC(tapi_cfg_if_rss_hash_indir_commit(ta, if_name, 0));
    }

    CHECK_RC(tapi_cfg_if_rss_print_indir_table(ta, if_name, 0));
}

/*
 * Create a single connection which should be mapped to different Rx
 * queues depending on whether standard Toeplitz hash is used or "symmetric
 * Toeplitz". Find out which algorithm variant points to the actually
 * used queue.
 */
static void
try_detect_toeplitz_variant(rcf_rpc_server *iut_rpcs, rcf_rpc_server *tst_rpcs,
                            const struct sockaddr *iut_addr,
                            const struct sockaddr *tst_addr,
                            const char *iut_if_name, int *iut_s, int *tst_s,
                            unsigned int bpf_id, unsigned int table_size,
                            te_toeplitz_hash_cache *cache,
                            bool jumbo,
                            te_toeplitz_hash_variant *detected)
{
    struct sockaddr_storage new_iut_addr_st;
    struct sockaddr_storage new_tst_addr_st;
    struct sockaddr *new_iut_addr = NULL;
    struct sockaddr *new_tst_addr = NULL;
    uint16_t iut_port = 0;
    uint16_t tst_port = 0;
    static const unsigned int max_attempts = 10000;
    static const unsigned int min_port = 20000;

    unsigned int hash_std;
    unsigned int hash_sym;
    unsigned int idx_std;
    unsigned int idx_sym;
    int queue_std;
    int queue_sym;
    unsigned int queue_got;
    unsigned int i;

    tapi_sockaddr_clone_exact(iut_addr, &new_iut_addr_st);
    tapi_sockaddr_clone_exact(tst_addr, &new_tst_addr_st);
    new_iut_addr = SA(&new_iut_addr_st);
    new_tst_addr = SA(&new_tst_addr_st);

    for (i = 0; i < max_attempts; i++)
    {
        iut_port = rand_range(min_port, 0xffff);
        tst_port = rand_range(min_port, 0xffff);
        te_sockaddr_set_port(new_iut_addr, htons(iut_port));
        te_sockaddr_set_port(new_tst_addr, htons(tst_port));

        CHECK_RC(te_toeplitz_hash_sa(cache, new_tst_addr, new_iut_addr,
                                     TE_TOEPLITZ_HASH_STANDARD, &hash_std));
        CHECK_RC(te_toeplitz_hash_sa(cache, new_tst_addr, new_iut_addr,
                                     TE_TOEPLITZ_HASH_SYM_OR_XOR,
                                     &hash_sym));
        idx_std = hash_std % table_size;
        idx_sym = hash_sym % table_size;

        if (idx_std != idx_sym)
        {
            CHECK_RC(tapi_cfg_if_rss_indir_get(iut_rpcs->ta,
                                               iut_if_name, 0, idx_std,
                                               &queue_std));

            CHECK_RC(tapi_cfg_if_rss_indir_get(iut_rpcs->ta,
                                               iut_if_name, 0, idx_sym,
                                               &queue_sym));

            if (queue_std != queue_sym)
            {
                if (iut_port == 0 ||
                    (rpc_check_port_is_free(iut_rpcs, iut_port) &&
                     rpc_check_port_is_free(tst_rpcs, tst_port)))
                    break;
            }
        }
    }

    GEN_CONNECTION(iut_rpcs, tst_rpcs, RPC_SOCK_DGRAM, RPC_PROTO_DEF,
                   new_iut_addr, new_tst_addr, iut_s, tst_s);

    CHECK_RC(tapi_bpf_rxq_stats_reset(iut_rpcs->ta, bpf_id));
    CHECK_RC(
        tapi_bpf_rxq_stats_set_params(
             iut_rpcs->ta, bpf_id, iut_addr->sa_family,
             new_tst_addr, new_iut_addr,
             IPPROTO_UDP, TRUE));

    RING("Standard Toeplitz hash should be equal to %u, mapping into "
         "indirection table entry %u, where queue %d is specified",
         hash_std, idx_std, queue_std);
    RING("Symmetric Toeplitz hash should be equal to %u, mapping "
         "into indirection table entry %u, where queue %d is specified",
         hash_sym, idx_sym, queue_sym);

    send_get_rx_queue(tst_rpcs, *tst_s, iut_rpcs, *iut_s,
                      bpf_id, jumbo, &queue_got);

    if ((int)queue_got == queue_std)
    {
        RING("Rx queue defined by standard Toeplitz hash matches");
        *detected = TE_TOEPLITZ_HASH_STANDARD;
    }
    else if ((int)queue_got == queue_sym)
    {
        RING("Rx queue defined by symmetric-or-xor Toeplitz hash matches");
        *detected = TE_TOEPLITZ_HASH_SYM_OR_XOR;
    }
    else
    {
        TEST_VERDICT("Cannot detect Toeplitz hash variant");
    }

    RPC_CLOSE(iut_rpcs, *iut_s);
    RPC_CLOSE(tst_rpcs, *tst_s);
}

/*
 * Determine whether standard Toeplitz hash is used or "symmetric
 * Toeplitz".
 */
static void
detect_toeplitz_variant(rcf_rpc_server *iut_rpcs, rcf_rpc_server *tst_rpcs,
                        const struct sockaddr *iut_addr,
                        const struct sockaddr *tst_addr,
                        const char *iut_if_name, int *iut_s, int *tst_s,
                        unsigned int bpf_id, unsigned int table_size,
                        te_toeplitz_hash_cache *cache, bool jumbo)
{
    static const unsigned int attempts_num = 10;
    unsigned int i;
    te_toeplitz_hash_variant prev_variant;
    te_toeplitz_hash_variant variant;
    cfg_obj_descr obj_descr;

    /*
     * Make multiple attempts to determine used algorithm to make
     * improbable the possibility that some unknown algorithm
     * variant is used that makes by chance the same predictions
     * about Rx queues for tested connections as one of the known
     * algorithm variants.
     */
    for (i = 0; i < attempts_num; i++)
    {
        try_detect_toeplitz_variant(iut_rpcs, tst_rpcs, iut_addr, tst_addr,
                                    iut_if_name, iut_s, tst_s, bpf_id,
                                    table_size, cache, jumbo, &variant);

        if (i > 0 && prev_variant != variant)
            TEST_VERDICT("Cannot detect Toeplitz hash variant");

        prev_variant = variant;
    }

    if (variant == TE_TOEPLITZ_HASH_SYM_OR_XOR)
        RING_VERDICT("NIC uses symmetric-or-xor Toeplitz hash");

    /*
     * Save information about detected Toeplitz hash variant in
     * configuration tree where other tests can read it.
     */

    memset(&obj_descr, 0, sizeof(obj_descr));
    obj_descr.type = CVT_INT32;
    obj_descr.access = CFG_READ_CREATE;
    obj_descr.def_val = NULL;

    CHECK_RC(cfg_register_object_str("/local/iut_toeplitz_variant",
                                     &obj_descr, NULL));
    CHECK_RC(cfg_add_instance_fmt(NULL, CFG_VAL(INT32, variant),
                                  "/local:/iut_toeplitz_variant:"));
}

int
main(int argc, char **argv)
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;
    const struct if_nameindex *tst_if = NULL;
    const struct sockaddr *iut_addr = NULL;
    const struct sockaddr *tst_addr = NULL;

    int iut_s = -1;
    int tst_s = -1;

    uint8_t *hash_key = NULL;
    size_t key_len;
    te_toeplitz_hash_cache *cache = NULL;

    unsigned int bpf_id;
    int rx_queues;
    unsigned int table_size;
    bool multi_seg;
    tapi_bpf_rxq_stats_attach_mode attach_mode;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_IF(tst_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_BOOL_PARAM(multi_seg);

    /*
     * Try to disable flow-director-atr private flag if it is
     * present. "Application Targeted Routing" on Intel NICs
     * interferes with RSS hash indirection for TCP connections,
     * it should be disabled for RSS tests.
     */
    rc = tapi_cfg_if_priv_flag_set(iut_rpcs->ta, iut_if->if_name,
                                   "flow-director-atr", FALSE);
    if (rc != 0 && rc != TE_RC(TE_CS, TE_ENOENT))
    {
        TEST_VERDICT("Attempt to disable flow-director-atr private "
                     "flag failed unexpectedly: %r", rc);
    }

    if (multi_seg)
    {
        net_drv_set_mtu(iut_rpcs->ta, iut_if->if_name, RSS_MULTI_SEG_MTU,
                        "IUT interface");
        net_drv_set_mtu(tst_rpcs->ta, tst_if->if_name, RSS_MULTI_SEG_MTU,
                        "Tester interface");

        CHECK_RC(cfg_synchronize_fmt(true, "/agent:%s/interface:%s",
                                     iut_rpcs->ta, iut_if->if_name));
    }

    /*
     * There should be more than one Rx queue and more than one
     * indirection table entry to see whether RSS hash indirection
     * works.
     */

    rc = tapi_cfg_if_rss_rx_queues_get(iut_rpcs->ta, iut_if->if_name,
                                       &rx_queues);
    if (rc != 0)
    {
        if (rc == TE_RC(TE_CS, TE_ENOENT))
            TEST_SKIP("RSS is not supported for IUT interface");
        else
            TEST_STOP;
    }
    if (rx_queues <= 1)
        TEST_SKIP("There is no multiple Rx queues");

    CHECK_RC(tapi_cfg_if_rss_indir_table_size(iut_rpcs->ta,
                                              iut_if->if_name, 0,
                                              &table_size));
    if (table_size <= 1)
        TEST_SKIP("There is no multiple indirection table entries");

    /* Only Toeplitz hash is supported in the tests currently */
    NET_DRV_RSS_CHECK_SET_HFUNC(iut_rpcs->ta, iut_if->if_name, 0,
                                "toeplitz", TEST_STOP);

    /* Indirection table should mention more than one Rx queue */
    check_fix_indir_table(iut_rpcs->ta, iut_if->if_name,
                          rx_queues, table_size);

    net_drv_rss_get_check_hkey(iut_rpcs->ta, iut_if->if_name, 0,
                               &hash_key, &key_len);
    CHECK_NOT_NULL(cache = te_toeplitz_cache_init_size(hash_key,
                                                       key_len));

    /* Link XDP hook used to get per-queue statistics. */
    attach_mode = multi_seg ? TAPI_BPF_RXQ_STATS_ATTACH_MODE_FRAGS :
                              TAPI_BPF_RXQ_STATS_ATTACH_MODE_REGULAR;
    rc = tapi_bpf_rxq_stats_init_mode(iut_rpcs->ta, iut_if->if_name,
                                      "rss_bpf", attach_mode, &bpf_id);
    if (rc != 0)
    {
        if (multi_seg)
            TEST_SKIP("Multi-segment XDP support is missing: %r", rc);

        CHECK_RC(rc);
    }

    if (multi_seg)
        CFG_WAIT_CHANGES;

    /*
     * On balin-x710 configuration (Intel X710 NIC) IUT may not receive UDP
     * packet sent from Tester in the next step without this delay.
     */
    te_motivated_sleep(2, "waiting after linking XDP hook helps to "
                       "avoid problems with receiving packets");

    /* Detect whether nonstandard Toeplitz hash variant is used */
    detect_toeplitz_variant(iut_rpcs, tst_rpcs, iut_addr, tst_addr,
                            iut_if->if_name, &iut_s, &tst_s, bpf_id,
                            table_size, cache, multi_seg);

    CHECK_RC(cfg_synchronize("/:", TRUE));

    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);

    te_toeplitz_hash_fini(cache);

    TEST_END;
}

#endif
