/**
 * @file firebase_example.c
 * @brief Ejemplo de uso de la librer�a Firebase Realtime Database para ESP32 con ESP-IDF
 */

#include "bi_firebase.h"
#include "bi_params.hpp"
#include "secrets.h"
#include "firebase_controller.h"

// Configuraci�n Firebase
static const char *TAG = "FIREBASE_CONTROLLER";

extern BIParams biParams;
static std::string listen_path = "/devices/"+  std::string(biParams.getParams().deviceName) +"/listen_ts";

// Manejador para Firebase
firebase_handle_t *firebase_handle = NULL;

// Callback para eventos de Firebase
void firebase_event_callback(void *data, int event_id) {
    ESP_LOGI(TAG, "Evento Firebase recibido: %d", event_id);
    DeviceParams& params = biParams.getParams();

    // Actualizar configuraci�n del dispositivo desde Firebase
    if (event_id == 1) { // Suponiendo que 1 es el ID del listener de configuraci�n
        firebase_data_value_t config_value;

        if (firebase_get(firebase_handle, "/devices/config", &config_value)) {
            if (config_value.type == FIREBASE_DATA_TYPE_JSON && config_value.data.string_val) {
                cJSON *json = cJSON_Parse(config_value.data.string_val);

                if (json) {
                    cJSON *interval = cJSON_GetObjectItem(json, "sampling_interval");
                    if (interval && cJSON_IsNumber(interval)) {
                        params.sampleInterval = interval->valueint;
                        biParams.saveParams();
                        ESP_LOGI(TAG, "Intervalo actualizado: %d segundos", params.sampleInterval);
                    }

                    cJSON *name = cJSON_GetObjectItem(json, "device_name");
                    if (name && cJSON_IsString(name)) {
                        strncpy(params.deviceName, name->valuestring, sizeof(params.deviceName) - 1);
                        biParams.saveParams();
                        ESP_LOGI(TAG, "Nombre dispositivo: %s", (params.deviceName));
                    }

                    cJSON_Delete(json);
                }
            }

            firebase_free_value(&config_value);
        }
    }
}

// Inicializar Firebase y autenticarse
bool init_firebase(void) {
    // Configurar Firebase
    firebase_config_t config = {.host              = FIREBASE_HOST,
                                .database_url      = FIREBASE_DATABASE_URL,
                                .auth              = {.auth_type     = FIREBASE_AUTH_API_KEY,
                                                      .api_key       = FIREBASE_API_KEY,
                                                      .user_email    = NULL,
                                                      .user_password = NULL},
                                .event_callback    = firebase_event_callback,
                                .user_data         = NULL,
                                .timeout_ms        = 10000,
                                .secure_connection = true};

    // Configurar HTTP para Firebase
    config.http_config.cert_pem       = NULL;
    config.http_config.is_async       = false;
    config.http_config.timeout_ms     = 10000;
    config.http_config.transport_type = HTTP_TRANSPORT_OVER_TCP;

    // Inicializar Firebase
    firebase_handle = firebase_init(&config);
    if (!firebase_handle) {
        ESP_LOGE(TAG, "Error al inicializar Firebase");
        return false;
    }

    // Autenticarse con Firebase
    if (!firebase_auth_with_password(firebase_handle, FIREBASE_EMAIL, FIREBASE_PASSWORD)) {
        ESP_LOGE(TAG, "Error al autenticarse con Firebase");
        return false;
    }

    ESP_LOGI(TAG, "Firebase inicializado y autenticado correctamente");
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
            if (firebase_update(firebase_handle, "/devices/data", &value)) {
                ESP_LOGI(TAG, "Datos enviados correctamente a Firebase");
            } else {
                ESP_LOGE(TAG, "Error al enviar datos a Firebase");
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
                ESP_LOGI(TAG, "Registro hist�rico guardado con clave: %s", key);
            } else {
                ESP_LOGE(TAG, "Error al guardar registro hist�rico");
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

                // Registrar listener para cambios en la configuraci�n
                firebase_listen(firebase_handle, , firebase_event_callback, NULL);
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
    xTaskCreate(firebase_task, "firebase_task", 4096, NULL, 5, NULL);
}