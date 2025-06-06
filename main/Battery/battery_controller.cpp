/// battery_controller.cpp
#include "battery_controller.h"
#include "bi_debug.h"
#include "../Firebase/firebase_controller.h"
#include <cstdlib>
#include <ctime>
#include <cmath>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "../custom_config.h"

// Logger para el controlador de batería
static LoggerPtr g_BatteryLogger;

// Instancia global del controlador
static BatteryController g_batteryController;

// Constantes de simulación
constexpr float MIN_CELL_VOLTAGE = 3.0f;
constexpr float MAX_CELL_VOLTAGE = 4.2f;
constexpr float NOMINAL_CELL_VOLTAGE = 3.7f;
constexpr float MIN_TEMPERATURE = 10.0f;
constexpr float MAX_TEMPERATURE = 45.0f;
constexpr float MIN_CURRENT = -10.0f;  // Descarga máxima
constexpr float MAX_CURRENT = 5.0f;    // Carga máxima

// Genera un número aleatorio entre min y max
float randomFloat(float min, float max) {
    return min + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (max - min)));
}

// Constructor de Cell
Cell::Cell(uint16_t id) : m_id(id) {
    // Inicializar con valores aleatorios pero coherentes
    m_voltage = randomFloat(NOMINAL_CELL_VOLTAGE - 0.2f, NOMINAL_CELL_VOLTAGE + 0.2f);
    m_temperature = randomFloat(20.0f, 30.0f);
    m_soc = static_cast<uint8_t>(randomFloat(70.0f, 90.0f));
    m_soh = static_cast<uint8_t>(randomFloat(90.0f, 100.0f));
}

void Cell::update() {
    // Simular ligeras variaciones en voltaje (±0.05V)
    m_voltage += randomFloat(-0.05f, 0.05f);
    // Asegurar que el voltaje esté dentro de límites
    if (m_voltage < MIN_CELL_VOLTAGE) m_voltage = MIN_CELL_VOLTAGE;
    if (m_voltage > MAX_CELL_VOLTAGE) m_voltage = MAX_CELL_VOLTAGE;
    
    // Simular variaciones en temperatura (±0.5°C)
    m_temperature += randomFloat(-0.5f, 0.5f);
    // Asegurar que la temperatura esté dentro de límites
    if (m_temperature < MIN_TEMPERATURE) m_temperature = MIN_TEMPERATURE;
    if (m_temperature > MAX_TEMPERATURE) m_temperature = MAX_TEMPERATURE;
    
    // Calcular el SOC basado en el voltaje (simplificado)
    // Mapeo lineal entre MIN_VOLTAGE (0% SOC) y MAX_VOLTAGE (100% SOC)
    float socPercentage = (m_voltage - MIN_CELL_VOLTAGE) / (MAX_CELL_VOLTAGE - MIN_CELL_VOLTAGE) * 100.0f;
    m_soc = static_cast<uint8_t>(std::max(0.0f, std::min(100.0f, socPercentage)));
    
    // El SOH disminuye muy lentamente con el tiempo (simulación)
    if (rand() % 100 == 0 && m_soh > 80) {
        m_soh -= 1;
    }
}

// Constructor de Pack
Pack::Pack(uint16_t cellCount) : m_uptime(0), m_cellCount(cellCount) {
    // Crear las celdas
    m_cells.reserve(cellCount);
    for (uint16_t i = 0; i < cellCount; ++i) {
        m_cells.emplace_back(i + 1);  // IDs comienzan en 1
    }
    
    // Inicializar otros valores
    m_status = PackStatus::IDLE;
    update();  // Esto actualizará los valores derivados
}

void Pack::update() {
    // Actualizar todas las celdas
    for (auto& cell : m_cells) {
        cell.update();
    }
    
    // Calcular el voltaje total sumando todas las celdas
    m_totalVoltage = 0.0f;
    for (const auto& cell : m_cells) {
        m_totalVoltage += cell.getVoltage();
    }
    
    // Simular corriente basada en el estado
    // 10% de probabilidad de cambiar de estado
    if (rand() % 10 == 0) {
        int newState = rand() % 5;
        m_status = static_cast<PackStatus>(newState);
    }
    
    // Ajustar corriente según el estado
    switch (m_status) {
        case PackStatus::IDLE:
            m_current = randomFloat(-0.1f, 0.1f);
            break;
        case PackStatus::CHARGING:
            m_current = randomFloat(1.0f, MAX_CURRENT);
            break;
        case PackStatus::DISCHARGING:
            m_current = randomFloat(MIN_CURRENT, -1.0f);
            break;
        case PackStatus::ERROR:
            m_current = 0.0f;
            break;
        case PackStatus::BALANCING:
            m_current = randomFloat(-0.5f, 0.5f);
            break;
    }
    
    // Calcular potencia (P = V * I)
    m_power = m_totalVoltage * m_current;
    
    // Incrementar el tiempo de funcionamiento
    m_uptime += 1;
}

const char* Pack::getStatusString() const {
    switch (m_status) {
        case PackStatus::IDLE: return "Idle";
        case PackStatus::CHARGING: return "Charging";
        case PackStatus::DISCHARGING: return "Discharging";
        case PackStatus::ERROR: return "Error";
        case PackStatus::BALANCING: return "Balancing";
        default: return "Unknown";
    }
}

// Implementación de BatteryController
BatteryController::BatteryController() : m_initialized(false) {}

bool BatteryController::init(uint16_t cellCount) {
    if (m_initialized) {
        return true;  // Ya inicializado
    }
    
    // Sembrar el generador de números aleatorios
    srand(static_cast<unsigned int>(time(nullptr)));
    
    // Inicializar el pack con el número de celdas especificado
    m_pack = Pack(cellCount);
    m_initialized = true;
    
    BI_DEBUG_INFO(g_BatteryLogger, "Battery controller initialized with %d cells", cellCount);
    return true;
}

void BatteryController::update() {
    if (!m_initialized) {
        return;
    }
    
    // Actualizar el pack
    m_pack.update();
    
    // Log de algunos valores
    BI_DEBUG_INFO(g_BatteryLogger, "Battery status: %s, Voltage: %.2fV, Current: %.2fA, Power: %.2fW",
                 m_pack.getStatusString(), m_pack.getTotalVoltage(), m_pack.getCurrent(), m_pack.getPower());
}

// Tarea FreeRTOS para el controlador de batería
void BatteryController::batteryTask(void* pvParameters) {
    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t interval = pdMS_TO_TICKS(5000);  // Actualizar cada 5 segundos

    static uint32_t lastStoreTime = 0;
    const uint32_t storeInterval = pdMS_TO_TICKS(10000);  // 10 segundos

    static uint32_t lastStoreTimeHour = 0;
    const uint32_t storeIntervalHour = pdMS_TO_TICKS(3600000);  // 1 hora
       
    while (true) {
        // Actualizar el controlador
        g_batteryController.update();
        
        if ((xTaskGetTickCount() - lastStoreTime) >= storeInterval && check_firebase_connectivity()) {
            // Preparar datos para Firebase
            const Pack& pack = g_batteryController.getPack();
            const std::vector<Cell>& cells = pack.getCells();
            
            // Crear un array con los datos de las celdas
            battery_cell_t* cellData = new battery_cell_t[cells.size()];
            
            for (size_t i = 0; i < cells.size(); ++i) {
                cellData[i].voltage = cells[i].getVoltage();
                cellData[i].temperature = cells[i].getTemperature();
                cellData[i].soc = cells[i].getSOC();
                cellData[i].soh = cells[i].getSOH();
            }
            
            // Actualizar Firebase con los datos de las celdas
            update_battery_cells(cellData, cells.size());
                        
            // Actualizar también los datos del pack
            update_battery_pack(pack.getTotalVoltage(), pack.getCurrent(), pack.getPower(), pack.getStatusString(), pack.getUptime());
            
            if ((xTaskGetTickCount() - lastStoreTimeHour) >= storeIntervalHour) {
                store_battery_history(cellData, cells.size(), pack.getTotalVoltage(), pack.getCurrent(), pack.getPower(), pack.getStatusString());
                lastStoreTimeHour = xTaskGetTickCount();
            }
            
            // Liberar memoria
            delete[] cellData;
            
            // Actualizar la marca de tiempo
            lastStoreTime = xTaskGetTickCount();
        }
       
        // Esperar hasta el próximo intervalo
        vTaskDelayUntil(&lastWakeTime, interval);
    }
}

// Función de inicialización global
void battery_controller_init() {
    // Inicializar el logger
    g_BatteryLogger = createLogger("BATTERY_CTRL", INFO, DEBUG_BATTERY);
    
    BI_DEBUG_INFO(g_BatteryLogger, "Initializing battery controller");
    
    // Inicializar el controlador con 4 celdas por defecto
    if (g_batteryController.init(4)) {
        // Crear la tarea de actualización de batería
        xTaskCreate(BatteryController::batteryTask, "battery_task", 4096 * 2, NULL, 5, NULL);
        BI_DEBUG_INFO(g_BatteryLogger, "Battery controller task created");
    } else {
        BI_DEBUG_ERROR(g_BatteryLogger, "Failed to initialize battery controller");
    }
}