#ifndef FIREBASE_CONTROLLER_H
#define FIREBASE_CONTROLLER_H

#include "../Battery/battery_controller.h"

/**
 * @brief Tipo de campo cambiado en rtdb
 */
typedef enum {
    RTDB_VALUE_CHANGED_NONE = 0,
    RTDB_CONFIG_CHANGED,
    RTDB_COMMAND_CHANGED,
    RTDB_HISTORY_CHANGED,
    RTDB_STATUS_CHANGED,
    RTDB_ERROR
} rtdb_event_t;

void firebase_controller_init(void);

// Añadir al archivo de cabecera (firebase_controller.h)

/**
 * @brief Estructura para almacenar datos de una celda de batería
 */
typedef struct {
    float voltage;       // Voltaje de la celda en V
    float temperature;   // Temperatura en °C
    uint8_t soc;         // Estado de carga (0-100%)
    uint8_t soh;         // Estado de salud (0-100%)
} battery_cell_t;

/**
 * @brief Actualiza los datos de las celdas de la batería en Firebase
 * 
 * @param cell_data Arreglo con los datos de las celdas
 * @param cell_count Número de celdas a actualizar
 * @return bool true si la actualización fue exitosa
 */
bool update_battery_cells(const battery_cell_t* cell_data, uint8_t cell_count);

/**
 * @brief Actualiza los datos del pack de la batería en Firebase
 * @param voltage Voltaje total del pack en V
 * @param current Corriente del pack en A
 * @param power Potencia del pack en W
 * @param status Estado del pack (e.g., "Charging", "Discharging")
 * @param uptime Tiempo de funcionamiento en segundos
 * @return true si la actualización fue exitosa, false en caso contrario
 */
bool update_battery_pack(float voltage, float current, float power, const char* status, uint32_t uptime);

/**
 * @brief Almacena un registro histórico de la batería en Firebase
 * @param cells_data Arreglo con los datos de las celdas de la batería
 * @param num_cells Número de celdas en el arreglo
 * @param voltage Voltaje total del pack en V
 * @param current Corriente del pack en A
 * @param power Potencia del pack en W
 * @param status Estado del pack (e.g., "Charging", "Discharging")
 * @return true si el almacenamiento fue exitoso, false en caso contrario
 */
bool store_battery_history(const battery_cell_t* cells_data, uint8_t num_cells, 
    float voltage, float current, float power, const char* status);

/**
 * @brief Verifica si hay conectividad adecuada para operaciones con Firebase
 * @return true si hay conectividad completa, false en caso contrario
 */
bool check_firebase_connectivity();

#endif // FIREBASE_CONTROLLER_H