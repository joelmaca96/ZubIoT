#ifndef FIREBASE_CONTROLLER_H
#define FIREBASE_CONTROLLER_H

void firebase_controller_init(void);


/**
 * @brief Tipo de campo cambiado en rtdb
 */
typedef enum {
    RTDB_CONFIG_CHANGED,
    RTDB_COMMAND_CHANGED,
    RTDB_HISTORY_CHANGED,
    RTDB_STATUS_CHANGED,
    RTDB_ERROR
} rtdb_event_t;

#endif // FIREBASE_CONTROLLER_H
