// battery_controller.h
#ifndef BATTERY_CONTROLLER_H
#define BATTERY_CONTROLLER_H

#include <vector>
#include <cstdint>

/**
 * @brief Clase que representa una celda individual de la batería
 */
class Cell {
private:
    uint16_t m_id;
    float m_voltage;         // Voltaje en V
    float m_temperature;     // Temperatura en °C
    uint8_t m_soc;           // Estado de carga (0-100%)
    uint8_t m_soh;           // Estado de salud (0-100%)

public:
    /**
     * @brief Constructor de la celda
     * @param id Identificador único de la celda
     */
    Cell(uint16_t id);

    /**
     * @brief Actualiza los valores de la celda con datos simulados
     */
    void update();

    /**
     * @brief Obtiene el identificador de la celda
     * @return Identificador de la celda
     */
    uint16_t getId() const { return m_id; }

    /**
     * @brief Obtiene el voltaje actual de la celda
     * @return Voltaje en V
     */
    float getVoltage() const { return m_voltage; }

    /**
     * @brief Obtiene la temperatura actual de la celda
     * @return Temperatura en °C
     */
    float getTemperature() const { return m_temperature; }

    /**
     * @brief Obtiene el estado de carga de la celda
     * @return SOC (0-100%)
     */
    uint8_t getSOC() const { return m_soc; }

    /**
     * @brief Obtiene el estado de salud de la celda
     * @return SOH (0-100%)
     */
    uint8_t getSOH() const { return m_soh; }
};

/**
 * @brief Enumera los posibles estados del pack de baterías
 */
enum class PackStatus {
    IDLE,
    CHARGING,
    DISCHARGING,
    ERROR,
    BALANCING
};

/**
 * @brief Clase que representa un pack de baterías completo
 */
class Pack {
private:
    std::vector<Cell> m_cells;
    float m_totalVoltage;    // Voltaje total del pack en V
    float m_current;         // Corriente en A (positivo = carga, negativo = descarga)
    float m_power;           // Potencia en W
    PackStatus m_status;     // Estado del pack
    uint32_t m_uptime;       // Tiempo de funcionamiento en segundos

    // Número de celdas en el pack
    uint16_t m_cellCount;

public:
    /**
     * @brief Constructor del pack de baterías
     * @param cellCount Número de celdas en el pack
     */
    Pack(uint16_t cellCount = 4);

    /**
     * @brief Actualiza el estado del pack y todas sus celdas
     */
    void update();

    /**
     * @brief Obtiene el vector de celdas
     * @return Vector de celdas
     */
    const std::vector<Cell>& getCells() const { return m_cells; }

    /**
     * @brief Obtiene el voltaje total del pack
     * @return Voltaje total en V
     */
    float getTotalVoltage() const { return m_totalVoltage; }

    /**
     * @brief Obtiene la corriente actual
     * @return Corriente en A
     */
    float getCurrent() const { return m_current; }

    /**
     * @brief Obtiene la potencia actual
     * @return Potencia en W
     */
    float getPower() const { return m_power; }

    /**
     * @brief Obtiene el estado actual del pack
     * @return Estado del pack
     */
    PackStatus getStatus() const { return m_status; }

    /**
     * @brief Obtiene el tiempo de funcionamiento
     * @return Tiempo en segundos
     */
    uint32_t getUptime() const { return m_uptime; }

    /**
     * @brief Convierte el estado del pack a string
     * @return String con el estado
     */
    const char* getStatusString() const;
};

/**
 * @brief Controlador principal de la batería
 */
class BatteryController {
private:
    Pack m_pack;
    bool m_initialized;

    /**
     * @brief Verifica alertas de temperatura y voltaje según configuración
     */
    static void checkBatteryAlerts();
    
    /**
     * @brief Determina si es necesario iniciar balanceo de celdas
     * @return true si la diferencia de voltajes supera el umbral configurado
     */
    static bool shouldStartBalancing();

public:
    /**
     * @brief Constructor del controlador
     */
    BatteryController();

    /**
     * @brief Inicializa el controlador de batería
     * @param cellCount Número de celdas en el pack
     * @return true si la inicialización fue exitosa
     */
    bool init(uint16_t cellCount = 4);

    /**
     * @brief Actualiza el estado de la batería
     */
    void update();

    /**
     * @brief Obtiene el pack de baterías
     * @return Referencia al pack
     */
    const Pack& getPack() const { return m_pack; }

    /**
     * @brief Tarea FreeRTOS para el controlador de batería
     * @param pvParameters Parámetros (no usado)
     */
    static void batteryTask(void* pvParameters);


};

// Función de inicialización global para el controlador
void battery_controller_init();

#endif // BATTERY_CONTROLLER_H