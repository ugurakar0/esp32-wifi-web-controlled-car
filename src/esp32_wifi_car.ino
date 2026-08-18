/*
  ESP32 WiFi Controlled Robotic Vehicle - Multi-Page Web Interface
  ----------------------------------------------------------------
  - DEFAULT MODE: Access Point (AP) mode. The ESP32 hosts its own Wi-Fi
    network, requiring no external router or internet access.
  - Default Gateway: http://192.168.4.1 (Static IP).
  - OPTIONAL MODE: Station (STA) mode can be enabled to connect the ESP32
    to a local WLAN router. See "WIFI CONFIGURATION" and setup() sections.

  PAGES & ENDPOINTS:
  1) "/"          -> Touch D-Pad control (Forward/Reverse/Left/Right + PWM Slider)
  2) "/joystick"  -> Analog 360-degree touch joystick + differential mixing
  3) "/graph"     -> Real-time Wi-Fi signal strength (RSSI) telemetry chart

  HARDWARE INTERFACE (L298N Dual H-Bridge Driver):
  ESP32 Pin  ->  L298N Terminal
  GPIO 27    ->  IN1  (Left motor forward)
  GPIO 26    ->  IN2  (Left motor backward)
  GPIO 32    ->  IN3  (Right motor forward)
  GPIO 33    ->  IN4  (Right motor backward)
  GPIO 25    ->  ENA  (Left motor speed PWM)
  GPIO 14    ->  ENB  (Right motor speed PWM)
  GND        ->  GND  (Common logic ground required)
  VCC        ->  12V  (External battery source: 7.4V - 11.1V)
*/

#include <WiFi.h>
#include <WebServer.h>

// ------------------- WIFI CONFIGURATION -------------------
// ACCESS POINT (AP) MODE CONFIGURATION
// Connect directly to this SSID from your mobile device or PC.
const char* ssid     = "ESP32-Car";
const char* password = "password123";

/* =========================================================================
   STATION (STA) MODE CONFIGURATION (OPTIONAL)
   =========================================================================
   To connect the ESP32 to your home/office WLAN router:
   1) Comment out the AP credentials above.
   2) Uncomment and configure the STA credentials below:

        const char* ssid     = "YOUR_ROUTER_SSID";
        const char* password = "YOUR_ROUTER_PASSWORD";

   3) In setup(), comment out "WiFi.softAP(...)" and uncomment "WiFi.begin(...)".
   ========================================================================= */

// ------------------- PIN DEFINITIONS -------------------
const int IN1 = 27;   // Left motor directional pin 1
const int IN2 = 26;   // Left motor directional pin 2
const int IN3 = 32;   // Right motor directional pin 1
const int IN4 = 33;   // Right motor directional pin 2
const int ENA = 25;   // Left motor PWM speed pin
const int ENB = 14;   // Right motor PWM speed pin

// Hardware PWM Configuration (ESP32 Arduino Core 3.x ledc architecture)
const int PWM_FREQ = 1000;   // 1 kHz switching frequency
const int PWM_RES  = 8;      // 8-bit resolution (0 - 255)
int vehicleSpeed   = 200;    // Default duty cycle (0 - 255)

// ------------------- FAIL-SAFE (WATCHDOG) -------------------
// Safety mechanism: If client drops Wi-Fi connection or closes browser,
// the vehicle halts automatically after timeout to prevent runaway.
unsigned long lastCommandTime = 0;
const unsigned long TIMEOUT_THRESHOLD = 1500; // Milliseconds (1.5s timeout)

WebServer server(80);

// ------------------- MOTOR CONTROL CORE -------------------
// Parameter range: -255 (Full Reverse) .. 0 (Halt) .. 255 (Full Forward)
void driveMotors(int left, int right) {
  left  = constrain(left, -255, 255);
  right = constrain(right, -255, 255);

  // Left Motor H-Bridge Logic
  if (left > 0) {
    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
  } else if (left < 0) {
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, HIGH);
  } else {
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);
  }
  ledcWrite(ENA, abs(left));

  // Right Motor H-Bridge Logic
  if (right > 0) {
    digitalWrite(IN3, HIGH);
    digitalWrite(IN4, LOW);
  } else if (right < 0) {
    digitalWrite(IN3, LOW);
    digitalWrite(IN4, HIGH);
  } else {
    digitalWrite(IN3, LOW);
    digitalWrite(IN4, LOW);
  }
  ledcWrite(ENB, abs(right));
}

void stopMotors()    { driveMotors(0, 0); }
void moveForward()   { driveMotors(vehicleSpeed, vehicleSpeed); }
void moveBackward()  { driveMotors(-vehicleSpeed, -vehicleSpeed); }
void turnLeft()      { driveMotors(-vehicleSpeed, vehicleSpeed); }
void turnRight()     { driveMotors(vehicleSpeed, -vehicleSpeed); }

// ------------------- UI NAVIGATION COMPONENT -------------------
String buildNavBar(String active) {
  String s = "<div class='nav'>";
  s += "<a href='/' class='" + String(active == "1" ? "active" : "") + "'>Buttons</a>";
  s += "<a href='/joystick' class='" + String(active == "2" ? "active" : "") + "'>Joystick</a>";
  s += "<a href='/graph' class='" + String(active == "3" ? "active" : "") + "'>Signal Graph</a>";
  s += "</div>";
  return s;
}

// ------------------- PAGE 1: BUTTON D-PAD -------------------
String getButtonsPage() {
  String html = R"page1(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Car - D-Pad Control</title>
<style>
  * { box-sizing: border-box; -webkit-tap-highlight-color: transparent; user-select:none; }
  body { font-family: Arial, sans-serif; text-align: center; background:#000000; color:white; margin:0; padding:10px; overflow-x:hidden; }
  h2 { margin: 8px 0 4px 0; font-size:18px; }
  .nav { margin-bottom: 10px; }
  .nav a { color:#FFD700; text-decoration:none; margin:0 10px; font-weight:bold; padding:6px 10px; border-radius:6px; }
  .nav a.active { background:#333; }
  .rotateHint { display:none; color:#FFD700; font-size:13px; margin-bottom:6px; }
  @media (orientation: portrait) { .rotateHint { display:block; } }
  .controlArea { display:flex; justify-content:space-between; align-items:center; max-width:700px; margin:10px auto; padding:0 10px; }
  .pad { width:190px; height:190px; border-radius:50%; background: radial-gradient(circle at center, #221c00 0%, #0a0800 70%, #000 100%); border:2px solid #FFD700; box-shadow: 0 0 25px rgba(255,215,0,0.45), inset 0 0 20px rgba(255,215,0,0.15); position:relative; touch-action:none; }
  .padLine { position:absolute; background:rgba(255,215,0,0.5); }
  #padFB .padLine { left:12px; right:12px; top:50%; height:1px; }
  #padLR .padLine { top:12px; bottom:12px; left:50%; width:1px; }
  .half { position:absolute; display:flex; align-items:center; justify-content:center; color:#FFD700; font-size:34px; }
  #padFB .half { left:0; right:0; height:50%; }
  #padFB .up   { top:0; align-items:flex-end; padding-bottom:14px; }
  #padFB .down { bottom:0; align-items:flex-start; padding-top:14px; }
  #padLR .half { top:0; bottom:0; width:50%; }
  #padLR .left  { left:0; justify-content:flex-end; padding-right:14px; }
  #padLR .right { right:0; justify-content:flex-start; padding-left:14px; }
  .half:active { color:#fff; }
  #stopBtn { width:64px; height:64px; border-radius:50%; border:none; background:#c0392b; color:white; font-weight:bold; font-size:13px; box-shadow: 0 0 15px rgba(192,57,43,0.7); }
  #stopBtn:active { background:#e74c3c; }
  .speedRow { margin-top:6px; }
  input[type=range]{ width:250px; accent-color:#FFD700; }
</style>
</head>
<body>
NAVBAR_PLACEHOLDER
<h2>ESP32 Vehicle Control</h2>
<div class="rotateHint">Tip: Rotate your device to landscape for optimal control</div>

<div class="controlArea">
  <div class="pad" id="padFB">
    <div class="padLine"></div>
    <div class="half up" ontouchstart="sendCommand('forward')" ontouchend="sendCommand('stop')" onmousedown="sendCommand('forward')" onmouseup="sendCommand('stop')">&#9650;</div>
    <div class="half down" ontouchstart="sendCommand('backward')" ontouchend="sendCommand('stop')" onmousedown="sendCommand('backward')" onmouseup="sendCommand('stop')">&#9660;</div>
  </div>

  <button id="stopBtn" onclick="sendCommand('stop')">STOP</button>

  <div class="pad" id="padLR">
    <div class="padLine"></div>
    <div class="half left" ontouchstart="sendCommand('left')" ontouchend="sendCommand('stop')" onmousedown="sendCommand('left')" onmouseup="sendCommand('stop')">&#9664;</div>
    <div class="half right" ontouchstart="sendCommand('right')" ontouchend="sendCommand('stop')" onmousedown="sendCommand('right')" onmouseup="sendCommand('stop')">&#9654;</div>
  </div>
</div>

<div class="speedRow">
  <p>PWM Output: <span id="speedVal">200</span> / 255</p>
  <input type="range" min="0" max="255" value="200" id="speedSlider" oninput="setSpeed(this.value)">
</div>

<script>
function sendCommand(cmd) { fetch("/" + cmd); }
function setSpeed(val) {
  document.getElementById("speedVal").innerText = val;
  fetch("/speed?val=" + val);
}
document.querySelectorAll('.half').forEach(el => {
  el.addEventListener('touchcancel', () => fetch('/stop'));
});
</script>
</body>
</html>
)page1";
  html.replace("NAVBAR_PLACEHOLDER", buildNavBar("1"));
  return html;
}

// ------------------- PAGE 2: ANALOG JOYSTICK -------------------
String getJoystickPage() {
  String html = R"page2(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Car - Joystick Control</title>
<style>
  body { font-family: Arial, sans-serif; text-align: center; background:#000000; color:white; margin:0; padding:20px;}
  h2 { margin-bottom: 20px; }
  .nav { margin-bottom: 25px; }
  .nav a { color:#39FF14; text-decoration:none; margin:0 10px; font-weight:bold; padding:6px 10px; border-radius:6px; }
  .nav a.active { background:#222; }
  #joyBase { width:220px; height:220px; border-radius:50%; background:#111; border:3px solid #39FF14; margin:20px auto; position:relative; touch-action:none; box-shadow: 0 0 15px #39FF14; }
  #joyStick { width:80px; height:80px; border-radius:50%; background:#ffffff; position:absolute; left:70px; top:70px; box-shadow: 0 0 20px #ffffff; }
  input[type=range]{ width:250px; accent-color:#39FF14; }
  p { color:#39FF14; }
</style>
</head>
<body>
NAVBAR_PLACEHOLDER
<h2 style="color:#39FF14;">Differential Joystick Control</h2>

<div id="joyBase">
  <div id="joyStick"></div>
</div>

<p>Max PWM Speed: <span id="speedVal">200</span></p>
<input type="range" min="0" max="255" value="200" id="speedSlider" oninput="setSpeed(this.value)">

<script>
const base = document.getElementById('joyBase');
const stick = document.getElementById('joyStick');
const baseRect = () => base.getBoundingClientRect();
const maxRadius = 70;
let dragging = false;
let lastSend = 0;

function setStick(dx, dy) {
  stick.style.left = (70 + dx) + "px";
  stick.style.top  = (70 + dy) + "px";
}

function sendJoy(x, y) {
  const now = Date.now();
  if (now - lastSend < 100) return; // Throttling: Max 10 requests per second
  lastSend = now;
  fetch("/joy?x=" + Math.round(x) + "&y=" + Math.round(y));
}

function handleMove(clientX, clientY) {
  const rect = baseRect();
  const cx = rect.left + rect.width / 2;
  const cy = rect.top + rect.height / 2;
  let dx = clientX - cx;
  let dy = clientY - cy;
  const dist = Math.sqrt(dx*dx + dy*dy);
  if (dist > maxRadius) {
    dx = dx * maxRadius / dist;
    dy = dy * maxRadius / dist;
  }
  setStick(dx, dy);
  const xPercent = (dx / maxRadius) * 100;
  const yPercent = (-dy / maxRadius) * 100;
  sendJoy(xPercent, yPercent);
}

function resetStick() {
  setStick(0, 0);
  dragging = false;
  fetch("/joy?x=0&y=0");
}

base.addEventListener('mousedown', e => { dragging = true; handleMove(e.clientX, e.clientY); });
window.addEventListener('mousemove', e => { if (dragging) handleMove(e.clientX, e.clientY); });
window.addEventListener('mouseup', () => { if (dragging) resetStick(); });

base.addEventListener('touchstart', e => { dragging = true; const t = e.touches[0]; handleMove(t.clientX, t.clientY); });
base.addEventListener('touchmove', e => { if (dragging) { const t = e.touches[0]; handleMove(t.clientX, t.clientY); } e.preventDefault(); }, {passive:false});
base.addEventListener('touchend', () => resetStick());

function setSpeed(val) {
  document.getElementById("speedVal").innerText = val;
  fetch("/speed?val=" + val);
}
</script>
</body>
</html>
)page2";
  html.replace("NAVBAR_PLACEHOLDER", buildNavBar("2"));
  return html;
}

// ------------------- PAGE 3: SIGNAL TELEMETRY GRAPH -------------------
String getGraphPage() {
  String html = R"page3(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Car - Signal Telemetry</title>
<style>
  body { font-family: Arial, sans-serif; text-align: center; background:#000000; color:white; margin:0; padding:20px;}
  h2 { margin-bottom: 10px; }
  .nav { margin-bottom: 25px; }
  .nav a { color:white; text-decoration:none; margin:0 10px; font-weight:bold; padding:6px 10px; border-radius:6px; }
  .nav a.active { background:#333; }
  canvas { background:#000; border:1px solid #444; }
  #currentVal { font-size:22px; margin-top:10px; }
</style>
</head>
<body>
NAVBAR_PLACEHOLDER
<h2>Wi-Fi Signal Strength (RSSI)</h2>
<p id="currentVal">-- dBm</p>
<canvas id="chart" width="360" height="220"></canvas>

<script>
const canvas = document.getElementById('chart');
const ctx = canvas.getContext('2d');
const maxPoints = 60;
let data = [];
const minRSSI = -100;
const maxRSSI = -30;
const thresholdRSSI = -70;

function draw() {
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.strokeStyle = "#555";
  ctx.beginPath();
  ctx.moveTo(30, 10);
  ctx.lineTo(30, 200);
  ctx.lineTo(350, 200);
  ctx.stroke();

  ctx.fillStyle = "white";
  ctx.font = "10px Arial";
  ctx.fillText(maxRSSI + " dBm", 2, 15);
  ctx.fillText(minRSSI + " dBm", 2, 200);

  const ty = 200 - ((thresholdRSSI - minRSSI) / (maxRSSI - minRSSI)) * 190;
  ctx.strokeStyle = "red";
  ctx.lineWidth = 1.5;
  ctx.setLineDash([5, 4]);
  ctx.beginPath();
  ctx.moveTo(30, ty);
  ctx.lineTo(350, ty);
  ctx.stroke();
  ctx.setLineDash([]);
  ctx.fillStyle = "red";
  ctx.font = "10px Arial";
  ctx.fillText("Min " + thresholdRSSI + " dBm", 280, ty - 4);

  if (data.length < 2) return;

  ctx.strokeStyle = "#00ffcc";
  ctx.lineWidth = 2;
  ctx.beginPath();
  for (let i = 0; i < data.length; i++) {
    const x = 30 + (i / (maxPoints - 1)) * 320;
    const clamped = Math.max(minRSSI, Math.min(maxRSSI, data[i]));
    const y = 200 - ((clamped - minRSSI) / (maxRSSI - minRSSI)) * 190;
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.stroke();
}

function poll() {
  fetch("/rssi")
    .then(r => r.text())
    .then(val => {
      const num = parseInt(val);
      document.getElementById('currentVal').innerText = num + " dBm";
      data.push(num);
      if (data.length > maxPoints) data.shift();
      draw();
    });
}

setInterval(poll, 1000);
poll();
</script>
</body>
</html>
)page3";
  html.replace("NAVBAR_PLACEHOLDER", buildNavBar("3"));
  return html;
}

// ------------------- HTTP SERVER ROUTE HANDLERS -------------------
void handleRoot()     { server.send(200, "text/html", getButtonsPage()); }
void handleJoystick() { server.send(200, "text/html", getJoystickPage()); }
void handleGraph()    { server.send(200, "text/html", getGraphPage()); }

void handleForward()  { moveForward();   lastCommandTime = millis(); server.send(200, "text/plain", "FORWARD"); }
void handleBackward() { moveBackward();  lastCommandTime = millis(); server.send(200, "text/plain", "BACKWARD"); }
void handleLeft()     { turnLeft();      lastCommandTime = millis(); server.send(200, "text/plain", "LEFT"); }
void handleRight()    { turnRight();     lastCommandTime = millis(); server.send(200, "text/plain", "RIGHT"); }
void handleStop()     { stopMotors();    lastCommandTime = millis(); server.send(200, "text/plain", "STOP"); }

void handleSpeed() {
  if (server.hasArg("val")) {
    vehicleSpeed = server.arg("val").toInt();
  }
  server.send(200, "text/plain", "OK");
}

void handleJoy() {
  if (server.hasArg("x") && server.hasArg("y")) {
    int x = server.arg("x").toInt(); // Range: -100 to 100
    int y = server.arg("y").toInt(); // Range: -100 to 100

    // Differential steering algorithm
    int left  = constrain(y + x, -100, 100);
    int right = constrain(y - x, -100, 100);

    int leftPWM  = (int)(left  * vehicleSpeed / 100.0);
    int rightPWM = (int)(right * vehicleSpeed / 100.0);

    driveMotors(leftPWM, rightPWM);
    lastCommandTime = millis();
  }
  server.send(200, "text/plain", "OK");
}

void handleRssi() {
  // Telemetry note: WiFi.RSSI() operates in STA mode.
  server.send(200, "text/plain", String(WiFi.RSSI()));
}

// ------------------- INITIAL SETUP -------------------
void setup() {
  Serial.begin(115200);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  // ESP32 Arduino Core 3.x PWM Initialization
  ledcAttach(ENA, PWM_FREQ, PWM_RES);
  ledcAttach(ENB, PWM_FREQ, PWM_RES);

  stopMotors();

  // ================== ACCESS POINT (AP) INITIALIZATION ==================
  WiFi.softAP(ssid, password);
  Serial.println("\n[Wi-Fi] Access Point initialized.");
  Serial.print("[Wi-Fi] SSID: ");
  Serial.println(ssid);
  Serial.print("[Wi-Fi] Server Gateway IP: ");
  Serial.println(WiFi.softAPIP());

  /* ================== STATION (STA) INITIALIZATION ==================
  WiFi.begin(ssid, password);
  Serial.print("[Wi-Fi] Connecting to Network");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[Wi-Fi] Connected successfully.");
  Serial.print("[Wi-Fi] Assigned IP: ");
  Serial.println(WiFi.localIP());
  ================================================================== */

  // Route Definitions
  server.on("/", handleRoot);
  server.on("/joystick", handleJoystick);
  server.on("/graph", handleGraph);
  server.on("/forward", handleForward);
  server.on("/backward", handleBackward);
  server.on("/left", handleLeft);
  server.on("/right", handleRight);
  server.on("/stop", handleStop);
  server.on("/speed", handleSpeed);
  server.on("/joy", handleJoy);
  server.on("/rssi", handleRssi);

  server.begin();
  Serial.println("[HTTP] Web server active and listening.");
}

// ------------------- MAIN LOOP -------------------
void loop() {
  server.handleClient();

  // WATCHDOG FAIL-SAFE: Automatic halt if link drops beyond threshold
  if (millis() - lastCommandTime > TIMEOUT_THRESHOLD) {
    stopMotors();
  }
}
