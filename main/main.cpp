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

    BI_DEBUG_INFO(g_mainLogger, "Inicializacion del sistema");

    // Inicializa parametros
    biParams.init();
    biParams.incrementCounter("bootCount");

    biParams.printState();
    biParams.resetState();

    // Inicializa controlador de bateria
    battery_controller_init();

    // Inicializa WiFi
    wifi_controller_init();

    // Inicializa firebase
    firebase_controller_init();



    while (1) {
        // Agrega tu código principal aqui
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        //biParams.printParams();
        //biParams.printCounters();
        //biParams.printState();

    }
}