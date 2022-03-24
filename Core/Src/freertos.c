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

extern uint8_t retUSER; /* Return value for USER */
extern char USERPath[4]; /* USER logical drive path */
extern FATFS USERFatFS; /* File system object for USER logical drive */
extern FIL USERFile; /* File object for USER */

SemaphoreHandle_t read_mesh_sem;
QueueHandle_t timestamps;
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
	/* add mutexes, ... */
	/* USER CODE END RTOS_MUTEX */

	/* USER CODE BEGIN RTOS_SEMAPHORES */
	read_mesh_sem = xSemaphoreCreateBinary();
	if (read_mesh_sem == NULL)
		while (1)
			;

	/* USER CODE END RTOS_SEMAPHORES */

	/* USER CODE BEGIN RTOS_TIMERS */
	/* start timers, add new ones, ... */
	/* USER CODE END RTOS_TIMERS */

	/* USER CODE BEGIN RTOS_QUEUES */
	timestamps = xQueueCreate(5, sizeof(struct date_time));
	logs = xQueueCreate(5, sizeof(struct log));
	if (timestamps == NULL || logs == NULL)
		Error_Handler();
	/* USER CODE END RTOS_QUEUES */

	/* Create the thread(s) */
	/* definition and creation of defaultTask */

	/* USER CODE BEGIN RTOS_THREADS */
	xTaskCreate(read_rtc, "read_rtc", 150, NULL, 2, NULL);
	xTaskCreate(build_log, "build_log", 150, NULL, 3, NULL);
	xTaskCreate(write_sd, "write_sd", 310, NULL, 4, NULL);
	/* USER CODE END RTOS_THREADS */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
void read_rtc() {
	uint8_t receive[MESSAGE_SIZE] = { 0 };
	struct date_time t;
	for (;;) {
		if (xSemaphoreTake(read_mesh_sem, (TickType_t )20) == pdFALSE)
			continue;

		memcpy(receive, uart_buffer, MESSAGE_SIZE);
		memset(uart_buffer, 0, MESSAGE_SIZE);

		get_time(&t);
		xQueueSendToBack(timestamps, (void *)&t, (TickType_t )5);

		HAL_UART_Receive_IT(&huart1, uart_buffer, MESSAGE_SIZE);
	}
}

void build_log() {
	struct date_time t;
	struct log mesh_log;
	for (;;) {
		if (xQueueReceive(timestamps, (void *)&t, (TickType_t) 30) == pdFALSE)
			continue;

		char conversion[CONVERSION_SIZE] = { 0 };

		mesh_log.msg[0] = '\0';
		sprintf(conversion, "%02d", t.date.Date);
		strcat(mesh_log.msg, conversion);
		sprintf(conversion, "%02d", t.date.Month);
		strcat(mesh_log.msg, conversion);
		sprintf(conversion, "%2d", t.date.Year);
		strcat(mesh_log.msg, conversion);

		mesh_log.file_name_length = strlen(mesh_log.msg);

		sprintf(conversion, "%02d", t.time.Hours);
		strcat(mesh_log.msg, conversion);
		sprintf(conversion, "%02d", t.time.Minutes);
		strcat(mesh_log.msg, conversion);
		sprintf(conversion, "%02d", t.time.Seconds);
		strcat(mesh_log.msg, conversion);

		uint8_t timestamp_length = strlen(mesh_log.msg);
		mesh_log.msg[timestamp_length] = ' ';
		for (int i = 0; i < MESSAGE_SIZE; i++)
			mesh_log.msg[timestamp_length + 1 + i] = uart_buffer[i];

		mesh_log.log_length = timestamp_length + 1 + MESSAGE_SIZE;

		xQueueSendToBack(logs, (void *)&mesh_log, (TickType_t )5);
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
		if (xQueueReceive(logs, (void *)&mesh_log,
				(TickType_t) 100) == pdFALSE)
			continue;

		char *file_name = pvPortMalloc(
				(mesh_log.file_name_length + 1 + EXTENSION_SIZE) * sizeof(char));

		for (int i = 0; i < mesh_log.file_name_length + 1; i++)
			file_name[i] = mesh_log.msg[i];

		file_name[mesh_log.file_name_length] = '\0';
		strcat(file_name, txt_ext);

		retUSER = f_mount(&USERFatFS, "/", 1);
		if (retUSER != FR_OK)
			Error_Handler();

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
