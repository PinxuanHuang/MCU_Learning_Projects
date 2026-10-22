/*
 * dashboard.h
 *
 *  Created on: Oct 12, 2026
 *      Author: pinxuanhuang
 */

#ifndef INC_DASHBOARD_H_
#define INC_DASHBOARD_H_

#include <stdint.h>

typedef struct
{
    uint32_t rx_kbps;
    uint32_t tx_kbps;
    uint8_t cpu_usage;
    uint8_t mem_usage;

} DashboardStats;

void Dashboard_Init(void);
void Dashboard_Update(const DashboardStats *stats);
void Dashboard_Service(void);
uint8_t Dashboard_IsBusy(void);

#endif /* INC_DASHBOARD_H_ */
