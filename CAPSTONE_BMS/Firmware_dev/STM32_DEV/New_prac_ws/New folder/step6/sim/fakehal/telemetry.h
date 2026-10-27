#ifndef FAKE_TELEMETRY_H
#define FAKE_TELEMETRY_H
#include "main.h"
#include "bms_status.h"
void Telemetry_Init(UART_HandleTypeDef*); void Telemetry_Print(const char*); void Telemetry_Send(const BmsStatus*);
#endif
