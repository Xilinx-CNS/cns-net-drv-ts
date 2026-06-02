/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2026 OKTET Labs Ltd. All rights reserved. */
/*
 * Net Driver Test Suite
 * Ethtool tests
 */

/**
 * @defgroup ethtool-tx_coalesce Setting Tx coalescing parameters
 * @ingroup ethtool
 * @{
 *
 * @objective Check that Tx interrupt coalescing parameters are applied
 *            on a tested interface.
 *
 * @param env                     Testing environment:
 *                                - @ref env-iut_only
 * @param profile                 Tx coalescing settings profile:
 *                                - @c disabled
 *                                - @c time_only
 *                                - @c frames_only
 *                                - @c time_frames
 *                                - @c irq
 *                                - @c irq_usecs
 *                                - @c irq_frames
 *                                - @c adaptive
 *                                - @c adaptive_low_time
 *                                - @c adaptive_low_frames
 *                                - @c adaptive_high_time
 *                                - @c adaptive_high_frames
 *                                - @c adaptive_rate
 *                                - @c invalid
 * @param per_queue               Whether to set per-queue coalescing.
 *
 * @par Scenario:
 *
 * @author Denis Pryazhennikov <denis.pryazhennikov@oktetlabs.ru>
 */

#define TE_TEST_NAME "ethtool/tx_coalesce"

#include "net_drv_test.h"
#include "tapi_cfg_if_coalesce.h"
#include "te_enum.h"

typedef struct test_coalesce_param {
    const char *name;
    uint64_t value;
    uint64_t prev_value;
} test_coalesce_param;

typedef enum test_profile {
    TEST_PROFILE_DISABLED,
    TEST_PROFILE_TIME_ONLY,
    TEST_PROFILE_FRAMES_ONLY,
    TEST_PROFILE_TIME_FRAMES,
    TEST_PROFILE_IRQ,
    TEST_PROFILE_IRQ_USECS,
    TEST_PROFILE_IRQ_FRAMES,
    TEST_PROFILE_ADAPTIVE,
    TEST_PROFILE_ADAPTIVE_LOW_TIME,
    TEST_PROFILE_ADAPTIVE_LOW_FRAMES,
    TEST_PROFILE_ADAPTIVE_HIGH_TIME,
    TEST_PROFILE_ADAPTIVE_HIGH_FRAMES,
    TEST_PROFILE_ADAPTIVE_RATE,
    TEST_PROFILE_INVALID,
} test_profile;

#define TEST_PROFILES \
    { "disabled", TEST_PROFILE_DISABLED },                          \
    { "time_only", TEST_PROFILE_TIME_ONLY },                        \
    { "frames_only", TEST_PROFILE_FRAMES_ONLY },                    \
    { "time_frames", TEST_PROFILE_TIME_FRAMES },                    \
    { "irq", TEST_PROFILE_IRQ },                                    \
    { "irq_usecs", TEST_PROFILE_IRQ_USECS },                        \
    { "irq_frames", TEST_PROFILE_IRQ_FRAMES },                      \
    { "adaptive", TEST_PROFILE_ADAPTIVE },                          \
    { "adaptive_low_time", TEST_PROFILE_ADAPTIVE_LOW_TIME },        \
    { "adaptive_low_frames", TEST_PROFILE_ADAPTIVE_LOW_FRAMES },    \
    { "adaptive_high_time", TEST_PROFILE_ADAPTIVE_HIGH_TIME },      \
    { "adaptive_high_frames", TEST_PROFILE_ADAPTIVE_HIGH_FRAMES },  \
    { "adaptive_rate", TEST_PROFILE_ADAPTIVE_RATE },                \
    { "invalid", TEST_PROFILE_INVALID }

static const te_enum_map test_profile_map[] = {
    TEST_PROFILES,
    TE_ENUM_MAP_END,
};

#define TEST_COALESCE_PARAM(_name, _value) \
    {                                      \
        .name = (_name),                   \
        .value = (_value),                 \
        .prev_value = 0,                   \
    }

static test_coalesce_param disabled_params[] = {
    TEST_COALESCE_PARAM("tx_coalesce_usecs", 0),
    TEST_COALESCE_PARAM("tx_max_coalesced_frames", 1),
};

static test_coalesce_param time_only_params[] = {
    TEST_COALESCE_PARAM("tx_coalesce_usecs", 100),
    TEST_COALESCE_PARAM("tx_max_coalesced_frames", 0),
};

static test_coalesce_param frames_only_params[] = {
    TEST_COALESCE_PARAM("tx_coalesce_usecs", 0),
    TEST_COALESCE_PARAM("tx_max_coalesced_frames", 16),
};

static test_coalesce_param time_frames_params[] = {
    TEST_COALESCE_PARAM("tx_coalesce_usecs", 100),
    TEST_COALESCE_PARAM("tx_max_coalesced_frames", 16),
};

static test_coalesce_param irq_params[] = {
    TEST_COALESCE_PARAM("tx_coalesce_usecs_irq", 50),
    TEST_COALESCE_PARAM("tx_max_coalesced_frames_irq", 8),
};

static test_coalesce_param irq_usecs_params[] = {
    TEST_COALESCE_PARAM("tx_coalesce_usecs_irq", 50),
};

static test_coalesce_param irq_frames_params[] = {
    TEST_COALESCE_PARAM("tx_max_coalesced_frames_irq", 8),
};

static test_coalesce_param adaptive_params[] = {
    TEST_COALESCE_PARAM("use_adaptive_tx_coalesce", 1),
};

static test_coalesce_param adaptive_low_time_params[] = {
    TEST_COALESCE_PARAM("tx_coalesce_usecs_low", 10),
    TEST_COALESCE_PARAM("use_adaptive_tx_coalesce", 1),
};

static test_coalesce_param adaptive_low_frames_params[] = {
    TEST_COALESCE_PARAM("tx_max_coalesced_frames_low", 1),
    TEST_COALESCE_PARAM("use_adaptive_tx_coalesce", 1),
};

static test_coalesce_param adaptive_high_time_params[] = {
    TEST_COALESCE_PARAM("tx_coalesce_usecs_high", 200),
    TEST_COALESCE_PARAM("use_adaptive_tx_coalesce", 1),
};

static test_coalesce_param adaptive_high_frames_params[] = {
    TEST_COALESCE_PARAM("tx_max_coalesced_frames_high", 32),
    TEST_COALESCE_PARAM("use_adaptive_tx_coalesce", 1),
};

static test_coalesce_param adaptive_rate_params[] = {
    TEST_COALESCE_PARAM("pkt_rate_low", 1000),
    TEST_COALESCE_PARAM("pkt_rate_high", 10000),
    TEST_COALESCE_PARAM("rate_sample_interval", 1),
    TEST_COALESCE_PARAM("use_adaptive_tx_coalesce", 1),
};

static test_coalesce_param invalid_params[] = {
    TEST_COALESCE_PARAM("tx_coalesce_usecs", 0),
    TEST_COALESCE_PARAM("tx_max_coalesced_frames", 0),
};

static void
dump_profile_params(int profile, test_coalesce_param *params,
                    unsigned int params_num)
{
    unsigned int i;

    RING("Tx coalescing profile: %s",
         te_enum_map_from_value(test_profile_map, profile));

    for (i = 0; i < params_num; i++)
        RING("%s=%llu", params[i].name, (unsigned long long)params[i].value);
}

static bool
profile_enables_adaptive_tx(int profile)
{
    switch (profile)
    {
        case TEST_PROFILE_ADAPTIVE:
        case TEST_PROFILE_ADAPTIVE_LOW_TIME:
        case TEST_PROFILE_ADAPTIVE_LOW_FRAMES:
        case TEST_PROFILE_ADAPTIVE_HIGH_TIME:
        case TEST_PROFILE_ADAPTIVE_HIGH_FRAMES:
        case TEST_PROFILE_ADAPTIVE_RATE:
            return true;

        default:
            return false;
    }
}

static te_errno
set_coalesce_params_locally(const char *ta, const char *if_name, int queue_id,
                            test_coalesce_param *params,
                            unsigned int params_num, bool restore)
{
    unsigned int i;
    te_errno rc;

    for (i = 0; i < params_num; i++)
    {
        if (strcmp(params[i].name, "use_adaptive_tx_coalesce") == 0)
            continue;

        rc = tapi_cfg_if_coalesce_queue_set_local(ta, if_name, queue_id,
                                                  params[i].name,
                                                  restore ?
                                                      params[i].prev_value :
                                                      params[i].value);
        if (rc != 0)
            return rc;
    }

    return 0;
}

static void
save_profile_params(const char *ta, const char *if_name, int queue_id,
                    test_coalesce_param *params, unsigned int params_num)
{
    unsigned int i;
    te_errno rc;

    for (i = 0; i < params_num; i++)
    {
        rc = tapi_cfg_if_coalesce_queue_get(ta, if_name, queue_id,
                                            params[i].name,
                                            &params[i].prev_value);
        if (rc != 0)
            TEST_FAIL("Failed to get %s: %r", params[i].name, rc);
    }
}

static void
check_profile_params(const char *ta, const char *if_name, int queue_id,
                     test_coalesce_param *params, unsigned int params_num)
{
    uint64_t actual;
    unsigned int i;

    for (i = 0; i < params_num; i++)
    {
        CHECK_RC(tapi_cfg_if_coalesce_queue_get(ta, if_name, queue_id,
                                                params[i].name, &actual));

        if (actual != params[i].value)
        {
            TEST_VERDICT("Expected %s=%llu, got %llu",
                         params[i].name, (unsigned long long)params[i].value,
                         (unsigned long long)actual);
        }
    }
}

static test_coalesce_param *
profile_params(int profile, unsigned int *params_num)
{
    switch (profile)
    {
        case TEST_PROFILE_DISABLED:
            *params_num = TE_ARRAY_LEN(disabled_params);
            return disabled_params;

        case TEST_PROFILE_TIME_ONLY:
            *params_num = TE_ARRAY_LEN(time_only_params);
            return time_only_params;

        case TEST_PROFILE_FRAMES_ONLY:
            *params_num = TE_ARRAY_LEN(frames_only_params);
            return frames_only_params;

        case TEST_PROFILE_TIME_FRAMES:
            *params_num = TE_ARRAY_LEN(time_frames_params);
            return time_frames_params;

        case TEST_PROFILE_IRQ:
            *params_num = TE_ARRAY_LEN(irq_params);
            return irq_params;

        case TEST_PROFILE_IRQ_USECS:
            *params_num = TE_ARRAY_LEN(irq_usecs_params);
            return irq_usecs_params;

        case TEST_PROFILE_IRQ_FRAMES:
            *params_num = TE_ARRAY_LEN(irq_frames_params);
            return irq_frames_params;

        case TEST_PROFILE_ADAPTIVE:
            *params_num = TE_ARRAY_LEN(adaptive_params);
            return adaptive_params;

        case TEST_PROFILE_ADAPTIVE_LOW_TIME:
            *params_num = TE_ARRAY_LEN(adaptive_low_time_params);
            return adaptive_low_time_params;

        case TEST_PROFILE_ADAPTIVE_LOW_FRAMES:
            *params_num = TE_ARRAY_LEN(adaptive_low_frames_params);
            return adaptive_low_frames_params;

        case TEST_PROFILE_ADAPTIVE_HIGH_TIME:
            *params_num = TE_ARRAY_LEN(adaptive_high_time_params);
            return adaptive_high_time_params;

        case TEST_PROFILE_ADAPTIVE_HIGH_FRAMES:
            *params_num = TE_ARRAY_LEN(adaptive_high_frames_params);
            return adaptive_high_frames_params;

        case TEST_PROFILE_ADAPTIVE_RATE:
            *params_num = TE_ARRAY_LEN(adaptive_rate_params);
            return adaptive_rate_params;

        case TEST_PROFILE_INVALID:
            *params_num = TE_ARRAY_LEN(invalid_params);
            return invalid_params;

        default:
            TEST_FAIL("Unexpected profile %d", profile);
    }

    return NULL;
}

int
main(int argc, char *argv[])
{
    const struct if_nameindex *iut_if = NULL;
    rcf_rpc_server *iut_rpcs = NULL;

    test_coalesce_param *params = NULL;
    bool restore_prev_values = false;
    bool restore_adaptive_tx = false;
    uint64_t prev_adaptive_tx = 0;
    unsigned int params_num = 0;
    uint64_t test_tx_coalesce;
    bool per_queue = false;
    int queue_id;
    int profile;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_ENUM_PARAM(profile, TEST_PROFILES);
    TEST_GET_BOOL_PARAM(per_queue);

    params = profile_params(profile, &params_num);
    queue_id = per_queue ? 0 : -1;

    TEST_STEP("Check that Tx interrupt coalescing settings are supported.");
    rc = tapi_cfg_if_coalesce_queue_get(iut_rpcs->ta, iut_if->if_name,
                                        queue_id, "tx_coalesce_usecs",
                                        &test_tx_coalesce);
    if (TE_RC_GET_ERROR(rc) == TE_ENOENT ||
        TE_RC_GET_ERROR(rc) == TE_EOPNOTSUPP)
    {
        TEST_SKIP("%s Tx coalescing settings are not supported",
                  per_queue ? "Per-queue" : "Global");
    }
    else if (rc != 0)
    {
        TEST_FAIL("Failed to get tx_coalesce_usecs: %r", TE_RC_GET_ERROR(rc));
    }

    dump_profile_params(profile, params, params_num);

    TEST_STEP("Save current values of coalescing parameters changed by @p profile.");
    save_profile_params(iut_rpcs->ta, iut_if->if_name, queue_id, params,
                        params_num);
    restore_prev_values = true;

    CHECK_RC(tapi_cfg_if_coalesce_queue_get(iut_rpcs->ta, iut_if->if_name,
                                            queue_id,
                                            "use_adaptive_tx_coalesce",
                                            &prev_adaptive_tx));
    restore_adaptive_tx = true;

    TEST_STEP("Disable adaptive Tx coalescing before changing Tx parameters.");
    if (profile_enables_adaptive_tx(profile) || prev_adaptive_tx != 0)
    {
        rc = tapi_cfg_if_coalesce_queue_set(iut_rpcs->ta, iut_if->if_name,
                                            queue_id,
                                            "use_adaptive_tx_coalesce", 0);
        if (rc != 0)
        {
            TEST_VERDICT("Failed to disable use_adaptive_tx_coalesce, rc=%r",
                         TE_RC_GET_ERROR(rc));
        }
    }

    if (profile == TEST_PROFILE_INVALID)
    {
        TEST_STEP("Set Tx time and frame limits to zero");
        CHECK_RC(set_coalesce_params_locally(iut_rpcs->ta, iut_if->if_name,
                                             queue_id, params, params_num,
                                             false));

        rc = tapi_cfg_if_coalesce_queues_commit(iut_rpcs->ta,
                                                iut_if->if_name,
                                                per_queue);
        if (TE_RC_GET_ERROR(rc) != TE_EINVAL)
        {
            if (rc == 0)
                TEST_VERDICT("Zero Tx time and frame limits unexpectedly succeeded");

            TEST_VERDICT("Zero Tx time and frame limits failed with unexpected rc=%r",
                         TE_RC_GET_ERROR(rc));
        }

        TEST_SUCCESS;
    }

    TEST_STEP("Set Tx interrupt coalescing parameters from @p profile.");
    CHECK_RC(set_coalesce_params_locally(iut_rpcs->ta, iut_if->if_name,
                                         queue_id, params, params_num, false));
    rc = tapi_cfg_if_coalesce_queues_commit(iut_rpcs->ta, iut_if->if_name,
                                            per_queue);
    if (rc != 0)
    {
        TEST_VERDICT("Failed to set Tx coalescing settings, rc=%r",
                     TE_RC_GET_ERROR(rc));
    }

    if (profile_enables_adaptive_tx(profile))
    {
        rc = tapi_cfg_if_coalesce_queue_set(iut_rpcs->ta, iut_if->if_name,
                                            queue_id,
                                            "use_adaptive_tx_coalesce", 1);
        if (rc != 0)
        {
            TEST_VERDICT("Failed to enable use_adaptive_tx_coalesce, rc=%r",
                         TE_RC_GET_ERROR(rc));
        }
    }

    TEST_STEP("Read back Tx coalescing parameters and check they were applied.");
    check_profile_params(iut_rpcs->ta, iut_if->if_name, queue_id, params,
                         params_num);

    TEST_SUCCESS;

cleanup:

    /*
     * Keep adaptive Tx disabled while restoring concrete values, then
     * restore original adaptive Tx state.
     */
    if (restore_prev_values)
    {
        if (restore_adaptive_tx && profile_enables_adaptive_tx(profile))
        {
            CLEANUP_CHECK_RC(tapi_cfg_if_coalesce_queue_set(
                                                   iut_rpcs->ta,
                                                   iut_if->if_name, queue_id,
                                                   "use_adaptive_tx_coalesce",
                                                   0));
        }

        CLEANUP_CHECK_RC(set_coalesce_params_locally(iut_rpcs->ta,
                                                     iut_if->if_name,
                                                     queue_id, params,
                                                     params_num, true));
        CLEANUP_CHECK_RC(tapi_cfg_if_coalesce_queues_commit(
                                                    iut_rpcs->ta,
                                                    iut_if->if_name,
                                                    per_queue));

        if (restore_adaptive_tx && prev_adaptive_tx != 0)
        {
            CLEANUP_CHECK_RC(tapi_cfg_if_coalesce_queue_set(
                                                   iut_rpcs->ta,
                                                   iut_if->if_name, queue_id,
                                                   "use_adaptive_tx_coalesce",
                                                   prev_adaptive_tx));
        }
    }

    TEST_END;
}
