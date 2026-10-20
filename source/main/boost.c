/*
 Copyright (C) 2026

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/

#include <math.h>
#include "boost.h"

void boost_config_set_default(tBoostConfig* config)
{
    config->Version = BOOST_CONFIG_VERSION;
    config->OffDb10 = BOOST_DB10_DEFAULT_OFF;
    config->OnDb10 = BOOST_DB10_DEFAULT_ON;
    config->OffAtBoot = 1;
}

/****************************************************************************
* RETURN:      BOOST_CONFIG_OK, 아니면 BoostConfigErrors 중 하나
*****************************************************************************/
uint8_t boost_config_check(const tBoostConfig* config)
{
    if ((config->OffDb10 < BOOST_DB10_MIN) || (config->OffDb10 > BOOST_DB10_MAX) ||
        (config->OnDb10 < BOOST_DB10_MIN) || (config->OnDb10 > BOOST_DB10_MAX))
    {
        return BOOST_CONFIG_ERR_RANGE;
    }

    if (((config->OffDb10 % BOOST_DB10_STEP) != 0) || ((config->OnDb10 % BOOST_DB10_STEP) != 0))
    {
        return BOOST_CONFIG_ERR_STEP;
    }

    if (config->OffDb10 >= config->OnDb10)
    {
        return BOOST_CONFIG_ERR_ORDER;
    }

    return BOOST_CONFIG_OK;
}

float boost_db10_to_db(int16_t db10)
{
    return (float)db10 / 10.0f;
}

/****************************************************************************
* RETURN:      BOOST_STATE_ON / BOOST_STATE_OFF / BOOST_STATE_CUSTOM
* NOTES:       OFF < ON이 최소 한 단계 차이라 두 허용 범위는 겹치지 않는다
*****************************************************************************/
uint8_t boost_state_for_volume(const tBoostConfig* config, float volume_db)
{
    if (fabsf(volume_db - boost_db10_to_db(config->OnDb10)) <= BOOST_MATCH_TOLERANCE_DB)
    {
        return BOOST_STATE_ON;
    }

    if (fabsf(volume_db - boost_db10_to_db(config->OffDb10)) <= BOOST_MATCH_TOLERANCE_DB)
    {
        return BOOST_STATE_OFF;
    }

    return BOOST_STATE_CUSTOM;
}

// ON이면 OFF로, OFF나 사용자 지정이면 ON으로 간다
float boost_toggle_target_db(const tBoostConfig* config, float volume_db)
{
    if (boost_state_for_volume(config, volume_db) == BOOST_STATE_ON)
    {
        return boost_db10_to_db(config->OffDb10);
    }

    return boost_db10_to_db(config->OnDb10);
}
