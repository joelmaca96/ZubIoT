#ifndef WIFI_CONTROLLER_H
#define WIFI_CONTROLLER_H

enum class ConnectionType {
    PROVISIONING,
    NEW_CREDENTIALS,
    STORED_CREDENTIALS
};


bool wifi_controller_init(void);
bool wifi_controller_provision(void);
bool wifi_controller_connect(ConnectionType conexion, const char *ssid = nullptr, const char *password = nullptr, bool save = true);
bool wifi_controller_disconnect(void);

#endif // WIFI_CONTROLLER_H
