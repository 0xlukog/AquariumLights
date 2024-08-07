#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>

// WiFi credentials
const char* ssid = "TP-Link_B432";
const char* password = "10499252";

// NTP setup
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000); // 19800 seconds offset for Asia/Kolkata (5.5 hours)

// Relay pin
const int relayPin = 13;

// LED pin for sync failure indication
const int ledPin = 2;

// Start and end times (24-hour format)
int startHour = 9;
int startMinute = 0;
int endHour = 18;
int endMinute = 0;


char buff[256]; 

// Server setup
AsyncWebServer server(80);

// Max number of NTP retries
const int maxRetries = 10;

const char* index_html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Update Aquarium Light Timings</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            margin: 20px;
        }

        h1 {
            text-align: center;
        }

        form {
            display: flex;
            flex-direction: column;
            align-items: center;
        }

        label {
            margin-bottom: 5px;
        }

        input[type="number"] {
            width: 50px;
            padding: 5px;
            border: 1px solid #ccc;
            border-radius: 3px;
        }

        button[type="submit"] {
            background-color: #4CAF50;
            color: white;
            padding: 10px 20px;
            border: none;
            border-radius: 4px;
            cursor: pointer;
        }

        button[type="submit"]:hover {
            background-color: #45A049;
        }
    </style>
</head>
<body>
    <h1>Update Aquarium Light Timings</h1>
    <form action="/update-time" method="POST" id="updateTimeForm">
        <label for="startHour">Start Hour:</label>
        <input type="number" id="startHour" name="startHour" min="0" max="23" required>
        <label for="startMinute">Start Minute:</label>
        <input type="number" id="startMinute" name="startMinute" min="0" max="59" required>
        <br>
        <label for="endHour">End Hour:</label>
        <input type="number" id="endHour" name="endHour" min="0" max="23" required>
        <label for="endMinute">End Minute:</label>
        <input type="number" id="endMinute" name="endMinute" min="0" max="59" required>
        <br>
        <button type="submit">Update</button>
    </form>
    <form action="/switch-on" method="POST" id="switchOn">
        <button type="submit">ON</button>
    </form>
    <form action="/switch-off" method="POST" id="switchOff">
        <button type="submit">OFF</button>
    </form>
    <script>
        const form = document.getElementById('updateTimeForm');

        form.addEventListener('submit', (event) => {
            const startHour = parseInt(document.getElementById('startHour').value);
            const startMinute = parseInt(document.getElementById('startMinute').value);
            const endHour = parseInt(document.getElementById('endHour').value);
            const endMinute = parseInt(document.getElementById('endMinute').value);

            if (isNaN(startHour) || isNaN(startMinute) || isNaN(endHour) || isNaN(endMinute)) {
                alert('Please enter valid numbers for all fields.');
                event.preventDefault();
            }
        });
    </script>
</body>
</html>

)rawliteral";

void setup() {
  Serial.begin(115200);

  // Initialize relay and LED pins
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  // Print the IP address
  Serial.print("ESP32 IP Address: ");
  Serial.println(WiFi.localIP());

  // Initialize mDNS
  if (!MDNS.begin("aquarium-light")) {
    Serial.println("Error starting mDNS");
    return;
  }
  Serial.println("mDNS responder started");

  // Initialize NTP
  timeClient.begin();

  // Sync time with NTP
  bool ntpSyncSuccess = syncTimeWithRetries();

  // Initialize server
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", index_html);
  });

  server.on("/update-time", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("startHour", true) && request->hasParam("startMinute", true) &&
        request->hasParam("endHour", true) && request->hasParam("endMinute", true)) {
      startHour = request->getParam("startHour", true)->value().toInt();
      startMinute = request->getParam("startMinute", true)->value().toInt();
      endHour = request->getParam("endHour", true)->value().toInt();
      endMinute = request->getParam("endMinute", true)->value().toInt();
      request->send(200, "text/plain", "Time updated");
      execute();
    } else {
      request->send(400, "text/plain", "Invalid parameters");
    }
  });

  server.on("/switchOn", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Switched On!");
    digitalWrite(relayPin, HIGH);
    Serial.println("Light Switched On!");
  });

  server.on("/switchOff", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Switched Off!");
    digitalWrite(relayPin, LOW);
    Serial.println("Light Switched Off!");
  });

  server.begin();

  // Indicate sync failure if necessary
  if (!ntpSyncSuccess) {
    digitalWrite(ledPin, HIGH); // Turn on LED to indicate failure
  }
}

void execute() {
    timeClient.update();
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();
  sprintf(buff,"\nstart hour : %d\nstart min: %d\n curr hour : %d\ncurr min : %d",startHour,startMinute,currentHour,currentMinute);
    Serial.println(buff);

  if ((currentHour > startHour || (currentHour == startHour && currentMinute >= startMinute)) && 
      (currentHour < endHour || (currentHour == endHour && currentMinute <= endMinute))) {
    Serial.println("Lights Turn ON");
    digitalWrite(relayPin, HIGH);
  } else {
    Serial.println  ("Lights Turn OFF");
    digitalWrite(relayPin, LOW);
  }
  delay(60*1000); // Check every second
}

bool syncTimeWithRetries() {
  for (int i = 0; i < maxRetries; ++i) {
    timeClient.update();
    if (timeClient.getEpochTime() != 0) {
      Serial.println("Time synced successfully");
      return true;
    }
    delay(2000); // Wait 2 seconds before retrying
  }
  Serial.println("Failed to sync time after multiple attempts");
  return false;
}

void loop() {
  execute();
  delay(10*60*1000); // Check every 10 mins
}
