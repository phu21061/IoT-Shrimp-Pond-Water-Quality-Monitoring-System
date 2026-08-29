#pragma once


#include <Arduino.h>
#include "secrets.h"

#define FW_VERSION "2.2.1"
extern char DEVICE_ID[18];


#define WIFI_CONNECT_TIMEOUT_MS 15000UL // 
#define WIFI_MAX_RETRIES 5


#define AP_CHANNEL 1
#define AP_MAX_CLIENTS 4
#define AP_TIMEOUT_MS 300000UL 


extern char FB_PATH_LATEST[64];
extern char FB_PATH_HISTORY[64]; 
extern char FB_PATH_HISTORY_FULL[64]; // lấy dữ liệu toàn vụ for AI 
extern char FB_PATH_ALERTS[64];    
extern char FB_PATH_CONFIG[64];
extern char FB_PATH_STATUS[64];
extern char FB_PATH_COMMAND[64];
extern char FB_PATH_LOGS[64];      

// Đường dẫn lưu log trên Cloud Storage: logs/{device_id}/YYYY-MM-DD.csv
extern char FB_STORAGE_LOG_PREFIX[64];


#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.google.com"
#define NTP_GMT_OFFSET_S 25200L // <- CẤU HÌNH  UTC+7 (Việt Nam)
#define NTP_DST_OFFSET_S 0L

// cấu hình rs485 to ttl max485
#define RS485_UART_NUM    2       
#define RS485_RX_PIN      18     
#define RS485_TX_PIN      17     
#define RS485_BAUD        9600   
#define RS485_DE_PIN      -1     

// cấu hình modbus chung
#define MODBUS_TIMEOUT_MS     1000UL  
#define MODBUS_MAX_RETRIES    3       
#define MODBUS_RETRY_DELAY_MS 50      

// cấu hình cảm biến ph
#define PH_MODBUS_ID      3       
#define PH_REG_START      0x0000 
#define PH_REG_COUNT      2 


// cấu hình cảm biến đo oxy hòa tan
#define DO_MODBUS_ID      2      
#define DO_REG_START      0x0000  
#define DO_REG_COUNT      6       
#define DO_CAL_REG        0x1010  
#define DO_CAL_AIR_VALUE  0x0002  

// cấu hình cảm biến dòng điện — PZEM-016 
#define PZEM_MODBUS_ID    1       
#define PZEM_REG_START    0x0000  
#define PZEM_REG_COUNT    10      
#define PZEM_FUNC_CODE    0x04    

// cấu hình còi báo và đèn 
#define RELAY_1_PIN       5       // còi báo 
#define STATUS_LED_PIN    4       // LED trạng thái 
#define ALERT_OUTPUT_DURATION_MS 60000UL // 60 giây

// cấu hình lên lịch đo đạc
#define DEFAULT_INTERVAL_MS 60000UL       // 60 giây
#define MIN_INTERVAL_MS 10000UL           // 10 giây (mức tối thiểu để test)
#define MAX_INTERVAL_MS 86400000UL        // 24 giờ (mức tối đa)
#define CONFIG_POLL_MS 60000UL            // Kiểm tra cấu hình từ xa mỗi 60 giây

// Ngưỡng cảnh báo mặc định (Bị ghi đè bởi Firebase /config)
#define DEFAULT_PH_MIN      6.5f
#define DEFAULT_PH_MAX      8.5f
#define DEFAULT_DO_MIN      4.0f  
#define DEFAULT_CURRENT_MAX 15.0f 

#define DEFAULT_PH_NEUTRAL_VOLTAGE 0.0f
#define DEFAULT_PH_VOLTAGE_SLOPE   59.16f


#define DEFAULT_DEBOUNCE_COUNT 3

// lost wifi
#define OFFLINE_BUFFER_SIZE 48           // Số bản ghi tối đa
#define LOG_UPLOAD_INTERVAL_MS 3600000UL  // Upload batch mỗi 1 giờ

#define HEARTBEAT_INTERVAL_MS 30000UL     // Gửi heartbeat mỗi 30 giây


#define WDT_TIMEOUT_S 120 

// OTA
#define OTA_VALIDATION_PERIOD_MS  300000UL  
#define OTA_MIN_HEALTHY_CYCLES   3           
#define OTA_DOWNLOAD_TIMEOUT_MS  120000UL    
#define OTA_POLL_INTERVAL_MS     60000UL    



// Các cấp độ: ERROR=1  WARN=2  INFO=3  DEBUG=4  VERBOSE=5
#define LOG_LEVEL 4 // <- CẤU HÌNH (3=cho sản phẩm thật, 4=dành cho lúc code/phát triển)

#define LOG_E(tag, fmt, ...)                                                   \
  if (LOG_LEVEL >= 1)                                                          \
  Serial.printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_W(tag, fmt, ...)                                                   \
  if (LOG_LEVEL >= 2)                                                          \
  Serial.printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_I(tag, fmt, ...)                                                   \
  if (LOG_LEVEL >= 3)                                                          \
  Serial.printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_D(tag, fmt, ...)                                                   \
  if (LOG_LEVEL >= 4)                                                          \
  Serial.printf("[D][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_V(tag, fmt, ...)                                                   \
  if (LOG_LEVEL >= 5)                                                          \
  Serial.printf("[V][%s] " fmt "\n", tag, ##__VA_ARGS__)
