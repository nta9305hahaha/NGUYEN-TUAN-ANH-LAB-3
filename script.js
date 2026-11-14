/*
  LAB 3 - SMART GARDEN DASHBOARD
  ESP32 + FreeRTOS + MQTT
*/

// ======================================
// 1. MQTT CONFIGURATION
// ======================================

const PREFIX = "lab3/smartgarden/esp32_2026_new01";

const BROKER = "wss://broker.emqx.io:8084/mqtt";

const connectionEl = document.getElementById("connection");
const messageEl = document.getElementById("message");

let currentMode = null;
let client = null;

// ======================================
// 2. DISPLAY MESSAGES
// ======================================

function showMessage(message) {
  messageEl.textContent = message;
  console.log("[Dashboard]", message);
}

function setConnection(connected) {
  connectionEl.textContent = connected
    ? "● MQTT Connected"
    : "● MQTT Disconnected";

  connectionEl.classList.toggle("online", connected);

  updateControls();
}

// ======================================
// 3. UPDATE CONTROL BUTTONS
// ======================================

function updateControls() {
  const connected = Boolean(client && client.connected);

  document.getElementById("autoBtn").disabled = !connected;
  document.getElementById("manualBtn").disabled = !connected;

  document.getElementById("autoBtn")
    .classList.toggle("active", currentMode === "AUTO");

  document.getElementById("manualBtn")
    .classList.toggle("active", currentMode === "MANUAL");

  document.querySelectorAll(".manual-control")
    .forEach(function(button) {
      button.disabled =
        !connected || currentMode !== "MANUAL";
    });
}

// ======================================
// 4. PUBLISH MQTT COMMAND
// ======================================

function publish(topic, payload) {
  if (!client || !client.connected) {
    showMessage("MQTT chưa kết nối!");
    return;
  }

  const fullTopic = PREFIX + "/" + topic;

  client.publish(
    fullTopic,
    payload,
    { qos: 0, retain: false },
    function(error) {
      if (error) {
        console.error("Publish error:", error);
        showMessage(
          "Gửi lệnh thất bại: " + error.message
        );
      } else {
        showMessage(
          "Đã gửi: " + fullTopic + " = " + payload
        );
      }
    }
  );
}

// ======================================
// 5. AUTO / MANUAL MODE
// ======================================

function setMode(mode) {
  if (mode !== "AUTO" && mode !== "MANUAL") {
    return;
  }

  publish("mode/set", mode);
}

// ======================================
// 6. MANUAL DEVICE CONTROL
// ======================================

function sendCommand(device, state) {
  if (currentMode !== "MANUAL") {
    showMessage("Hãy chuyển sang MANUAL trước!");
    return;
  }

  if (!["pump", "fan", "mist"].includes(device)) {
    return;
  }

  if (state !== "ON" && state !== "OFF") {
    return;
  }

  publish(device + "/set", state);
}

// Connect functions to HTML buttons
window.setMode = setMode;
window.sendCommand = sendCommand;

// ======================================
// 7. CHART CONFIGURATION
// ======================================

function chartConfig(label, color) {
  return {
    type: "line",

    data: {
      labels: [],

      datasets: [{
        label: label,
        data: [],
        borderColor: color,
        backgroundColor: color,
        tension: 0.3,
        pointRadius: 2,
        fill: false
      }]
    },

    options: {
      responsive: true,
      maintainAspectRatio: true,
      animation: false,

      scales: {
        x: {
          title: {
            display: true,
            text: "Thời gian"
          }
        }
      }
    }
  };
}

// ======================================
// 8. CREATE CHARTS
// ======================================

let tempChart = null;
let humChart = null;

if (typeof Chart === "undefined") {
  showMessage(
    "Không tải được Chart.js. Kiểm tra Internet."
  );
} else {
  tempChart = new Chart(
    document.getElementById("tempChart"),
    chartConfig("Nhiệt độ (°C)", "#ef4444")
  );

  humChart = new Chart(
    document.getElementById("humChart"),
    chartConfig("Độ ẩm (%)", "#0284c7")
  );
}

// ======================================
// 9. UPDATE REAL-TIME CHARTS
// ======================================

function addChartData(chart, value) {
  if (!chart) {
    return;
  }

  const time = new Date().toLocaleTimeString("vi-VN");

  chart.data.labels.push(time);
  chart.data.datasets[0].data.push(value);

  if (chart.data.labels.length > 30) {
    chart.data.labels.shift();
    chart.data.datasets[0].data.shift();
  }

  chart.update();
}

// ======================================
// 10. RECEIVE MQTT DATA
// ======================================

function handleMessage(topic, payload) {
  if (!topic.startsWith(PREFIX + "/")) {
    return;
  }

  const key = topic.substring(PREFIX.length + 1);
  const value = payload.toString().trim();

  console.log("[MQTT RECEIVE]", key, value);

  switch (key) {

    // TEMPERATURE
    case "temperature": {
      if (value === "") {
        break;
      }

      const t = Number(value);

      if (Number.isFinite(t)) {
        document.getElementById("temperature")
          .textContent = t.toFixed(1) + " °C";

        addChartData(tempChart, t);
      }

      break;
    }

    // HUMIDITY
    case "humidity": {
      if (value === "") {
        break;
      }

      const h = Number(value);

      if (Number.isFinite(h)) {
        document.getElementById("humidity")
          .textContent = h.toFixed(1) + " %";

        addChartData(humChart, h);
      }

      break;
    }

    // AUTO / MANUAL
    case "mode/state": {
      const mode = value.toUpperCase();

      if (mode === "AUTO" || mode === "MANUAL") {
        currentMode = mode;

        document.getElementById("modeState")
          .textContent = mode;

        updateControls();
      }

      break;
    }

    // PUMP STATE
    case "pump/state": {
      document.getElementById("pumpState")
        .textContent = value;

      break;
    }

    // FAN STATE
    case "fan/state": {
      document.getElementById("fanState")
        .textContent = value;

      break;
    }

    // MIST STATE
    case "mist/state": {
      let state = value;

      try {
        const obj = JSON.parse(value);

        if (typeof obj.active === "boolean") {
          state = obj.active ? "ON" : "OFF";

          if (typeof obj.power === "number") {
            document.getElementById("mistPower")
              .textContent = obj.power + "%";
          }
        }
      } catch (error) {
        // Plaintext ON/OFF is valid
      }

      document.getElementById("mistState")
        .textContent = state;

      break;
    }

    // MIST POWER
    case "mist/power": {
      const power = Number(value);

      if (value !== "" && Number.isFinite(power)) {
        document.getElementById("mistPower")
          .textContent = power + "%";
      }

      break;
    }
  }
}

// ======================================
// 11. INITIAL CONNECTION STATUS
// ======================================

setConnection(false);

showMessage("Đang kết nối MQTT Broker...");

// ======================================
// 12. CONNECT MQTT BROKER
// ======================================

if (typeof mqtt === "undefined") {
  showMessage(
    "Không tải được MQTT.js. Kiểm tra Internet."
  );
} else {

  client = mqtt.connect(BROKER, {
    clientId: "web_garden_" +
      Math.random().toString(16).slice(2, 12),

    clean: true,
    reconnectPeriod: 3000,
    connectTimeout: 10000,
    keepalive: 60
  });

  // MQTT CONNECTED
  client.on("connect", function() {
    setConnection(true);

    showMessage("Đã kết nối MQTT Broker!");

    client.subscribe(
      PREFIX + "/#",
      { qos: 0 },
      function(error) {
        if (error) {
          console.error("Subscribe error:", error);

          showMessage(
            "Không subscribe được MQTT: " +
            error.message
          );
        } else {
          console.log(
            "[MQTT] Subscribed to",
            PREFIX + "/#"
          );
        }
      }
    );
  });

  // RECEIVE DATA
  client.on("message", handleMessage);

  // RECONNECT
  client.on("reconnect", function() {
    setConnection(false);

    showMessage("Đang kết nối lại MQTT...");
  });

  // OFFLINE
  client.on("offline", function() {
    setConnection(false);

    showMessage("MQTT đang Offline!");
  });

  // CONNECTION CLOSED
  client.on("close", function() {
    setConnection(false);
  });

  // CONNECTION ERROR
  client.on("error", function(error) {
    console.error("[MQTT ERROR]", error);

    showMessage(
      "Lỗi MQTT: " + error.message
    );
  });
}
