/**
 * @file    Messanger_queue.h
 * @brief   Core message queues for inter-task and ISR communication.
 * @author  Mariano Oms
 * @date    2026-02-06
 */
#ifndef MESSANGER_QUEUE_H
#define MESSANGER_QUEUE_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include <stddef.h>
#include <stdint.h>

/*****************************************************************************
*                                   Defines                                  *
******************************************************************************/

#ifndef MESSANGER_QUEUE_MAX_PAYLOAD
    #define MESSANGER_QUEUE_MAX_PAYLOAD 256
#endif


#ifndef MESSANGER_QUEUE_BUILDER_LENGTH
    #define MESSANGER_QUEUE_BUILDER_LENGTH 10
#endif


#ifndef MESSANGER_QUEUE_SENDER_LENGTH
    #define MESSANGER_QUEUE_SENDER_LENGTH 10
#endif

/*****************************************************************************
*                                   Structs                                  *
******************************************************************************/

typedef struct {
	size_t len;         // Payload length without the null terminator.
	char data[MESSANGER_QUEUE_MAX_PAYLOAD + 1]; // Payload buffer.
} MessangerQueueMessage;

/*****************************************************************************
*                            Public Prototypes                               *
******************************************************************************/

// Common functions

bool MessangerQueue_init(void);
void MessangerQueue_deinit(void);

// Builder queue functions

bool MessangerQueue_sendToBuilder(const char *data, size_t len, TickType_t timeout);
bool MessangerQueue_sendToBuilderFromISR(const char *data, size_t len, BaseType_t *higherPriorityTaskWoken);
bool MessangerQueue_receiveFromBuilder(MessangerQueueMessage *out, TickType_t timeout);

// Sender queue functions

bool MessangerQueue_sendToSender(const char *data, size_t len, TickType_t timeout);
bool MessangerQueue_sendToSenderFromISR(const char *data, size_t len, BaseType_t *higherPriorityTaskWoken);
bool MessangerQueue_receiveFromSender(MessangerQueueMessage *out, TickType_t timeout);

#endif // MESSANGER_QUEUE_H