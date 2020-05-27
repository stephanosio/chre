/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "chpp/app.h"
#include "chpp/pal_api.h"
#include "chpp/services.h"

#include "chpp/clients/discovery.h"

#include "chpp/services/discovery.h"
#include "chpp/services/loopback.h"
#include "chpp/services/nonhandle.h"

/************************************************
 *  Prototypes
 ***********************************************/

static void chppProcessPredefinedClientRequest(struct ChppAppState *context,
                                               uint8_t *buf, size_t len);
static void chppProcessPredefinedServiceResponse(struct ChppAppState *context,
                                                 uint8_t *buf, size_t len);
static void chppProcessPredefinedClientNotification(
    struct ChppAppState *context, uint8_t *buf, size_t len);
static void chppProcessPredefinedServiceNotification(
    struct ChppAppState *context, uint8_t *buf, size_t len);
static bool chppDatagramLenIsOk(struct ChppAppState *context, uint8_t handle,
                                size_t len);
ChppDispatchFunction *chppGetDispatchFunction(struct ChppAppState *context,
                                              uint8_t handle,
                                              enum ChppMessageType type);
static inline const struct ChppService *chppServiceOfHandle(
    struct ChppAppState *appContext, uint8_t handle);

/************************************************
 *  Private Functions
 ***********************************************/

/**
 * Processes a client request that is determined to be for a predefined CHPP
 * service.
 *
 * @param context Maintains status for each app layer instance.
 * @param buf Input data. Cannot be null.
 * @param len Length of input data in bytes.
 */
static void chppProcessPredefinedClientRequest(struct ChppAppState *context,
                                               uint8_t *buf, size_t len) {
  struct ChppAppHeader *rxHeader = (struct ChppAppHeader *)buf;

  switch (rxHeader->handle) {
    case CHPP_HANDLE_LOOPBACK:
      chppDispatchLoopbackClientRequest(context, buf, len);
      break;

    case CHPP_HANDLE_DISCOVERY:
      chppDispatchDiscoveryClientRequest(context, buf, len);
      break;

    default:
      LOGE(
          "Client request received for an invalid predefined service handle "
          "%" PRIu8,
          rxHeader->handle);
  }
}

/**
 * Processes a service response that is determined to be for a predefined CHPP
 * client.
 *
 * @param context Maintains status for each app layer instance.
 * @param buf Input data. Cannot be null.
 * @param len Length of input data in bytes.
 */
static void chppProcessPredefinedServiceResponse(struct ChppAppState *context,
                                                 uint8_t *buf, size_t len) {
  struct ChppAppHeader *rxHeader = (struct ChppAppHeader *)buf;

  switch (rxHeader->handle) {
    case CHPP_HANDLE_LOOPBACK:
      // TODO
      break;

    case CHPP_HANDLE_DISCOVERY:
      chppDispatchDiscoveryClient(context, buf, len);
      break;

    default:
      LOGE(
          "Service response received for an invalid predefined service handle "
          "%" PRIu8,
          rxHeader->handle);
  }
}

/**
 * Processes a client notification that is determined to be for a predefined
 * CHPP service.
 *
 * @param context Maintains status for each app layer instance.
 * @param buf Input data. Cannot be null.
 * @param len Length of input data in bytes.
 */
static void chppProcessPredefinedClientNotification(
    struct ChppAppState *context, uint8_t *buf, size_t len) {
  struct ChppAppHeader *rxHeader = (struct ChppAppHeader *)buf;

  // No predefined services support these yet
  LOGE("Predefined service handle %" PRIu8
       " does not support client notifications",
       rxHeader->handle);

  UNUSED_VAR(context);
  UNUSED_VAR(len);
}
/**
 * Processes a service notification that is determined to be for a predefined
 * CHPP client.
 *
 * @param context Maintains status for each app layer instance.
 * @param buf Input data. Cannot be null.
 * @param len Length of input data in bytes.
 */
static void chppProcessPredefinedServiceNotification(
    struct ChppAppState *context, uint8_t *buf, size_t len) {
  struct ChppAppHeader *rxHeader = (struct ChppAppHeader *)buf;

  // No predefined clients support these yet
  LOGE("Predefined client handle %" PRIu8
       " does not support service notifications",
       rxHeader->handle);

  UNUSED_VAR(context);
  UNUSED_VAR(len);
}

/**
 * Verifies if the length of a Rx Datagram from the transport layer is
 * sufficient for the associated service.
 *
 * @param context Maintains status for each app layer instance.
 * @param handle Handle number for the service.
 * @param len Length of the datagram in bytes.
 *
 * @return true if length is ok.
 */
static bool chppDatagramLenIsOk(struct ChppAppState *context, uint8_t handle,
                                size_t len) {
  size_t minLen = SIZE_MAX;

  if (handle < CHPP_HANDLE_NEGOTIATED_RANGE_START) {
    // Predefined services

    switch (handle) {
      case CHPP_HANDLE_NONE:
        minLen = sizeof_member(struct ChppAppHeader, handle);
        break;

      case CHPP_HANDLE_LOOPBACK:
        minLen = sizeof_member(struct ChppAppHeader, handle) +
                 sizeof_member(struct ChppAppHeader, type);
        break;

      case CHPP_HANDLE_DISCOVERY:
        minLen = sizeof(struct ChppAppHeader);
        break;

      default:
        LOGE("Invalid predefined handle %" PRIu8, handle);
    }

  } else {
    // Negotiated services

    minLen = chppServiceOfHandle(context, handle)->minLength;
  }

  if (len < minLen) {
    LOGE("Received datagram too short for handle=%" PRIu8 ", len=%zu", handle,
         len);
  }
  return (len >= minLen);
}

/**
 * Returns the dispatch function of a particular negotiated client/service
 * handle and message type. This shall be null if it is unsupported by the
 * service.
 *
 * @param context Maintains status for each app layer instance.
 * @param handle Handle number for the client/service.
 * @param type Message type
 *
 * @return Pointer to a function that dispatches incoming datagrams for any
 * particular client/service.
 */
ChppDispatchFunction *chppGetDispatchFunction(struct ChppAppState *context,
                                              uint8_t handle,
                                              enum ChppMessageType type) {
  switch (type) {
    case CHPP_MESSAGE_TYPE_CLIENT_REQUEST: {
      return (chppServiceOfHandle(context, handle)->requestDispatchFunctionPtr);
      break;
    }
    case CHPP_MESSAGE_TYPE_SERVICE_RESPONSE: {
      // TODO
      return (NULL);
      break;
    }
    case CHPP_MESSAGE_TYPE_CLIENT_NOTIFICATION: {
      return (chppServiceOfHandle(context, handle)
                  ->notificationDispatchFunctionPtr);
      break;
    }
    case CHPP_MESSAGE_TYPE_SERVICE_NOTIFICATION: {
      // TODO
      return (NULL);
      break;
    }
    default: {
      LOGE("Cannot dispatch unknown message type = %#x (handle = %" PRIu8 ")",
           type, handle);
      chppEnqueueTxErrorDatagram(context->transportContext,
                                 CHPP_TRANSPORT_ERROR_APPLAYER);
    }
  }
}

/**
 * Returns a pointer to the ChppService struct of a particular negotiated
 * service handle.
 *
 * @param context Maintains status for each app layer instance.
 * @param handle Handle number for the service.
 *
 * @return Pointer to the ChppService struct of a particular service handle.
 */
static inline const struct ChppService *chppServiceOfHandle(
    struct ChppAppState *appContext, uint8_t handle) {
  return (appContext->registeredServices[handle -
                                         CHPP_HANDLE_NEGOTIATED_RANGE_START]);
}

/************************************************
 *  Public Functions
 ***********************************************/

void chppAppInit(struct ChppAppState *appContext,
                 struct ChppTransportState *transportContext) {
  CHPP_NOT_NULL(appContext);
  CHPP_NOT_NULL(transportContext);

  memset(appContext, 0, sizeof(struct ChppAppState));

  appContext->transportContext = transportContext;

  chppPalSystemApiInit(appContext);

  chppRegisterCommonServices(appContext);
}

void chppAppDeinit(struct ChppAppState *appContext) {
  // TODO

  chppPalSystemApiDeinit(appContext);
}

void chppProcessRxDatagram(struct ChppAppState *context, uint8_t *buf,
                           size_t len) {
  struct ChppAppHeader *rxHeader = (struct ChppAppHeader *)buf;

  if (chppDatagramLenIsOk(context, rxHeader->handle, len)) {
    if (rxHeader->handle >=
        context->registeredServiceCount + CHPP_HANDLE_NEGOTIATED_RANGE_START) {
      LOGE("Received datagram for invalid handle: %" PRIu8
           ", len = %zu, type = %#x, transaction ID = %" PRIu8,
           rxHeader->handle, len, rxHeader->type, rxHeader->transaction);

    } else if (rxHeader->handle == CHPP_HANDLE_NONE) {
      // Non-handle based communication

      chppDispatchNonHandle(context, buf, len);

    } else if (rxHeader->handle < CHPP_HANDLE_NEGOTIATED_RANGE_START) {
      // Predefined services / clients

      switch (rxHeader->type) {
        case CHPP_MESSAGE_TYPE_CLIENT_REQUEST: {
          chppProcessPredefinedClientRequest(context, buf, len);
          break;
        }
        case CHPP_MESSAGE_TYPE_CLIENT_NOTIFICATION: {
          chppProcessPredefinedClientNotification(context, buf, len);
          break;
        }
        case CHPP_MESSAGE_TYPE_SERVICE_RESPONSE: {
          chppProcessPredefinedServiceResponse(context, buf, len);
          break;
        }
        case CHPP_MESSAGE_TYPE_SERVICE_NOTIFICATION: {
          chppProcessPredefinedServiceNotification(context, buf, len);
          break;
        }
        default: {
          LOGE(
              "Received unknown message type = %#x for predefined handle  = "
              "%" PRIu8 " len = %zu, transaction ID = %" PRIu8,
              rxHeader->type, rxHeader->handle, len, rxHeader->transaction);
          chppEnqueueTxErrorDatagram(context->transportContext,
                                     CHPP_TRANSPORT_ERROR_APPLAYER);
        }
      }

    } else {
      // Negotiated services / clients

      ChppDispatchFunction *dispatchFunc =
          chppGetDispatchFunction(context, rxHeader->handle, rxHeader->type);

      if (dispatchFunc == NULL) {
        LOGE("Negotiated handle = %" PRIu8
             " does not support Rx message type = %" PRIu8,
             rxHeader->handle, rxHeader->type);
      } else {
        dispatchFunc(context, buf, len);
      }
    }
  }

  chppAppProcessDoneCb(context->transportContext, buf);
}

void chppUuidToStr(const uint8_t uuid[CHPP_SERVICE_UUID_LEN],
                   char strOut[CHPP_SERVICE_UUID_STRING_LEN]) {
  sprintf(
      strOut,
      "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
      uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7],
      uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14],
      uuid[15]);
}