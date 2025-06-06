#include <stdio.h>
#include "WiFi/wifi_controller.h"
#include "freertos/FreeRTOS.h"
#include "bi_params.hpp"
#include "Firebase/firebase_controller.h"
#include "custom_config.h"
#include "bi_debug.h"
#include "Battery/battery_controller.h"

BIParams biParams;
LoggerPtr g_mainLogger;

extern "C" void app_main(void)
{
    g_mainLogger = createLogger("MAIN", INFO, DEBUG_MAIN);

    BI_DEBUG_INFO(g_mainLogger, "Sistema de gestión de baterías Bihar iniciando...");

    // Inicializa parametros y actualiza contadores arranque
    biParams.init();
    biParams.incrementCounter("bootCount");

    biParams.printState();
    biParams.resetState();

    // Inicializa controlador de bateria (usa configuración de celdas automáticamente)
    battery_controller_init();

    // Inicializa WiFi
    wifi_controller_init();

    // Inicializa firebase
    firebase_controller_init();

    BI_DEBUG_INFO(g_mainLogger, "Sistema Bihar inicializado correctamente");
    BI_DEBUG_INFO(g_mainLogger, "Monitoreo de %d celdas activo", biParams.getCellCount());

    while (1) {
        // Agrega tu código principal aqui
        vTaskDelay(pdMS_TO_TICKS(10000)); // 10 segundos
        
    }
}