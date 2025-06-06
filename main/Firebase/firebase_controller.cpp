/**
 * @file firebase_example.c
 * @brief Firebase Realtime Database para ESP32 con ESP-IDF
 */

#include "bi_firebase.h"
#include "bi_params.hpp"
#include "bi_debug.h"
#include "secrets.h"
#include "firebase_controller.h"
#include "../custom_config.h"
#include "math.h"

extern BIParams biParams;
static std::string device_path = "/batteries/";

LoggerPtr g_FirebaseLogger;

// Manejador para Firebase
firebase_handle_t *firebase_handle = NULL;

static void procesarComando(const char* tipo, cJSON* valor, const char* comando_id);
static bool update_command_status(const char* comando_id, const char* status, const char* result = nullptr);
static cJSON* create_firebase_server_timestamp();

void firebase_listen_callback(void *data, int event_id, firebase_data_value_t *value) {
    BI_DEBUG_INFO(g_FirebaseLogger, "Firebase listener event received: %d, data: %i", event_id, (uint32_t)(data));
    
    DeviceParams& params = biParams.getParams();
    DeviceState& state = biParams.getState();
    bool config_changed = false;

    // Actualizar configuración del dispositivo desde Firebase
    switch (uint32_t(data)) {
        case RTDB_CONFIG_CHANGED: 
        {
            if (value && value->type == FIREBASE_DATA_TYPE_JSON && value->data.string_val) {
                cJSON *json = cJSON_Parse(value->data.string_val);
                if (json) {
                    // Actualizar nombre del dispositivo
                    cJSON *name = cJSON_GetObjectItem(json, "name");
                    if (name && cJSON_IsString(name)) {
                        strncpy(params.deviceName, name->valuestring, sizeof(params.deviceName) - 1);
                        params.deviceName[sizeof(params.deviceName) - 1] = '\0';
                        config_changed = true;
                        BI_DEBUG_INFO(g_FirebaseLogger, "Device name updated: %s", params.deviceName);
                    }
                    
                    // Actualizar modelo del dispositivo
                    cJSON *model = cJSON_GetObjectItem(json, "model");
                    if (model && cJSON_IsString(model)) {
                        strncpy(params.deviceModel, model->valuestring, sizeof(params.deviceModel) - 1);
                        params.deviceModel[sizeof(params.deviceModel) - 1] = '\0';
                        config_changed = true;
                        BI_DEBUG_INFO(g_FirebaseLogger, "Device model updated: %s", params.deviceModel);
                    }
                    
                    // Actualizar número de celdas desde configuración
                    cJSON *cellCount = cJSON_GetObjectItem(json, "cellCount");
                    if (cellCount && cJSON_IsNumber(cellCount)) {
                        uint8_t newCellCount = (uint8_t)cellCount->valueint;
                        if (biParams.setCellCount(newCellCount)) {
                            config_changed = true;
                            BI_DEBUG_INFO(g_FirebaseLogger, "Cell count configuration updated: %d", newCellCount);
                        } else {
                            BI_DEBUG_WARNING(g_FirebaseLogger, "Invalid cell count in configuration: %d (valid range: %d-%d)", 
                                           newCellCount, MIN_CELL_COUNT, MAX_CELL_COUNT);
                        }
                    }
                    
                    // Actualizar intervalo de muestreo (reporting.interval)
                    cJSON *reporting = cJSON_GetObjectItem(json, "reporting");
                    if (reporting && cJSON_IsObject(reporting)) {
                        cJSON *interval = cJSON_GetObjectItem(reporting, "interval");
                        if (interval && cJSON_IsNumber(interval)) {
                            // Convertir de ms a segundos
                            params.sampleInterval = interval->valueint / 1000;
                            if (params.sampleInterval < 1) params.sampleInterval = 1; // Mínimo 1 segundo
                            config_changed = true;
                            BI_DEBUG_INFO(g_FirebaseLogger, "Sample interval updated: %d seconds", params.sampleInterval);
                        }
                    }
                    
                    // Actualizar configuración de power
                    cJSON *power = cJSON_GetObjectItem(json, "power");
                    if (power && cJSON_IsObject(power)) {
                        cJSON *autoShutdown = cJSON_GetObjectItem(power, "autoShutdown");
                        if (autoShutdown && cJSON_IsBool(autoShutdown)) {
                            params.deepSleepEnabled = cJSON_IsTrue(autoShutdown);
                            config_changed = true;
                            BI_DEBUG_INFO(g_FirebaseLogger, "Auto shutdown %s", params.deepSleepEnabled ? "enabled" : "disabled");
                        }
                        
                        cJSON *shutdownVoltage = cJSON_GetObjectItem(power, "shutdownVoltage");
                        if (shutdownVoltage && cJSON_IsNumber(shutdownVoltage)) {
                            params.shutdownVoltage = (float)shutdownVoltage->valuedouble;
                            config_changed = true;
                            BI_DEBUG_INFO(g_FirebaseLogger, "Shutdown voltage updated: %.2fV", params.shutdownVoltage);
                        }
                        
                        cJSON *maxCurrent = cJSON_GetObjectItem(power, "maxCurrent");
                        if (maxCurrent && cJSON_IsNumber(maxCurrent)) {
                            params.maxCurrent = (float)maxCurrent->valuedouble;
                            config_changed = true;
                            BI_DEBUG_INFO(g_FirebaseLogger, "Max current updated: %.2fA", params.maxCurrent);
                        }
                    }
                    
                    // Actualizar configuración de alertas
                    cJSON *alerts = cJSON_GetObjectItem(json, "alerts");
                    if (alerts && cJSON_IsObject(alerts)) {
                        cJSON *highTemp = cJSON_GetObjectItem(alerts, "highTemp");
                        if (highTemp && cJSON_IsNumber(highTemp)) {
                            params.alertHighTemp = (float)highTemp->valuedouble;
                            config_changed = true;
                            BI_DEBUG_INFO(g_FirebaseLogger, "High temp alert: %.1f°C", params.alertHighTemp);
                        }
                        
                        cJSON *lowTemp = cJSON_GetObjectItem(alerts, "lowTemp");
                        if (lowTemp && cJSON_IsNumber(lowTemp)) {
                            params.alertLowTemp = (float)lowTemp->valuedouble;
                            config_changed = true;
                            BI_DEBUG_INFO(g_FirebaseLogger, "Low temp alert: %.1f°C", params.alertLowTemp);
                        }
                        
                        cJSON *highVoltage = cJSON_GetObjectItem(alerts, "highVoltage");
                        if (highVoltage && cJSON_IsNumber(highVoltage)) {
                            params.alertHighVoltage = (float)highVoltage->valuedouble;
                            config_changed = true;
                            BI_DEBUG_INFO(g_FirebaseLogger, "High voltage alert: %.2fV", params.alertHighVoltage);
                        }
                        
                        cJSON *lowVoltage = cJSON_GetObjectItem(alerts, "lowVoltage");
                        if (lowVoltage && cJSON_IsNumber(lowVoltage)) {
                            params.alertLowVoltage = (float)lowVoltage->valuedouble;
                            config_changed = true;
                            BI_DEBUG_INFO(g_FirebaseLogger, "Low voltage alert: %.2fV", params.alertLowVoltage);
                        }
                    }
                    
                    // Actualizar configuración de balanceo
                    cJSON *balancing = cJSON_GetObjectItem(json, "balancing");
                    if (balancing && cJSON_IsObject(balancing)) {
                        cJSON *enabled = cJSON_GetObjectItem(balancing, "enabled");
                        if (enabled && cJSON_IsBool(enabled)) {
                            params.balancingEnabled = cJSON_IsTrue(enabled);
                            config_changed = true;
                            BI_DEBUG_INFO(g_FirebaseLogger, "Balancing %s", params.balancingEnabled ? "enabled" : "disabled");
                        }
                        
                        cJSON *threshold = cJSON_GetObjectItem(balancing, "threshold");
                        if (threshold && cJSON_IsNumber(threshold)) {
                            params.balancingThreshold = (float)threshold->valuedouble;
                            config_changed = true;
                            BI_DEBUG_INFO(g_FirebaseLogger, "Balancing threshold: %.3fV", params.balancingThreshold);
                        }
                    }
                    
                    // Guardar cambios si hubo alguno
                    if (config_changed) {
                        biParams.saveParams();
                        BI_DEBUG_INFO(g_FirebaseLogger, "Configuration saved to NVS");
                        
                        // Log especial si cambió el número de celdas
                        cJSON *cellCountCheck = cJSON_GetObjectItem(json, "cellCount");
                        if (cellCountCheck && cJSON_IsNumber(cellCountCheck)) {
                            BI_DEBUG_INFO(g_FirebaseLogger, "Battery pack will be reconfigured to %d cells on next cycle", 
                                         params.cellCount);
                        }
                    }
                    
                    cJSON_Delete(json);
                } else {
                    BI_DEBUG_ERROR(g_FirebaseLogger, "Error parsing configuration JSON");
                }
            }
            break;
        }
        case RTDB_COMMAND_CHANGED:
        {
            if (value && value->type == FIREBASE_DATA_TYPE_JSON && value->data.string_val) {
                cJSON *json = cJSON_Parse(value->data.string_val);
                if (json) {
                    // Iterar sobre todos los comandos en el objeto
                    cJSON *comando = NULL;
                    cJSON_ArrayForEach(comando, json) {
                        if (cJSON_IsObject(comando)) {
                            cJSON *type = cJSON_GetObjectItem(comando, "type");
                            cJSON *cmd_value = cJSON_GetObjectItem(comando, "value");
                            cJSON *status = cJSON_GetObjectItem(comando, "status");
                            
                            // Solo procesar comandos pendientes
                            if (type && cmd_value && status && 
                                cJSON_IsString(type) && 
                                cJSON_IsString(status) && 
                                strcmp(status->valuestring, "pending") == 0) {
                                
                                // Obtener el ID del comando (clave del objeto)
                                const char* comando_id = comando->string;
                                if (comando_id) {
                                    BI_DEBUG_INFO(g_FirebaseLogger, "Processing command ID: %s", comando_id);
                                    
                                    // Marcar como recibido inmediatamente
                                    update_command_status(comando_id, "received");
                                    
                                    // Procesar el comando
                                    procesarComando(type->valuestring, cmd_value, comando_id);
                                }
                            }
                        }
                    }
                    
                    cJSON_Delete(json);
                }
            }
            break;
        }
        default:
            break;
    }
}

// Función auxiliar para procesar comandos - ACTUALIZADA
static void procesarComando(const char* tipo, cJSON* valor, const char* comando_id) {
    if (!tipo || !valor || !comando_id) return;
    
    BI_DEBUG_INFO(g_FirebaseLogger, "Processing command: %s (ID: %s)", tipo, comando_id);
    
    bool comando_exitoso = false;
    char resultado[128] = {0};
    
    if (strcmp(tipo, "power") == 0 && cJSON_IsString(valor)) {
        if (strcmp(valor->valuestring, "on") == 0) {
            // Código para encender
            BI_DEBUG_INFO(g_FirebaseLogger, "Command: Power ON");
            comando_exitoso = true;
            strcpy(resultado, "System powered on successfully");
        } else if (strcmp(valor->valuestring, "off") == 0) {
            // Código para apagar
            BI_DEBUG_INFO(g_FirebaseLogger, "Command: Power OFF");
            comando_exitoso = true;
            strcpy(resultado, "System powered off successfully");
        } else if (strcmp(valor->valuestring, "restart") == 0) {
            BI_DEBUG_INFO(g_FirebaseLogger, "Command: Reboot system");
            update_command_status(comando_id, "completed", "System rebooting...");
            
            // Esperar un poco para que se envíe la respuesta antes del reinicio
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart(); 
        } else {
            strcpy(resultado, "Invalid power value");
        }
    } 
    else if (strcmp(tipo, "balancing") == 0 && cJSON_IsString(valor)) {
        if (strcmp(valor->valuestring, "start") == 0) {
            // Iniciar balanceo
            BI_DEBUG_INFO(g_FirebaseLogger, "Command: Start balancing");
            comando_exitoso = true;
            strcpy(resultado, "Balancing started successfully");
        } else if (strcmp(valor->valuestring, "stop") == 0) {
            // Detener balanceo
            BI_DEBUG_INFO(g_FirebaseLogger, "Command: Stop balancing");
            comando_exitoso = true;
            strcpy(resultado, "Balancing stopped successfully");
        } else {
            strcpy(resultado, "Invalid balancing value");
        }
    }
    else {
        snprintf(resultado, sizeof(resultado), "Unknown command: %s", tipo);
    }
    
    // Actualizar estado del comando
    const char* estado_final = comando_exitoso ? "completed" : "failed";
    update_command_status(comando_id, estado_final, resultado);
}

// Función para actualizar el estado de un comando en Firebase
static bool update_command_status(const char* comando_id, const char* status, const char* result) {
    if (!comando_id || !status) {
        return false;
    }
    
    // Verificar conectividad
    if (!check_firebase_connectivity()) {
        return false;
    }
    
    // Crear objeto JSON para la actualización
    cJSON *update_json = cJSON_CreateObject();
    if (!update_json) {
        return false;
    }
    
    cJSON_AddStringToObject(update_json, "status", status);
    
    // Usar Firebase Server Timestamp
    if (strcmp(status, "received") == 0) {
        cJSON *timestamp = create_firebase_server_timestamp();
        if (timestamp) {
            cJSON_AddItemToObject(update_json, "receivedAt", timestamp);
        }
    } else if (strcmp(status, "completed") == 0 || strcmp(status, "failed") == 0) {
        cJSON *timestamp = create_firebase_server_timestamp();
        if (timestamp) {
            cJSON_AddItemToObject(update_json, "completedAt", timestamp);
        }
        if (result) {
            cJSON_AddStringToObject(update_json, "result", result);
        }
    }
    
    // Convertir a string
    char *json_string = cJSON_PrintUnformatted(update_json);
    cJSON_Delete(update_json);
    
    if (!json_string) {
        return false;
    }
    
    // Crear valor Firebase
    firebase_data_value_t value;
    bool success = false;
    
    if (firebase_set_json(&value, json_string)) {
        // Construir la ruta del comando específico
        std::string command_path = device_path + "/commands/" + comando_id;
        
        // Actualizar en Firebase
        if (firebase_update(firebase_handle, command_path.c_str(), &value)) {
            BI_DEBUG_INFO(g_FirebaseLogger, "Estado del comando %s actualizado a: %s", comando_id, status);
            success = true;
        } else {
            BI_DEBUG_ERROR(g_FirebaseLogger, "Error al actualizar estado del comando %s", comando_id);
        }
        
        firebase_free_value(&value);
    }
    
    free(json_string);
    return success;
}

// Función para crear timestamp de servidor Firebase
static cJSON* create_firebase_server_timestamp() {
    cJSON *timestamp_obj = cJSON_CreateObject();
    if (timestamp_obj) {
        cJSON *sv = cJSON_CreateString("timestamp");
        if (sv) {
            cJSON_AddItemToObject(timestamp_obj, ".sv", sv);
        } else {
            cJSON_Delete(timestamp_obj);
            timestamp_obj = NULL;
        }
    }
    return timestamp_obj;
}

// Inicializar Firebase y autenticarse
bool init_firebase(void) {
    // Inicializar el logger
    g_FirebaseLogger = createLogger("FIREBASE_CONTROLLER", INFO, true);

    // Configurar Firebase
    firebase_config_t config = {.database_url      = FIREBASE_DATABASE_URL,
                                .auth              = {.auth_type     = FIREBASE_AUTH_API_KEY,
                                                      .api_key       = FIREBASE_API_KEY,
                                                      .user_email    = FIREBASE_EMAIL,
                                                      .user_password = FIREBASE_PASSWORD,
                                                      .custom_token  = NULL,
                                                      .id_token      = NULL,
                                                      .refresh_token = NULL,
                                                      .token_expiry  = 0,
                                                      .uid           = NULL},
                                .user_data         = NULL,
                                .timeout_ms        = 30000,
                                .secure_connection = true};

    // Configurar HTTP para Firebase
    config.http_config.cert_pem       = NULL;
    config.http_config.is_async       = false;
    config.http_config.timeout_ms     = 30000;
    config.http_config.transport_type = HTTP_TRANSPORT_OVER_TCP;
    config.http_config.buffer_size     = 4096;

    // Inicializar Firebase
    firebase_handle = firebase_init(&config);
    if (!firebase_handle) {
        BI_DEBUG_ERROR(g_FirebaseLogger, "Error al inicializar Firebase");
        return false;
    }

    // Autenticarse con Firebase
    if (!firebase_auth_with_password(firebase_handle, FIREBASE_EMAIL, FIREBASE_PASSWORD)) {
        BI_DEBUG_ERROR(g_FirebaseLogger, "Error al autenticarse con Firebase");
        return false;
    }

    // Si lo conseguimos, actualizar el UID de la bateria
    DeviceParams& params = biParams.getParams();
    strncpy(params.deviceKey, firebase_handle->auth.uid, strlen(firebase_handle->auth.uid));
    biParams.saveParams();

    // Actualizar las rutas del sistema apuntando al uid
    device_path = "/batteries/" + std::string(firebase_handle->auth.uid);

    BI_DEBUG_INFO(g_FirebaseLogger, "Firebase inicializado y autenticado correctamente");
    return true;
}

/**
 * @brief Verifica si hay conectividad adecuada para operaciones con Firebase
 * @return true si hay conectividad completa, false en caso contrario
 */
bool check_firebase_connectivity() {
    DeviceState& state = biParams.getState();
    
    if (!state.wifiConnected) {
        return false;
    }
    
    if (!state.firebaseConnected || !firebase_handle) {
        return false;
    }
    
    // Verificar que la autenticación sigue activa
    if (!firebase_is_authenticated(firebase_handle)) {
        
        // Intentar refrescar el token
        if (!firebase_refresh_token(firebase_handle)) {
            state.firebaseConnected = false;
            biParams.saveState();
            return false;
        }
    }
    
    return true;
}

/**
 * @brief Actualiza los datos de las celdas de la batería en Firebase
 * 
 * @param cell_data Arreglo con los datos de las celdas
 * @param cell_count Número de celdas a actualizar
 * @return bool true si la actualización fue exitosa
 */
bool update_battery_cells(const battery_cell_t* cell_data, uint8_t cell_count) {
    if (!firebase_handle || !cell_data || cell_count == 0)
        return false;

    // Verificar conectividad
    if (!check_firebase_connectivity()) {
        return false;
    }
    
    bool result = false;
    firebase_data_value_t value;
    memset(&value, 0, sizeof(value)); // Inicializar a cero
    
    // Crear array JSON para las celdas
    cJSON *cells_array = cJSON_CreateArray();
    if (!cells_array) {
        BI_DEBUG_ERROR(g_FirebaseLogger, "Error al crear array JSON");
        return false;
    }
    
    // Añadir cada celda al array
    bool cell_create_error = false;
    for (uint8_t i = 0; i < cell_count && !cell_create_error; i++) {
        cJSON *cell = cJSON_CreateObject();
        if (!cell) {
            cell_create_error = true;
        } else {
            // Añadir valores con validación
            cJSON_AddNumberToObject(cell, "id", i + 1);
            
            // Validar valores antes de añadirlos (prevenir NaN o inf)
            float voltage = cell_data[i].voltage;
            float temp = cell_data[i].temperature;
            if (isfinite(voltage)) {
                cJSON_AddNumberToObject(cell, "voltage", voltage);
            } else {
                cJSON_AddNumberToObject(cell, "voltage", 0.0);
            }
            
            if (isfinite(temp)) {
                cJSON_AddNumberToObject(cell, "temperature", temp);
            } else {
                cJSON_AddNumberToObject(cell, "temperature", 0.0);
            }
            
            cJSON_AddNumberToObject(cell, "soc", cell_data[i].soc);
            cJSON_AddNumberToObject(cell, "soh", cell_data[i].soh);
            
            cJSON_AddItemToArray(cells_array, cell);
        }
    }
    
    if (cell_create_error) {
        cJSON_Delete(cells_array);
        BI_DEBUG_ERROR(g_FirebaseLogger, "Error al crear objetos de celda JSON");
        return false;
    }
    
    // Crear objeto JSON principal
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        cJSON_Delete(cells_array);
        BI_DEBUG_ERROR(g_FirebaseLogger, "Error al crear objeto JSON");
        return false;
    }
    
    // Añadir array de celdas al objeto principal
    cJSON_AddItemToObject(json, "cells", cells_array);
    
    // Convertir a string
    char *json_string = cJSON_PrintUnformatted(json);
    cJSON_Delete(json); // Libera json y cells_array
    
    if (!json_string) {
        BI_DEBUG_ERROR(g_FirebaseLogger, "Error al serializar JSON");
        return false;
    }
    
    // Crear valor Firebase de tipo JSON con comprobación de errores
    if (firebase_set_json(&value, json_string)) {
        // Actualizar datos en Firebase
        if (firebase_update(firebase_handle, (device_path).c_str(), &value)) {
            BI_DEBUG_INFO(g_FirebaseLogger, "Datos de celdas actualizados correctamente");
            result = true;
        } else {
            BI_DEBUG_ERROR(g_FirebaseLogger, "Error al actualizar datos de celdas");
        }
    } else {
        BI_DEBUG_ERROR(g_FirebaseLogger, "Error al crear valor JSON para Firebase");
    }
    
    // Liberar recursos siempre
    firebase_free_value(&value);
    free(json_string);
    
    return result;
}

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
                           float voltage, float current, float power, const char* status) {
    // Validar parámetros de entrada
    if (!cells_data || num_cells == 0 || !status) {
        BI_DEBUG_ERROR(g_FirebaseLogger, "Parámetros inválidos para store_battery_history");
        return false;
    }
    
    // Verificar conectividad
    if (!check_firebase_connectivity()) {
        return false;
    }
    
    // Crear objeto JSON principal
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        BI_DEBUG_ERROR(g_FirebaseLogger, "Error creando objeto JSON principal");
        return false;
    }
    
    // Obtener timestamp de Firebase (usando Server Timestamp para sincronización)
    cJSON *timestamp_obj = create_firebase_server_timestamp();
    if (timestamp_obj) {
        cJSON_AddItemToObject(json, "timestamp", timestamp_obj);
    } else {
        // Fallback al timestamp local si falla crear el server timestamp
        int64_t local_timestamp = esp_timer_get_time() / 1000;
        cJSON_AddNumberToObject(json, "timestamp", local_timestamp);
    }
    
    // Crear array JSON para las celdas
    cJSON *cells_array = cJSON_CreateArray();
    if (!cells_array) {
        cJSON_Delete(json);
        BI_DEBUG_ERROR(g_FirebaseLogger, "Error creando array JSON para celdas");
        return false;
    }
    
    // Añadir datos de cada celda al array
    for (uint8_t i = 0; i < num_cells; i++) {
        cJSON *cell = cJSON_CreateObject();
        if (!cell) {
            cJSON_Delete(json);
            BI_DEBUG_ERROR(g_FirebaseLogger, "Error creando objeto celda JSON");
            return false;
        }
        
        cJSON_AddNumberToObject(cell, "id", i + 1);
        cJSON_AddNumberToObject(cell, "voltage", cells_data[i].voltage);
        cJSON_AddNumberToObject(cell, "temperature", cells_data[i].temperature);
        cJSON_AddNumberToObject(cell, "soc", cells_data[i].soc);
        
        cJSON_AddItemToArray(cells_array, cell);
    }
    
    // Añadir array de celdas al objeto principal
    cJSON_AddItemToObject(json, "cells", cells_array);
    
    // Crear objeto JSON para los datos del pack
    cJSON *pack_json = cJSON_CreateObject();
    if (!pack_json) {
        cJSON_Delete(json);
        BI_DEBUG_ERROR(g_FirebaseLogger, "Error creando objeto JSON para pack");
        return false;
    }
    
    // Añadir datos del pack
    cJSON_AddNumberToObject(pack_json, "totalVoltage", voltage);
    cJSON_AddNumberToObject(pack_json, "current", current);
    cJSON_AddNumberToObject(pack_json, "power", power);
    cJSON_AddStringToObject(pack_json, "status", status);
    
    // Añadir objeto pack al objeto principal
    cJSON_AddItemToObject(json, "pack", pack_json);
    
    // Convertir a string
    char *json_string = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    
    if (!json_string) {
        BI_DEBUG_ERROR(g_FirebaseLogger, "Error creando string JSON");
        return false;
    }
    
    // Crear valor Firebase de tipo JSON
    firebase_data_value_t value;
    bool result = false;
    
    if (firebase_set_json(&value, json_string)) {
        // Establecer un timeout para la operación
        int retry_count = 0;
        const int max_retries = 3;
        char key[64] = {0};
        std::string history_path = device_path + "/history";
        
        while (retry_count < max_retries) {
            // Verificar conectividad antes de cada intento
            if (!check_firebase_connectivity()) {
                firebase_free_value(&value);
                free(json_string);
                return false;
            }
            
            // Enviar datos a Firebase en la ruta /batteries/{uid}/history
            if (firebase_push(firebase_handle, history_path.c_str(), &value, key, sizeof(key))) {
                BI_DEBUG_INFO(g_FirebaseLogger, "Registro histórico almacenado con clave: %s", key);
                
                // También actualizar el último timestamp en los metadatos
                firebase_data_value_t timestamp_value;
                cJSON *server_timestamp = create_firebase_server_timestamp();
                if (server_timestamp) {
                    char *timestamp_str = cJSON_PrintUnformatted(server_timestamp);
                    if (timestamp_str) {
                        if (firebase_set_json(&timestamp_value, timestamp_str)) {
                            std::string last_update_path = device_path + "/lastUpdate";
                            firebase_set(firebase_handle, last_update_path.c_str(), &timestamp_value);
                            firebase_free_value(&timestamp_value);
                        }
                        free(timestamp_str);
                    }
                    cJSON_Delete(server_timestamp);
                } else {
                    // Fallback al timestamp local
                    int64_t local_timestamp = esp_timer_get_time() / 1000;
                    if (firebase_set_int(&timestamp_value, local_timestamp)) {
                        std::string last_update_path = device_path + "/lastUpdate";
                        firebase_set(firebase_handle, last_update_path.c_str(), &timestamp_value);
                        firebase_free_value(&timestamp_value);
                    }
                }
                
                result = true;
                break;
            } else {
                retry_count++;
                if (retry_count < max_retries) {
                    BI_DEBUG_WARNING(g_FirebaseLogger, "Reintentando almacenamiento histórico (%d/%d)", 
                                 retry_count, max_retries);
                    vTaskDelay(pdMS_TO_TICKS(1000)); // Esperar 1 segundo antes de reintentar
                } else {
                    BI_DEBUG_ERROR(g_FirebaseLogger, "Error al almacenar registro histórico después de %d intentos", 
                                  max_retries);
                }
            }
        }
        
        // Liberar recursos
        firebase_free_value(&value);
    }
    
    free(json_string);
    return result;
}

/**
 * @brief Actualiza los datos del pack de la batería en Firebase
 * @param voltage Voltaje total del pack en V
 * @param current Corriente del pack en A
 * @param power Potencia del pack en W
 * @param status Estado del pack (e.g., "Charging", "Discharging")
 * @param uptime Tiempo de funcionamiento en segundos
 * @return true si la actualización fue exitosa, false en caso contrario
 */
bool update_battery_pack(float voltage, float current, float power, const char* status, uint32_t uptime) {
    // Validar parámetros de entrada
    if (!status) {
        BI_DEBUG_ERROR(g_FirebaseLogger, "Parámetro de estado inválido para update_battery_pack");
        return false;
    }
    
    // Verificar conectividad
    if (!check_firebase_connectivity()) {
        return false;
    }
    
    // Crear objeto JSON para los datos del pack
    cJSON *pack_json = cJSON_CreateObject();
    if (!pack_json) {
        BI_DEBUG_ERROR(g_FirebaseLogger, "Error creando objeto JSON para pack");
        return false;
    }
    
    // Añadir datos del pack
    cJSON_AddNumberToObject(pack_json, "totalVoltage", voltage);
    cJSON_AddNumberToObject(pack_json, "current", current);
    cJSON_AddNumberToObject(pack_json, "power", power);
    cJSON_AddStringToObject(pack_json, "status", status);
    cJSON_AddNumberToObject(pack_json, "uptime", uptime);
    
    // Crear objeto JSON principal para actualizar
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        cJSON_Delete(pack_json);
        BI_DEBUG_ERROR(g_FirebaseLogger, "Error creando objeto JSON principal");
        return false;
    }
    
    // Añadir datos del pack al objeto principal
    cJSON_AddItemToObject(json, "pack", pack_json);
    
    // Convertir a string
    char *json_string = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    
    if (!json_string) {
        BI_DEBUG_ERROR(g_FirebaseLogger, "Error creando string JSON");
        return false;
    }
    
    // Crear valor Firebase de tipo JSON
    firebase_data_value_t value;
    bool result = false;
    
    if (firebase_set_json(&value, json_string)) {
        // Establecer un timeout para la operación
        int retry_count = 0;
        const int max_retries = 3;
        
        while (retry_count < max_retries) {
            // Verificar conectividad antes de cada intento
            if (!check_firebase_connectivity()) {
                firebase_free_value(&value);
                free(json_string);
                return false;
            }
            
            // Enviar datos a Firebase en la ruta /batteries/{uid}/
            if (firebase_update(firebase_handle, device_path.c_str(), &value)) {
                BI_DEBUG_INFO(g_FirebaseLogger, "Datos del pack de batería actualizados correctamente");
                result = true;
                break;
            } else {
                retry_count++;
                if (retry_count < max_retries) {
                    BI_DEBUG_WARNING(g_FirebaseLogger, "Reintentando actualización del pack (%d/%d)", 
                                 retry_count, max_retries);
                    vTaskDelay(pdMS_TO_TICKS(1000)); // Esperar 1 segundo antes de reintentar
                } else {
                    BI_DEBUG_ERROR(g_FirebaseLogger, "Error al actualizar datos del pack después de %d intentos", 
                                  max_retries);
                }
            }
        }
        
        // Liberar recursos
        firebase_free_value(&value);
    }
    
    free(json_string);
    return result;
}

// Tarea para simular la lectura de sensores
void firebase_task(void *pvParameters) {
    TickType_t last_wake_time = xTaskGetTickCount();
    DeviceParams& params = biParams.getParams();
    DeviceCounters& counters = biParams.getCounters();
    DeviceState& state = biParams.getState();

    while (1) {
        if(!state.firebaseConnected)
        {
            if(state.wifiConnected){
                state.firebaseConnected = init_firebase();
                biParams.saveState();

                // Registrar listener para configuracion y commandos
                firebase_listen(firebase_handle, (device_path + "/config").c_str(), firebase_listen_callback, (void*)RTDB_CONFIG_CHANGED);
                firebase_listen(firebase_handle, (device_path + "/commands").c_str(), firebase_listen_callback, (void*)RTDB_COMMAND_CHANGED);
            }
        }

        else{
            if(!state.wifiConnected  && state.firebaseConnected) {
                // Desconectar de firebase
                firebase_deinit(firebase_handle);
                state.firebaseConnected = false;
                biParams.saveState();
                
            }
            else{
                // Verificar autenticación
                if (!firebase_maintain_auth(firebase_handle)) {
                    BI_DEBUG_ERROR(g_FirebaseLogger, "Error en el mantenimiento de la autenticación");
                }
            }
        }
    
        // Dormir hasta el próximo ciclo usando el intervalo de muestreo configurado
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(params.sampleInterval * 1000));
    }
}


void firebase_controller_init(void) {

    // Crear tareas de firebase
    xTaskCreate(firebase_task, "firebase_task", 8192, NULL, 5, NULL);
}