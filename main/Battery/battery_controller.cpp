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
#include "bi_params.hpp"

extern BIParams biParams;
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
    if (rand() % 1000 == 0 && m_soh > 80) {
        m_soh -= 1;
    }
}

// Constructor de Pack - Actualizado para usar configuración
Pack::Pack() : m_uptime(0), m_cellCount(0) {
    // Constructor vacío - se inicializará con init()
}

bool Pack::init(uint16_t cellCount) {
    if (cellCount == 0) {
        BI_DEBUG_ERROR(g_BatteryLogger, "Invalid cell count: %d", cellCount);
        return false;
    }
    
    // Limpiar celdas existentes si las hay
    m_cells.clear();
    
    m_cellCount = cellCount;
    
    // Crear las celdas - CORREGIDO: usar push_back con constructor
    m_cells.reserve(cellCount);
    for (uint16_t i = 0; i < cellCount; ++i) {
        m_cells.push_back(Cell(i + 1));  // Crear Cell con ID específico
    }
    
    // Inicializar otros valores
    m_status = PackStatus::IDLE;
    m_uptime = 0;
    update();  // Esto actualizará los valores derivados
    
    BI_DEBUG_INFO(g_BatteryLogger, "Pack initialized with %d cells", cellCount);
    return true;
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
    // 5% de probabilidad de cambiar de estado (menos frecuente)
    if (rand() % 20 == 0) {
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

bool Pack::reconfigure(uint16_t newCellCount) {
    if (newCellCount == 0) {
        BI_DEBUG_ERROR(g_BatteryLogger, "Invalid new cell count: %d", newCellCount);
        return false;
    }
    
    if (newCellCount == m_cellCount) {
        BI_DEBUG_INFO(g_BatteryLogger, "Cell count unchanged: %d", newCellCount);
        return true;
    }
    
    BI_DEBUG_INFO(g_BatteryLogger, "Reconfiguring pack from %d to %d cells", m_cellCount, newCellCount);
    
    if (newCellCount > m_cellCount) {
        // Añadir nuevas celdas
        m_cells.reserve(newCellCount);
        for (uint16_t i = m_cellCount; i < newCellCount; ++i) {
            m_cells.push_back(Cell(i + 1));
        }
    } else if (newCellCount < m_cellCount) {
        // Remover celdas excedentes
        m_cells.erase(m_cells.begin() + newCellCount, m_cells.end());
    }
    
    m_cellCount = newCellCount;
    
    // Actualizar para recalcular valores
    update();
    
    BI_DEBUG_INFO(g_BatteryLogger, "Pack reconfigured successfully to %d cells", newCellCount);
    return true;
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

bool BatteryController::init() {
    if (m_initialized) {
        return true;  // Ya inicializado
    }
    
    // Sembrar el generador de números aleatorios
    srand(static_cast<unsigned int>(time(nullptr)));
    
    // Obtener número de celdas desde configuración
    uint16_t cellCount = DEFAULT_CELL_COUNT;
    if (biParams.isInitialized()) {
        cellCount = biParams.getCellCount();
    }
    
    // Inicializar el pack con el número de celdas especificado
    if (!m_pack.init(cellCount)) {
        BI_DEBUG_ERROR(g_BatteryLogger, "Failed to initialize pack with %d cells", cellCount);
        return false;
    }
    
    m_initialized = true;
    
    BI_DEBUG_INFO(g_BatteryLogger, "Battery controller initialized with %d cells", cellCount);
    return true;
}

bool BatteryController::reconfigureCells(uint16_t newCellCount) {
    if (!m_initialized) {
        BI_DEBUG_ERROR(g_BatteryLogger, "Cannot reconfigure: controller not initialized");
        return false;
    }
    
    if (newCellCount < MIN_CELL_COUNT || newCellCount > MAX_CELL_COUNT) {
        BI_DEBUG_ERROR(g_BatteryLogger, "Invalid cell count %d. Must be between %d and %d", 
                      newCellCount, MIN_CELL_COUNT, MAX_CELL_COUNT);
        return false;
    }
    
    return m_pack.reconfigure(newCellCount);
}

void BatteryController::update() {
    if (!m_initialized) {
        return;
    }
    
    // Actualizar el pack
    m_pack.update();
    
    // Log de algunos valores (solo cada 10 actualizaciones para reducir spam)
    static int update_counter = 0;
    if (++update_counter >= 10) {
        BI_DEBUG_VERBOSE(g_BatteryLogger, "Battery status: %s, Voltage: %.2fV, Current: %.2fA, Power: %.2fW, Cells: %d",
                        m_pack.getStatusString(), m_pack.getTotalVoltage(), m_pack.getCurrent(), 
                        m_pack.getPower(), m_pack.getCellCount());
        update_counter = 0;
    }
}

void BatteryController::batteryTask(void* pvParameters) {
    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t updateInterval = pdMS_TO_TICKS(1000);  // Actualizar controlador cada 1 segundo
    
    static uint32_t lastStoreTime = 0;
    static uint32_t lastHistoryTime = 0;
    static uint32_t lastConfigCheck = 0;
    static uint8_t lastCellCount = 0;
    
    // Variables para intervalos configurables (en ms)
    static uint32_t currentStoreInterval = 5000;    // Por defecto 5 segundos
    static uint32_t currentHistoryInterval = 3600000; // Por defecto 1 hora
    
    while (true) {
        uint32_t currentTime = xTaskGetTickCount();
        
        // Actualizar el controlador cada segundo
        g_batteryController.update();
        
        // Verificar configuración cada 10 segundos para actualizaciones dinámicas
        if ((currentTime - lastConfigCheck) >= pdMS_TO_TICKS(10000)) {
            if (biParams.isInitialized()) {
                DeviceParams& params = biParams.getParams();
                
                // Verificar si cambió el número de celdas
                if (lastCellCount == 0) {
                    // Primera inicialización
                    lastCellCount = params.cellCount;
                } else if (lastCellCount != params.cellCount) {
                    BI_DEBUG_INFO(g_BatteryLogger, "Cell count configuration changed from %d to %d", 
                                 lastCellCount, params.cellCount);
                    
                    if (g_batteryController.reconfigureCells(params.cellCount)) {
                        lastCellCount = params.cellCount;
                        BI_DEBUG_INFO(g_BatteryLogger, "Successfully reconfigured to %d cells", params.cellCount);
                    } else {
                        BI_DEBUG_ERROR(g_BatteryLogger, "Failed to reconfigure cells, reverting to %d", lastCellCount);
                        // Revertir parámetro si falla la reconfiguración
                        biParams.setCellCount(lastCellCount);
                    }
                }
                
                // Convertir de segundos a milisegundos
                currentStoreInterval = params.sampleInterval * 1000;
                
                // El intervalo de históricos puede ser configurable o fijo
                // Por ahora mantenemos 1 hora, pero se podría añadir otro parámetro
                currentHistoryInterval = 3600000; // 1 hora fija
                
                // Validar intervalos mínimos para evitar sobrecarga
                if (currentStoreInterval < 1000) {
                    currentStoreInterval = 1000; // Mínimo 1 segundo
                }
                
                BI_DEBUG_VERBOSE(g_BatteryLogger, "Config: Cells=%d, Store=%lums, History=%lums", 
                                params.cellCount, currentStoreInterval, currentHistoryInterval);
            }
            lastConfigCheck = currentTime;
        }
        
        // Almacenar datos en tiempo real según configuración
        if ((currentTime - lastStoreTime) >= pdMS_TO_TICKS(currentStoreInterval) && 
            check_firebase_connectivity()) {
            
            // Preparar datos para Firebase
            const Pack& pack = g_batteryController.getPack();
            const std::vector<Cell>& cells = pack.getCells();
            
            if (!cells.empty()) {
                // Crear un array con los datos de las celdas
                battery_cell_t* cellData = new battery_cell_t[cells.size()];
                
                for (size_t i = 0; i < cells.size(); ++i) {
                    cellData[i].voltage = cells[i].getVoltage();
                    cellData[i].temperature = cells[i].getTemperature();
                    cellData[i].soc = cells[i].getSOC();
                    cellData[i].soh = cells[i].getSOH();
                }
                
                // Actualizar Firebase con los datos de las celdas
                if (update_battery_cells(cellData, cells.size())) {
                    BI_DEBUG_VERBOSE(g_BatteryLogger, "Cell data updated in Firebase (%zu cells)", cells.size());
                }
                
                // Actualizar también los datos del pack
                if (update_battery_pack(pack.getTotalVoltage(), pack.getCurrent(), 
                                      pack.getPower(), pack.getStatusString(), pack.getUptime())) {
                    BI_DEBUG_VERBOSE(g_BatteryLogger, "Pack data updated in Firebase");
                }
                
                // Verificar si es momento de almacenar histórico
                if ((currentTime - lastHistoryTime) >= pdMS_TO_TICKS(currentHistoryInterval)) {
                    if (store_battery_history(cellData, cells.size(), pack.getTotalVoltage(), 
                                            pack.getCurrent(), pack.getPower(), pack.getStatusString())) {
                        BI_DEBUG_INFO(g_BatteryLogger, "Historical record stored (%zu cells)", cells.size());
                        lastHistoryTime = currentTime;
                    } else {
                        BI_DEBUG_WARNING(g_BatteryLogger, "Failed to store historical record");
                    }
                }
                
                // Liberar memoria
                delete[] cellData;
                
                // Incrementar contador de puntos de datos
                biParams.incrementCounter("dataPoints", 1, false);
            }
            
            // Actualizar la marca de tiempo
            lastStoreTime = currentTime;
        }
        
        // Verificar alertas de temperatura y voltaje
        if (biParams.isInitialized()) {
            checkBatteryAlerts();
        }
        
        // Esperar hasta el próximo intervalo de actualización
        vTaskDelayUntil(&lastWakeTime, updateInterval);
    }
}

// Nueva función para verificar alertas
void BatteryController::checkBatteryAlerts() {
    if (!biParams.isInitialized()) return;
    
    DeviceParams& params = biParams.getParams();
    const Pack& pack = g_batteryController.getPack();
    const std::vector<Cell>& cells = pack.getCells();
    
    static uint32_t lastAlertTime = 0;
    uint32_t currentTime = xTaskGetTickCount();
    
    // Verificar alertas solo cada 30 segundos para evitar spam
    if ((currentTime - lastAlertTime) < pdMS_TO_TICKS(30000)) {
        return;
    }
    
    bool alertTriggered = false;
    char alertMessage[128];
    
    // Verificar alertas por celda
    for (size_t i = 0; i < cells.size(); ++i) {
        const Cell& cell = cells[i];
        
        // Alerta de temperatura alta
        if (cell.getTemperature() > params.alertHighTemp) {
            snprintf(alertMessage, sizeof(alertMessage), 
                    "High temp cell %d: %.1f°C (limit: %.1f°C)", 
                    (int)(i + 1), cell.getTemperature(), params.alertHighTemp);
            BI_DEBUG_WARNING(g_BatteryLogger, "%s", alertMessage);
            biParams.updateStateValue("lastError", alertMessage, strlen(alertMessage), true);
            alertTriggered = true;
        }
        
        // Alerta de temperatura baja
        if (cell.getTemperature() < params.alertLowTemp) {
            snprintf(alertMessage, sizeof(alertMessage), 
                    "Low temp cell %d: %.1f°C (limit: %.1f°C)", 
                    (int)(i + 1), cell.getTemperature(), params.alertLowTemp);
            BI_DEBUG_WARNING(g_BatteryLogger, "%s", alertMessage);
            biParams.updateStateValue("lastError", alertMessage, strlen(alertMessage), true);
            alertTriggered = true;
        }
        
        // Alerta de voltaje alto
        if (cell.getVoltage() > params.alertHighVoltage) {
            snprintf(alertMessage, sizeof(alertMessage), 
                    "High voltage cell %d: %.2fV (limit: %.2fV)", 
                    (int)(i + 1), cell.getVoltage(), params.alertHighVoltage);
            BI_DEBUG_WARNING(g_BatteryLogger, "%s", alertMessage);
            biParams.updateStateValue("lastError", alertMessage, strlen(alertMessage), true);
            alertTriggered = true;
        }
        
        // Alerta de voltaje bajo
        if (cell.getVoltage() < params.alertLowVoltage) {
            snprintf(alertMessage, sizeof(alertMessage), 
                    "Low voltage cell %d: %.2fV (limit: %.2fV)", 
                    (int)(i + 1), cell.getVoltage(), params.alertLowVoltage);
            BI_DEBUG_WARNING(g_BatteryLogger, "%s", alertMessage);
            biParams.updateStateValue("lastError", alertMessage, strlen(alertMessage), true);
            alertTriggered = true;
        }
    }
    
    // Verificar límite de corriente
    if (fabs(pack.getCurrent()) > params.maxCurrent) {
        snprintf(alertMessage, sizeof(alertMessage), 
                "Excessive current: %.2fA (limit: %.2fA)", 
                pack.getCurrent(), params.maxCurrent);
        BI_DEBUG_WARNING(g_BatteryLogger, "%s", alertMessage);
        biParams.updateStateValue("lastError", alertMessage, strlen(alertMessage), true);
        alertTriggered = true;
    }
    
    // Verificar voltaje de apagado
    if (pack.getTotalVoltage() < (params.shutdownVoltage * cells.size())) {
        snprintf(alertMessage, sizeof(alertMessage), 
                "Critical pack voltage: %.2fV (limit: %.2fV)", 
                pack.getTotalVoltage(), params.shutdownVoltage * cells.size());
        BI_DEBUG_ERROR(g_BatteryLogger, "%s", alertMessage);
        biParams.updateStateValue("lastError", alertMessage, strlen(alertMessage), true);
        alertTriggered = true;
        
        // Si está habilitado el auto-shutdown, apagar el sistema
        if (params.deepSleepEnabled) {
            BI_DEBUG_ERROR(g_BatteryLogger, "Initiating auto-shutdown for critical voltage");
            // Aquí se podría implementar el apagado real del sistema
        }
    }
    
    if (alertTriggered) {
        biParams.incrementCounter("errorCount", 1, true);
        lastAlertTime = currentTime;
    }
}

// Función para verificar necesidad de balanceo
bool BatteryController::shouldStartBalancing() {
    if (!biParams.isInitialized()) return false;
    
    DeviceParams& params = biParams.getParams();
    if (!params.balancingEnabled) return false;
    
    const Pack& pack = g_batteryController.getPack();
    const std::vector<Cell>& cells = pack.getCells();
    
    if (cells.size() < 2) return false;
    
    // Encontrar voltajes mínimo y máximo
    float minVoltage = cells[0].getVoltage();
    float maxVoltage = cells[0].getVoltage();
    
    for (const auto& cell : cells) {
        if (cell.getVoltage() < minVoltage) {
            minVoltage = cell.getVoltage();
        }
        if (cell.getVoltage() > maxVoltage) {
            maxVoltage = cell.getVoltage();
        }
    }
    
    float voltageDifference = maxVoltage - minVoltage;
    
    BI_DEBUG_VERBOSE(g_BatteryLogger, "Voltage difference: %.3fV (threshold: %.3fV)", 
                    voltageDifference, params.balancingThreshold);
    
    return voltageDifference > params.balancingThreshold;
}

// Función de inicialización global
void battery_controller_init() {
    // Inicializar el logger
    g_BatteryLogger = createLogger("BATTERY_CTRL", INFO, DEBUG_BATTERY);
    
    BI_DEBUG_INFO(g_BatteryLogger, "Initializing battery controller");
    
    // Inicializar el controlador
    if (g_batteryController.init()) {
        // Crear la tarea de actualización de batería
        xTaskCreate(BatteryController::batteryTask, "battery_task", 4096 * 2, NULL, 5, NULL);
        BI_DEBUG_INFO(g_BatteryLogger, "Battery controller task created");
    } else {
        BI_DEBUG_ERROR(g_BatteryLogger, "Failed to initialize battery controller");
    }
}