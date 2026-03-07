/*
 * test_mode.h
 *
 * Compile-time test mode switches.
 */

#ifndef TEST_MODE_H_
#define TEST_MODE_H_

#if (defined(VOICE_TEST_MODE) + defined(UDP_TEST_MODE) + defined(CAN_TEST_MODE)) > 1
#error "Define only one test mode: VOICE_TEST_MODE, UDP_TEST_MODE, or CAN_TEST_MODE."
#endif

#if defined(VOICE_TEST_MODE) || defined(UDP_TEST_MODE) || defined(CAN_TEST_MODE)
#define TEST_MODE_ACTIVE (1)
#else
#define TEST_MODE_ACTIVE (0)
#endif

#if defined(VOICE_TEST_MODE)
#define TEST_KEEP_LOG_TASK         (1)
#define TEST_KEEP_VOICE_COMMAND    (1)
#define TEST_KEEP_NET_CONNECT      (0)
#define TEST_KEEP_CAN_COMMS        (0)
#define TEST_KEEP_SCREEN_INTERACT  (0)
#define TEST_KEEP_WIFI_DEBUG       (0)
#elif defined(UDP_TEST_MODE)
#define TEST_KEEP_LOG_TASK         (1)
#define TEST_KEEP_VOICE_COMMAND    (0)
#define TEST_KEEP_NET_CONNECT      (1)
#define TEST_KEEP_CAN_COMMS        (0)
#define TEST_KEEP_SCREEN_INTERACT  (0)
#define TEST_KEEP_WIFI_DEBUG       (0)
#elif defined(CAN_TEST_MODE)
#define TEST_KEEP_LOG_TASK         (1)
#define TEST_KEEP_VOICE_COMMAND    (0)
#define TEST_KEEP_NET_CONNECT      (0)
#define TEST_KEEP_CAN_COMMS        (1)
#define TEST_KEEP_SCREEN_INTERACT  (0)
#define TEST_KEEP_WIFI_DEBUG       (0)
#else
#define TEST_KEEP_LOG_TASK         (1)
#define TEST_KEEP_VOICE_COMMAND    (1)
#define TEST_KEEP_NET_CONNECT      (1)
#define TEST_KEEP_CAN_COMMS        (1)
#define TEST_KEEP_SCREEN_INTERACT  (1)
#define TEST_KEEP_WIFI_DEBUG       (1)
#endif

#endif /* TEST_MODE_H_ */
