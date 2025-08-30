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


## Hướng dẫn chạy code
1. Cài Arduino IDE hoặc PlatformIO.  
2. Cài **ESP32 Board Support Package** (Arduino-ESP32 Core by Espressif).  
3. Mở file `code.ino` và chọn board `ESP32 Dev Module`.  
4. Nạp code vào ESP32.  
5. Sau khi ESP32 phát WiFi AP, truy cập địa chỉ **http://192.168.4.1/** để điều khiển robot.  

---

## Thành viên nhóm
- Nguyễn Văn Khánh - MSSV 22050079
- Phạm Thanh Phong - MSSV 22050001


---

## Tài liệu đính kèm
- 📄 [Báo cáo Word](./word.docx)  
- 📄 [Báo cáo PDF](./pdf.pdf)  
- 📊 [Slide thuyết trình](./ppt.pptx)  
- 💻 [Source code](./code.ino)  

---
