/**
 * @file firebase_example.c
 * @brief Ejemplo de uso de la librer�a Firebase Realtime Database para ESP32 con ESP-IDF
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

static void procesarComando(const char* tipo, cJSON* valor);

void firebase_listen_callback(void *data, int event_id, firebase_data_value_t *value) {
    BI_DEBUG_INFO(g_FirebaseLogger, "Evento de escucha firebase recibido: %d, data: %i", event_id, (uint32_t)(data));
    
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
                        params.deviceName[sizeof(params.deviceName) - 1] = '\0'; // Asegurar terminación nula
                        config_changed = true;
                        BI_DEBUG_INFO(g_FirebaseLogger, "Nombre dispositivo actualizado: %s", params.deviceName);
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
                            BI_DEBUG_INFO(g_FirebaseLogger, "Intervalo de muestreo actualizado: %d segundos", params.sampleInterval);
                        }
                    }
                    
                    // Actualizar configuración de deep sleep
                    cJSON *power = cJSON_GetObjectItem(json, "power");
                    if (power && cJSON_IsObject(power)) {
                        cJSON *autoShutdown = cJSON_GetObjectItem(power, "autoShutdown");
                        if (autoShutdown && cJSON_IsBool(autoShutdown)) {
                            params.deepSleepEnabled = cJSON_IsTrue(autoShutdown);
                            config_changed = true;
                            BI_DEBUG_INFO(g_FirebaseLogger, "Deep sleep %s", params.deepSleepEnabled ? "activado" : "desactivado");
                        }
                        
                        cJSON *shutdownTime = cJSON_GetObjectItem(power, "shutdownTime");
                        if (shutdownTime && cJSON_IsNumber(shutdownTime)) {
                            params.deepSleepTime = shutdownTime->valueint;
                            config_changed = true;
                            BI_DEBUG_INFO(g_FirebaseLogger, "Tiempo deep sleep actualizado: %lu segundos", params.deepSleepTime);
                        }
                    }
                    
                    // Guardar cambios si hubo alguno
                    if (config_changed) {
                        biParams.saveParams();
                        BI_DEBUG_INFO(g_FirebaseLogger, "Configuración guardada en NVS");
                    }
                    
                    cJSON_Delete(json);
                } else {
                    BI_DEBUG_ERROR(g_FirebaseLogger, "Error parseando JSON de configuración");
                }
            }
            break;
        }
        case RTDB_COMMAND_CHANGED:
        {
            // IMPORTANTE: No modificar los valores de Firebase directamente aquí
            // En su lugar, sólo leer y procesar, sin llamar a firebase_free_value()
            if (value && value->type == FIREBASE_DATA_TYPE_JSON && value->data.string_val) {
                cJSON *json = cJSON_Parse(value->data.string_val);
                if (json) {
                    // Procesar todos los comandos
                    // Para objetos individuales
                    if (cJSON_IsObject(json)) {
                        cJSON *type = cJSON_GetObjectItem(json, "type");
                        cJSON *cmd_value = cJSON_GetObjectItem(json, "value");
                        cJSON *status = cJSON_GetObjectItem(json, "status");
                        
                        // Solo procesar comandos pendientes
                        if (type && cmd_value && status && 
                            cJSON_IsString(type) && 
                            cJSON_IsString(status) && 
                            strcmp(status->valuestring, "pending") == 0) {
                            
                            procesarComando(type->valuestring, cmd_value);
                        }
                    }
                    // Para arrays de comandos
                    else if (cJSON_IsArray(json)) {
                        for (int i = 0; i < cJSON_GetArraySize(json); i++) {
                            cJSON *cmd = cJSON_GetArrayItem(json, i);
                            if (cJSON_IsObject(cmd)) {
                                cJSON *type = cJSON_GetObjectItem(cmd, "type");
                                cJSON *cmd_value = cJSON_GetObjectItem(cmd, "value");
                                cJSON *status = cJSON_GetObjectItem(cmd, "status");
                                
                                if (type && cmd_value && status && 
                                    cJSON_IsString(type) && 
                                    cJSON_IsString(status) && 
                                    strcmp(status->valuestring, "pending") == 0) {
                                    
                                    procesarComando(type->valuestring, cmd_value);
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

// Función auxiliar para procesar comandos
static void procesarComando(const char* tipo, cJSON* valor) {
    if (!tipo || !valor) return;
    
    BI_DEBUG_INFO(g_FirebaseLogger, "Procesando comando: %s", tipo);
    
    if (strcmp(tipo, "power") == 0 && cJSON_IsString(valor)) {
        if (strcmp(valor->valuestring, "on") == 0) {
            // Código para encender
            BI_DEBUG_INFO(g_FirebaseLogger, "Comando: Encender sistema");
        } else if (strcmp(valor->valuestring, "off") == 0) {
            // Código para apagar
            BI_DEBUG_INFO(g_FirebaseLogger, "Comando: Apagar sistema");
        }
    } 
    else if (strcmp(tipo, "balancing") == 0 && cJSON_IsString(valor)) {
        if (strcmp(valor->valuestring, "start") == 0) {
            // Iniciar balanceo
            BI_DEBUG_INFO(g_FirebaseLogger, "Comando: Iniciar balanceo");
        } else if (strcmp(valor->valuestring, "stop") == 0) {
            // Detener balanceo
            BI_DEBUG_INFO(g_FirebaseLogger, "Comando: Detener balanceo");
        }
    }
    else if (strcmp(tipo, "reboot") == 0) {
        // Reiniciar dispositivo
        BI_DEBUG_INFO(g_FirebaseLogger, "Comando: Reiniciar sistema");
        esp_restart(); // Descomentar para implementar
    }
    
    // Para marcar como completado, debes llamar a update_command_status() después
    // Implementar una función separada para ello
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
    
    // Obtener timestamp actual en milisegundos
    int64_t timestamp = esp_timer_get_time() / 1000; // Convertir microsegundos a milisegundos
    
    // Crear objeto JSON principal
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        BI_DEBUG_ERROR(g_FirebaseLogger, "Error creando objeto JSON principal");
        return false;
    }
    
    // Añadir timestamp
    cJSON_AddNumberToObject(json, "timestamp", timestamp);
    
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
                if (firebase_set_int(&timestamp_value, timestamp)) {
                    std::string last_update_path = device_path + "/lastUpdate";
                    firebase_set(firebase_handle, last_update_path.c_str(), &timestamp_value);
                    firebase_free_value(&timestamp_value);
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
    
        // Dormir hasta el pr�ximo ciclo usando el intervalo de muestreo configurado
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(params.sampleInterval * 1000));
    }
}


void firebase_controller_init(void) {

    // Crear tareas de firebase
    xTaskCreate(firebase_task, "firebase_task", 8192, NULL, 5, NULL);
}
