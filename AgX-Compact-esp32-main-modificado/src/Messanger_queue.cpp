/**
 * @file Messanger_queue.cpp
 * @brief Implementation of core message queues for inter-task and ISR communication.
 * @author Mariano Oms
 * @date 2026-02-06
 */

#include "Messanger_queue.h"
#include <string.h>

/*****************************************************************************
*                             Global Variables                               *
******************************************************************************/

// For more information regarding queues see:
// https://www.freertos.org/Documentation/02-Kernel/04-API-references/06-Queues/00-QueueManagement
// https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/02-Queues-mutexes-and-semaphores/01-Queues

static QueueHandle_t s_builderQueue = nullptr;
static QueueHandle_t s_senderQueue = nullptr;

/*****************************************************************************
*                          Local Functions Prototypes                        *
******************************************************************************/

static size_t MessangerQueue_copyMessage(MessangerQueueMessage *msg, const char *data, size_t len);

/*****************************************************************************
*                        Public Functions Definitions                        *
******************************************************************************/

/**
 * @brief Create builder and sender queues if they are not created yet.
 * @return true if both queues are valid, false otherwise.
 */
bool MessangerQueue_init(void) {
	if (s_builderQueue == nullptr) {
		s_builderQueue = xQueueCreate(MESSANGER_QUEUE_BUILDER_LENGTH, sizeof(MessangerQueueMessage));
	}

	if (s_senderQueue == nullptr) {
		s_senderQueue = xQueueCreate(MESSANGER_QUEUE_SENDER_LENGTH, sizeof(MessangerQueueMessage));
	}

	return (s_builderQueue != nullptr) && (s_senderQueue != nullptr);
}

/**
 * @brief Delete the queues and release their resources.
 */
void MessangerQueue_deinit(void) {
	if (s_builderQueue != nullptr) {
		vQueueDelete(s_builderQueue);
		s_builderQueue = nullptr;
	}

	if (s_senderQueue != nullptr) {
		vQueueDelete(s_senderQueue);
		s_senderQueue = nullptr;
	}
}

/**
 * @brief Send a message to the builder queue from a task.
 * @param data Payload buffer (copied into the queue item).
 * @param len Payload length; if 0 the length is computed with strnlen.
 * @param timeout Ticks to wait for space in the queue.
 * @return true if the item was queued, false otherwise.
 */
bool MessangerQueue_sendToBuilder(const char *data, size_t len, TickType_t timeout) {
	if (s_builderQueue == nullptr) {
		return false;
	}

	MessangerQueueMessage msg;
	MessangerQueue_copyMessage(&msg, data, len);
	return xQueueSend(s_builderQueue, &msg, timeout) == pdTRUE;
}

/**
 * @brief Send a message to the builder queue from an ISR.
 * @param data Payload buffer (copied into the queue item).
 * @param len Payload length; if 0 the length is computed with strnlen.
 * @param higherPriorityTaskWoken Pointer used by FreeRTOS to request a context switch.
 * @return true if the item was queued, false otherwise.
 */
bool MessangerQueue_sendToBuilderFromISR(const char *data, size_t len, BaseType_t *higherPriorityTaskWoken) {
	if (s_builderQueue == nullptr) {
		return false;
	}

	MessangerQueueMessage msg;
	MessangerQueue_copyMessage(&msg, data, len);
	return xQueueSendFromISR(s_builderQueue, &msg, higherPriorityTaskWoken) == pdTRUE;
}

/**
 * @brief Receive a message from the builder queue.
 * @param out Output buffer for the dequeued item.
 * @param timeout Ticks to wait for an item.
 * @return true if an item was received, false otherwise.
 */
bool MessangerQueue_receiveFromBuilder(MessangerQueueMessage *out, TickType_t timeout) {
	if (s_builderQueue == nullptr || out == nullptr) {
		return false;
	}

	return xQueueReceive(s_builderQueue, out, timeout) == pdTRUE;
}



/**
 * @brief Send a message to the sender queue from a task.
 * @param data Payload buffer (copied into the queue item).
 * @param len Payload length; if 0 the length is computed with strnlen.
 * @param timeout Ticks to wait for space in the queue.
 * @return true if the item was queued, false otherwise.
 */
bool MessangerQueue_sendToSender(const char *data, size_t len, TickType_t timeout) {
	if (s_senderQueue == nullptr) {
		return false;
	}

	MessangerQueueMessage msg;
	MessangerQueue_copyMessage(&msg, data, len);
	return xQueueSend(s_senderQueue, &msg, timeout) == pdTRUE;
}

/**
 * @brief Send a message to the sender queue from an ISR.
 * @param data Payload buffer (copied into the queue item).
 * @param len Payload length; if 0 the length is computed with strnlen.
 * @param higherPriorityTaskWoken Pointer used by FreeRTOS to request a context switch.
 * @return true if the item was queued, false otherwise.
 */
bool MessangerQueue_sendToSenderFromISR(const char *data, size_t len, BaseType_t *higherPriorityTaskWoken) {
	if (s_senderQueue == nullptr) {
		return false;
	}

	MessangerQueueMessage msg;
	MessangerQueue_copyMessage(&msg, data, len);
	return xQueueSendFromISR(s_senderQueue, &msg, higherPriorityTaskWoken) == pdTRUE;
}

/**
 * @brief Receive a message from the sender queue.
 * @param out Output buffer for the dequeued item.
 * @param timeout Ticks to wait for an item.
 * @return true if an item was received, false otherwise.
 */
bool MessangerQueue_receiveFromSender(MessangerQueueMessage *out, TickType_t timeout) {
	if (s_senderQueue == nullptr || out == nullptr) {
		return false;
	}

	return xQueueReceive(s_senderQueue, out, timeout) == pdTRUE;
}

/*****************************************************************************
*                         Local Functions Definitions                        *
******************************************************************************/

/**
 * @brief Copy payload into a queue message with bounded length and null termination.
 * @param msg Destination message.
 * @param data Source buffer.
 * @param len Payload length; if 0, it is computed with strnlen.
 * @return Copied length.
 */
static size_t MessangerQueue_copyMessage(MessangerQueueMessage *msg, const char *data, size_t len) {
	if (msg == nullptr) {
		return 0;
	}

	if (data == nullptr) {
		msg->len = 0;
		msg->data[0] = '\0';
		return 0;
	}

	size_t max_len = MESSANGER_QUEUE_MAX_PAYLOAD;
	size_t copy_len = len;

	if (copy_len == 0) {
		copy_len = strnlen(data, max_len);
	} else if (copy_len > max_len) {
		copy_len = max_len;
	}

	if (copy_len > 0) {
		memcpy(msg->data, data, copy_len);
	}

	msg->data[copy_len] = '\0';
	msg->len = copy_len;
	return copy_len;
}