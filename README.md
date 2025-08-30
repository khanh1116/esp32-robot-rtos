# ESP32 Robot Car - FreeRTOS Project

## Giới thiệu
Đây là đồ án cuối kỳ môn **Lập trình Hệ thống Nhúng**.  
Dự án hiện thực một **xe robot điều khiển qua WiFi** bằng ESP32, sử dụng **FreeRTOS** để quản lý đa nhiệm.  

- **Vi điều khiển**: ESP32 DevKit v1  
- **Ngôn ngữ**: C++ (Arduino Framework)  
- **RTOS**: FreeRTOS  
- **Chức năng chính**:
  - Giao diện Web Control (WiFi AP + HTTP server)  
  - 4 Task độc lập:  
    1. Data Reception (WiFi/WebServer)  
    2. Command Processing  
    3. Motor Control (PWM + H-Bridge)  
    4. LED Status Indicator  
  - Cơ chế Auto-stop khi mất kết nối hoặc không thao tác  
  - Chế độ Lock/Unlock  
  - Queue bảo vệ giao tiếp giữa các task  

---

