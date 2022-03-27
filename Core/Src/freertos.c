/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * File Name          : freertos.c
 * Description        : Code for freertos applications
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2022 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "timers.h"
#include "queue.h"
#include "semphr.h"
#include "event_groups.h"
#include "string.h"
#include "rtc.h"
#include "stdio.h"
#include "fatfs.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define LOG_SIZE			40
#define TIMESTAMP_SIZE		13
#define CONVERSION_SIZE		3
#define EXTENSION_SIZE		4
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
extern UART_HandleTypeDef huart1;
extern uint8_t uart_buffer[MESSAGE_SIZE];

extern FRESULT retUSER; /* Return value for USER */
extern char USERPath[4]; /* USER logical drive path */
extern FATFS USERFatFS; /* File system object for USER logical drive */
extern FIL USERFile; /* File object for USER */

TaskHandle_t read_rtc_task;
TaskHandle_t build_log_task;
TaskHandle_t write_sd_task;
QueueHandle_t logs;

struct date_time {
	RTC_DateTypeDef date;
	RTC_TimeTypeDef time;
};

struct log {
	char msg[LOG_SIZE];
	uint8_t log_length;
	uint8_t file_name_length;
	struct date_time timestamp;
};
/* USER CODE END Variables */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void read_rtc();
void build_log();
void write_sd();

void get_time(struct date_time *t);
/* USER CODE END FunctionPrototypes */

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
 * @brief  FreeRTOS initialization
 * @param  None
 * @retval None
 */
void MX_FREERTOS_Init(void) {
	/* USER CODE BEGIN Init */
	/* USER CODE END Init */

	/* USER CODE BEGIN RTOS_MUTEX */
	/* USER CODE END RTOS_MUTEX */

	/* USER CODE BEGIN RTOS_SEMAPHORES */
	/* USER CODE END RTOS_SEMAPHORES */

	/* USER CODE BEGIN RTOS_TIMERS */
	/* start timers, add new ones, ... */
	/* USER CODE END RTOS_TIMERS */

	/* USER CODE BEGIN RTOS_QUEUES */
	logs = xQueueCreate(5, sizeof(struct log));
	if (logs == NULL)
		Error_Handler();
	/* USER CODE END RTOS_QUEUES */

	/* Create the thread(s) */
	/* definition and creation of defaultTask */

	/* USER CODE BEGIN RTOS_THREADS */
	xTaskCreate(read_rtc, "read_rtc", 150, NULL, 2, &read_rtc_task);
	xTaskCreate(build_log, "build_log", 150, NULL, 3, &build_log_task);
	xTaskCreate(write_sd, "write_sd", 310, NULL, 4, &write_sd_task);
	/* USER CODE END RTOS_THREADS */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
void read_rtc() {
	struct log mesh_log;
	for (;;) {
		if (ulTaskNotifyTake(pdTRUE, 10) != 1)
			continue;

		if (uart_buffer[0] != ':') {
			memset(uart_buffer, 0, MESSAGE_SIZE);
			HAL_UART_Receive_IT(&huart1, uart_buffer, MESSAGE_SIZE);
			continue;
		}

		memcpy(mesh_log.msg, uart_buffer, MESSAGE_SIZE);
		memset(uart_buffer, 0, MESSAGE_SIZE);
		get_time(&mesh_log.timestamp);
		xQueueSendToBack(logs, (void *)&mesh_log, (TickType_t )5);

		HAL_UART_Receive_IT(&huart1, uart_buffer, MESSAGE_SIZE);
		xTaskNotifyGive(build_log_task);
	}
}

void build_log() {
	struct log mesh_log;
	for (;;) {
		if (ulTaskNotifyTake(pdTRUE, 20) != 1)
			continue;

		if (xQueueReceive(logs, (void *)&mesh_log, (TickType_t) 2) == pdFALSE)
			continue;

		char conversion[CONVERSION_SIZE] = { 0 };

		mesh_log.msg[MESSAGE_SIZE - 1] = ' ';
		mesh_log.msg[MESSAGE_SIZE] = '\0';

		mesh_log.file_name_length = sprintf(conversion, "%02d", mesh_log.timestamp.date.Date);
		strcat(mesh_log.msg, conversion);
		mesh_log.file_name_length += sprintf(conversion, "%02d", mesh_log.timestamp.date.Month);
		strcat(mesh_log.msg, conversion);
		mesh_log.file_name_length += sprintf(conversion, "%2d", mesh_log.timestamp.date.Year);
		strcat(mesh_log.msg, conversion);

		mesh_log.log_length = mesh_log.file_name_length;

		mesh_log.log_length += sprintf(conversion, "%02d", mesh_log.timestamp.time.Hours);
		strcat(mesh_log.msg, conversion);
		mesh_log.log_length += sprintf(conversion, "%02d", mesh_log.timestamp.time.Minutes);
		strcat(mesh_log.msg, conversion);
		mesh_log.log_length += sprintf(conversion, "%02d", mesh_log.timestamp.time.Seconds);
		strcat(mesh_log.msg, conversion);

		mesh_log.log_length += 1 + MESSAGE_SIZE;
		mesh_log.msg[mesh_log.log_length - 1] = '\n';

		xQueueSendToBack(logs, (void *)&mesh_log, (TickType_t )5);
		xTaskNotifyGive(write_sd_task);
	}
}
/*
*	extern uint8_t retUSER	->	Return value for USER
*	extern char USERPath[4]	->	USER logical drive path
*	extern FATFS USERFatFS	->	File system object for USER logical drive
*	extern FIL USERFile		->	File object for USER
*/
void write_sd() {
	struct log mesh_log;
	UINT bw = 0;
	const char *txt_ext = ".txt";
	HAL_UART_Receive_IT(&huart1, uart_buffer, MESSAGE_SIZE);
	for (;;) {
		if (ulTaskNotifyTake(pdTRUE, 100) != 1)
			continue;

		if (xQueueReceive(logs, (void *)&mesh_log, (TickType_t)2) == pdFALSE)
			continue;

		char *file_name = pvPortMalloc(
				(mesh_log.file_name_length + 1 + EXTENSION_SIZE) * sizeof(char));

		memcpy(file_name, &mesh_log.msg[MESSAGE_SIZE], mesh_log.file_name_length);
//		for (int i = 0; i < mesh_log.file_name_length + 1; i++)
//			file_name[i] = mesh_log.msg[MESSAGE_SIZE + i];

		file_name[mesh_log.file_name_length] = '\0';
		strcat(file_name, txt_ext);

		retUSER = f_mount(&USERFatFS, "/", 1);
		retUSER = f_open(&USERFile, file_name, FA_OPEN_ALWAYS | FA_READ | FA_WRITE);
		retUSER = f_lseek(&USERFile, f_size(&USERFile));
		retUSER = f_write(&USERFile, (void *)mesh_log.msg, mesh_log.log_length, &bw);
		retUSER = f_close(&USERFile);
		retUSER = f_mount(0, "/", 1);

		vPortFree(file_name);
	}
}

void get_time(struct date_time *t) {
	HAL_RTC_GetTime(&hrtc, &t->time, RTC_FORMAT_BIN);
	HAL_RTC_GetDate(&hrtc, &t->date, RTC_FORMAT_BIN);
}
/* USER CODE END Application */
