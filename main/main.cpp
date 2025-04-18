#include <stdio.h>
#include "WiFi/wifi_controller.h"
#include "freertos/FreeRTOS.h"
#include "bi_params.hpp"
#include "Firebase/firebase_controller.h"
BIParams biParams;

extern "C" void app_main(void)
{
    // Inicializa parametros
    biParams.init();
    biParams.incrementCounter("bootCount");

    biParams.printState();
    biParams.resetState();

    // Inicializa WiFi
    wifi_controller_init();

    // Inicializa firebase
    firebase_controller_init();

    while (1) {
        // Agrega tu código principal aqui
        vTaskDelay(pdMS_TO_TICKS(40000));
        
        biParams.printParams();
        biParams.printCounters();
        biParams.printState();

    }
}