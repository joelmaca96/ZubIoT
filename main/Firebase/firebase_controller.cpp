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

extern BIParams biParams;
static std::string device_path = "/batteries/";

LoggerPtr g_FirebaseLogger;

// Manejador para Firebase
firebase_handle_t *firebase_handle = NULL;

// Callback para eventos de Firebase
void firebase_event_callback(void *data, int event_id) {
    BI_DEBUG_INFO(g_FirebaseLogger, "Evento Firebase recibido: %d", event_id);
    DeviceParams& params = biParams.getParams();

    // Actualizar configuración del dispositivo desde Firebase
    switch (event_id){
        case RTDB_CONFIG_CHANGED:
        {
            firebase_data_value_t config_value;

            if (firebase_get(firebase_handle, (device_path+"/config").c_str(), &config_value)) {
                if (config_value.type == FIREBASE_DATA_TYPE_JSON && config_value.data.string_val) {
                    cJSON *json = cJSON_Parse(config_value.data.string_val);
                    if (json) {
                        /*
                        cJSON *interval = cJSON_GetObjectItem(json, "sampling_interval");
                        if (interval && cJSON_IsNumber(interval)) {
                            params.sampleInterval = interval->valueint;
                            biParams.saveParams();
                            BI_DEBUG_INFO(g_FirebaseLogger, "Intervalo actualizado: %d segundos", params.sampleInterval);
                        }*/

                        cJSON *name = cJSON_GetObjectItem(json, "name");
                        if (name && cJSON_IsString(name)) {
                            strncpy(params.deviceName, name->valuestring, sizeof(params.deviceName) - 1);
                            biParams.saveParams();
                            BI_DEBUG_INFO(g_FirebaseLogger, "Nombre dispositivo: %s", (params.deviceName));
                        }

                        cJSON_Delete(json);
                    }
                }

                firebase_free_value(&config_value);
            }
            break;
        }
        case RTDB_COMMAND_CHANGED:
            firebase_data_value_t command_value;
            if (firebase_get(firebase_handle, (device_path+"/commands").c_str(), &command_value)) {
            }
            firebase_free_value(&command_value);
            break;
        default:
            break;

    }
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
                                                      .user_password = FIREBASE_PASSWORD},
                                .event_callback    = firebase_event_callback,
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

// Enviar datos del sensor a Firebase
void send_sensor_data(void) {
    if (!firebase_handle)
        return;

    // Crear objeto JSON para los datos del sensor
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "temperature", 10);
    cJSON_AddNumberToObject(json, "humidity", 20);
    cJSON_AddNumberToObject(json, "pressure", 30);
    cJSON_AddNumberToObject(json, "battery", 40);
    cJSON_AddNumberToObject(json, "timestamp", esp_timer_get_time() / 1000); // Timestamp en ms

    char *json_string = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (json_string) {
        // Crear valor Firebase de tipo JSON
        firebase_data_value_t value;
        if (firebase_set_json(&value, json_string)) {
            // Enviar datos a Firebase en la ruta /devices/data
            if (firebase_update(firebase_handle, (device_path + "/status").c_str(), &value)) {
                BI_DEBUG_INFO(g_FirebaseLogger, "Datos enviados correctamente a Firebase");
            } else {
                BI_DEBUG_ERROR(g_FirebaseLogger, "Error al enviar datos a Firebase");
            }

            // Liberar recursos
            firebase_free_value(&value);
        }

        free(json_string);
    }
}

// Guardar un registro hist�rico de datos usando push()
void 
log_sensor_data(void) {
    if (!firebase_handle)
        return;

    // Crear objeto JSON para los datos del sensor
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "temperature", 20);
    cJSON_AddNumberToObject(json, "humidity", 30);
    cJSON_AddNumberToObject(json, "timestamp", esp_timer_get_time() / 1000); // Timestamp en ms

    char *json_string = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (json_string) {
        // Crear valor Firebase de tipo JSON
        firebase_data_value_t value;
        if (firebase_set_json(&value, json_string)) {
            // Generar una clave �nica y a�adir a la lista de registros
            char key[64] = {0};
            if (firebase_push(firebase_handle, "/devices/history", &value, key, sizeof(key))) {
                BI_DEBUG_INFO(g_FirebaseLogger, "Registro histórico guardado con clave: %s", key);
            } else {
                BI_DEBUG_ERROR(g_FirebaseLogger, "Error al guardar registro histórico");
            }

            // Liberar recursos
            firebase_free_value(&value);
        }

        free(json_string);
    }
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

                // Registrar listener para comandos
                firebase_listen(firebase_handle, (device_path + "/config").c_str(), firebase_event_callback, (void*)RTDB_CONFIG_CHANGED);
            }
        }

        else{
            if(state.wifiConnected ) {
                // Enviar datos a Firebase
                send_sensor_data();

                // Cada 10 iteraciones, guardar un registro hist�rico
                static int count = 0;
                if (++count % 10 == 0) {
                    log_sensor_data();
                }
            
            }
            else{
                // Desconectar de firebase
                firebase_deinit(firebase_handle);
                state.firebaseConnected = false;
                biParams.saveState();
                
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
