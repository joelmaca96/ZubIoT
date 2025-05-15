#include "wifi_controller.h"
#include "esp_mac.h"
#include "esp_wifi.h"

#include "bi_wifi.hpp"
#include "bi_params.hpp"
#include "bi_debug.h"
#include "../custom_config.h"

extern BIParams biParams;

WiFiManager wifi_manager;
LoggerPtr g_commLogger;

void onWiFiStateChanged(WiFiManager::WiFiState state, void* data) {
    switch (state) {
        case WiFiManager::WiFiState::DISCONNECTED:
        {
            BI_DEBUG_INFO(g_commLogger, "WiFi desconectado");
            bool connected = false;
            biParams.updateStateValue("wifiConnected", &connected, sizeof(bool), true);
           
            break;
        }
        case WiFiManager::WiFiState::CONNECTING:
            BI_DEBUG_INFO(g_commLogger, "WiFi conectando...");
            break;
        case WiFiManager::WiFiState::CONNECTED:
        {
            BI_DEBUG_INFO(g_commLogger, "WiFi conectado!");
            
            // Obtén una referencia al WiFiManager
            WiFiManager* wifi = static_cast<WiFiManager*>(data);
            if (wifi) {
                BI_DEBUG_INFO(g_commLogger, "Conectado a la red: %s", wifi->getSSID().c_str());
                BI_DEBUG_INFO(g_commLogger, "Dirección IP: %s", wifi->getIPAddress().c_str());
            }
            bool connected = true;
            biParams.updateStateValue("wifiConnected", &connected, sizeof(bool), true);
            biParams.incrementCounter("wifiConnectCount", 1, false);
            break;
        }
        case WiFiManager::WiFiState::PROVISIONING:
            BI_DEBUG_INFO(g_commLogger, "Modo de provisioning WiFi activo");
            break;
        case WiFiManager::WiFiState::ERROR:
            BI_DEBUG_ERROR(g_commLogger, "Error en la conexión WiFi");
            biParams.incrementCounter("wifiFailCount", 1, true);
            break;
    }
}
/**
 * @brief Conecta al WiFi utilizando credenciales almacenadas o inicia el
 *        modo de provisioning.
 *
 * Crea una instancia de WiFiManager y la inicializa, luego configura un
 * callback para recibir notificaciones de cambios de estado y conecta al
 * WiFi utilizando credenciales almacenadas o inicia el modo de
 * provisioning.
 *
 * El loop principal se encuentra al final de la función, puedes agregar
 * tu código principal allí.
 */
bool wifi_controller_init(void) {
    g_commLogger = createLogger("COMM", DEBUG, true);
    BI_DEBUG_INFO(g_commLogger, "Iniciando aplicación...");
    
    // Crear instancia de WiFiManager
    wifi_manager = WiFiManager("wifi");
    
    // Inicializar el gestor WiFi
    if (!wifi_manager.init()) {
        BI_DEBUG_ERROR(g_commLogger, "Error al inicializar WiFi Manager");
        return false;
    }
    
    // Configurar callback para cambios de estado
    wifi_manager.setConnectionCallback(onWiFiStateChanged, &wifi_manager);

    // Conectar al wifi
    wifi_controller_connect(ConnectionType::STORED_CREDENTIALS);
    
    return true;
}

bool wifi_controller_provision(void){
    // Generate a unique AP name based on MAC address
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char ap_name[32];
    snprintf(ap_name, sizeof(ap_name), "zubIOT_%02X%02X%02X", mac[3], mac[4], mac[5]);

    return wifi_manager.startProvisioning(ap_name);
}   


bool wifi_controller_connect(ConnectionType conexion, const char *ssid, const char *password, bool save) {

    switch (conexion) {
        case ConnectionType::PROVISIONING:
            return wifi_controller_provision();
        case ConnectionType::NEW_CREDENTIALS:
            return wifi_manager.connect(ssid, password, save);
        case ConnectionType::STORED_CREDENTIALS:
            return wifi_manager.connect();
        default:
            break;
    }

    return false;
}

bool wifi_controller_disconnect(void) {
    return wifi_manager.disconnect();
}