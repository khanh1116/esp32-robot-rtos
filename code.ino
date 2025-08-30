/*
 * ESP32 Robot - FreeRTOS 4-Task Architecture - HEARTBEAT LOGIC FIXED
 * Task 1: Data Reception (WiFi)
 * Task 2: Command Processing 
 * Task 3: Motor Control
 * Task 4: LED Status
 * 
 * FIXES:
 * - Separated heartbeat vs command timeout logic
 * - Queue overflow protection with timeout
 * - WiFi disconnection auto-stop (ignores lock mode)
 * - Smart movement status display
 * - Independent timeout tracking for different purposes
 */

#include <WiFi.h>         
#include <WebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// ===== WIFI CONFIG =====
const char* ssid = "kk_79";
const char* password = "12345678";

// ===== PIN DEFINITIONS =====
#define IN1 27  // Motor A (left wheel)
#define IN2 26
#define ENA 32
#define IN3 25  // Motor B (right wheel)  
#define IN4 33
#define ENB 14
#define LED_BUILTIN 2

// ===== CONSTANTS =====
#define MAX_SPEED 255
#define MIN_SPEED 1
#define DEFAULT_SPEED 150

// ===== SEPARATED TIMEOUTS =====
#define AUTO_STOP_TIMEOUT 2000      // Động cơ tự động dừng khi không có hoạt động của người dùng
#define HEARTBEAT_TIMEOUT 3000      // thời gian kiểm tra kết nối
#define COMMAND_TIMEOUT 100         // thời gian xử lý lệnh

// ===== STRUCTURES =====
typedef struct {
    String command;
    int speed;
} RawCommand;

typedef struct {
    String leftWheel;   // "F", "B", ""
    String rightWheel;  // "F", "B", ""
    int speed;
} ProcessedCommand;

// ===== GLOBAL VARIABLES =====
WebServer server(80);
QueueHandle_t rawCommandQueue;
QueueHandle_t processedCommandQueue;
TaskHandle_t dataTaskHandle;
TaskHandle_t commandTaskHandle;
TaskHandle_t motorTaskHandle;
TaskHandle_t ledTaskHandle;

// ===== SEPARATED TIMEOUT TRACKING =====
unsigned long lastUserActivity = 0;    // For auto-stop motor
unsigned long lastHeartbeat = 0;       // For connection check
unsigned long lastCommandProcess = 0;  // For command processing

bool clientConnected = false;
bool motorsActive = false;
int currentSpeed = DEFAULT_SPEED;
bool lockMode = false;

// Current wheel states
String leftWheelState = "";   // "F", "B", ""
String rightWheelState = "";  // "F", "B", ""

// ===== WEB INTERFACE =====
const char webpage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Robot Control</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        body { 
            font-family: Arial, sans-serif; text-align: center; 
            background: linear-gradient(135deg, #667eea, #764ba2);
            margin: 0; padding: 15px;
        }
        .container {
            background: white; border-radius: 15px; padding: 25px;
            max-width: 380px; margin: 0 auto;
            box-shadow: 0 8px 25px rgba(0,0,0,0.3);
        }
        h1 { color: #333; margin-bottom: 20px; font-size: 22px; }
        
        .speed-control {
            margin: 15px 0; background: #f8f9fa;
            padding: 12px; border-radius: 8px;
        }
        .speed-slider {
            width: 100%; height: 12px; border-radius: 6px;
            outline: none; cursor: pointer; margin: 8px 0;
            background: linear-gradient(90deg, #ff4444, #ffaa00, #44aa44);
        }
        
        .main-controls {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 10px; margin: 20px 0;
        }
        .motor-btn {
            padding: 25px 10px; font-size: 14px; border: none;
            border-radius: 12px; cursor: pointer; transition: all 0.2s;
            color: white; font-weight: bold; user-select: none;
        }
        .motor-btn:active { transform: scale(0.95); }
        
        .left-forward { background: #4caf50; }
        .right-forward { background: #2196f3; }
        .left-backward { background: #ff9800; }
        .right-backward { background: #f44336; }
        
        .extra-controls {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 10px; margin: 15px 0;
        }
        .extra-btn {
            padding: 12px; font-size: 14px; border: none;
            border-radius: 8px; cursor: pointer; color: white; font-weight: bold;
        }
        .stop-btn { background: #9e9e9e; }
        .lock-btn { background: #673ab7; }
        .lock-btn.active { background: #ff5722; }
        
        .status {
            background: #f0f0f0; padding: 12px; border-radius: 8px;
            margin-top: 15px; font-size: 13px; text-align: left;
        }
        .status-line { margin: 2px 0; }
        
        .connection-status {
            padding: 5px; border-radius: 4px; margin: 8px 0; font-size: 11px;
        }
        .connected { background: #e8f5e8; color: #2e7d32; }
        .disconnected { background: #ffebee; color: #c62828; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Robot Control</h1>
        
        <div class="connection-status" id="connectionDiv">
            <strong>Connection:</strong> <span id="connectionStatus">checking...</span>
        </div>
        
        <div class="speed-control">
            <label><strong>Speed: <span id="speedValue">150</span></strong></label>
            <input type="range" class="speed-slider" id="speedSlider" 
                   min="1" max="255" value="150" oninput="updateSpeed(this.value)">
        </div>
        
        <div class="main-controls">
            <button class="motor-btn left-forward" 
                    onmousedown="pressButton('LF')" onmouseup="releaseButton('LF')"
                    ontouchstart="pressButton('LF')" ontouchend="releaseButton('LF')">
                Left Forward
            </button>
            <button class="motor-btn right-forward" 
                    onmousedown="pressButton('RF')" onmouseup="releaseButton('RF')"
                    ontouchstart="pressButton('RF')" ontouchend="releaseButton('RF')">
                Right Forward
            </button>
            <button class="motor-btn left-backward" 
                    onmousedown="pressButton('LB')" onmouseup="releaseButton('LB')"
                    ontouchstart="pressButton('LB')" ontouchend="releaseButton('LB')">
                Left Backward
            </button>
            <button class="motor-btn right-backward" 
                    onmousedown="pressButton('RB')" onmouseup="releaseButton('RB')"
                    ontouchstart="pressButton('RB')" ontouchend="releaseButton('RB')">
                Right Backward
            </button>
        </div>
        
        <div class="extra-controls">
            <button class="extra-btn stop-btn" onclick="emergencyStop()">STOP</button>
            <button class="extra-btn lock-btn" id="lockBtn" onclick="toggleLock()">Lock OFF</button>
        </div>
        
        <div class="status">
            <div class="status-line"><strong>Movement:</strong> <span id="status">stopped</span></div>
            <div class="status-line"><strong>Buttons:</strong> <span id="activeButtons">none</span></div>
            <div class="status-line"><strong>Speed:</strong> <span id="currentSpeed">150</span></div>
            <div class="status-line"><strong>Motors:</strong> <span id="motorStatus">stopped</span></div>
            <div class="status-line"><strong>Lock:</strong> <span id="lockStatus">off</span></div>
        </div>
    </div>

    <script>
        let currentSpeed = 150;
        let leftWheel = "";   // "F", "B", ""
        let rightWheel = "";  // "F", "B", ""
        let lockMode = false;
        let keepAliveInterval;
        let connectionCheckInterval;
        let isConnected = true;
        
        function updateSpeed(value) {
            currentSpeed = parseInt(value);
            document.getElementById('speedValue').innerText = value;
            document.getElementById('currentSpeed').innerText = value;
            
            fetch('/set_speed?speed=' + value)
            .then(response => response.text())
            .then(data => console.log('Speed updated:', data))
            .catch(error => console.log('Speed update failed:', error));
        }
        
        function updateConnectionStatus(connected) {
            isConnected = connected;
            const statusEl = document.getElementById('connectionStatus');
            const divEl = document.getElementById('connectionDiv');
            
            if (connected) {
                statusEl.innerText = 'connected';
                divEl.className = 'connection-status connected';
            } else {
                statusEl.innerText = 'disconnected';
                divEl.className = 'connection-status disconnected';
                emergencyStop();
            }
        }
        
        function getMovementStatus() {
            // Calculate smart movement status
            if (leftWheel === "" && rightWheel === "") {
                return "stopped";
            } else if (leftWheel === "F" && rightWheel === "F") {
                return "forward";
            } else if (leftWheel === "B" && rightWheel === "B") {
                return "backward";
            } else if (leftWheel === "F" && rightWheel === "") {
                return "left forward";
            } else if (leftWheel === "" && rightWheel === "F") {
                return "right forward";
            } else if (leftWheel === "B" && rightWheel === "") {
                return "left backward";
            } else if (leftWheel === "" && rightWheel === "B") {
                return "right backward";
            } else if (leftWheel === "F" && rightWheel === "B") {
                return "turn left";
            } else if (leftWheel === "B" && rightWheel === "F") {
                return "turn right";
            }
            return "unknown";
        }
        
        function updateStatus() {
            document.getElementById('status').innerText = getMovementStatus();
        }
        
        function pressButton(button) {
            // Independent wheel control - toggle behavior
            if (button === 'LF') {
                leftWheel = leftWheel === 'F' ? '' : 'F';
                sendCommand('left_forward');
            } else if (button === 'LB') {
                leftWheel = leftWheel === 'B' ? '' : 'B';
                sendCommand('left_backward');
            } else if (button === 'RF') {
                rightWheel = rightWheel === 'F' ? '' : 'F';
                sendCommand('right_forward');
            } else if (button === 'RB') {
                rightWheel = rightWheel === 'B' ? '' : 'B';
                sendCommand('right_backward');
            }
            
            updateActiveButtons();
            updateStatus();
            
            // Start frequent keepalive for user activity tracking
            if (!keepAliveInterval && (leftWheel !== '' || rightWheel !== '' || lockMode)) {
                keepAliveInterval = setInterval(sendUserActivityKeepAlive, 200);  // Faster for user activity
            }
        }
        
        function releaseButton(button) {
            if (!lockMode) {
                // Only clear the specific wheel when released
                if (button === 'LF' || button === 'LB') {
                    leftWheel = '';
                    sendCommand('left_stop');
                } else if (button === 'RF' || button === 'RB') {
                    rightWheel = '';
                    sendCommand('right_stop');
                }
                updateActiveButtons();
                updateStatus();
            }
        }
        
        function updateActiveButtons() {
            let active = [];
            if (leftWheel === 'F') active.push('LF');
            if (leftWheel === 'B') active.push('LB');
            if (rightWheel === 'F') active.push('RF');
            if (rightWheel === 'B') active.push('RB');
            
            document.getElementById('activeButtons').innerText = 
                active.length > 0 ? active.join(' + ') : 'none';
        }
        
        function sendCommand(command) {
            if (!isConnected) return;
            
            const commandMap = {
                'left_forward': '/left_forward',
                'left_backward': '/left_backward',
                'left_stop': '/left_stop',
                'right_forward': '/right_forward',
                'right_backward': '/right_backward',
                'right_stop': '/right_stop'
            };
            
            const url = commandMap[command];
            if (!url) return;
            
            fetch(url)
            .then(response => response.text())
            .then(data => {
                updateConnectionStatus(true);
                updateMotorStatus();
            })
            .catch(error => {
                updateConnectionStatus(false);
            });
        }
        
        function updateMotorStatus() {
            let status = 'stopped';
            if (leftWheel !== '' || rightWheel !== '') {
                status = 'running';
            }
            document.getElementById('motorStatus').innerText = status;
        }
        
        // USER ACTIVITY KEEPALIVE - for motor timeout prevention
        function sendUserActivityKeepAlive() {
            // Only send when user is actively controlling or lock is on
            if (leftWheel === '' && rightWheel === '' && !lockMode) {
                if (keepAliveInterval) {
                    clearInterval(keepAliveInterval);
                    keepAliveInterval = null;
                }
                return;
            }
            
            fetch('/user_activity')  // New endpoint for user activity
            .then(response => updateConnectionStatus(true))
            .catch(error => {
                updateConnectionStatus(false);
                emergencyStop();
            });
        }
        
        // CONNECTION HEARTBEAT - separate from user activity
        function sendConnectionHeartbeat() {
            fetch('/heartbeat')  // Separate endpoint for connection check
            .then(response => updateConnectionStatus(true))
            .catch(error => updateConnectionStatus(false));
        }
        
        function emergencyStop() {
            leftWheel = '';
            rightWheel = '';
            lockMode = false;
            updateActiveButtons();
            updateStatus();
            document.getElementById('lockBtn').innerText = 'Lock OFF';
            document.getElementById('lockBtn').className = 'extra-btn lock-btn';
            document.getElementById('lockStatus').innerText = 'off';
            document.getElementById('motorStatus').innerText = 'emergency stop';
            
            if (keepAliveInterval) {
                clearInterval(keepAliveInterval);
                keepAliveInterval = null;
            }
            
            fetch('/emergency')
            .then(response => response.text());
        }
        
        function toggleLock() {
            lockMode = !lockMode;
            
            const lockBtn = document.getElementById('lockBtn');
            if (lockMode) {
                lockBtn.innerText = 'Lock ON';
                lockBtn.className = 'extra-btn lock-btn active';
                document.getElementById('lockStatus').innerText = 'on';
                fetch('/lock_on');
                
                // Start user activity keepalive when lock enabled
                if (!keepAliveInterval) {
                    keepAliveInterval = setInterval(sendUserActivityKeepAlive, 200);
                }
            } else {
                lockBtn.innerText = 'Lock OFF';
                lockBtn.className = 'extra-btn lock-btn';
                document.getElementById('lockStatus').innerText = 'off';
                fetch('/lock_off');
                
                leftWheel = '';
                rightWheel = '';
                updateActiveButtons();
                updateStatus();
                sendCommand('left_stop');
                sendCommand('right_stop');
            }
        }
        
        // Keyboard control
        let keysPressed = {};
        
        document.addEventListener('keydown', function(event) {
            if (keysPressed[event.key]) return;
            keysPressed[event.key] = true;
            
            switch(event.key.toLowerCase()) {
                case 'q': pressButton('LF'); break;
                case 'z': pressButton('LB'); break;
                case 'e': pressButton('RF'); break;
                case 'c': pressButton('RB'); break;
                case ' ': emergencyStop(); event.preventDefault(); break;
                case 'l': toggleLock(); break;
            }
            
            if ('qzecl '.includes(event.key.toLowerCase())) {
                event.preventDefault();
            }
        });
        
        document.addEventListener('keyup', function(event) {
            keysPressed[event.key] = false;
            
            switch(event.key.toLowerCase()) {
                case 'q': releaseButton('LF'); break;
                case 'z': releaseButton('LB'); break;
                case 'e': releaseButton('RF'); break;
                case 'c': releaseButton('RB'); break;
            }
            
            if ('qzec'.includes(event.key.toLowerCase())) {
                event.preventDefault();
            }
        });
        
        // Connection monitoring - separate from user activity
        connectionCheckInterval = setInterval(sendConnectionHeartbeat, 2000);  // Every 2s
        
        window.addEventListener('beforeunload', emergencyStop);
        window.addEventListener('blur', function() {
            if (!lockMode) emergencyStop();
        });
        
        document.addEventListener('contextmenu', e => e.preventDefault());
        document.addEventListener('dragstart', e => e.preventDefault());
        
        // Initialize status
        updateStatus();
    </script>
</body>
</html>
)rawliteral";

// ===== TASK 1: DATA RECEPTION =====
void dataReceptionTask(void *parameter) {
    Serial.println("[DATA] task started");
    
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
    WiFi.softAP(ssid, password, 6, 0, 4);
    
    Serial.printf("[DATA] AP: %s\n", ssid);
    Serial.printf("[DATA] IP: %s\n", WiFi.softAPIP().toString().c_str());
    
    // Routes
    server.on("/", []() {
        server.send(200, "text/html", webpage);
    });
    
    server.on("/set_speed", []() {
        int speed = server.arg("speed").toInt();
        if (speed >= MIN_SPEED && speed <= MAX_SPEED) {
            currentSpeed = speed;
            server.send(200, "text/plain", "speed " + String(speed));
        } else {
            server.send(400, "text/plain", "invalid");
        }
    });
    
    // QUEUE TIMEOUT
    server.on("/left_forward", []() {
        RawCommand cmd = {"left_forward", currentSpeed};
        if (xQueueSend(rawCommandQueue, &cmd, pdMS_TO_TICKS(100))) {
            server.send(200, "text/plain", "LF");
        } else {
            server.send(503, "text/plain", "queue_full");
        }
    });
    
    server.on("/left_backward", []() {
        RawCommand cmd = {"left_backward", currentSpeed};
        if (xQueueSend(rawCommandQueue, &cmd, pdMS_TO_TICKS(100))) {
            server.send(200, "text/plain", "LB");
        } else {
            server.send(503, "text/plain", "queue_full");
        }
    });
    
    server.on("/left_stop", []() {
        RawCommand cmd = {"left_stop", 0};
        if (xQueueSend(rawCommandQueue, &cmd, pdMS_TO_TICKS(100))) {
            server.send(200, "text/plain", "L_STOP");
        } else {
            server.send(503, "text/plain", "queue_full");
        }
    });
    
    server.on("/right_forward", []() {
        RawCommand cmd = {"right_forward", currentSpeed};
        if (xQueueSend(rawCommandQueue, &cmd, pdMS_TO_TICKS(100))) {
            server.send(200, "text/plain", "RF");
        } else {
            server.send(503, "text/plain", "queue_full");
        }
    });
    
    server.on("/right_backward", []() {
        RawCommand cmd = {"right_backward", currentSpeed};
        if (xQueueSend(rawCommandQueue, &cmd, pdMS_TO_TICKS(100))) {
            server.send(200, "text/plain", "RB");
        } else {
            server.send(503, "text/plain", "queue_full");
        }
    });
    
    server.on("/right_stop", []() {
        RawCommand cmd = {"right_stop", 0};
        if (xQueueSend(rawCommandQueue, &cmd, pdMS_TO_TICKS(100))) {
            server.send(200, "text/plain", "R_STOP");
        } else {
            server.send(503, "text/plain", "queue_full");
        }
    });
    
    server.on("/emergency", []() {
        RawCommand cmd = {"emergency_stop", 0};
        // Emergency always gets priority - wait longer
        if (xQueueSend(rawCommandQueue, &cmd, pdMS_TO_TICKS(500))) {
            server.send(200, "text/plain", "emergency stop");
        } else {
            server.send(503, "text/plain", "emergency_failed");
        }
    });
    
    server.on("/lock_on", []() {
        RawCommand cmd = {"lock_on", 0};
        if (xQueueSend(rawCommandQueue, &cmd, pdMS_TO_TICKS(100))) {
            server.send(200, "text/plain", "lock on");
        } else {
            server.send(503, "text/plain", "queue_full");
        }
    });
    
    server.on("/lock_off", []() {
        RawCommand cmd = {"lock_off", 0};
        if (xQueueSend(rawCommandQueue, &cmd, pdMS_TO_TICKS(100))) {
            server.send(200, "text/plain", "lock off");
        } else {
            server.send(503, "text/plain", "queue_full");
        }
    });
    
    // ===== SEPARATED ENDPOINTS =====
    // Giữ hoạt động của người dùng - để ngăn chặn thời gian chờ của động cơ
    server.on("/user_activity", []() {
        lastUserActivity = millis();        // Cập nhật bộ đếm thời gian hoạt động của người dùng
        lastHeartbeat = millis();           // Cũng cập nhật kết nối
        clientConnected = true;
        server.send(200, "text/plain", "user_active");
    });
    
    // Connection heartbeat - để theo dõi kết nốig
    server.on("/heartbeat", []() {
        lastHeartbeat = millis();           // Chỉ cập nhật bộ hẹn giờ kết nối
        clientConnected = true;
        server.send(200, "text/plain", "heartbeat");
    });
    
    // Legacy keepalive endpoint (for compatibility)
    server.on("/keepalive", []() {
        lastUserActivity = millis();
        lastHeartbeat = millis();
        clientConnected = true;
        server.send(200, "text/plain", "alive");
    });
    
    server.on("/ping", []() {
        server.send(200, "text/plain", "pong");
    });
    
    server.begin();
    Serial.println("[DATA] server started");
    
    while (1) {
        server.handleClient();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ===== TASK 2: COMMAND PROCESSING =====
void commandProcessingTask(void *parameter) {
    RawCommand rawCmd;
    ProcessedCommand processedCmd;
    
    Serial.println("[CMD] task started");
    
    while (1) {
        if (xQueueReceive(rawCommandQueue, &rawCmd, pdMS_TO_TICKS(10))) {
            Serial.printf("[CMD] processing: %s\n", rawCmd.command.c_str());
            
            // Process raw commands into wheel states
            if (rawCmd.command == "left_forward") {
                leftWheelState = "F";
            } else if (rawCmd.command == "left_backward") {
                leftWheelState = "B";
            } else if (rawCmd.command == "left_stop") {
                leftWheelState = "";
            } else if (rawCmd.command == "right_forward") {
                rightWheelState = "F";
            } else if (rawCmd.command == "right_backward") {
                rightWheelState = "B";
            } else if (rawCmd.command == "right_stop") {
                rightWheelState = "";
            } else if (rawCmd.command == "emergency_stop") {
                leftWheelState = "";
                rightWheelState = "";
                lockMode = false;
            } else if (rawCmd.command == "lock_on") {
                lockMode = true;
            } else if (rawCmd.command == "lock_off") {
                lockMode = false;
                leftWheelState = "";
                rightWheelState = "";
            }
            
            // Create processed command
            processedCmd.leftWheel = leftWheelState;
            processedCmd.rightWheel = rightWheelState;
            processedCmd.speed = rawCmd.speed;
            
            // Send to motor control
            xQueueSend(processedCommandQueue, &processedCmd, 0);
            lastCommandProcess = millis();
        }
        
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ===== TASK 3: MOTOR CONTROL =====
void motorControlTask(void *parameter) {
    ProcessedCommand cmd;
    
    pinMode(IN1, OUTPUT);
    pinMode(IN2, OUTPUT);
    pinMode(IN3, OUTPUT);
    pinMode(IN4, OUTPUT);
    pinMode(ENA, OUTPUT);
    pinMode(ENB, OUTPUT);
    
    Serial.println("[MOTOR] task started");
    
    while (1) {
        // ===== CONNECTION CHECK - SAFETY FIRST =====
        if (clientConnected && (millis() - lastHeartbeat > HEARTBEAT_TIMEOUT)) {
            clientConnected = false;
            Serial.println("[MOTOR] Client disconnected - EMERGENCY STOP");
            
            // Force stop all motors - IGNORE LOCK MODE
            analogWrite(ENA, 0);
            analogWrite(ENB, 0);
            digitalWrite(IN1, LOW);
            digitalWrite(IN2, LOW);
            digitalWrite(IN3, LOW);
            digitalWrite(IN4, LOW);
            motorsActive = false;
            leftWheelState = "";
            rightWheelState = "";
            lockMode = false;  // Force unlock when disconnected
        }
        
        // ===== AUTO-STOP CHECK - USER ACTIVITY BASED =====
        if (motorsActive && !lockMode && clientConnected && 
            (millis() - lastUserActivity > AUTO_STOP_TIMEOUT)) {
            // Stop both motors due to user inactivity
            analogWrite(ENA, 0);
            analogWrite(ENB, 0);
            digitalWrite(IN1, LOW);
            digitalWrite(IN2, LOW);
            digitalWrite(IN3, LOW);
            digitalWrite(IN4, LOW);
            motorsActive = false;
            leftWheelState = "";
            rightWheelState = "";
            Serial.println("[MOTOR] auto stop - user inactivity");
        }
        
        // Process motor commands
        if (xQueueReceive(processedCommandQueue, &cmd, pdMS_TO_TICKS(10))) {
            Serial.printf("[MOTOR] L:%s R:%s speed:%d\n", 
                         cmd.leftWheel.c_str(), cmd.rightWheel.c_str(), cmd.speed);
            
            // Control left wheel (Motor A)
            if (cmd.leftWheel == "F") {
                digitalWrite(IN1, LOW);
                digitalWrite(IN2, HIGH);
                analogWrite(ENA, cmd.speed);
            } else if (cmd.leftWheel == "B") {
                digitalWrite(IN1, HIGH);
                digitalWrite(IN2, LOW);
                analogWrite(ENA, cmd.speed);
            } else {
                digitalWrite(IN1, LOW);
                digitalWrite(IN2, LOW);
                analogWrite(ENA, 0);
            }
            
            // Control right wheel (Motor B)
            if (cmd.rightWheel == "F") {
                digitalWrite(IN3, HIGH);
                digitalWrite(IN4, LOW);
                analogWrite(ENB, cmd.speed);
            } else if (cmd.rightWheel == "B") {
                digitalWrite(IN3, LOW);
                digitalWrite(IN4, HIGH);
                analogWrite(ENB, cmd.speed);
            } else {
                digitalWrite(IN3, LOW);
                digitalWrite(IN4, LOW);
                analogWrite(ENB, 0);
            }
            
            // Update motor status
            motorsActive = (cmd.leftWheel != "" || cmd.rightWheel != "");
        }
        
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ===== TASK 4: LED STATUS =====
void ledStatusTask(void *parameter) {
    pinMode(LED_BUILTIN, OUTPUT);
    Serial.println("[LED] task started");
    
    while (1) {
        if (!clientConnected) {
            // Nhấp nháy rất nhanh khi ngắt kết nối
            digitalWrite(LED_BUILTIN, HIGH);
            vTaskDelay(pdMS_TO_TICKS(100));
            digitalWrite(LED_BUILTIN, LOW);
            vTaskDelay(pdMS_TO_TICKS(100));
        } else if (lockMode) {
            // Fast blink when locked
            digitalWrite(LED_BUILTIN, HIGH);
            vTaskDelay(pdMS_TO_TICKS(50));
            digitalWrite(LED_BUILTIN, LOW);
            vTaskDelay(pdMS_TO_TICKS(50));
        } else if (motorsActive) {
            // Medium blink when running
            digitalWrite(LED_BUILTIN, HIGH);
            vTaskDelay(pdMS_TO_TICKS(150));
            digitalWrite(LED_BUILTIN, LOW);
            vTaskDelay(pdMS_TO_TICKS(150));
        } else {
            // Slow blink when idle
            digitalWrite(LED_BUILTIN, HIGH);
            vTaskDelay(pdMS_TO_TICKS(800));
            digitalWrite(LED_BUILTIN, LOW);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

// ===== SETUP =====
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n=== ESP32 ROBOT - FREERTOS 4-TASK - HEARTBEAT FIXED ===");
    
    // Initialize timeout tracking
    lastUserActivity = millis();
    lastHeartbeat = millis();
    lastCommandProcess = millis();
    clientConnected = false;  // Start disconnected
    
    // Create queues
    rawCommandQueue = xQueueCreate(10, sizeof(RawCommand));
    processedCommandQueue = xQueueCreate(10, sizeof(ProcessedCommand));
    
    if (rawCommandQueue == NULL || processedCommandQueue == NULL) {
        Serial.println("[ERROR] queue creation failed");
        return;
    }
    
    // Create tasks
    xTaskCreatePinnedToCore(dataReceptionTask, "data", 8192, NULL, 2, &dataTaskHandle, 1);
    xTaskCreatePinnedToCore(commandProcessingTask, "command", 4096, NULL, 3, &commandTaskHandle, 0);
    xTaskCreatePinnedToCore(motorControlTask, "motor", 4096, NULL, 4, &motorTaskHandle, 0);
    xTaskCreatePinnedToCore(ledStatusTask, "led", 2048, NULL, 1, &ledTaskHandle, 0);
    
    Serial.println("[SETUP] Features:");
    Serial.println("  - Independent wheel control");
    Serial.println("  - Smart movement status display");
    Serial.println("  - Queue overflow protection");
    Serial.println("  - SEPARATED heartbeat vs user activity timeouts");
    Serial.println("  - WiFi disconnection safety");
    Serial.println("  - 4-task FreeRTOS architecture");
    Serial.println("  - Lock mode support");
    Serial.printf("[SETUP] Timeouts: auto-stop=%dms heartbeat=%dms\n", AUTO_STOP_TIMEOUT, HEARTBEAT_TIMEOUT);
    Serial.printf("[SETUP] WiFi: %s\n", ssid);
    Serial.println("[SETUP] URL: http://192.168.4.1");
}

// ===== LOOP =====
void loop() {
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 10000) {
        lastPrint = millis();
        
        Serial.printf("[STATUS] heap:%d clients:%d motors:%s lock:%s conn:%s L:%s R:%s speed:%d\n", 
                     ESP.getFreeHeap(),
                     WiFi.softAPgetStationNum(),
                     motorsActive ? "on" : "off",
                     lockMode ? "on" : "off",
                     clientConnected ? "yes" : "no",
                     leftWheelState.c_str(),
                     rightWheelState.c_str(),
                     currentSpeed);
        
        unsigned long now = millis();
        Serial.printf("[TIMERS] userActivity:%lu heartbeat:%lu command:%lu\n",
                     now - lastUserActivity,
                     now - lastHeartbeat,
                     now - lastCommandProcess);
    }
    
    delay(1000);
}